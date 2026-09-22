

#include "mod.hpp"
#include "print.hpp"

#include "mods/svc/websocket.hpp"

#include "net/messages.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>
#include <string_view>

namespace {

const char* const kDefaultRoomServer = "wss://crests-rooms.crestsofcourage.workers.dev";

const char* const kTailscaleHint =
    "Your networks are too strict to connect directly. Use Tailscale instead. See How to play "
    "together.";

const uint32_t kPunchMagic = 0x31485043u;
const uint8_t kPunchProbe = 1;
const uint8_t kPunchAnswer = 2;
const size_t kPunchSize = 4 + 8 + 1;

const uint32_t kStunCookie = 0x2112A442u;

const uint64_t kWelcomeTimeoutMs = 10000;
const uint64_t kStunRetryMs = 300;
const uint64_t kStunGiveUpMs = 2500;
const uint64_t kStunEnoughMs = 1200;
const uint64_t kPeerTimeoutMs = 15000;
const uint64_t kPunchEveryMs = 100;
const uint64_t kJoinPunchMs = 8000;
const uint64_t kHostPunchMs = 15000;

uint64_t now_ms() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

std::mt19937_64& rng() {
    static std::mt19937_64 engine{std::random_device{}() ^ (static_cast<uint64_t>(now_ms()) << 21)};
    return engine;
}

struct JsonField {
    std::string str;
    double num = 0;
    bool boolean = false;
    std::vector<std::string> list;
};

struct JsonObject {
    std::vector<std::pair<std::string, JsonField>> fields;

    const JsonField* find(const char* key) const {
        for (const auto& f : fields) {
            if (f.first == key) return &f.second;
        }
        return nullptr;
    }
    std::string str(const char* key) const {
        const JsonField* f = find(key);
        return f != nullptr ? f->str : std::string{};
    }
    bool boolean(const char* key) const {
        const JsonField* f = find(key);
        return f != nullptr && f->boolean;
    }
    std::vector<std::string> list(const char* key) const {
        const JsonField* f = find(key);
        return f != nullptr ? f->list : std::vector<std::string>{};
    }
};

class JsonReader {
public:
    explicit JsonReader(std::string_view text) : m_text(text) {}

    bool object(JsonObject& out) {
        skip_ws();
        if (!eat('{')) return false;
        skip_ws();
        if (eat('}')) return true;
        for (;;) {
            std::string key;
            skip_ws();
            if (!string(key)) return false;
            skip_ws();
            if (!eat(':')) return false;
            JsonField field;
            if (!value(field)) return false;
            out.fields.emplace_back(std::move(key), std::move(field));
            skip_ws();
            if (eat(',')) continue;
            return eat('}');
        }
    }

private:
    bool value(JsonField& out) {
        skip_ws();
        if (m_pos >= m_text.size()) return false;
        const char c = m_text[m_pos];
        if (c == '"') return string(out.str);
        if (c == '[') {
            ++m_pos;
            skip_ws();
            if (eat(']')) return true;
            for (;;) {
                JsonField item;
                if (!value(item)) return false;
                out.list.push_back(std::move(item.str));
                skip_ws();
                if (eat(',')) continue;
                return eat(']');
            }
        }
        if (c == '{') {
            JsonObject ignored;
            return object(ignored);
        }
        if (m_text.substr(m_pos, 4) == "true") {
            m_pos += 4;
            out.boolean = true;
            return true;
        }
        if (m_text.substr(m_pos, 5) == "false") {
            m_pos += 5;
            return true;
        }
        if (m_text.substr(m_pos, 4) == "null") {
            m_pos += 4;
            return true;
        }
        const size_t start = m_pos;
        while (m_pos < m_text.size() && std::strchr("+-0123456789.eE", m_text[m_pos]) != nullptr) {
            ++m_pos;
        }
        if (m_pos == start) return false;
        out.str = std::string(m_text.substr(start, m_pos - start));
        out.num = std::strtod(out.str.c_str(), nullptr);
        return true;
    }

