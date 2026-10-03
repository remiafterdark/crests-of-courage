

#include "mod.hpp"
#include "net/json.hpp"
#include "net/protocol.hpp"
#include "net/messages.hpp"
#include "net/seal.hpp"
#include "print.hpp"

#include "mods/svc/websocket.hpp"

#include "d/actor/d_a_alink.h"
#include "d/d_bg_s_gnd_chk.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_mng.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

const uint32_t kPunchMagic = 0x31475043u;
const uint8_t kPunchProbe = 1;
const uint8_t kPunchAnswer = 2;
const size_t kPunchSize = 4 + 8 + 1;

enum GlobalType : uint8_t {
    kGlobalPresence = 1,
    kGlobalSnapshot = 2,
    kGlobalSkins = 3,
    kGlobalColors = 4,
    kGlobalBye = 5,
    kGlobalHorse = 6,
    kGlobalMidna = 7,
    kGlobalSounds = 8,
    kGlobalParticles = 9,
};

const uint64_t kPunchForMs = 10000;
const uint64_t kPunchEveryMs = 100;
const uint64_t kQuietDropMs = 15000;
const uint64_t kStunWaitMs = 2500;

const int kMaxPacketsPerSecond = 400;
const int kFloodStrikes = 3;

uint64_t now_ms() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

std::mt19937_64& rng() {
    static std::mt19937_64 r(static_cast<uint64_t>(now_ms()) ^ 0x9E3779B97F4A7C15ull);
    return r;
}

struct Peer {
    bool used = false;
    uint32_t id = 0;
    std::string tag;
    std::string name;
    uint64_t token = 0;
    uint8_t key[32] = {};
    uint32_t keyId = 0;
    uint32_t mine = seal::kFromHost;
    uint32_t theirs = seal::kFromJoiner;
    std::vector<std::string> candidates;
    std::string endpoint;
    bool reached = false;
    bool gaveUp = false;
    uint64_t startedMs = 0;
    uint64_t lastProbeMs = 0;
    uint64_t heardMs = 0;
    uint64_t counter = 0;
    seal::Window seen;
    uint64_t secondStart = 0;
    int packets = 0;
    int strikes = 0;
    uint32_t lastSnapSeq = 0;
    bool haveSnapSeq = false;

    bool havePos = false;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    int8_t room = -1;
    uint32_t lastSentSeq = 0;
    uint32_t lastHorseTick = 0;
    uint32_t lastHorseSeq = 0;
    bool haveHorseSeq = false;
    uint32_t lastMidnaTick = 0;
    uint32_t lastMidnaSeq = 0;
    bool haveMidnaSeq = false;
};

Peer s_peers[kCoopMaxPlayers];
uint32_t s_blocked[32] = {};
int s_blockedNext = 0;

enum class Phase { Off, Connecting, Stun, Live };
Phase s_phase = Phase::Off;
mods::ws::Connection s_ws;
uint32_t s_myId = 0;
std::vector<std::string> s_stun;
struct StunAsk {
    uint8_t txn[12];
};
std::vector<StunAsk> s_stunAsks;
std::string s_mapped;
uint64_t s_phaseMs = 0;
int s_localPort = 0;
std::string s_area = "\x01";
uint32_t s_total = 0;
uint32_t s_here = 0;
uint64_t s_retryAtMs = 0;
uint64_t s_retryDelayMs = 5000;
std::string s_status = "Off";
uint32_t s_tick = 0;
ConfigVarHandle s_enabledVar = 0;

bool enabled() {
    return cfg_bool(s_enabledVar, false);
}

ConfigVarHandle s_blockedVar = 0;
ConfigVarHandle s_keyVar = 0;
ConfigVarHandle s_tagsVar = 0;
ConfigVarHandle s_tagDistVar = 0;
std::vector<GlobalPlayer> s_userBlocked;

std::vector<GlobalPlayer> s_seen;

void remember_seen(uint32_t id, const std::string& tag) {
    if (id == 0 || tag.empty()) return;
    for (const GlobalPlayer& s : s_seen) {
        if (s.id == id) return;
    }
    s_seen.push_back(GlobalPlayer{id, "", tag});
    if (s_seen.size() > 256) s_seen.erase(s_seen.begin());
}

