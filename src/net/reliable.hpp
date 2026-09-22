#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <vector>
#include <utility>

namespace rudp {

constexpr uint32_t kMagic = 0x31535243u;

enum Kind : uint8_t {
    kKindData = 1,
    kKindAck = 2,
    kKindHello = 3,
    kKindHelloAck = 4,
    kKindBye = 5,
};

#pragma pack(push, 1)
struct Header {
    uint32_t magic;

    uint32_t conn;
    uint8_t kind;
    uint32_t seq;
    uint32_t ack;
    uint32_t ackBits;
    uint16_t len;
};
#pragma pack(pop)
static_assert(sizeof(Header) == 23, "rudp::Header must stay packed");

constexpr size_t kMaxSegment = 1100;

constexpr uint32_t kWindow = 512;

constexpr size_t kMaxQueuedBytes = 4u << 20;

constexpr uint64_t kDeadAfterMs = 20000;

constexpr uint64_t kHeartbeatMs = 250;

class Channel {
public:

    template <typename SendFn>
    void flush(uint64_t nowMs, SendFn&& send) {
        segment_pending();
        bool sentAny = false;
        std::vector<uint8_t> packet;
        for (Segment& s : m_inflight) {
            if (s.acked) continue;
            if (s.sends != 0 && nowMs - s.lastSentMs < retransmit_after(s.sends)) continue;
            build(packet, kKindData, s.seq, s.data.data(), s.data.size());
            send(packet.data(), packet.size());
            if (s.sends != 0) ++m_resends;
            s.lastSentMs = nowMs;
            ++s.sends;
            sentAny = true;
        }
        if (sentAny) {
            m_ackDirty = false;
            m_lastSentMs = nowMs;
        } else if (m_ackDirty || nowMs - m_lastSentMs >= kHeartbeatMs) {
            build(packet, kKindAck, 0, nullptr, 0);
            send(packet.data(), packet.size());
            m_ackDirty = false;
            m_lastSentMs = nowMs;
        }
    }

    bool write(const void* data, size_t size) {
        if (queued_bytes() + size > kMaxQueuedBytes) {
            m_overflowed = true;
            return false;
        }
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        m_pending.insert(m_pending.end(), bytes, bytes + size);
        return true;
    }

    void on_packet(const Header& h, const uint8_t* payload, uint64_t nowMs,
        std::vector<uint8_t>& delivered) {
        m_lastHeardMs = nowMs;
        m_heardAny = true;
        apply_ack(h.ack, h.ackBits, nowMs);
        if (h.kind != kKindData) return;
        m_ackDirty = true;
        if (seq_before(h.seq, m_recvNext)) return;
        if (h.seq - m_recvNext >= kWindow * 2) return;
        if (h.seq == m_recvNext) {
            delivered.insert(delivered.end(), payload, payload + h.len);
            ++m_recvNext;
            for (auto it = m_outOfOrder.find(m_recvNext); it != m_outOfOrder.end();
                 it = m_outOfOrder.find(m_recvNext)) {
                delivered.insert(delivered.end(), it->second.begin(), it->second.end());
                m_outOfOrder.erase(it);
                ++m_recvNext;
            }
        } else {
            m_outOfOrder.emplace(h.seq, std::vector<uint8_t>(payload, payload + h.len));
        }
    }

    void control_packet(std::vector<uint8_t>& out, Kind kind, const void* payload = nullptr,
        size_t size = 0) const {
        build(out, kind, 0, static_cast<const uint8_t*>(payload), size);
    }

    void start(uint32_t conn, uint64_t nowMs) {
        *this = Channel{};
        m_conn = conn;
        m_lastHeardMs = nowMs;
        m_lastSentMs = nowMs;
    }

    uint32_t conn() const { return m_conn; }
    bool heard_any() const { return m_heardAny; }
    bool dead(uint64_t nowMs) const { return m_overflowed || nowMs - m_lastHeardMs > kDeadAfterMs; }
    bool idle() const { return m_pending.empty() && m_inflight.empty(); }
    uint32_t smoothed_rtt_ms() const { return m_srttMs; }
    uint64_t resends() const { return m_resends; }