    bool string(std::string& out) {
        if (!eat('"')) return false;
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (m_pos >= m_text.size()) return false;
            const char e = m_text[m_pos++];
            switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u':

                if (m_pos + 4 > m_text.size()) return false;
                m_pos += 4;
                out += '?';
                break;
            default: out += e; break;
            }
        }
        return false;
    }

    void skip_ws() {
        while (m_pos < m_text.size() && std::strchr(" \t\r\n", m_text[m_pos]) != nullptr) ++m_pos;
    }
    bool eat(char c) {
        if (m_pos < m_text.size() && m_text[m_pos] == c) {
            ++m_pos;
            return true;
        }
        return false;
    }

    std::string_view m_text;
    size_t m_pos = 0;
};

std::string json_escape(const std::string& text) {
    std::string out;
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out;
}

std::string bare(std::string_view endpoint) {
    if (endpoint.rfind("udp://", 0) == 0) endpoint.remove_prefix(6);
    return std::string(endpoint);
}

std::string as_udp(const std::string& address) {
    return "udp://" + address;
}

bool parse_hex_token(const std::string& text, uint64_t& out) {
    if (text.size() != 16) return false;
    out = 0;
    for (const char c : text) {
        uint64_t v;
        if (c >= '0' && c <= '9') v = static_cast<uint64_t>(c - '0');
        else if (c >= 'a' && c <= 'f') v = static_cast<uint64_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v = static_cast<uint64_t>(c - 'A' + 10);
        else return false;
        out = (out << 4) | v;
    }
    return true;
}

enum class Phase {
    Idle,
    Connecting,
    Stun,
    Waiting,
    Punching,
    Done,
    Failed,
};

struct PendingPeer {
    uint64_t token = 0;
    std::vector<std::string> candidates;
    std::string heardFrom;
    uint64_t startedMs = 0;
    uint64_t lastProbeMs = 0;
    bool accepted = false;
};

struct StunAsk {
    uint8_t txn[12];
    std::string server;
};

bool s_host = false;

bool s_named = false;
Phase s_phase = Phase::Idle;
std::string s_status;
std::string s_code;
std::string s_serverUrl;
int s_localPort = 0;
mods::ws::Connection s_ws;
uint64_t s_phaseStartMs = 0;

std::vector<std::string> s_stunServers;
std::vector<StunAsk> s_stunAsks;
std::string s_mapped;
std::string s_mappedOther;
uint64_t s_lastStunSendMs = 0;
uint64_t s_firstStunAnswerMs = 0;

std::vector<PendingPeer> s_peers;
std::string s_sentUpnp;
int s_reconnects = 0;
uint64_t s_reconnectAtMs = 0;

PendingPeer s_target;
std::string s_hostUpnp;
bool s_sameNet = false;

void set_phase(Phase phase, std::string status) {
    s_phase = phase;
    s_status = std::move(status);
    s_phaseStartMs = now_ms();
}

void close_ws() {
    if (s_ws) s_ws.close(1000, "done");
    s_ws = mods::ws::Connection{};
}

void joiner_give_up(const std::string& why) {
    close_ws();
    const std::string upnp = s_hostUpnp;
    set_phase(Phase::Failed, why);
    coop_log::info("coop_mod: [ONLINE] giving up on the room: {}", why);
    coop_online_failed(why, upnp);
}

void host_lost_server(const std::string& why) {
    close_ws();

    if (s_reconnects < 6) {
        const uint64_t delays[] = {2000, 5000, 10000, 20000, 30000, 60000};
        s_reconnectAtMs = now_ms() + delays[s_reconnects];
        ++s_reconnects;
        set_phase(Phase::Failed, "Lost the room server (" + why + "). Trying again...");
    } else {
        s_reconnectAtMs = 0;
        set_phase(Phase::Failed, "Could not reach the room server. Players can still join by "
                                 "address.");
    }
    coop_log::info("coop_mod: [ONLINE] {}", s_status);
}

void fail(const std::string& why) {
    if (s_host) {
        host_lost_server(why);
    } else {
        joiner_give_up(why);
    }
}

std::string room_server() {
    std::string url = cfg_string(coop_net_room_server_var(), "");
    if (url.empty()) url = kDefaultRoomServer;
    while (!url.empty() && url.back() == '/') url.pop_back();
    return url;
}