bool is_tag(const std::string& tag) {
    if (tag.size() != 16) return false;
    for (const char c : tag) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

void load_blocked() {
    s_userBlocked.clear();
    const std::string all = cfg_string(s_blockedVar, "");
    size_t at = 0;
    while (at < all.size()) {
        size_t end = all.find('\n', at);
        if (end == std::string::npos) end = all.size();
        const std::string line = all.substr(at, end - at);
        at = end + 1;
        const std::string tag = line.substr(0, 16);
        if (!is_tag(tag)) continue;
        s_userBlocked.push_back(GlobalPlayer{0, line.size() > 17 ? line.substr(17) : "", tag});
    }
}

void save_blocked() {
    std::string all;
    for (const GlobalPlayer& b : s_userBlocked) all += b.tag + " " + b.name + "\n";
    if (s_blockedVar != 0) svc_config->set_string(mod_ctx, s_blockedVar, all.c_str());
}

bool tag_blocked(const std::string& tag) {
    if (!is_tag(tag)) return false;
    for (const GlobalPlayer& b : s_userBlocked) {
        if (b.tag == tag) return true;
    }
    return false;
}

std::string install_key() {
    std::string key = cfg_string(s_keyVar, "");
    bool ok = key.size() == 32;
    for (const char c : key) ok = ok && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    if (ok) return key;
    std::random_device device;
    static const char kHex[] = "0123456789abcdef";
    key.clear();
    for (int i = 0; i < 32; ++i) key += kHex[device() & 15];
    if (s_keyVar != 0) svc_config->set_string(mod_ctx, s_keyVar, key.c_str());
    return key;
}

bool blocked(uint32_t id) {
    for (uint32_t b : s_blocked) {
        if (b != 0 && b == id) return true;
    }
    return false;
}

bool parse_hex(const std::string& text, uint8_t* out, size_t bytes) {
    if (text.size() != bytes * 2) return false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        uint8_t v;
        if (c >= '0' && c <= '9') v = static_cast<uint8_t>(c - '0');
        else if (c >= 'a' && c <= 'f') v = static_cast<uint8_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v = static_cast<uint8_t>(c - 'A' + 10);
        else return false;
        out[i / 2] = static_cast<uint8_t>((i % 2 == 0) ? v << 4 : out[i / 2] | v);
    }
    return true;
}

std::string current_area() {
    if (daAlink_getAlinkActorClass() == nullptr || coop_on_title_screen()) return "";
    const char* stage = dComIfGp_getStartStageName();
    if (stage == nullptr) return "";
    std::string out;
    for (int i = 0; i < 8 && stage[i] != '\0'; ++i) {
        const char c = stage[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            return "";
        }
        out.push_back(c);
    }
    return out;
}

void send_json(const std::string& text) {
    if (s_ws) s_ws.send_text(text);
}

std::string json_text(const std::string& text) {
    std::string out;
    for (const char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (u < 0x20) {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out;
}

void send_to(Peer& p, uint8_t type, const void* payload, size_t size) {
    if (!p.used || !p.reached || p.endpoint.empty()) return;
    std::vector<uint8_t> plain(1 + size);
    plain[0] = type;
    if (size > 0) std::memcpy(plain.data() + 1, payload, size);
    std::vector<uint8_t> packet(plain.size() + seal::kOverhead);
    seal::wrap(p.key, p.keyId, p.mine, ++p.counter, plain.data(), plain.size(), packet.data());
    coop_udp_send_raw(p.endpoint, packet.data(), packet.size());
}

void send_punch(const std::string& to, uint64_t token, uint8_t kind) {
    uint8_t packet[kPunchSize];
    std::memcpy(packet, &kPunchMagic, 4);
    std::memcpy(packet + 4, &token, 8);
    packet[12] = kind;
    coop_udp_send_raw(to, packet, sizeof(packet));
}

void send_looks(Peer& p) {
    MsgSkinChoices skins{};
    features_build_skin_choices(&skins);
    send_to(p, kGlobalSkins, &skins, sizeof(skins));
    MsgColorEntry colors[kCoopColorSlots] = {};
    colors_build_local(colors);
    send_to(p, kGlobalColors, colors, sizeof(colors));
}

void release(int slot, const char* why) {
    Peer& p = s_peers[slot];
    if (!p.used) return;
    if (p.reached) send_to(p, kGlobalBye, nullptr, 0);
    coop_log::info("coop_mod: [GLOBAL] player {} slot {} {}", p.id, slot, why);
    p = Peer{};
    puppet_hook_release_player(static_cast<uint8_t>(slot));
    features_global_forget(static_cast<uint8_t>(slot));
}

void release_all(const char* why) {
    for (int i = 1; i < kCoopMaxPlayers; ++i) release(i, why);
}

int slot_of_id(uint32_t id) {
    for (int i = 1; i < kCoopMaxPlayers; ++i) {
        if (s_peers[i].used && s_peers[i].id == id) return i;
    }
    return -1;
}

int free_slot() {
    for (int i = 1; i < kCoopMaxPlayers; ++i) {
        if (!s_peers[i].used) return i;
    }
    return -1;
}

void block(int slot, const char* why) {
    s_blocked[s_blockedNext] = s_peers[slot].id;
    s_blockedNext = (s_blockedNext + 1) % 32;
    release(slot, why);
}

bool sane_float(float v, float limit = 1.0e6f) {
    return std::isfinite(v) && std::fabs(v) < limit;
}

const int kGlobalParticlesPerPacket = 8;

bool clean_particle(const MsgParticleEntry& e) {
    if (e.type >= 19) return false;
    for (float v : e.rel) {
        if (!sane_float(v, 5000.0f)) return false;
    }
    for (float v : e.scale) {
        if (!sane_float(v, 50.0f)) return false;
    }
    for (float v : e.gscl) {
        if (!sane_float(v, 50.0f)) return false;
    }
    for (float v : e.pscl) {
        if (!sane_float(v, 50.0f)) return false;
    }
    for (float v : e.dir) {
        if (!sane_float(v, 10.0f)) return false;
    }
    for (float v : e.grot) {
        if (!sane_float(v, 10.0f)) return false;
    }
    return sane_float(e.blend, 10.0f) && sane_float(e.rate, 50.0f) && sane_float(e.awayCenter, 1000.0f) &&
           sane_float(e.awayAxis, 1000.0f) && sane_float(e.dirSpeed, 1000.0f) && e.life < 600;
}

void clean_text(char* text, size_t size) {
    text[size - 1] = '\0';
    for (size_t i = 0; i < size && text[i] != '\0'; ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x20 || c == 0x7F) text[i] = '?';
    }
}

void clean_word(char* text, size_t size) {
    bool ended = false;
    for (size_t i = 0; i < size; ++i) {
        const char c = text[i];
        if (c == '\0') ended = true;
        if (ended) {
            text[i] = '\0';
            continue;
        }
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            std::memset(text, 0, size);
            return;
        }
    }
    if (!ended) std::memset(text, 0, size);
}