    size_t queued_bytes() const {
        size_t n = m_pending.size();
        for (const Segment& s : m_inflight) n += s.data.size();
        return n;
    }

    static bool parse(const void* data, size_t size, Header& h, const uint8_t*& payload) {
        if (size < sizeof(Header)) return false;
        std::memcpy(&h, data, sizeof(Header));
        if (h.magic != kMagic || size != sizeof(Header) + h.len || h.len > kMaxSegment) {
            return false;
        }
        payload = static_cast<const uint8_t*>(data) + sizeof(Header);
        return true;
    }

private:
    struct Segment {
        uint32_t seq = 0;
        std::vector<uint8_t> data;
        uint64_t lastSentMs = 0;
        uint32_t sends = 0;
        bool acked = false;
    };

    static bool seq_before(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) < 0; }

    void segment_pending() {
        size_t offset = 0;
        while (offset < m_pending.size() && m_inflight.size() < kWindow) {
            const size_t n = std::min(kMaxSegment, m_pending.size() - offset);
            Segment s;
            s.seq = m_nextSeq++;
            s.data.assign(m_pending.begin() + static_cast<std::ptrdiff_t>(offset),
                m_pending.begin() + static_cast<std::ptrdiff_t>(offset + n));
            m_inflight.push_back(std::move(s));
            offset += n;
        }
        if (offset > 0) {
            m_pending.erase(m_pending.begin(), m_pending.begin() + static_cast<std::ptrdiff_t>(offset));
        }
    }

    uint64_t retransmit_after(uint32_t sends) const {
        uint64_t rto = std::clamp<uint64_t>(m_srttMs + m_srttMs / 2 + 30, 60, 1000);
        const uint32_t doublings = std::min<uint32_t>(sends - 1, 4);
        return std::min<uint64_t>(rto << doublings, 3000);
    }

    void apply_ack(uint32_t ack, uint32_t ackBits, uint64_t nowMs) {
        for (Segment& s : m_inflight) {
            if (s.acked) continue;
            bool acked = seq_before(s.seq, ack);
            if (!acked && s.seq != ack) {
                const uint32_t bit = s.seq - ack - 1;
                acked = bit < 32 && (ackBits & (1u << bit)) != 0;
            }
            if (!acked) continue;
            s.acked = true;

            if (s.sends == 1) {
                const uint32_t sample = static_cast<uint32_t>(nowMs - s.lastSentMs);
                m_srttMs = m_haveRtt ? (m_srttMs * 7 + sample) / 8 : sample;
                m_haveRtt = true;
            }
        }
        while (!m_inflight.empty() && m_inflight.front().acked) m_inflight.pop_front();
    }

    void build(std::vector<uint8_t>& out, uint8_t kind, uint32_t seq, const uint8_t* payload,
        size_t size) const {
        Header h{};
        h.magic = kMagic;
        h.conn = m_conn;
        h.kind = kind;
        h.seq = seq;
        h.ack = m_recvNext;
        h.ackBits = 0;
        for (uint32_t i = 0; i < 32; ++i) {
            if (m_outOfOrder.count(m_recvNext + 1 + i) != 0) h.ackBits |= 1u << i;
        }
        h.len = static_cast<uint16_t>(size);
        out.resize(sizeof(Header) + size);
        std::memcpy(out.data(), &h, sizeof(Header));
        if (size > 0) std::memcpy(out.data() + sizeof(Header), payload, size);
    }

    uint32_t m_conn = 0;
    std::vector<uint8_t> m_pending;
    std::deque<Segment> m_inflight;
    uint32_t m_nextSeq = 1;
    uint32_t m_recvNext = 1;
    std::map<uint32_t, std::vector<uint8_t>> m_outOfOrder;
    bool m_ackDirty = false;
    bool m_heardAny = false;
    bool m_overflowed = false;
    bool m_haveRtt = false;
    uint32_t m_srttMs = 100;
    uint64_t m_lastHeardMs = 0;
    uint64_t m_lastSentMs = 0;
    uint64_t m_resends = 0;
};

}