bool open_ws(const std::string& path) {
    s_serverUrl = room_server();
    if (s_serverUrl.find("YOUR-SUBDOMAIN") != std::string::npos) {
        set_phase(Phase::Failed, "Room codes are not set up in this build yet. Use an address.");
        return false;
    }
    mods::ws::Options options;
    options.url = s_serverUrl + path;
    options.connectTimeoutMs = 8000;

    options.keepaliveIntervalMs = 0;
    options.maxMessageBytes = 16 * 1024;
    s_ws = mods::ws::connect(options);
    if (!s_ws) {
        set_phase(Phase::Failed, s_ws.result() == MOD_UNAVAILABLE
                                     ? "Room codes are not available on this device. Use an "
                                       "address."
                                     : "Could not reach the room server.");
        return false;
    }
    set_phase(Phase::Connecting, s_host ? "Getting a room code..." : "Looking for room " + s_code +
                                                                       "...");
    coop_log::info("coop_mod: [ONLINE] connecting to {}", options.url);
    return true;
}

void send_json(const std::string& text) {
    if (s_ws) s_ws.send_text(text);
}

void send_stun_requests() {
    s_lastStunSendMs = now_ms();
    s_stunAsks.clear();
    for (const std::string& server : s_stunServers) {
        StunAsk ask;
        for (uint8_t& b : ask.txn) b = static_cast<uint8_t>(rng()());
        ask.server = as_udp(server);

        uint8_t packet[20] = {0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42};
        std::memcpy(packet + 8, ask.txn, 12);
        coop_udp_send_raw(ask.server, packet, sizeof(packet));
        s_stunAsks.push_back(ask);
    }
}

void say_hello() {
    std::string hello = "{\"op\":\"hello\",\"v\":" + std::to_string(kCoopProtocolVersion) +
                        ",\"ep\":\"" + json_escape(s_mapped) + "\",\"port\":" +
                        std::to_string(s_localPort);
    if (s_host) {
        s_sentUpnp = upnp_external_address();
        hello += ",\"upnp\":\"" + json_escape(s_sentUpnp) + "\"";
    }
    hello += "}";
    send_json(hello);
    if (!s_mappedOther.empty() && bare(s_mappedOther) != bare(s_mapped)) {

        coop_log::info("coop_mod: [ONLINE] strict router here - STUN saw {} and {}", s_mapped,
            s_mappedOther);
    }
    if (s_host) {
        set_phase(Phase::Waiting, "");
    } else {
        set_phase(Phase::Waiting, "Found room " + s_code + ". Introducing you...");
    }
}

bool parse_stun(const uint8_t* data, size_t size, std::string& out) {
    if (size < 20 || data[0] != 0x01 || data[1] != 0x01) return false;
    const size_t length = (static_cast<size_t>(data[2]) << 8) | data[3];
    if (20 + length > size) return false;
    std::string plain;
    for (size_t at = 20; at + 4 <= 20 + length;) {
        const uint16_t type = static_cast<uint16_t>((data[at] << 8) | data[at + 1]);
        const size_t len = (static_cast<size_t>(data[at + 2]) << 8) | data[at + 3];
        const uint8_t* v = data + at + 4;
        if (at + 4 + len > 20 + length) break;
        if ((type == 0x0020 || type == 0x0001) && len >= 8 && v[1] == 0x01) {
            uint16_t port = static_cast<uint16_t>((v[2] << 8) | v[3]);
            uint8_t ip[4] = {v[4], v[5], v[6], v[7]};
            if (type == 0x0020) {
                port ^= static_cast<uint16_t>(kStunCookie >> 16);
                ip[0] ^= 0x21;
                ip[1] ^= 0x12;
                ip[2] ^= 0xA4;
                ip[3] ^= 0x42;
            }
            std::string text = std::to_string(ip[0]) + "." + std::to_string(ip[1]) + "." +
                               std::to_string(ip[2]) + "." + std::to_string(ip[3]) + ":" +
                               std::to_string(port);
            if (type == 0x0020) {
                out = text;
                return true;
            }
            plain = text;
        }
        at += 4 + ((len + 3) & ~static_cast<size_t>(3));
    }
    if (plain.empty()) return false;
    out = plain;
    return true;
}