void clean_model_name(char* text, size_t size) {
    text[size - 1] = '\0';
    for (size_t i = 0; i < size && text[i] != '\0'; ++i) {
        const char c = text[i];
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                        c == ' ' || c == '_' || c == '-' || c == '(' || c == ')' || c == '\'' ||
                        c == '.' || c == '&' || c == '!';
        if (!ok) {
            std::memset(text, 0, size);
            return;
        }
    }
    if (std::strstr(text, "..") != nullptr) std::memset(text, 0, size);
}

bool clean_presence(MsgPresence& m) {
    clean_text(m.name, sizeof(m.name));
    clean_word(m.stage, sizeof(m.stage));
    if (!sane_float(m.x) || !sane_float(m.y) || !sane_float(m.z)) return false;
    m.storyBits = 0;
    if (m.maxLife > 400) m.maxLife = 400;
    if (m.life > m.maxLife * 4 / 5 + 4) m.life = static_cast<uint16_t>(m.maxLife * 4 / 5);
    return true;
}

bool clean_snapshot(PlayerSnapshot& s) {
    if (!sane_float(s.posX) || !sane_float(s.posY) || !sane_float(s.posZ)) return false;
    for (const AnmSlotSnapshot* half : {s.under, s.upper}) {
        for (int i = 0; i < 3; ++i) {
            if (!sane_float(half[i].frame, 1.0e5f) || !sane_float(half[i].rate, 1.0e3f)) return false;
        }
    }
    for (const AttachedModelSnapshot& a : s.attached) {
        if (!sane_float(a.frame, 1.0e5f) || !sane_float(a.scale, 1.0e3f)) return false;
        for (float m : a.mtx) {
            if (!sane_float(m)) return false;
        }
    }
    for (auto& pt : s.chainPts) {
        for (float v : pt) {
            if (!sane_float(v)) return false;
        }
    }
    if (!sane_float(s.rootClearX) || !sane_float(s.rootClearY) || !sane_float(s.rootClearZ) ||
        !sane_float(s.vfxDigPos[0]) || !sane_float(s.vfxDigPos[1]) || !sane_float(s.vfxDigPos[2]) ||
        !sane_float(s.warpScroll, 1.0e5f) || !sane_float(s.warpDissolve, 1.0e5f)) {
        return false;
    }
    if (s.outfit > kPuppetOutfitWolf) s.outfit = kPuppetOutfitDefault;
    if (s.sword > kPuppetSwordWood) s.sword = kPuppetSwordNone;
    if (s.sheath > kPuppetSheathMaster) s.sheath = kPuppetSheathNone;
    if (s.chainKind > kPuppetChainRodLure) s.chainKind = kPuppetChainNone;
    if (s.chainCount > kPuppetChainPts) s.chainCount = kPuppetChainPts;
    for (AttachedModelSnapshot& a : s.attached) {
        if (a.kind >= kPuppetHeldCount) a.kind = kPuppetHeldNone;
    }
    clean_word(s.shieldArc, sizeof(s.shieldArc));
    clean_word(s.rodArc, sizeof(s.rodArc));
    clean_word(s.rideArc, sizeof(s.rideArc));
    return true;
}