bool on_stun(const uint8_t* data, size_t size) {
    const auto ask = std::find_if(s_stunAsks.begin(), s_stunAsks.end(),
        [&](const StunAsk& a) { return std::memcmp(a.txn, data + 8, 12) == 0; });
    if (ask == s_stunAsks.end()) return true;
    std::string mapped;
    if (!parse_stun(data, size, mapped)) return true;
    const std::string server = ask->server;
    s_stunAsks.erase(ask);
    if (s_phase != Phase::Stun) return true;
    if (s_mapped.empty()) {
        s_mapped = mapped;
        s_firstStunAnswerMs = now_ms();
        coop_log::info("coop_mod: [ONLINE] {} says we are {}", server, mapped);
    } else if (s_mappedOther.empty()) {
        s_mappedOther = mapped;
        say_hello();
    }
    return true;
}

void update_stun() {
    const uint64_t now = now_ms();
    if (!s_mapped.empty() && now - s_firstStunAnswerMs >= kStunEnoughMs) {
        say_hello();
        return;
    }
    if (now - s_phaseStartMs >= kStunGiveUpMs) {

        coop_log::info("coop_mod: [ONLINE] no STUN answer - going on the server's view alone");
        say_hello();
        return;
    }
    if (now - s_lastStunSendMs >= kStunRetryMs) send_stun_requests();
}

int s_punchLogs = 0;

void send_punch(const std::string& to, uint64_t token, uint8_t kind) {
    uint8_t packet[kPunchSize];
    std::memcpy(packet, &kPunchMagic, 4);
    std::memcpy(packet + 4, &token, 8);
    packet[12] = kind;
    coop_udp_send_raw(to, packet, sizeof(packet));
}

void probe(PendingPeer& peer) {
    const uint64_t now = now_ms();
    if (now - peer.lastProbeMs < kPunchEveryMs) return;
    if (peer.lastProbeMs == 0) {
        coop_log::info("coop_mod: [ONLINE] +{}ms probing {} addresses{}", now - peer.startedMs,
            peer.candidates.size(), peer.heardFrom.empty() ? "" : " + " + peer.heardFrom);
    }
    peer.lastProbeMs = now;
    for (const std::string& to : peer.candidates) send_punch(to, peer.token, kPunchProbe);
    if (!peer.heardFrom.empty() &&
        std::find(peer.candidates.begin(), peer.candidates.end(), peer.heardFrom) ==
            peer.candidates.end()) {
        send_punch(peer.heardFrom, peer.token, kPunchProbe);
    }
}

bool on_punch(const std::string& from, const uint8_t* data, size_t size) {
    if (size != kPunchSize) return false;
    uint32_t magic;
    std::memcpy(&magic, data, 4);
    if (magic != kPunchMagic) return false;
    uint64_t token;
    std::memcpy(&token, data + 4, 8);
    const uint8_t kind = data[12];
    if (s_punchLogs < 12) {
        ++s_punchLogs;
        coop_log::info("coop_mod: [ONLINE] +{}ms punch {} from {} (token {:016x}, want {:016x})",
            now_ms() - s_phaseStartMs, kind == kPunchProbe ? "probe" : "answer", from, token,
            s_host ? (s_peers.empty() ? 0 : s_peers.back().token) : s_target.token);
    }

    if (s_host) {
        for (PendingPeer& p : s_peers) {
            if (p.token != token || p.accepted) continue;
            if (kind == kPunchProbe) send_punch(from, token, kPunchAnswer);
            if (p.heardFrom.empty()) {
                coop_log::info("coop_mod: [ONLINE] reached a joiner at {}", from);
            }
            p.heardFrom = from;
        }
        return true;
    }

    if (s_phase != Phase::Punching || token != s_target.token) return true;
    if (kind == kPunchProbe) send_punch(from, token, kPunchAnswer);

    coop_log::info("coop_mod: [ONLINE] reached the host at {}", from);
    const uint64_t token_ = s_target.token;
    close_ws();
    set_phase(Phase::Done, "Found the host. Connecting...");
    coop_online_punched(from, token_);
    return true;
}