void on_sealed(int slot, const uint8_t* data, size_t size) {
    if (size < 1) return;
    const uint8_t type = data[0];
    const uint8_t* payload = data + 1;
    const size_t len = size - 1;
    Peer& p = s_peers[slot];
    switch (type) {
    case kGlobalPresence: {
        if (len != sizeof(MsgPresence)) return;
        MsgPresence m;
        std::memcpy(&m, payload, sizeof(m));
        if (!clean_presence(m)) return;
        {
            char name[sizeof(m.name) + 1] = {};
            std::memcpy(name, m.name, sizeof(m.name));
            p.name = name;
        }
        features_on_message(kMsgPresence, reinterpret_cast<const uint8_t*>(&m), sizeof(m),
            static_cast<uint8_t>(slot));
        break;
    }
    case kGlobalSnapshot: {
        if (len != sizeof(PlayerSnapshot)) return;
        PlayerSnapshot s;
        std::memcpy(&s, payload, sizeof(s));
        if (!clean_snapshot(s)) return;

        if (p.haveSnapSeq && static_cast<int32_t>(s.seq - p.lastSnapSeq) <= 0 &&
            p.lastSnapSeq - s.seq < 240) {
            return;
        }
        p.haveSnapSeq = true;
        p.lastSnapSeq = s.seq;
        p.havePos = true;
        p.x = s.posX;
        p.y = s.posY;
        p.z = s.posZ;
        p.room = s.roomNo;
        coop_accept_global_snapshot(static_cast<uint8_t>(slot), s);
        break;
    }
    case kGlobalSkins: {
        if (len != sizeof(MsgSkinChoices)) return;
        MsgSkinChoices m;
        std::memcpy(&m, payload, sizeof(m));
        for (auto& name : m.name) clean_model_name(name, sizeof(name));
        features_on_message(kMsgSkinChoices, reinterpret_cast<const uint8_t*>(&m), sizeof(m),
            static_cast<uint8_t>(slot));
        break;
    }
    case kGlobalColors:
        if (len != sizeof(MsgColorEntry) * kCoopColorSlots) return;
        features_on_message(kMsgColors, payload, len, static_cast<uint8_t>(slot));
        break;
    case kGlobalHorse: {
        if (len != sizeof(HorseSnapshot)) return;
        HorseSnapshot h;
        std::memcpy(&h, payload, sizeof(h));
        if (h.magic != kHorseSnapshotMagic) return;
        for (float v : h.baseMtx) {
            if (!sane_float(v)) return;
        }
        if (!sane_float(h.idleFrame, 1.0e5f) || !sane_float(h.idleRate, 1.0e3f)) return;
        if (h.jointCount > kHorseJoints) h.jointCount = kHorseJoints;
        if (h.reinHand > 3) h.reinHand = kHorseReinHold;
        if (p.haveHorseSeq && static_cast<int32_t>(h.seq - p.lastHorseSeq) <= 0 &&
            p.lastHorseSeq - h.seq < 240) {
            return;
        }
        p.haveHorseSeq = true;
        p.lastHorseSeq = h.seq;
        h.playerId = static_cast<uint8_t>(slot);
        puppet_hook_on_horse_snapshot(static_cast<uint8_t>(slot), h);
        break;
    }
    case kGlobalMidna: {
        if (len != sizeof(MidnaSnapshot)) return;
        MidnaSnapshot m;
        std::memcpy(&m, payload, sizeof(m));
        if (m.magic != kMidnaSnapshotMagic || m.mode > kMidnaModeShadow) return;
        if (!sane_float(m.baseScale, 100.0f)) return;
        for (float v : m.baseMtx) {
            if (!sane_float(v)) return;
        }
        for (float v : m.hairMtx) {
            if (!sane_float(v)) return;
        }
        for (const MidnaJointSnapshot& j : m.joints) {
            for (float v : j.pos) {
                if (!sane_float(v)) return;
            }
        }
        if (p.haveMidnaSeq && static_cast<int32_t>(m.seq - p.lastMidnaSeq) <= 0 &&
            p.lastMidnaSeq - m.seq < 240) {
            return;
        }
        p.haveMidnaSeq = true;
        p.lastMidnaSeq = m.seq;
        m.playerId = static_cast<uint8_t>(slot);
        puppet_hook_on_midna_snapshot(static_cast<uint8_t>(slot), m);
        break;
    }
    case kGlobalSounds: {

        if (len < 1) return;
        const size_t count = payload[0];
        if (count > static_cast<size_t>(kCoopMaxSoundsPerMessage) || len != 1 + count * sizeof(MsgSoundEntry)) return;
        for (size_t i = 0; i < count; ++i) {
            MsgSoundEntry e;
            std::memcpy(&e, payload + 1 + i * sizeof(e), sizeof(e));
            if (e.id >= 0x000A0000u || e.kind > 1) return;
            for (float v : e.rel) {
                if (!sane_float(v, 5000.0f)) return;
            }
        }
        fx_on_sounds(payload, len, static_cast<uint8_t>(slot));
        break;
    }
    case kGlobalParticles: {

        if (len < 1) return;
        const size_t count = payload[0];
        if (count > static_cast<size_t>(kGlobalParticlesPerPacket) || len != 1 + count * sizeof(MsgParticleEntry)) return;
        for (size_t i = 0; i < count; ++i) {
            MsgParticleEntry e;
            std::memcpy(&e, payload + 1 + i * sizeof(e), sizeof(e));
            if (!clean_particle(e)) return;
        }
        fx_on_particles(payload, len, static_cast<uint8_t>(slot));
        break;
    }
    case kGlobalBye:
        release(slot, "left");
        break;
    default:
        break;
    }
}

std::string s_sentName;
std::string s_pendingName;
uint64_t s_pendingNameMs = 0;

void send_name_if_changed(uint64_t now) {
    const std::string name = features_local_name();
    if (name == s_sentName) {
        s_pendingName.clear();
        return;
    }
    if (name != s_pendingName) {
        s_pendingName = name;
        s_pendingNameMs = now;
        return;
    }
    if (now - s_pendingNameMs < 1000) return;
    s_sentName = name;
    s_pendingName.clear();
    send_json("{\"op\":\"name\",\"name\":\"" + json_text(name) + "\"}");
}

void say_hello() {
    std::string hello = "{\"op\":\"hello\",\"v\":" + std::to_string(kCoopWireVersion) + ",\"ep\":\"" +
                        s_mapped + "\",\"port\":" + std::to_string(s_localPort);
    const std::string lan = coop_lan_address();
    if (!lan.empty()) hello += ",\"lan\":\"" + lan + "\"";
    s_sentName = features_local_name();
    hello += ",\"name\":\"" + json_text(s_sentName) + "\"";
    hello += ",\"key\":\"" + install_key() + "\"";
    send_json(hello + "}");
    chat_first_online();
    s_phase = Phase::Live;
    s_area = "\x01";
    s_status = "Online";
    s_retryDelayMs = 5000;
    coop_log::info("coop_mod: [GLOBAL] online as {}", s_myId);
}

void on_server_message(const std::string& text) {
    JsonObject msg;
    JsonReader reader(text);
    if (!reader.object(msg)) return;
    const std::string op = msg.str("op");
    if (op == "welcome") {
        s_myId = static_cast<uint32_t>(std::strtoul(msg.str("id").c_str(), nullptr, 10));
        s_stun = msg.list("stun");
        s_stunAsks.clear();
        s_mapped.clear();
        for (const std::string& server : s_stun) {
            StunAsk ask;
            for (uint8_t& b : ask.txn) b = static_cast<uint8_t>(rng()());
            uint8_t packet[20] = {0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42};
            std::memcpy(packet + 8, ask.txn, 12);
            coop_udp_send_raw("udp://" + server, packet, sizeof(packet));
            s_stunAsks.push_back(ask);
        }
        s_phase = Phase::Stun;
        s_phaseMs = now_ms();
        return;
    }
    if (op == "chat") {
        const std::string tag = msg.str("tag");
        remember_seen(static_cast<uint32_t>(std::strtoul(msg.str("id").c_str(), nullptr, 10)),
            is_tag(tag) ? tag : "");
        chat_on_message(static_cast<uint32_t>(std::strtoul(msg.str("id").c_str(), nullptr, 10)),
            is_tag(tag) ? tag : "", msg.str("name"), msg.str("text"));
        return;
    }
    if (op == "rename") {
        chat_on_rename(static_cast<uint32_t>(std::strtoul(msg.str("id").c_str(), nullptr, 10)),
            msg.str("name"));
        return;
    }
    if (op == "chat_slow") {
        chat_note("Slow down a little.");
        return;
    }
    if (op == "chat_banned") {
        chat_note("You can't use chat.");
        return;
    }
    if (op == "count") {
        s_total = static_cast<uint32_t>(std::strtoul(msg.str("total").c_str(), nullptr, 10));
        s_here = static_cast<uint32_t>(std::strtoul(msg.str("here").c_str(), nullptr, 10));
        return;
    }
    if (op == "gone") {
        const int slot = slot_of_id(static_cast<uint32_t>(std::strtoul(msg.str("id").c_str(), nullptr, 10)));
        if (slot > 0) release(slot, "left the area");
        return;
    }
    if (op != "peer") return;
    const uint32_t id = static_cast<uint32_t>(std::strtoul(msg.str("id").c_str(), nullptr, 10));
    const std::string tag = msg.str("tag");
    remember_seen(id, is_tag(tag) ? tag : "");
    if (tag_blocked(tag)) {

        send_json("{\"op\":\"block\",\"id\":" + std::to_string(id) + "}");
        return;
    }
    if (id == 0 || id == s_myId || blocked(id) || slot_of_id(id) > 0) return;
    const int slot = free_slot();
    if (slot < 0) return;
    Peer p;
    p.used = true;
    p.id = id;
    p.tag = is_tag(tag) ? tag : "";
    uint8_t token[8];
    if (!parse_hex(msg.str("token"), token, 8) || !parse_hex(msg.str("key"), p.key, 32)) return;
    std::memcpy(&p.token, token, 8);
    p.keyId = static_cast<uint32_t>(p.token);

    p.mine = s_myId < id ? seal::kFromHost : seal::kFromJoiner;
    p.theirs = s_myId < id ? seal::kFromJoiner : seal::kFromHost;
    for (const std::string& ep : msg.list("eps")) p.candidates.push_back("udp://" + ep);
    p.startedMs = now_ms();
    s_peers[slot] = p;
    coop_log::info("coop_mod: [GLOBAL] player {} nearby, slot {} ({} addresses)", id, slot,
        p.candidates.size());
}

void connect() {
    int port = 0;
    if (!coop_global_udp_open(&port)) {
        s_status = "Couldn't start networking";
        s_retryAtMs = now_ms() + 30000;
        return;
    }
    s_localPort = port;
    mods::ws::Options options;
    options.url = online_room_server() + "/global";
    options.connectTimeoutMs = 8000;
    options.keepaliveIntervalMs = 0;
    options.maxMessageBytes = 16 * 1024;
    s_ws = mods::ws::connect(options);
    if (!s_ws) {
        s_status = "Couldn't reach the server";
        s_retryAtMs = now_ms() + s_retryDelayMs;
        s_retryDelayMs = std::min<uint64_t>(s_retryDelayMs * 2, 120000);
        return;
    }
    s_phase = Phase::Connecting;
    s_phaseMs = now_ms();
    s_status = "Connecting...";
}

void disconnect(const char* why) {
    release_all(why);
    if (s_ws) {
        s_ws.close();
        s_ws = mods::ws::Connection{};
    }
    if (s_phase != Phase::Off) coop_log::info("coop_mod: [GLOBAL] off ({})", why);
    s_phase = Phase::Off;
    s_myId = 0;
    s_total = 0;
    s_here = 0;
    s_area = "\x01";
    coop_global_udp_close();
}

}