std::string error_text(const std::string& why, const std::string& detail) {
    if (why == "no_room") return "There is no room " + s_code + ". Check the code with the host.";
    if (why == "version") {
        return "The host is on version " + detail + " and you are on " +
               std::to_string(kCoopProtocolVersion) + ". You both need the same one.";
    }
    if (why == "busy") return "That room is busy. Try again in a moment.";
    if (why == "taken") {
        return "Somebody is already using the room name " + s_code +
               ". Pick another, then Disconnect and Host again.";
    }
    return "The room server said no (" + why + ").";
}

void on_server_message(const std::string& text) {
    JsonObject msg;
    JsonReader reader(text);
    if (!reader.object(msg)) {
        coop_log::warn("coop_mod: [ONLINE] unreadable message from the room server");
        return;
    }
    const std::string op = msg.str("op");

    if (op == "welcome") {
        if (s_host) {
            s_code = msg.str("code");
            s_reconnects = 0;
            coop_log::info("coop_mod: [ONLINE] room {}", s_code);
        }
        s_stunServers = msg.list("stun");
        s_mapped.clear();
        s_mappedOther.clear();
        set_phase(Phase::Stun, s_host ? "" : "Found room " + s_code + ". Checking your network...");
        send_stun_requests();
        return;
    }

    if (op == "error") {
        const std::string why = error_text(msg.str("why"), msg.str("detail"));
        if (s_host && msg.str("why") == "taken" && s_reconnects == 0) {

            close_ws();
            set_phase(Phase::Failed, why);
            s_reconnectAtMs = 0;
            s_code.clear();
            return;
        }
        if (s_host) {
            host_lost_server(why);
        } else {

            s_hostUpnp.clear();
            joiner_give_up(why);
        }
        return;
    }

    if (op != "peer") return;
    PendingPeer peer;
    if (!parse_hex_token(msg.str("token"), peer.token)) return;
    for (const std::string& ep : msg.list("eps")) peer.candidates.push_back(as_udp(ep));
    peer.startedMs = now_ms();

    if (s_host) {
        coop_log::info("coop_mod: [ONLINE] a joiner is on the way ({} addresses)",
            peer.candidates.size());
        s_peers.push_back(std::move(peer));
        probe(s_peers.back());
        return;
    }
    s_target = std::move(peer);
    s_hostUpnp = msg.str("upnp");
    s_sameNet = msg.boolean("sameNet");
    coop_log::info("coop_mod: [ONLINE] introduced to the host: {} addresses, upnp '{}', same "
                   "network {}", s_target.candidates.size(), s_hostUpnp, s_sameNet);
    if (s_target.candidates.empty() && s_hostUpnp.empty()) {
        joiner_give_up(kTailscaleHint);
        return;
    }
    set_phase(Phase::Punching, "Connecting to the host...");
    probe(s_target);
}

void pump_websocket() {
    mods::ws::Event event;
    while (mods::ws::poll(event)) {
        if (!s_ws || event.handle != s_ws.handle()) continue;
        switch (event.type) {
        case WEBSOCKET_EVENT_OPEN:
            coop_log::info("coop_mod: [ONLINE] room server connected");
            break;
        case WEBSOCKET_EVENT_MESSAGE:
            if (event.messageKind == WEBSOCKET_MESSAGE_TEXT) {
                on_server_message(std::string(reinterpret_cast<const char*>(event.data.data()),
                    event.data.size()));
            }
            break;
        case WEBSOCKET_EVENT_CLOSED: {
            s_ws.detach();
            s_ws = mods::ws::Connection{};

            if (s_phase == Phase::Done || s_phase == Phase::Failed || s_phase == Phase::Idle) break;
            if (!s_host && s_phase == Phase::Punching) break;
            coop_log::info("coop_mod: [ONLINE] room server closed: {} {} {}",
                static_cast<int>(event.error), event.message, event.closeReason);
            fail(event.handshakeStatus != 0 && event.handshakeStatus != 101
                     ? "the room server answered " + std::to_string(event.handshakeStatus)
                     : "could not reach the room server");
            break;
        }
        default:
            break;
        }
    }
}

}