void global_register_vars() {
    ConfigVarDesc desc = CONFIG_VAR_DESC_INIT;
    desc.name = "hyrule_online";
    desc.type = CONFIG_VAR_BOOL;
    desc.default_bool = false;
    if (svc_config->register_var(mod_ctx, &desc, &s_enabledVar) != MOD_OK) s_enabledVar = 0;
    ConfigVarDesc list = CONFIG_VAR_DESC_INIT;
    list.name = "hyrule_online_blocked";
    list.type = CONFIG_VAR_STRING;
    list.default_string = "";
    if (svc_config->register_var(mod_ctx, &list, &s_blockedVar) != MOD_OK) s_blockedVar = 0;
    ConfigVarDesc key = CONFIG_VAR_DESC_INIT;
    key.name = "hyrule_online_key";
    key.type = CONFIG_VAR_STRING;
    key.default_string = "";
    if (svc_config->register_var(mod_ctx, &key, &s_keyVar) != MOD_OK) s_keyVar = 0;
    ConfigVarDesc tags = CONFIG_VAR_DESC_INIT;
    tags.name = "hyrule_online_nametags";
    tags.type = CONFIG_VAR_BOOL;
    tags.default_bool = true;
    if (svc_config->register_var(mod_ctx, &tags, &s_tagsVar) != MOD_OK) s_tagsVar = 0;
    ConfigVarDesc dist = CONFIG_VAR_DESC_INIT;
    dist.name = "hyrule_online_nametag_distance";
    dist.type = CONFIG_VAR_INT;
    dist.default_int = 8000;
    if (svc_config->register_var(mod_ctx, &dist, &s_tagDistVar) != MOD_OK) s_tagDistVar = 0;
    load_blocked();
}

ConfigVarHandle global_enabled_var() {
    return s_enabledVar;
}

ConfigVarHandle global_nametags_var() {
    return s_tagsVar;
}

ConfigVarHandle global_nametag_distance_var() {
    return s_tagDistVar;
}

bool global_active() {
    return s_phase == Phase::Live;
}

bool global_blocked(const std::string& tag) {
    return tag_blocked(tag);
}

void global_set_blocked(const GlobalPlayer& who, bool on) {
    if (!is_tag(who.tag)) return;
    if (on) {
        if (!tag_blocked(who.tag)) s_userBlocked.push_back(GlobalPlayer{0, who.name, who.tag});
        save_blocked();
        for (int i = 1; i < kCoopMaxPlayers; ++i) {
            if (s_peers[i].used && s_peers[i].tag == who.tag) release(i, "blocked");
        }
        for (const GlobalPlayer& s : s_seen) {
            if (s.tag == who.tag) send_json("{\"op\":\"block\",\"id\":" + std::to_string(s.id) + "}");
        }
        chat_forget_player(who.tag);
        coop_log::info("coop_mod: [GLOBAL] blocked {}", who.tag);
        return;
    }
    s_userBlocked.erase(std::remove_if(s_userBlocked.begin(), s_userBlocked.end(),
                            [&](const GlobalPlayer& b) { return b.tag == who.tag; }),
        s_userBlocked.end());
    save_blocked();

    for (const GlobalPlayer& s : s_seen) {
        if (s.tag == who.tag) send_json("{\"op\":\"unblock\",\"id\":" + std::to_string(s.id) + "}");
    }
    coop_log::info("coop_mod: [GLOBAL] unblocked {}", who.tag);
}

std::vector<GlobalPlayer> global_players() {
    std::vector<GlobalPlayer> out;
    for (int i = 1; i < kCoopMaxPlayers; ++i) {
        const Peer& p = s_peers[i];
        if (p.used && p.reached && !p.name.empty() && is_tag(p.tag)) out.push_back(GlobalPlayer{p.id, p.name, p.tag});
    }
    return out;
}

std::vector<GlobalPlayer> global_blocked_list() {
    return s_userBlocked;
}

bool global_slot_present(uint8_t slot) {
    return slot > 0 && slot < kCoopMaxPlayers && s_peers[slot].used && s_peers[slot].reached;
}

std::string global_status() {
    if (!enabled()) return "Off";
    if (coop_net_connected() || coop_net_connecting()) return "Paused while in co-op";
    if (s_phase != Phase::Live) return s_status;
    int here = 0;
    for (int i = 1; i < kCoopMaxPlayers; ++i) {
        if (global_slot_present(static_cast<uint8_t>(i))) ++here;
    }
    return "Online: " + std::to_string(s_total) + " players, " + std::to_string(here) + " here";
}

void global_on_ws_event(const mods::ws::Event& event) {
    if (!s_ws || event.handle != s_ws.handle()) return;
    switch (event.type) {
    case WEBSOCKET_EVENT_MESSAGE:
        if (event.messageKind == WEBSOCKET_MESSAGE_TEXT && event.data.size() < 16 * 1024) {
            on_server_message(std::string(reinterpret_cast<const char*>(event.data.data()),
                event.data.size()));
        }
        break;
    case WEBSOCKET_EVENT_CLOSED:
        s_ws.detach();
        s_ws = mods::ws::Connection{};
        release_all("server connection closed");
        s_phase = Phase::Off;
        s_status = "Lost the server, trying again...";
        s_retryAtMs = now_ms() + s_retryDelayMs;
        s_retryDelayMs = std::min<uint64_t>(s_retryDelayMs * 2, 120000);
        coop_log::info("coop_mod: [GLOBAL] server closed: {} {}", event.message, event.closeReason);
        break;
    default:
        break;
    }
}

bool global_on_datagram(const std::string& from, const uint8_t* data, size_t size) {
    if (s_phase == Phase::Off) return false;

    if (s_phase == Phase::Stun && size >= 20 && data[4] == 0x21 && data[5] == 0x12 &&
        data[6] == 0xA4 && data[7] == 0x42 && (data[0] & 0xC0) == 0) {
        const bool ours = std::any_of(s_stunAsks.begin(), s_stunAsks.end(),
            [&](const StunAsk& a) { return std::memcmp(a.txn, data + 8, 12) == 0; });
        std::string mapped;
        if (ours && s_mapped.empty() && online_parse_stun(data, size, mapped)) s_mapped = mapped;
        return true;
    }

    if (size == kPunchSize) {
        uint32_t magic;
        std::memcpy(&magic, data, 4);
        if (magic != kPunchMagic) return false;
        uint64_t token;
        std::memcpy(&token, data + 4, 8);
        for (int i = 1; i < kCoopMaxPlayers; ++i) {
            Peer& p = s_peers[i];
            if (!p.used || p.token != token) continue;
            if (data[12] == kPunchProbe) send_punch(from, token, kPunchAnswer);
            if (!p.reached) {
                p.reached = true;
                p.endpoint = from;
                p.heardMs = now_ms();
                coop_log::info("coop_mod: [GLOBAL] reached player {}", p.id);
                MsgPresence presence{};
                features_build_presence(&presence);
                send_to(p, kGlobalPresence, &presence, sizeof(presence));
                send_looks(p);
            }
        }
        return true;
    }
    if (!seal::looks_sealed(data, size)) return false;
    const uint32_t keyId = seal::key_id_of(data);
    for (int i = 1; i < kCoopMaxPlayers; ++i) {
        Peer& p = s_peers[i];
        if (!p.used || !p.reached || p.keyId != keyId) continue;
        std::vector<uint8_t> plain(size - seal::kOverhead);
        if (!seal::unwrap(p.key, p.theirs, data, size, plain.data())) return true;
        if (!p.seen.take(seal::counter_of(data))) return true;
        const uint64_t now = now_ms();
        if (now - p.secondStart >= 1000) {
            if (p.packets > kMaxPacketsPerSecond && ++p.strikes >= kFloodStrikes) {
                block(i, "sent too much, blocked for this session");
                return true;
            }
            p.secondStart = now;
            p.packets = 0;
        }
        if (++p.packets > kMaxPacketsPerSecond) return true;

        if (p.endpoint != from) p.endpoint = from;
        p.heardMs = now;
        on_sealed(i, plain.data(), plain.size());
        return true;
    }
    return false;
}

int send_interval(const Peer& p, float x, float y, float z, int8_t room) {
    if (!p.havePos) return 6;
    if (p.room != room && p.room >= 0 && room >= 0) return 30;
    const float dx = p.x - x;
    const float dy = p.y - y;
    const float dz = p.z - z;
    const float d2 = dx * dx + dy * dy + dz * dz;
    if (d2 < 10000.0f * 10000.0f) return 2;
    if (d2 < 25000.0f * 25000.0f) return 4;
    return 10;
}

bool global_send_chat(const std::string& text) {
    if (s_phase != Phase::Live || !s_ws) return false;
    static uint64_t s_lastMs = 0;
    const uint64_t now = now_ms();
    if (s_lastMs != 0 && now - s_lastMs < 2000) {
        chat_note("Slow down a little.");
        return true;
    }
    s_lastMs = now;
    send_json("{\"op\":\"chat\",\"text\":\"" + json_text(text) + "\"}");
    return true;
}

uint32_t global_my_id() {
    return s_myId;
}

bool fx_reaches(const Peer& p) {
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (!p.reached || !p.havePos || alink == nullptr) return false;
    const int8_t room = static_cast<int8_t>(fopAcM_GetRoomNo(alink));
    if (p.room != room && p.room >= 0 && room >= 0) return false;
    const float dx = p.x - alink->current.pos.x;
    const float dy = p.y - alink->current.pos.y;
    const float dz = p.z - alink->current.pos.z;
    return dx * dx + dy * dy + dz * dz < 6000.0f * 6000.0f;
}

void global_send_sounds(const uint8_t* payload, size_t size) {
    if (s_phase != Phase::Live) return;
    for (int i = 1; i < kCoopMaxPlayers; ++i) {
        if (fx_reaches(s_peers[i])) send_to(s_peers[i], kGlobalSounds, payload, size);
    }
}

void global_send_particles(const MsgParticleEntry* entries, int count) {
    if (s_phase != Phase::Live || count <= 0) return;
    if (count > kGlobalParticlesPerPacket * 2) count = kGlobalParticlesPerPacket * 2;
    for (int sent = 0; sent < count;) {
        const int n = std::min(count - sent, kGlobalParticlesPerPacket);
        uint8_t buffer[1 + kGlobalParticlesPerPacket * sizeof(MsgParticleEntry)];
        buffer[0] = static_cast<uint8_t>(n);
        std::memcpy(buffer + 1, entries + sent, n * sizeof(MsgParticleEntry));
        for (int i = 1; i < kCoopMaxPlayers; ++i) {
            if (fx_reaches(s_peers[i])) send_to(s_peers[i], kGlobalParticles, buffer, 1 + n * sizeof(MsgParticleEntry));
        }
        sent += n;
    }
}