std::string host_key() {
    std::string key = cfg_string(coop_net_host_key_var(), "");
    if (key.size() >= 16) return key;
    static const char* const kHex = "0123456789abcdef";
    key.clear();
    for (int i = 0; i < 32; ++i) key += kHex[rng()() & 0xF];
    svc_config->set_string(mod_ctx, coop_net_host_key_var(), key.c_str());
    return key;
}

std::string host_path() {
    const std::string key = "key=" + host_key();
    if (s_code.empty()) return "/host?" + key;
    return "/host?code=" + s_code + (s_named ? "&strict=1" : "") + "&" + key;
}

void online_host_begin(int udpPort, const std::string& name) {
    online_stop();
    s_host = true;
    s_localPort = udpPort;
    s_reconnects = 0;
    s_named = !name.empty();
    s_code = name;
    open_ws(host_path());
}

void online_join_begin(const std::string& code, int localUdpPort) {
    online_stop();
    s_host = false;
    s_code = code;
    s_localPort = localUdpPort;
    s_hostUpnp.clear();
    if (!open_ws("/join/" + code)) coop_online_failed(s_status, "");
}

void online_stop() {
    close_ws();
    s_phase = Phase::Idle;
    s_status.clear();
    s_peers.clear();
    s_stunAsks.clear();
    s_target = PendingPeer{};
    s_reconnectAtMs = 0;
    s_code.clear();
}

bool online_active() {
    return s_phase != Phase::Idle;
}

std::string online_room_code() {
    return s_host ? s_code : std::string{};
}

std::string online_status() {
    return s_status;
}

bool online_accept_token(uint64_t token) {
    if (!s_host) return false;
    for (PendingPeer& p : s_peers) {
        if (p.token == token && !p.accepted) {
            p.accepted = true;
            return true;
        }
    }
    return false;
}

bool online_on_datagram(const std::string& from, const uint8_t* data, size_t size) {
    if (s_phase == Phase::Idle && s_peers.empty()) return false;
    if (on_punch(from, data, size)) return true;

    if (size >= 20 && data[4] == 0x21 && data[5] == 0x12 && data[6] == 0xA4 && data[7] == 0x42 &&
        (data[0] & 0xC0) == 0) {
        return on_stun(data, size);
    }
    return false;
}

void online_update() {
    pump_websocket();
    const uint64_t now = now_ms();

    if (s_host && s_phase == Phase::Failed && s_reconnectAtMs != 0 && now >= s_reconnectAtMs) {
        s_reconnectAtMs = 0;
        if (!open_ws(host_path())) host_lost_server("could not reach it");
        return;
    }

    switch (s_phase) {
    case Phase::Connecting:
        if (now - s_phaseStartMs > kWelcomeTimeoutMs) fail("the room server did not answer");
        break;
    case Phase::Stun:
        update_stun();
        break;
    case Phase::Waiting:
        if (!s_host && now - s_phaseStartMs > kPeerTimeoutMs) {
            joiner_give_up("The host did not answer. Is the game still open on their side?");
        }
        break;
    case Phase::Punching:
        if (now - s_phaseStartMs > kJoinPunchMs) {

            joiner_give_up(s_sameNet ? "You are on the same network as the host. Use Join by "
                                       "address with their local address instead."
                                     : kTailscaleHint);
            break;
        }
        probe(s_target);
        break;
    default:
        break;
    }

    if (s_host) {

        if (s_phase == Phase::Waiting && s_ws) {
            const std::string upnp = upnp_external_address();
            if (upnp != s_sentUpnp) {
                s_sentUpnp = upnp;
                send_json("{\"op\":\"update\",\"upnp\":\"" + json_escape(upnp) + "\"}");
            }
        }
        for (PendingPeer& p : s_peers) {
            if (!p.accepted && now - p.startedMs < kHostPunchMs) probe(p);
        }
        s_peers.erase(std::remove_if(s_peers.begin(), s_peers.end(),
                          [&](const PendingPeer& p) {
                              return p.accepted || now - p.startedMs >= kHostPunchMs;
                          }),
            s_peers.end());
    }
}