void global_send_snapshot(const PlayerSnapshot& snapshot) {
    if (s_phase != Phase::Live) return;
    for (int i = 1; i < kCoopMaxPlayers; ++i) {
        Peer& p = s_peers[i];
        if (!p.reached) continue;
        const int every = send_interval(p, snapshot.posX, snapshot.posY, snapshot.posZ, snapshot.roomNo);
        if (static_cast<int32_t>(snapshot.seq - p.lastSentSeq) < every) continue;
        p.lastSentSeq = snapshot.seq;
        send_to(p, kGlobalSnapshot, &snapshot, sizeof(snapshot));
    }
}

void global_send_horse(const HorseSnapshot& snap) {
    if (s_phase != Phase::Live) return;
    const float hx = snap.baseMtx[3];
    const float hy = snap.baseMtx[7];
    const float hz = snap.baseMtx[11];
    const bool riding = (snap.flags & kHorseFlagRiding) != 0;
    daAlink_c* alink = daAlink_getAlinkActorClass();
    for (int i = 1; i < kCoopMaxPlayers; ++i) {
        Peer& p = s_peers[i];
        if (!p.reached) continue;

        const int every = riding && alink != nullptr
            ? send_interval(p, alink->current.pos.x, alink->current.pos.y, alink->current.pos.z, snap.room)
            : send_interval(p, hx, hy, hz, snap.room);
        if (every > 2 && s_tick - p.lastHorseTick < static_cast<uint32_t>(every)) continue;
        p.lastHorseTick = s_tick;
        send_to(p, kGlobalHorse, &snap, sizeof(snap));
    }
}

void global_send_midna(const MidnaSnapshot& snap) {
    if (s_phase != Phase::Live) return;
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr) return;
    const int8_t room = static_cast<int8_t>(fopAcM_GetRoomNo(alink));
    for (int i = 1; i < kCoopMaxPlayers; ++i) {
        Peer& p = s_peers[i];
        if (!p.reached) continue;
        const int every = send_interval(p, alink->current.pos.x, alink->current.pos.y, alink->current.pos.z, room);
        if (every > 2 && s_tick - p.lastMidnaTick < static_cast<uint32_t>(every)) continue;
        p.lastMidnaTick = s_tick;
        send_to(p, kGlobalMidna, &snap, sizeof(snap));
    }
}

void global_update() {
    ++s_tick;
    const bool want = enabled() && !coop_net_connected() && !coop_net_connecting();
    if (!want) {
        if (s_phase != Phase::Off || s_ws) disconnect(enabled() ? "co-op started" : "turned off");
        s_status = enabled() ? "Paused while in co-op" : "Off";
        s_retryAtMs = 0;
        return;
    }
    const uint64_t now = now_ms();
    if (s_phase == Phase::Off) {
        if (now >= s_retryAtMs) connect();
        return;
    }
    if (s_phase == Phase::Connecting && now - s_phaseMs > 10000) {
        disconnect("the server did not answer");
        s_status = "The server did not answer, trying again...";
        s_retryAtMs = now + s_retryDelayMs;
        s_retryDelayMs = std::min<uint64_t>(s_retryDelayMs * 2, 120000);
        return;
    }
    if (s_phase == Phase::Stun) {
        if (!s_mapped.empty() || now - s_phaseMs > kStunWaitMs) say_hello();
        return;
    }
    if (s_phase != Phase::Live) return;
    send_name_if_changed(now);

    const std::string area = current_area();
    static std::string s_candidate;
    static uint32_t s_held = 0;
    if (area == s_area) {
        s_held = 0;
    } else {
        if (area != s_candidate) {
            s_candidate = area;
            s_held = 0;
        }
        const bool first = s_area == "\x01";
        if (first || ++s_held >= (area.empty() ? 300u : 90u)) {
            s_area = area;
            s_held = 0;
            release_all("changed area");
            send_json("{\"op\":\"area\",\"stage\":\"" + area + "\"}");
        }
    }

    for (int i = 1; i < kCoopMaxPlayers; ++i) {
        Peer& p = s_peers[i];
        if (!p.used) continue;
        if (!p.reached) {
            if (now - p.startedMs > kPunchForMs) {
                if (!p.gaveUp) {
                    p.gaveUp = true;
                    coop_log::info("coop_mod: [GLOBAL] could not reach player {}", p.id);
                }
                continue;
            }
            if (now - p.lastProbeMs >= kPunchEveryMs) {
                p.lastProbeMs = now;
                for (const std::string& to : p.candidates) send_punch(to, p.token, kPunchProbe);
            }
            continue;
        }
        if (now - p.heardMs > kQuietDropMs) {
            release(i, "went quiet");
            continue;
        }
        if (s_tick % 30 == static_cast<uint32_t>(i)) {
            MsgPresence presence{};
            features_build_presence(&presence);
            send_to(p, kGlobalPresence, &presence, sizeof(presence));
        }
        if (s_tick % 300 == static_cast<uint32_t>(i) * 7) send_looks(p);
    }
}

void global_shutdown() {
    disconnect("closing");
}
