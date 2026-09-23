#include "mods/service.hpp"
#include "mods/svc/config.h"
#include "mods/svc/hook.hpp"
#include "mods/svc/log.hpp"
#include "print.hpp"
#include "mods/svc/net.hpp"
#include "mods/svc/websocket.hpp"
#include "mods/svc/ui.h"
#include "mods/svc/item.h"
#include "mods/svc/host.h"
#include "mods/svc/overlay.h"
#include "mods/svc/resource.h"
#include "mods/svc/texture.h"

#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_itembase.h"
#include "d/actor/d_a_arrow.h"
#include "d/actor/d_a_boomerang.h"
#include "d/actor/d_a_mg_rod.h"
#include "d/actor/d_a_midna.h"
#include "d/actor/d_a_spinner.h"
#include "d/actor/d_a_horse.h"
#include "d/actor/d_a_nbomb.h"
#include "d/actor/d_a_canoe.h"
#include "d/actor/d_a_obj_iceleaf.h"
#include "d/d_com_inf_game.h"
#include "d/d_item_data.h"
#include "JSystem/J3DGraphBase/J3DMaterial.h"

#include "m_Do/m_Do_mtx.h"

#include "net/protocol.hpp"
#include "net/messages.hpp"
#include "net/reliable.hpp"
#include "mod.hpp"

#include "res/Object/AlAnm.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <cstdio>
#include <string_view>

void puppet_hook_init();

void puppet_hook_on_network_snapshot(uint8_t playerId, float x, float y, float z, int16_t angleX, int16_t angleY,
    int16_t angleZ, int8_t roomNo, uint8_t outfit, const AnmSlotSnapshot* under,
    const AnmSlotSnapshot* upper, uint8_t handL, uint8_t handR,
    const PlayerSnapshot& equipment);
void puppet_hook_on_midna_snapshot(uint8_t playerId, const MidnaSnapshot& snap);
void puppet_hook_on_horse_snapshot(uint8_t playerId, const HorseSnapshot& snap);

void puppet_hook_trigger_spawn();

DEFINE_MOD();
IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(ConfigService, svc_config);
IMPORT_SERVICE(NetService, svc_net);
IMPORT_SERVICE(HookService, svc_hook);
IMPORT_SERVICE(UiService, svc_ui);
IMPORT_OPTIONAL_SERVICE(ItemService, svc_item);
IMPORT_OPTIONAL_SERVICE(TextureService, svc_texture);
IMPORT_OPTIONAL_SERVICE(HostService, svc_host);
IMPORT_OPTIONAL_SERVICE(OverlayService, svc_overlay);

IMPORT_OPTIONAL_SERVICE(ResourceService, svc_resource);

IMPORT_OPTIONAL_SERVICE(WebSocketService, svc_websocket);

namespace {

ConfigVarHandle g_modeVar = 0;
ConfigVarHandle g_bindPortVar = 0;
ConfigVarHandle g_joinAddressVar = 0;
ConfigVarHandle g_joinPortVar = 0;
ConfigVarHandle g_autoConnectVar = 0;
ConfigVarHandle g_upnpVar = 0;
ConfigVarHandle g_roomCodeVar = 0;
ConfigVarHandle g_roomServerVar = 0;
ConfigVarHandle g_roomsVar = 0;
ConfigVarHandle g_hostKeyVar = 0;
ConfigVarHandle g_fakePlayersVar = 0;
ConfigVarHandle g_clearTwilightVar = 0;
ConfigVarHandle g_eponaFlagsVar = 0;
ConfigVarHandle g_giveKitVar = 0;

uint16_t g_fakeMask = 0;
}

bool upnp_owns(NetHandle handle);
void upnp_on_net_event(const mods::net::Event& event);

namespace {
ConfigVarHandle g_autoConnectDelayTicksVar = 0;
bool g_autoConnectPending = false;
uint32_t g_autoConnectTicksWaited = 0;

uint32_t g_ticksSinceRx = 0;

mods::net::Socket g_listener;
mods::net::Socket g_udp;
bool g_isHost = true;
bool g_handshakeSent = false;
bool g_connecting = false;
std::string g_statusText = "Not connected";

int g_udpPort = 0;

void remember_udp_port(const std::string& local) {
    const size_t colon = local.rfind(':');
    g_udpPort = colon == std::string::npos ? 0 : std::atoi(local.c_str() + colon + 1);
}

struct PeerLink {
    bool used = false;

    bool viaUdp = false;
    rudp::Channel rel;

    bool helloAcked = false;
    uint64_t token = 0;
    uint64_t helloSentMs = 0;
    uint64_t createdMs = 0;
    mods::net::Socket sock;
    std::vector<uint8_t> rx;
    std::string udpEndpoint;
    bool haveUdp = false;

    uint32_t lastRecvSeq = 0;
    bool haveRecvSeq = false;
};
PeerLink g_links[kCoopMaxPlayers];

uint8_t g_localId = kCoopHostId;
CoopRoster g_roster = 1u << kCoopHostId;

uint32_t g_playerQuiet[kCoopMaxPlayers] = {};

uint32_t g_playerWorldTick[kCoopMaxPlayers] = {};
bool g_playerWorldSeen[kCoopMaxPlayers] = {};
uint32_t g_playerWorldStill[kCoopMaxPlayers] = {};

bool g_playerPaused[kCoopMaxPlayers] = {};

uint32_t g_pingToken[kCoopMaxPlayers] = {};

std::chrono::steady_clock::time_point g_pingSentAt[kCoopMaxPlayers];
uint32_t g_rttMs[kCoopMaxPlayers] = {};
bool g_haveRttMs[kCoopMaxPlayers] = {};
uint32_t g_pingSentTick[kCoopMaxPlayers] = {};
uint32_t g_rttTicks[kCoopMaxPlayers] = {};
uint32_t g_pingTick = 0;

int live_link_count() {
    int n = 0;
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        if (g_links[i].used) ++n;
    }
    return n;
}

uint32_t g_sendSeq = 0;
uint32_t g_tickCounter = 0;

constexpr uint16_t kPuppetHookTipBck = dRes_INDEX_ALANM_BCK_HS_TIP_OPEN_e;

constexpr s16 kProcFieldItem = 0x218;
constexpr s16 kProcDemoItem = 0x69;

constexpr uint16_t kEquipTransformEffect = 0x106;
constexpr uint16_t kTransformEffectJoint = 4;
constexpr uint32_t kModelHoldTicks = 30;
uint8_t g_lastLocalOutfit = 0xFF;
uint8_t g_lastLocalWolf = 0xFF;
uint32_t g_modelHoldTicks = 0;

uint8_t g_lastHandL = 0xFE;
uint8_t g_lastHandR = 0xFE;
uint8_t g_lastSwordInHand = 0;
uint8_t g_lastShieldInHand = 0;
int g_swapDiagLogs = 0;
int g_poseDiagLogs = 0;

uint8_t g_vfxJumpLandCounter = 0;
u16 g_vfxLastProc = 0xFFFF;
bool g_peerConnected = false;

uint32_t send_every_n_ticks() {
    int players = 0;
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        if ((g_roster & (1u << i)) != 0) ++players;
    }
    if (players <= 4) return 1;
    if (players <= 8) return 2;
    return 3;
}

const f32 kFarForSnapshots = 5000.0f;

bool worth_sending(uint8_t about, int to, uint32_t seq) {
    if (!g_isHost || to < 0 || to >= kCoopMaxPlayers) return true;
    const CoopPeer& dest = features_peer_of(static_cast<uint8_t>(to));
    if (!dest.present || dest.stage[0] == '\0') return true;
    const char* stage = nullptr;
    cXyz pos;
    if (about == g_localId) {
        daAlink_c* alink = daAlink_getAlinkActorClass();
        stage = dComIfGp_getStartStageName();
        if (alink == nullptr || stage == nullptr) return true;
        pos = alink->current.pos;
    } else {
        if (about >= kCoopMaxPlayers) return true;
        const CoopPeer& src = features_peer_of(about);
        if (!src.present || src.stage[0] == '\0') return true;
        stage = src.stage;
        pos.set(src.x, src.y, src.z);
    }
    if (std::strncmp(stage, dest.stage, 8) != 0) return false;
    const cXyz there(dest.x, dest.y, dest.z);
    if ((pos - there).abs() > kFarForSnapshots) return seq % 4 == 0;
    return true;
}

bool stage_local_message(uint8_t type) {
    switch (type) {
    case kMsgSounds:
    case kMsgParticles:
    case kMsgArrowShot:
    case kMsgObjectMove:
    case kMsgObjectPush:
    case kMsgTorch:
    case kMsgAnimal:
    case kMsgCarry:
    case kMsgGrassCut:
    case kMsgActorState:
    case kMsgEnemyState:
        return true;
    default:
        return false;
    }
}

void process_tcp_rx(PeerLink& link, uint8_t fromId);

uint64_t steady_ms() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

void send_frame_to(PeerLink& link, uint8_t type, uint8_t from, const void* payload, size_t size) {
    if (!link.used) return;
    if (link.viaUdp) {
        std::vector<uint8_t> frame(sizeof(MsgHeader) + size);
        const MsgHeader header{static_cast<uint16_t>(size), type, from};
        std::memcpy(frame.data(), &header, sizeof(header));
        if (size > 0 && payload != nullptr) {
            std::memcpy(frame.data() + sizeof(header), payload, size);
        }

        link.rel.write(frame.data(), frame.size());
        return;
    }
    if (!link.sock) return;
    std::vector<std::byte> frame(sizeof(MsgHeader) + size);
    const MsgHeader header{static_cast<uint16_t>(size), type, from};
    std::memcpy(frame.data(), &header, sizeof(header));
    if (size > 0 && payload != nullptr) {
        std::memcpy(frame.data() + sizeof(header), payload, size);
    }
    link.sock.send({frame.data(), frame.size()});
}

void relay_frame(uint8_t type, uint8_t from, const uint8_t* payload, size_t size) {
    if (!g_isHost) return;
    const bool local = stage_local_message(type);
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        if (i == from) continue;
        if (local && !worth_sending(from, i, 0)) continue;
        send_frame_to(g_links[i], type, from, payload, size);
    }
}
void broadcast_roster();

int link_index_for(const mods::net::Event& event) {
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        if (g_links[i].used && g_links[i].sock && g_links[i].sock.handle() == event.handle) {
            return i;
        }
    }
    return -1;
}

int lowest_free_id() {
    for (int i = 1; i < kCoopMaxPlayers; ++i) {
        if (!g_links[i].used) return i;
    }
    return -1;
}

void drop_link(int id, const char* why);

void send_bye(PeerLink& link) {
    if (!link.used || !link.viaUdp || !g_udp || link.udpEndpoint.empty()) return;
    std::vector<uint8_t> packet;
    link.rel.control_packet(packet, rudp::kKindBye);
    for (int i = 0; i < 3; ++i) {
        g_udp.send_to(link.udpEndpoint,
            {reinterpret_cast<const std::byte*>(packet.data()), packet.size()});
    }
}

void on_session_up() {
    g_peerConnected = true;
    g_connecting = false;
    features_on_connected();
}

void on_extra_link_up() {
    g_peerConnected = true;
    g_connecting = false;
}

void broadcast_roster() {
    if (!g_isHost) return;
    MsgRoster msg{};
    msg.present = g_roster;
    coop_net_send(kMsgRoster, &msg, sizeof(msg));
}

void drop_link(int id, const char* why) {
    if (id < 0 || id >= kCoopMaxPlayers || !g_links[id].used) return;
    coop_log::warn("coop_mod: player {} disconnected ({})", id, why);
    send_bye(g_links[id]);
    g_links[id].sock.close();
    g_links[id] = PeerLink{};
    g_haveRttMs[id] = false;
    g_playerPaused[id] = false;
    g_roster = static_cast<CoopRoster>(g_roster & ~(1u << id));
    puppet_hook_release_player(static_cast<uint8_t>(id));
    if (g_isHost) {
        broadcast_roster();
        g_connecting = static_cast<bool>(g_listener);
        if (live_link_count() == 0) {
            g_peerConnected = false;
            g_statusText = g_listener ? "Hosting. Waiting for players" : "Disconnected";
            features_on_disconnected();
        } else {
            g_statusText = "Hosting, " + std::to_string(live_link_count() + 1) + " players";

            features_on_roster_changed();
        }
    } else {

        g_peerConnected = false;
        g_roster = 1u << kCoopHostId;
        g_localId = kCoopHostId;
        for (int i = 0; i < kCoopMaxPlayers; ++i) g_playerPaused[i] = false;
        g_connecting = static_cast<bool>(g_listener);
        g_statusText = "Disconnected";
        features_on_disconnected();
    }
}

void admit_player(int id) {
    g_roster = static_cast<CoopRoster>(g_roster | (1u << id));

    MsgAssignId assign{};
    assign.playerId = static_cast<uint8_t>(id);
    assign.maxPlayers = static_cast<uint8_t>(kCoopMaxPlayers);
    send_frame_to(g_links[id], kMsgAssignId, kCoopHostId, &assign, sizeof(assign));
    g_statusText = "Hosting, " + std::to_string(live_link_count() + 1) + " players";
    const bool firstLink = live_link_count() == 1;
    broadcast_roster();
    if (firstLink) {
        on_session_up();
    } else {
        on_extra_link_up();
    }
    features_on_roster_changed();
}

void handle_tcp_event(const mods::net::Event& event) {
    switch (event.type) {
        case NET_EVENT_ACCEPTED: {
            const int id = lowest_free_id();
            if (id < 0) {

                coop_log::warn("coop_mod: session full ({} players) - turned away {}",
                    kCoopMaxPlayers, event.endpoint);
                mods::net::Socket extra = mods::net::adopt(event.accepted);
                extra.close();
                break;
            }
            coop_log::info("coop_mod: player {} connected from {}", id, event.endpoint);
            g_links[id] = PeerLink{};
            g_links[id].used = true;
            g_links[id].sock = mods::net::adopt(event.accepted);
            admit_player(id);
            break;
        }
        case NET_EVENT_CONNECTED:

            coop_log::info("coop_mod: connected to host");
            g_statusText = "Connected";
            on_session_up();
            break;
        case NET_EVENT_STREAM_DATA: {
            const int id = link_index_for(event);
            if (id < 0) break;
            g_ticksSinceRx = 0;
            if (event.data.empty()) break;
            PeerLink& link = g_links[id];

            if (link.rx.size() + event.data.size() > (1u << 16)) {
                mods::log::error("coop_mod: player {} overflowed the reliable channel", id);
                drop_link(id, "reliable channel overflowed");
                break;
            }
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(event.data.data());
            link.rx.insert(link.rx.end(), bytes, bytes + event.data.size());

            process_tcp_rx(link, g_isHost ? static_cast<uint8_t>(id) : kCoopNoPlayer);
            break;
        }
        case NET_EVENT_DROPPED: {
            const int id = link_index_for(event);
            if (id >= 0) drop_link(id, "link dropped");
            puppet_hook_request_release();
            break;
        }
        case NET_EVENT_CLOSED:
            coop_log::info("coop_mod: socket closed");
            break;
        default:
            if (event.error != NET_ERROR_NONE) {
                mods::log::error("coop_mod: net error: {}", event.message);
                if (!g_peerConnected) g_connecting = static_cast<bool>(g_listener);

                g_statusText = g_isHost
                    ? "Connection problem: " + std::string{event.message}
                    : "Couldn't connect. Check the address and that the host's port is open";
            }
            break;
    }
}

void process_tcp_rx(PeerLink& link, uint8_t fromId) {
    size_t offset = 0;
    while (link.rx.size() - offset >= sizeof(MsgHeader)) {
        MsgHeader header;
        std::memcpy(&header, link.rx.data() + offset, sizeof(header));
        if (header.size > kCoopMaxMessagePayload) {
            mods::log::error("coop_mod: malformed message (size {}) - disconnecting", header.size);
            coop_net_disconnect();
            return;
        }
        if (link.rx.size() - offset < sizeof(header) + header.size) break;
        const uint8_t* payload = link.rx.data() + offset + sizeof(header);

        const uint8_t from = (fromId != kCoopNoPlayer) ? fromId : header.from;
        if (from < kCoopMaxPlayers) g_playerQuiet[from] = 0;

        if (g_isHost && from != kCoopHostId) {
            relay_frame(header.type, from, payload, header.size);
        }

        if (header.type == kMsgPing && header.size >= sizeof(MsgPing)) {
            MsgPing echo;
            std::memcpy(&echo, payload, sizeof(echo));
            coop_net_send_to(from, kMsgPong, &echo, sizeof(echo));
        } else if (header.type == kMsgPong && header.size >= sizeof(MsgPing)) {
            MsgPing back;
            std::memcpy(&back, payload, sizeof(back));
            if (from < kCoopMaxPlayers && back.token == g_pingToken[from]) {
                const uint32_t elapsed = g_pingTick - g_pingSentTick[from];

                g_rttTicks[from] = (g_rttTicks[from] * 3 + elapsed) / 4;
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - g_pingSentAt[from]).count();
                const uint32_t sample = ms < 0 ? 0u : static_cast<uint32_t>(ms);
                g_rttMs[from] = g_haveRttMs[from] ? (g_rttMs[from] * 3 + sample) / 4 : sample;
                g_haveRttMs[from] = true;
            }
        } else {
            features_on_message(header.type, payload, header.size, from);
        }
        offset += sizeof(header) + header.size;
        if (!link.used) return;
    }
    if (offset > 0) {
        link.rx.erase(link.rx.begin(), link.rx.begin() + static_cast<std::ptrdiff_t>(offset));
    }
}

ModResult start_hosting(int64_t port) {
    g_listener.close();
    g_udp.close();
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        g_links[i].sock.close();
        g_links[i] = PeerLink{};
    }
    g_localId = kCoopHostId;
    g_roster = 1u << kCoopHostId;

    const std::string bind = "tcp://0.0.0.0:" + std::to_string(port);
    mods::net::BindOutcome outcome;
    g_listener = mods::net::listen(bind, &outcome);
    if (!g_listener) {
        mods::log::error(
            "coop_mod: failed to listen on {}: {}", bind, static_cast<int>(outcome.error));
        g_statusText = "Could not host on port " + std::to_string(port) +
                       ". Is something else using it?";
        return MOD_ERROR;
    }
    g_isHost = true;
    g_connecting = true;
    g_statusText = "Hosting on port " + std::to_string(port);
    coop_log::info("coop_mod: hosting on {}", outcome.local);

    mods::net::BindOutcome udpOutcome;
    g_udp = mods::net::open_datagram("udp://0.0.0.0:" + std::to_string(port), &udpOutcome);
    if (!g_udp) {
        g_listener.close();
        g_connecting = false;
        g_statusText = "Couldn't start networking";
        return MOD_ERROR;
    }
    coop_log::info("coop_mod: udp bound on {}", udpOutcome.local);
    remember_udp_port(udpOutcome.local);

    if (cfg_bool(g_upnpVar, true)) upnp_begin(static_cast<int>(port));

    if (cfg_bool(g_roomsVar, true)) {

        const std::string name = normalize_room_code(cfg_string(g_roomCodeVar, ""));
        if (!name.empty() && (name.size() < kRoomNameMin || name.size() > kRoomNameMax)) {

            g_statusText = "Room names are 4 to 24 letters and numbers. Hosting by address only.";
        } else {
            online_host_begin(static_cast<int>(port), name);
        }
    }
    return MOD_OK;
}

ModResult start_joining_code(const std::string& typed) {
    const std::string code = normalize_room_code(typed);
    if (code.size() < kRoomNameMin || code.size() > kRoomNameMax) {
        g_statusText = "Enter the room code or name the host sees";
        return MOD_ERROR;
    }
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        g_links[i].sock.close();
        g_links[i] = PeerLink{};
    }
    g_udp.close();
    mods::net::BindOutcome udpOutcome;
    g_udp = mods::net::open_datagram("udp://0.0.0.0:0", &udpOutcome);
    if (!g_udp) {
        g_statusText = "Couldn't start networking";
        return MOD_ERROR;
    }

    const size_t colon = udpOutcome.local.rfind(':');
    const int localPort =
        colon == std::string::npos ? 0 : std::atoi(udpOutcome.local.c_str() + colon + 1);
    remember_udp_port(udpOutcome.local);
    g_isHost = false;
    g_connecting = true;
    g_localId = kCoopNoPlayer;
    g_statusText = "Looking for room " + code + "...";
    coop_log::info("coop_mod: joining room {} (udp {})", code, udpOutcome.local);
    online_join_begin(code, localPort);
    return MOD_OK;
}

std::string normalize_join_address(std::string address) {
    const auto notSpace = [](unsigned char c) {
        return c != ' ' && c != '\t' && c != '\r' && c != '\n';
    };
    while (!address.empty() && !notSpace(static_cast<unsigned char>(address.front()))) {
        address.erase(address.begin());
    }
    while (!address.empty() && !notSpace(static_cast<unsigned char>(address.back()))) {
        address.pop_back();
    }
    for (const char* prefix : {"tcp://", "udp://"}) {
        if (address.rfind(prefix, 0) == 0) address.erase(0, std::strlen(prefix));
    }
    if (address.empty()) return address;
    const size_t colons = static_cast<size_t>(std::count(address.begin(), address.end(), ':'));
    const bool bracketed = address.front() == '[';
    if (bracketed) {
        if (address.back() == ']') address += ":27716";
    } else if (colons == 0) {
        address += ":27716";
    }
    return address;
}

ModResult start_joining(const std::string& rawAddress) {

    std::string typed = rawAddress;
    const size_t colons = static_cast<size_t>(std::count(typed.begin(), typed.end(), ':'));
    const bool hasPort = typed.rfind('[', 0) == 0 ? typed.find("]:") != std::string::npos
                                                  : colons == 1;
    if (!typed.empty() && !hasPort) {
        typed += ":" + std::to_string(cfg_int(g_joinPortVar, 27716));
    }
    const std::string address = normalize_join_address(typed);
    if (address.empty()) {
        g_statusText = "Enter the host's address first";
        return MOD_ERROR;
    }
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        g_links[i].sock.close();
        g_links[i] = PeerLink{};
    }
    g_udp.close();

    const std::string endpoint = "tcp://" + address;
    mods::net::Socket sock = mods::net::connect(endpoint);
    if (!sock) {
        mods::log::error("coop_mod: failed to connect to {}", endpoint);
        g_statusText = "Could not reach " + address + ". Check the address and port";
        return MOD_ERROR;
    }
    g_isHost = false;
    g_connecting = true;
    g_links[kCoopHostId] = PeerLink{};
    g_links[kCoopHostId].used = true;
    g_links[kCoopHostId].sock = std::move(sock);
    g_links[kCoopHostId].udpEndpoint = "udp://" + address;
    g_links[kCoopHostId].haveUdp = true;
    g_localId = kCoopNoPlayer;
    g_statusText = "Connecting to " + address + "...";
    coop_log::info("coop_mod: connecting to {}", endpoint);

    mods::net::BindOutcome udpOutcome;
    g_udp = mods::net::open_datagram("udp://0.0.0.0:0", &udpOutcome);
    if (!g_udp) {
        g_links[kCoopHostId].sock.close();
        g_links[kCoopHostId] = PeerLink{};
        g_connecting = false;
        g_statusText = "Couldn't start networking";
        return MOD_ERROR;
    }
    coop_log::info("coop_mod: udp bound on {}", udpOutcome.local);
    remember_udp_port(udpOutcome.local);
    return MOD_OK;
}

uint32_t g_lastMidnaSeq[kCoopMaxPlayers] = {};
bool g_haveMidnaSeq[kCoopMaxPlayers] = {};

void handle_midna_datagram(const mods::net::Event& event) {
    MidnaSnapshot snap;
    std::memcpy(&snap, event.data.data(), sizeof(snap));
    if (snap.magic != kMidnaSnapshotMagic) return;

    int id = -1;
    if (g_isHost) {
        for (int i = 0; i < kCoopMaxPlayers; ++i) {
            if (g_links[i].used && g_links[i].haveUdp && g_links[i].udpEndpoint == event.endpoint) {
                id = i;
                break;
            }
        }
        if (id < 0) return;
        snap.playerId = static_cast<uint8_t>(id);
    } else {
        id = snap.playerId;
        if (id < 0 || id >= kCoopMaxPlayers || id == g_localId) return;
    }

    if (g_haveMidnaSeq[id] && static_cast<int32_t>(snap.seq - g_lastMidnaSeq[id]) <= 0) return;
    g_lastMidnaSeq[id] = snap.seq;
    g_haveMidnaSeq[id] = true;

    if (g_isHost) {
        for (int i = 0; i < kCoopMaxPlayers; ++i) {
            if (i == id || !g_links[i].used || !g_links[i].haveUdp) continue;
            if (!worth_sending(static_cast<uint8_t>(id), i, snap.seq)) continue;
            g_udp.send_to(g_links[i].udpEndpoint,
                {reinterpret_cast<const std::byte*>(&snap), sizeof(snap)});
        }
    }
    puppet_hook_on_midna_snapshot(static_cast<uint8_t>(id), snap);
}

int udp_link_for(std::string_view endpoint) {
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        if (g_links[i].used && g_links[i].viaUdp && g_links[i].udpEndpoint == endpoint) return i;
    }
    return -1;
}

bool handle_reliable_datagram(const mods::net::Event& event) {
    rudp::Header h;
    const uint8_t* payload = nullptr;
    if (!rudp::Channel::parse(event.data.data(), event.data.size(), h, payload)) return false;
    const std::string from{event.endpoint};
    const uint64_t now = steady_ms();

    int id = udp_link_for(from);
    if (id >= 0 && g_links[id].rel.conn() != h.conn) {

        if (!g_isHost || h.kind != rudp::kKindHello) return true;
        drop_link(id, "reconnected");
        id = -1;
    }
    if (id < 0) {
        if (!g_isHost || h.kind != rudp::kKindHello || h.len != sizeof(uint64_t)) return true;
        uint64_t token;
        std::memcpy(&token, payload, sizeof(token));

        if (!online_accept_token(token)) return true;
        id = lowest_free_id();
        if (id < 0) {
            coop_log::warn("coop_mod: session full ({} players) - turned away {}",
                kCoopMaxPlayers, from);
            PeerLink refused;
            refused.rel.start(h.conn, now);
            std::vector<uint8_t> bye;
            refused.rel.control_packet(bye, rudp::kKindBye);
            g_udp.send_to(from, {reinterpret_cast<const std::byte*>(bye.data()), bye.size()});
            return true;
        }
        coop_log::info("coop_mod: player {} connected from {} (room code)", id, from);
        g_links[id] = PeerLink{};
        g_links[id].used = true;
        g_links[id].viaUdp = true;
        g_links[id].udpEndpoint = from;
        g_links[id].haveUdp = true;
        g_links[id].createdMs = now;
        g_links[id].rel.start(h.conn, now);
        admit_player(id);
        if (!g_links[id].used) return true;
    }

    PeerLink& link = g_links[id];
    g_ticksSinceRx = 0;
    if (h.kind == rudp::kKindBye) {
        drop_link(id, "left");
        return true;
    }
    if (g_isHost && h.kind == rudp::kKindHello) {

        std::vector<uint8_t> ack;
        link.rel.control_packet(ack, rudp::kKindHelloAck);
        g_udp.send_to(from, {reinterpret_cast<const std::byte*>(ack.data()), ack.size()});
    }
    if (!g_isHost && !link.helloAcked) {

        link.helloAcked = true;
        coop_log::info("coop_mod: connected to host (room code)");
        g_statusText = "Connected";
        online_stop();
        on_session_up();
        if (!link.used) return true;
    }

    std::vector<uint8_t> delivered;
    link.rel.on_packet(h, payload, now, delivered);
    if (delivered.empty()) return true;
    if (link.rx.size() + delivered.size() > (1u << 16)) {
        mods::log::error("coop_mod: player {} overflowed the reliable channel", id);
        drop_link(id, "reliable channel overflowed");
        return true;
    }
    link.rx.insert(link.rx.end(), delivered.begin(), delivered.end());
    process_tcp_rx(link, g_isHost ? static_cast<uint8_t>(id) : kCoopNoPlayer);
    return true;
}

void flush_udp_links() {
    if (!g_udp) return;
    const uint64_t now = steady_ms();
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        PeerLink& link = g_links[i];
        if (!link.used || !link.viaUdp) continue;
        const auto send = [&](const uint8_t* data, size_t size) {
            g_udp.send_to(link.udpEndpoint, {reinterpret_cast<const std::byte*>(data), size});
        };
        if (!g_isHost && !link.helloAcked) {
            if (now - link.createdMs > 8000) {
                coop_log::warn("coop_mod: the host never answered our hello");
                const std::string why =
                    "Reached the host but could not finish connecting. Try again, or use "
                    "Tailscale.";
                coop_net_disconnect();
                g_statusText = why;
                return;
            }
            if (now - link.helloSentMs >= 200) {
                link.helloSentMs = now;
                std::vector<uint8_t> hello;
                link.rel.control_packet(hello, rudp::kKindHello, &link.token, sizeof(link.token));
                send(hello.data(), hello.size());
            }
            continue;
        }
        link.rel.flush(now, send);
        if (link.rel.dead(now)) {
            drop_link(i, link.rel.heard_any() ? "timed out" : "never answered");
            if (!g_isHost) puppet_hook_request_release();
        }
    }
}

uint32_t g_lastHorseSeq[kCoopMaxPlayers] = {};
bool g_haveHorseSeq[kCoopMaxPlayers] = {};

void handle_horse_datagram(const mods::net::Event& event) {
    HorseSnapshot snap;
    std::memcpy(&snap, event.data.data(), sizeof(snap));
    if (snap.magic != kHorseSnapshotMagic) return;
    int id = -1;
    if (g_isHost) {
        for (int i = 0; i < kCoopMaxPlayers; ++i) {
            if (g_links[i].used && g_links[i].haveUdp && g_links[i].udpEndpoint == event.endpoint) {
                id = i;
                break;
            }
        }
        if (id < 0) return;
        snap.playerId = static_cast<uint8_t>(id);
    } else {
        id = snap.playerId;
        if (id < 0 || id >= kCoopMaxPlayers || id == g_localId) return;
    }
    if (g_haveHorseSeq[id] && static_cast<int32_t>(snap.seq - g_lastHorseSeq[id]) <= 0) return;
    g_lastHorseSeq[id] = snap.seq;
    g_haveHorseSeq[id] = true;
    if (g_isHost) {
        for (int i = 0; i < kCoopMaxPlayers; ++i) {
            if (i == id || !g_links[i].used || !g_links[i].haveUdp) continue;
            if (!worth_sending(static_cast<uint8_t>(id), i, snap.seq)) continue;
            g_udp.send_to(g_links[i].udpEndpoint,
                {reinterpret_cast<const std::byte*>(&snap), sizeof(snap)});
        }
    }
    puppet_hook_on_horse_snapshot(static_cast<uint8_t>(id), snap);
}

void reopen_udp_after_close(const char* why) {
    if (g_udpPort <= 0) return;

    static uint32_t s_windowStart = 0;
    static int s_inWindow = 0;
    if (g_tickCounter - s_windowStart > 600) {
        if (s_inWindow > 3) {
            coop_log::warn("coop_mod: udp socket reopened {} times in the last 10 seconds",
                s_inWindow);
        }
        s_windowStart = g_tickCounter;
        s_inWindow = 0;
    }
    ++s_inWindow;
    g_udp.detach();
    mods::net::BindOutcome outcome;
    g_udp = mods::net::open_datagram("udp://0.0.0.0:" + std::to_string(g_udpPort), &outcome);
    if (!g_udp) {
        coop_log::warn("coop_mod: udp socket closed ({}) and could not be reopened on port {}", why,
            g_udpPort);
        g_statusText = "Lost the network socket. Disconnect and try again";
    } else if (s_inWindow <= 3) {
        coop_log::warn("coop_mod: udp socket closed ({}) - reopened on {}", why, outcome.local);
    }
}

void handle_udp_event(const mods::net::Event& event) {
    if (event.type != NET_EVENT_DATAGRAM) {
        if (event.error != NET_ERROR_NONE) {
            mods::log::error("coop_mod: udp error: {}", event.message);
        }
        if (event.type == NET_EVENT_CLOSED && (g_isHost || g_connecting || g_peerConnected)) {
            reopen_udp_after_close(std::string{event.message}.c_str());
        }
        return;
    }

    if (online_on_datagram(std::string{event.endpoint},
            reinterpret_cast<const uint8_t*>(event.data.data()), event.data.size())) {
        return;
    }
    if (handle_reliable_datagram(event)) return;

    if (!g_isHost && !g_peerConnected) return;
    g_ticksSinceRx = 0;
    if (event.data.size() == sizeof(MidnaSnapshot)) {
        handle_midna_datagram(event);
        return;
    }
    if (event.data.size() == sizeof(HorseSnapshot)) {
        handle_horse_datagram(event);
        return;
    }
    if (event.data.size() != sizeof(PlayerSnapshot)) {
        return;
    }
    PlayerSnapshot snapshot;
    std::memcpy(&snapshot, event.data.data(), sizeof(snapshot));

    int id = -1;
    if (g_isHost) {

        for (int i = 0; i < kCoopMaxPlayers; ++i) {
            if (g_links[i].used && g_links[i].haveUdp && g_links[i].udpEndpoint == event.endpoint) {
                id = i;
                break;
            }
        }
        if (id < 0) {
            const uint8_t claim = snapshot.playerId;
            if (claim < kCoopMaxPlayers && g_links[claim].used && !g_links[claim].haveUdp) {
                g_links[claim].udpEndpoint = std::string{event.endpoint};
                g_links[claim].haveUdp = true;
                id = claim;
                coop_log::info("coop_mod: learned player {} udp endpoint {}", id, event.endpoint);
            }
        }
        if (id < 0) return;

        snapshot.playerId = static_cast<uint8_t>(id);
    } else {

        id = snapshot.playerId;
        if (id < 0 || id >= kCoopMaxPlayers || id == g_localId) return;
    }

    g_playerQuiet[id] = 0;

    if (!g_playerWorldSeen[id] || g_playerWorldTick[id] != snapshot.worldTick) {
        g_playerWorldTick[id] = snapshot.worldTick;
        g_playerWorldSeen[id] = true;
        g_playerWorldStill[id] = 0;
    }
    PeerLink& from = g_links[id];
    if (from.haveRecvSeq && static_cast<int32_t>(snapshot.seq - from.lastRecvSeq) <= 0) {
        return;
    }
    from.lastRecvSeq = snapshot.seq;
    from.haveRecvSeq = true;

    if (g_isHost) {
        for (int i = 0; i < kCoopMaxPlayers; ++i) {
            if (i == id || !g_links[i].used || !g_links[i].haveUdp) continue;
            if (!worth_sending(static_cast<uint8_t>(id), i, snapshot.seq)) continue;
            g_udp.send_to(g_links[i].udpEndpoint,
                {reinterpret_cast<const std::byte*>(&snapshot), sizeof(snapshot)});
        }
    }

    puppet_hook_on_network_snapshot(static_cast<uint8_t>(id), snapshot.posX, snapshot.posY,
        snapshot.posZ, snapshot.angleX, snapshot.angleY, snapshot.angleZ, snapshot.roomNo,
        snapshot.outfit, snapshot.under, snapshot.upper, snapshot.handL, snapshot.handR, snapshot);
}

uint8_t detect_local_outfit() {

    const uint8_t clothes = dComIfGs_getSelectEquipClothes();
    if (clothes == dItemNo_WEAR_CASUAL_e) return kPuppetOutfitCasual;
    if (clothes == dItemNo_WEAR_ZORA_e) return kPuppetOutfitZora;
    if (clothes == dItemNo_ARMOR_e) return kPuppetOutfitMagicArmor;
    return kPuppetOutfitDefault;
}

bool local_models_are_unsafe() {
    daAlink_c* alink = daAlink_getAlinkActorClass();
    const uint8_t outfitNow = detect_local_outfit();
    const bool outfitChanged = g_lastLocalOutfit != 0xFF && outfitNow != g_lastLocalOutfit;

    const uint8_t wolfNow = (alink != nullptr && alink->checkWolf()) ? 1 : 0;
    const bool formChanged = g_lastLocalWolf != 0xFF && wolfNow != g_lastLocalWolf;
    g_lastLocalWolf = wolfNow;
    const uint8_t timer = (alink != nullptr) ? alink->mClothesChangeWaitTimer : 0;

    if (outfitChanged || formChanged || timer != 0) {
        if (g_modelHoldTicks == 0 && g_swapDiagLogs < 24) {
            ++g_swapDiagLogs;

            coop_log::info("coop_mod: [SWAPDIAG] local model swap detected: outfitChanged={} "
                            "({} -> {}) formChanged={} (wolf={}) clothesTimer={} - holding model "
                            "reads for {} ticks",
                static_cast<int>(outfitChanged), g_lastLocalOutfit, outfitNow,
                static_cast<int>(formChanged), wolfNow, timer, kModelHoldTicks);
        }
        g_modelHoldTicks = kModelHoldTicks;
    }
    g_lastLocalOutfit = outfitNow;

    if (g_modelHoldTicks == 0) {
        return false;
    }
    if (--g_modelHoldTicks == 0 && g_swapDiagLogs < 24) {
        ++g_swapDiagLogs;
        coop_log::info("coop_mod: [SWAPDIAG] hold expired - resuming local model reads");
    }
    return true;
}

}

bool coop_local_models_unsafe() {
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink != nullptr && alink->mClothesChangeWaitTimer != 0) return true;
    return g_modelHoldTicks > 0;
}

namespace {

void drive_fake_players(const PlayerSnapshot& mine) {
    int64_t want = 0;
    if (g_fakePlayersVar != 0) svc_config->get_int(mod_ctx, g_fakePlayersVar, &want);
    if (!g_isHost || !g_udp) want = 0;
    want = std::clamp<int64_t>(want, 0, kCoopMaxPlayers - 1);

    uint16_t mask = 0;
    int placed = 0;
    for (int id = kCoopMaxPlayers - 1; id >= 1 && placed < want; --id) {
        if (id == g_localId || g_links[id].used) continue;
        mask = static_cast<uint16_t>(mask | (1u << id));
        ++placed;
    }

    for (int id = 0; id < kCoopMaxPlayers; ++id) {
        const uint16_t bit = static_cast<uint16_t>(1u << id);
        if ((g_fakeMask & bit) != 0 && (mask & bit) == 0) {
            puppet_hook_release_player(static_cast<uint8_t>(id));
            features_debug_fake_peer(static_cast<uint8_t>(id), false, nullptr, nullptr, 0, 0);
        }
    }
    g_fakeMask = mask;
    if (mask == 0) return;

    daAlink_c* alink = daAlink_getAlinkActorClass();
    const u16 life = dComIfGs_getLife();
    const u16 maxLife = dComIfGs_getMaxLife();
    int k = 0;
    for (int id = 0; id < kCoopMaxPlayers; ++id) {
        if ((mask & (1u << id)) == 0) continue;

        const s16 around = static_cast<s16>((k * 0x10000) / placed);
        const f32 radius = 220.0f + 60.0f * (k % 3);
        PlayerSnapshot fake = mine;
        fake.playerId = static_cast<uint8_t>(id);
        fake.posX = mine.posX + radius * cM_ssin(around);
        fake.posZ = mine.posZ + radius * cM_scos(around);
        fake.angleY = static_cast<int16_t>(mine.angleY + around);
        g_playerQuiet[id] = 0;
        puppet_hook_on_network_snapshot(static_cast<uint8_t>(id), fake.posX, fake.posY, fake.posZ,
            fake.angleX, fake.angleY, fake.angleZ, fake.roomNo, fake.outfit, fake.under, fake.upper,
            fake.handL, fake.handR, fake);
        const float at[3] = {fake.posX, fake.posY, fake.posZ};
        char name[16];
        std::snprintf(name, sizeof(name), "Bot %d", id);

        const u16 theirLife = static_cast<u16>(std::max<int>(4, life - k * 4));
        features_debug_fake_peer(static_cast<uint8_t>(id), true, name, at, theirLife, maxLife);
        (void)alink;
        ++k;
    }
}

void apply_debug_clear_twilight() {
    static bool s_done = false;
    if (s_done || g_clearTwilightVar == 0 || !cfg_bool(g_clearTwilightVar, false)) return;
    if (dComIfGs_getSaveInfo() == nullptr) return;
    for (int lv = 0; lv < 3; ++lv) {
        if (!dComIfGs_isDarkClearLV(lv)) dComIfGs_onDarkClearLV(lv);
    }
    if (daAlink_getAlinkActorClass() != nullptr) {
        s_done = true;
        coop_log::info("coop_mod: [DEBUG] twilight cleared in Faron, Eldin and Lanayru");
    }
}

void apply_debug_epona_flags() {
    static bool s_done = false;
    if (s_done || g_eponaFlagsVar == 0 || !cfg_bool(g_eponaFlagsVar, false)) return;
    if (dComIfGs_getSaveInfo() == nullptr) return;
    static const u16 kFlags[] = {0x0601, 0x4720, 0x5E20};
    for (u16 flag : kFlags) {
        if (!dComIfGs_isEventBit(flag)) dComIfGs_onEventBit(flag);
    }
    if (daAlink_getAlinkActorClass() != nullptr) {
        s_done = true;
        coop_log::info("coop_mod: [DEBUG] Epona flags set");
    }
}

void apply_debug_give_kit() {
    static bool s_done = false;
    if (s_done || g_giveKitVar == 0 || !cfg_bool(g_giveKitVar, false)) return;

    static bool s_wearDone = false;
    if (!s_wearDone && dComIfGs_getSaveInfo() != nullptr) {
        dComIfGs_setCollectClothes(KOKIRI_CLOTHES_FLAG);
        dComIfGs_setSelectEquipClothes(dItemNo_WEAR_KOKIRI_e);
        if (daAlink_getAlinkActorClass() != nullptr) {
            s_wearDone = true;
            coop_log::info("coop_mod: [DEBUG] hero's clothes on");
        }
    }
    if (daAlink_getAlinkActorClass() == nullptr || svc_item == nullptr) return;
    static const int kKit[] = {dItemNo_COPY_ROD_e, dItemNo_BOMB_BAG_LV1_e, dItemNo_KANTERA_e,
        dItemNo_BOW_e, dItemNo_BOOMERANG_e, dItemNo_HORSE_FLUTE_e};
    for (int item : kKit) {
        svc_item->give_item(mod_ctx, nullptr, static_cast<uint8_t>(item), ITEM_GIVE_SILENT);
    }

    dComIfGs_setCollectSword(COLLECT_ORDON_SWORD);
    dComIfGs_setCollectSword(COLLECT_MASTER_SWORD);
    dComIfGs_setCollectShield(COLLECT_ORDON_SHIELD);
    dComIfGs_setCollectShield(COLLECT_HYLIAN_SHIELD);
    dComIfGs_setSelectEquipSword(dItemNo_MASTER_SWORD_e);
    dComIfGs_setSelectEquipShield(dItemNo_HYLIA_SHIELD_e);
    s_done = true;
    coop_log::info("coop_mod: [DEBUG] test kit given (rod, bombs, lantern, bow, boomerang, horse "
                   "call, both swords, both shields)");
}

void send_local_snapshot() {
    const bool modelsUnsafe = local_models_are_unsafe();

    if (!g_udp || g_localId == kCoopNoPlayer) {
        return;
    }
    if (++g_tickCounter % send_every_n_ticks() != 0) {
        return;
    }

    fopAc_ac_c* player = dComIfGp_getPlayer(0);
    if (player == nullptr) {
        return;
    }

    PlayerSnapshot snapshot{};
    snapshot.seq = ++g_sendSeq;

    snapshot.worldTick = coop_local_world_frames();
    snapshot.posX = player->current.pos.x;
    snapshot.posY = player->current.pos.y;
    snapshot.posZ = player->current.pos.z;
    snapshot.angleX = player->shape_angle.x;
    snapshot.angleY = player->shape_angle.y;
    snapshot.angleZ = player->shape_angle.z;
    snapshot.roomNo = player->current.roomNo;
    daAlink_c* alinkForm = daAlink_getAlinkActorClass();
    const bool localIsWolf = alinkForm != nullptr && alinkForm->checkWolf();
    snapshot.outfit = localIsWolf ? kPuppetOutfitWolf : detect_local_outfit();

    daAlink_c* alink = daAlink_getAlinkActorClass();
    auto fill_slots = [](AnmSlotSnapshot* out, const daPy_anmHeap_c* heaps,
                         const daPy_frameCtrl_c* frameCtrls, mDoExt_AnmRatioPack* packs) {
        for (int i = 0; i < 3; ++i) {
            out[i].resIdx = heaps[i].getIdx();
            out[i].frame = frameCtrls[i].getFrame();
            out[i].ratio = packs[i].getRatio();
            out[i].rate = frameCtrls[i].getRate();

            switch (out[i].resIdx) {
            case dRes_INDEX_ALANM_BCK_BOMBD_e:
            case dRes_INDEX_ALANM_BCK_CARRYD_e:
            case dRes_INDEX_ALANM_BCK_GRABD_e:
            case dRes_INDEX_ALANM_BCK_RODD_e:
                out[i].rate = 0.0f;
                break;
            default:
                break;
            }
        }
    };

    auto looks_like_live_data = [](const void* ptr) { return coop_ptr_looks_live(ptr); };
    auto shown_hand_index = [&](daAlink_c* a, J3DShape* shown) -> uint8_t {
        if (a == nullptr || shown == nullptr || a->mpLinkHandModel == nullptr) return 0xFE;
        if (!looks_like_live_data(a->mpLinkHandModel)) return 0xFE;
        J3DModelData* md = a->mpLinkHandModel->getModelData();
        auto scan = [&](J3DModelData* data, uint8_t flag) -> uint8_t {
            if (data == nullptr || !looks_like_live_data(data)) return 0xFE;
            const u16 num = data->getMaterialNum();
            if (num == 0 || num > 0x7F) return 0xFE;
            for (u16 i = 0; i < num; ++i) {
                J3DMaterial* mat = data->getMaterialNodePointer(i);

                if (mat == nullptr || !looks_like_live_data(mat)) continue;
                if (mat->getShape() == shown) return static_cast<uint8_t>(i) | flag;
            }
            return 0xFE;
        };
        const uint8_t inHands = scan(md, 0x00);
        if (inHands != 0xFE) return inHands;

        J3DModelData* bodyData =
            (a->mpLinkModel != nullptr && looks_like_live_data(a->mpLinkModel))
                ? a->mpLinkModel->getModelData()
                : nullptr;
        return scan(bodyData, 0x80);
    };

    if (alink != nullptr && !localIsWolf) {
        fill_slots(snapshot.under, alink->mUnderAnmHeap, alink->mUnderFrameCtrl,
            alink->mNowAnmPackUnder);
        fill_slots(snapshot.upper, alink->mUpperAnmHeap, alink->mUpperFrameCtrl,
            alink->mNowAnmPackUpper);

        if (modelsUnsafe) {
            snapshot.handL = g_lastHandL;
            snapshot.handR = g_lastHandR;
        } else {
            snapshot.handL = g_lastHandL = shown_hand_index(alink, alink->field_0x06d0);
            snapshot.handR = g_lastHandR = shown_hand_index(alink, alink->field_0x06d4);
        }

        snapshot.sword = kPuppetSwordNone;
        if (alink->mSwordModel != nullptr) {
            if (alink->mSwordModel == alink->mpSwMModel) snapshot.sword = kPuppetSwordMaster;
            else if (alink->mSwordModel == alink->mWoodSwordModel) snapshot.sword = kPuppetSwordWood;
            else snapshot.sword = kPuppetSwordOrdon;
        }
        snapshot.sheath = kPuppetSheathNone;
        if (alink->mSheathModel != nullptr) {
            snapshot.sheath = (alink->mSheathModel == alink->mpSwMSheathModel)
                ? kPuppetSheathMaster : kPuppetSheathOrdon;
        }

        snapshot.bootsVisible = alink->checkEquipHeavyBoots() ? 1 : 0;
        snapshot.swordVisible = daAlink_c::checkSwordGet() ? 1 : 0;
        snapshot.shieldVisible = daAlink_c::checkShieldGet() ? 1 : 0;
        snapshot.swordJoint = alink->mLeftItemJntNo;
        snapshot.sheathJoint = alink->field_0x30b6;
        snapshot.shieldJoint = alink->mRightItemJntNo;

        auto origin_of = [](MtxP m) {
            cXyz v(0.0f, 0.0f, 0.0f);
            if (m != nullptr) mDoMtx_multVecZero(m, &v);
            return v;
        };
        auto sits_on_joint = [&](J3DModel* item, u16 joint) -> uint8_t {
            if (item == nullptr || alink->mpLinkModel == nullptr) return 0;
            if (!looks_like_live_data(item) || !looks_like_live_data(alink->mpLinkModel)) return 0;
            J3DModelData* body = alink->mpLinkModel->getModelData();
            if (body == nullptr || !looks_like_live_data(body)) return 0;
            if (body->getJointNum() == 0 || body->getJointNum() > 256) return 0;
            if (joint >= body->getJointNum()) return 0;
            const cXyz a = origin_of(item->getBaseTRMtx());
            const cXyz b = origin_of(alink->mpLinkModel->getAnmMtx(joint));
            return ((a - b).abs() < 1.0f) ? 1 : 0;
        };

        if (modelsUnsafe) {
            snapshot.swordInHand = g_lastSwordInHand;
            snapshot.shieldInHand = g_lastShieldInHand;
        } else {
            snapshot.swordInHand = g_lastSwordInHand =
                sits_on_joint(alink->mSwordModel, alink->mLeftItemJntNo);
            snapshot.shieldInHand = g_lastShieldInHand =
                sits_on_joint(alink->mShieldModel, alink->mRightItemJntNo);
        }

        if (alink->mShieldArcName != nullptr) {
            std::strncpy(snapshot.shieldArc, alink->mShieldArcName, sizeof(snapshot.shieldArc) - 1);
        }

        int slot = 0;
        for (int i = 0; i < kPuppetAttachSlots; ++i) {
            snapshot.attached[i].kind = kPuppetHeldNone;
            snapshot.attached[i].joint = kPuppetHeldJointRoot;
            snapshot.attached[i].bckResIdx = 0xFFFF;
            snapshot.attached[i].frame = 0.0f;
            snapshot.attached[i].scale = 1.0f;
            for (int e = 0; e < 12; ++e) {
                snapshot.attached[i].mtx[e] = (e % 5 == 0) ? 1.0f : 0.0f;
            }
        }

        J3DModel* body = (alink->mpLinkModel != nullptr && looks_like_live_data(alink->mpLinkModel))
                             ? alink->mpLinkModel
                             : nullptr;
        J3DModelData* bodyData = (body != nullptr) ? body->getModelData() : nullptr;
        if (!looks_like_live_data(bodyData)) bodyData = nullptr;

        auto add_attachment = [&](uint8_t kind, J3DModel* model, uint16_t joint, uint16_t bckResIdx,
                                  float frame, float scale = 1.0f) {
            if (slot >= kPuppetAttachSlots || model == nullptr || bodyData == nullptr) return;
            if (!looks_like_live_data(model)) return;
            const bool jointOk = joint != kPuppetHeldJointRoot && joint < bodyData->getJointNum();
            Mtx inv;
            Mtx local;
            mDoMtx_inverse(jointOk ? body->getAnmMtx(joint) : body->getBaseTRMtx(), inv);
            mDoMtx_concat(inv, model->getBaseTRMtx(), local);

            AttachedModelSnapshot& out = snapshot.attached[slot++];
            out.kind = kind;
            out.joint = jointOk ? joint : kPuppetHeldJointRoot;
            out.bckResIdx = bckResIdx;
            out.frame = frame;
            out.scale = scale;
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 4; ++c) {
                    out.mtx[r * 4 + c] = local[r][c];
                }
            }
        };

        auto nearest_hand_joint = [&](J3DModel* model) -> uint16_t {
            if (model == nullptr || bodyData == nullptr || !looks_like_live_data(model)) {
                return kPuppetHeldJointRoot;
            }
            const uint16_t candidates[2] = {alink->mLeftItemJntNo, alink->mRightItemJntNo};

            const f32 kHandAttachRange = 200.0f;
            uint16_t best = kPuppetHeldJointRoot;
            f32 bestDist = 0.0f;
            for (int i = 0; i < 2; ++i) {
                if (candidates[i] >= bodyData->getJointNum()) continue;
                cXyz jointPos(0.0f, 0.0f, 0.0f);
                cXyz modelPos(0.0f, 0.0f, 0.0f);
                mDoMtx_multVecZero(body->getAnmMtx(candidates[i]), &jointPos);
                mDoMtx_multVecZero(model->getBaseTRMtx(), &modelPos);
                const f32 dist = (jointPos - modelPos).abs();
                if (best == kPuppetHeldJointRoot || dist < bestDist) {
                    best = candidates[i];
                    bestDist = dist;
                }
            }
            if (best != kPuppetHeldJointRoot && bestDist > kHandAttachRange) {
                return kPuppetHeldJointRoot;
            }
            return best;
        };

        J3DModel* heldModel = alink->mHeldItemModel;
        if (!modelsUnsafe && heldModel != nullptr && looks_like_live_data(heldModel) &&
            bodyData != nullptr)
        {
            const u16 equip = alink->mEquipItem;
            uint8_t kind = kPuppetHeldNone;
            if (daAlink_c::checkBowItem(equip)) {
                kind = kPuppetHeldBow;
            } else if (equip == dItemNo_PACHINKO_e) {
                kind = kPuppetHeldSling;
            } else if (daAlink_c::checkHookshotItem(equip)) {
                kind = kPuppetHeldHookshot;
            } else if (equip == dItemNo_IRONBALL_e) {
                kind = kPuppetHeldIronBall;
            } else if (equip == dItemNo_COPY_ROD_e) {
                kind = kPuppetHeldCopyRod;
            } else if (daAlink_c::checkBottleItem(equip)) {
                kind = kPuppetHeldBottle;
            } else if (equip == kEquipTransformEffect) {
                kind = kPuppetHeldTransform;
            }

            if (kind != kPuppetHeldNone) {

                const uint16_t itemBck = alink->mAnmHeap9.getIdx();
                const f32 itemFrame = alink->field_0x33dc;

                if (kind == kPuppetHeldTransform) {

                    add_attachment(kind, heldModel, kTransformEffectJoint, itemBck, itemFrame);
                } else if (kind == kPuppetHeldHookshot) {

                    add_attachment(kind, heldModel, nearest_hand_joint(heldModel), itemBck,
                        itemFrame);
                    add_attachment(kPuppetHeldHookTip, alink->mpHookTipModel,
                        nearest_hand_joint(alink->mpHookTipModel), kPuppetHookTipBck,
                        alink->field_0x33e0);
                    if (equip == dItemNo_W_HOOKSHOT_e) {
                        add_attachment(kind, alink->field_0x0710,
                            nearest_hand_joint(alink->field_0x0710), itemBck, itemFrame);
                        add_attachment(kPuppetHeldHookTip, alink->field_0x0714,
                            nearest_hand_joint(alink->field_0x0714), kPuppetHookTipBck,
                            alink->field_0x33e0);
                    }
                } else {

                    uint16_t joint = kPuppetHeldJointRoot;
                    if (kind != kPuppetHeldIronBall) {
                        if (alink->checkOilBottleItemNotGet(equip)) {
                            joint = alink->mRightItemJntNo;
                        } else if (daAlink_c::checkBottleItem(equip)) {
                            joint = alink->mLeftItemJntNo;
                        } else if (alink->checkBowAndSlingItem(equip)) {
                            joint = alink->checkBowGrabLeftHand() ? alink->mLeftItemJntNo
                                                                  : alink->mRightItemJntNo;
                        } else {
                            joint = alink->mLeftItemJntNo;
                        }
                    }
                    add_attachment(kind, heldModel, joint, itemBck, itemFrame);
                }
            }
        }

        snapshot.chainKind = kPuppetChainNone;
        snapshot.chainCount = 0;
        snapshot.chainStopTime = 0;
        for (int i = 0; i < kPuppetChainPts; ++i) {
            snapshot.chainPts[i][0] = snapshot.chainPts[i][1] = snapshot.chainPts[i][2] = 0.0f;
        }
        if (!modelsUnsafe && body != nullptr) {
            Mtx rootInv;
            mDoMtx_inverse(body->getBaseTRMtx(), rootInv);
            auto put_point = [&](int idx, const cXyz& world) {
                if (idx < 0 || idx >= kPuppetChainPts) return;
                cXyz local;
                mDoMtx_multVec(rootInv, &world, &local);
                snapshot.chainPts[idx][0] = local.x;
                snapshot.chainPts[idx][1] = local.y;
                snapshot.chainPts[idx][2] = local.z;
            };

            const u16 equip = alink->mEquipItem;
            if (daAlink_c::checkHookshotItem(equip)) {
                snapshot.chainKind = kPuppetChainHookshot;
                snapshot.chainCount = 4;
                snapshot.chainStopTime = alink->getHookshotStopTime();
                put_point(0, alink->getHsChainTopPos());
                put_point(1, alink->getHsChainRootPos());
                put_point(2, alink->getHsSubChainTopPos());
                put_point(3, alink->getHsSubChainRootPos());
            } else if (equip == dItemNo_IRONBALL_e && alink->getIronBallChainPos() != nullptr) {

                snapshot.chainKind = kPuppetChainIronBall;
                snapshot.chainCount = kPuppetChainPts;
                const cXyz* chain = alink->getIronBallChainPos();
                for (int i = 0; i < kPuppetChainPts; ++i) {
                    const int src = 1 + (i * 100) / (kPuppetChainPts - 1);
                    put_point(i, chain[src]);
                }
            } else if (daAlink_c::checkFishingRodItem(equip)) {

                fopAc_ac_c* rodActor = alink->mItemAcKeep.getActor();
                if (rodActor != nullptr && looks_like_live_data(rodActor)) {
                    dmg_rod_class* rod = reinterpret_cast<dmg_rod_class*>(rodActor);
                    snapshot.chainKind =
                        (rod->kind == MG_ROD_KIND_UKI) ? kPuppetChainRodUki : kPuppetChainRodLure;
                    snapshot.chainCount = kPuppetChainPts;
                    for (int i = 0; i < kPuppetChainPts; ++i) {
                        put_point(i, rod->mg_rod.field_0x0[i]);
                    }
                }
            }
        }

        {
            s16 pitch = alink->mBodyAngle.x;
            if (alink->checkReinRide() && !alink->checkHorseLieAnime() &&
                alink->mProcID != daAlink_c::PROC_HORSE_RUN &&
                alink->mProcID != daAlink_c::PROC_BOAR_RUN)
            {
                pitch = static_cast<s16>(alink->mBodyAngle.x - alink->shape_angle.x);
            }
            snapshot.bodyRotX = pitch;
            snapshot.bodyRotY = alink->field_0x30c8;
            snapshot.bodyRotZ = alink->mBodyAngle.z;
        }

        if (!modelsUnsafe && bodyData != nullptr) {
            fopAc_ac_c* itemActor = alink->getBoomerangActor();
            if (itemActor != nullptr && looks_like_live_data(itemActor)) {
                J3DModel* boomModel = static_cast<daBoomerang_c*>(itemActor)->mp_boomModel;

                if (boomModel != nullptr && looks_like_live_data(boomModel) &&
                    (fopAcM_GetParam(itemActor) == 0 ||
                     !spawns_replicates_procname(fpcNm_BOOMERANG_e))) {
                    add_attachment(kPuppetHeldBoomerang, boomModel, nearest_hand_joint(boomModel),
                        0xFFFF, 0.0f);
                }

                daBoomerang_c* boom = static_cast<daBoomerang_c*>(itemActor);
                const bool boomIsReplicated = spawns_replicates_procname(fpcNm_BOOMERANG_e);
                if (!boomIsReplicated && fopAcM_GetParam(boom) != 0 &&
                    boom->mp_shippuModel != nullptr &&
                    looks_like_live_data(boom->mp_shippuModel) && slot < kPuppetAttachSlots)
                {
                    MtxP shippuBase = boom->mp_shippuModel->getBaseTRMtx();
                    mDoMtx_stack_c::transS(shippuBase[0][3], shippuBase[1][3], shippuBase[2][3]);

                    mDoMtx_stack_c::YrotM(boom->shape_angle.y);
                    Mtx upright;
                    Mtx inv;
                    Mtx local;
                    cMtx_copy(mDoMtx_stack_c::get(), upright);
                    mDoMtx_inverse(body->getBaseTRMtx(), inv);
                    mDoMtx_concat(inv, upright, local);
                    AttachedModelSnapshot& out = snapshot.attached[slot++];
                    out.kind = kPuppetHeldBoomerangWind;
                    out.joint = kPuppetHeldJointRoot;
                    out.bckResIdx = 0xFFFF;
                    out.frame = boom->m_shippuFrame;
                    out.scale = boom->m_shippuSize;
                    for (int r = 0; r < 3; ++r) {
                        for (int c = 0; c < 4; ++c) out.mtx[r * 4 + c] = local[r][c];
                    }
                }

                if (fopAcM_GetParam(boom) == 0 && dComIfGp_checkPlayerStatus0(0, 0x80000) &&
                    boom->mp_setboomEfModel != nullptr && looks_like_live_data(boom->mp_setboomEfModel))
                {
                    add_attachment(kPuppetHeldBoomerangAimWind, boom->mp_setboomEfModel,
                        nearest_hand_joint(boom->mp_setboomEfModel), 0xFFFF, 0.0f);
                }
            }
        }

        if (!modelsUnsafe && bodyData != nullptr) {
            fopAc_ac_c* gotItem = fopAcM_getItemEventPartner(alink);
            const s16 gotName = gotItem != nullptr ? fopAcM_GetName(gotItem) : -1;
            if (gotItem != nullptr && (gotName == kProcFieldItem || gotName == kProcDemoItem) &&
                looks_like_live_data(gotItem))
            {
                auto* itemBase = static_cast<daItemBase_c*>(gotItem);
                if (itemBase->mpModel != nullptr && looks_like_live_data(itemBase->mpModel)) {
                    add_attachment(kPuppetHeldGetItem, itemBase->mpModel,
                        nearest_hand_joint(itemBase->mpModel), itemBase->getItemNo(), 0.0f);
                }
            }
        }

        if (!modelsUnsafe && bodyData != nullptr) {
            daSpinner_c* spinner = alink->getSpinnerActor();
            if (spinner != nullptr && looks_like_live_data(spinner) &&
                spinner->mpModel != nullptr && looks_like_live_data(spinner->mpModel))
            {
                add_attachment(kPuppetHeldSpinner, spinner->mpModel, kPuppetHeldJointRoot, 0xFFFF,
                    0.0f);
            }
        }

        if (!modelsUnsafe && bodyData != nullptr) {
            struct ArrowSearch {
                fopAc_ac_c* found[kPuppetAttachSlots];
                int count;
            };
            ArrowSearch search{};
            fopAcM_Search(
                [](void* proc, void* data) -> void* {
                    auto* s = static_cast<ArrowSearch*>(data);
                    auto* actor = static_cast<fopAc_ac_c*>(proc);
                    if (actor != nullptr && s->count < kPuppetAttachSlots &&
                        fopAcM_GetName(actor) == fpcNm_ARROW_e)
                    {
                        s->found[s->count++] = actor;
                    }
                    return nullptr;
                },
                &search);
            for (int i = 0; i < search.count; ++i) {
                daArrow_c* arrow = static_cast<daArrow_c*>(search.found[i]);
                if (!looks_like_live_data(arrow) || arrow->mpModel == nullptr ||
                    !looks_like_live_data(arrow->mpModel) || projectiles_is_remote(arrow))
                {
                    continue;
                }
                if (fopAcM_GetParam(arrow) != 0 || arrow->field_0x942 != 0 || arrow->field_0x93f != 0) {
                    continue;
                }

                MtxP base = arrow->mpModel->getBaseTRMtx();
                if (base[0][3] == 0.0f && base[1][3] == 0.0f && base[2][3] == 0.0f) continue;
                uint8_t kind = kPuppetHeldNone;
                if (arrow->mArrowType == daArrow_c::ARROW_TYPE_BOMB) {
                    kind = kPuppetHeldBombArrow;
                } else if (arrow->mArrowType == daArrow_c::ARROW_TYPE_NORMAL) {
                    kind = kPuppetHeldArrow;
                }
                if (kind == kPuppetHeldNone) continue;
                add_attachment(kind, arrow->mpModel, nearest_hand_joint(arrow->mpModel), 0xFFFF, 0.0f);
            }
        }

        if (!modelsUnsafe && bodyData != nullptr && daAlink_c::checkFishingRodItem(alink->mEquipItem)) {
            fopAc_ac_c* rodActor = alink->mItemAcKeep.getActor();
            if (rodActor != nullptr && looks_like_live_data(rodActor)) {
                dmg_rod_class* rod = reinterpret_cast<dmg_rod_class*>(rodActor);
                if (rod->kind == MG_ROD_KIND_UKI) {

                    if (rod->uki_model != nullptr && looks_like_live_data(rod->uki_model)) {
                        add_attachment(kPuppetHeldRodFloat, rod->uki_model,
                            nearest_hand_joint(rod->uki_model), 0xFFFF, 0.0f);
                    }
                    if (rod->uki_saki_model != nullptr && looks_like_live_data(rod->uki_saki_model)) {
                        add_attachment(kPuppetHeldRodFloatTip, rod->uki_saki_model,
                            nearest_hand_joint(rod->uki_saki_model), 0xFFFF, 0.0f);
                    }

                    const int hookKind = rod->hook_kind;
                    if (rod->action != ACTION_UKI_HIT && rod->action != ACTION_UKI_CATCH &&
                        (hookKind == 0 || hookKind == 1))
                    {
                        J3DModel* hook = rod->hook_model[hookKind];
                        if (hook != nullptr && looks_like_live_data(hook)) {
                            add_attachment(hookKind == 0 ? kPuppetHeldRodHookA : kPuppetHeldRodHookB,
                                hook, nearest_hand_joint(hook), 0xFFFF, 0.0f);
                        }
                    }
                } else if (rod->rod_modelMorf != nullptr && looks_like_live_data(rod->rod_modelMorf)) {
                    J3DModel* rodModel = rod->rod_modelMorf->getModel();
                    if (rodModel != nullptr && looks_like_live_data(rodModel)) {
                        if (rod->arcname != nullptr) {
                            std::strncpy(snapshot.rodArc, rod->arcname, sizeof(snapshot.rodArc) - 1);
                        }
                        add_attachment(kPuppetHeldFishRod, rodModel, nearest_hand_joint(rodModel),
                            0xFFFF, 0.0f);
                    }
                }
            }
        }

        if (!modelsUnsafe && bodyData != nullptr) {
            fopAc_ac_c* ride = alink->getRideActor();
            const s16 rideName = ride != nullptr ? fopAcM_GetName(ride) : -1;
            if (rideName == fpcNm_CANOE_e && looks_like_live_data(ride)) {
                auto* canoe = static_cast<daCanoe_c*>(ride);
                if (canoe->mArcName != nullptr) {
                    std::strncpy(snapshot.rideArc, canoe->mArcName, sizeof(snapshot.rideArc) - 1);

                    if (canoe->mpModel != nullptr && looks_like_live_data(canoe->mpModel)) {
                        add_attachment(kPuppetHeldRide, canoe->mpModel, kPuppetHeldJointRoot, 4,
                            0.0f);
                    }
                    if (canoe->mpPaddleModel != nullptr &&
                        looks_like_live_data(canoe->mpPaddleModel)) {
                        add_attachment(kPuppetHeldRideExtra, canoe->mpPaddleModel,
                            nearest_hand_joint(canoe->mpPaddleModel), 3, 0.0f);
                    }
                }
            } else if (rideName == fpcNm_Obj_IceLeaf_e && looks_like_live_data(ride)) {
                auto* board = static_cast<daObjIceLeaf_c*>(ride);
                std::strncpy(snapshot.rideArc, "V_IceLeaf", sizeof(snapshot.rideArc) - 1);
                if (board->mpModel != nullptr && looks_like_live_data(board->mpModel)) {

                    add_attachment(kPuppetHeldRide, board->mpModel, kPuppetHeldJointRoot, 7, 0.0f);
                }
            }
        }

        if (!modelsUnsafe && bodyData != nullptr) {
            const fpc_ProcID grabbed = alink->getGrabActorID();
            auto* held = grabbed != fpcM_ERROR_PROCESS_ID_e
                             ? static_cast<fopAc_ac_c*>(fopAcM_SearchByID(grabbed))
                             : nullptr;
            if (held != nullptr && fopAcM_GetName(held) == fpcNm_NBOMB_e &&
                fopAcM_checkCarryNow(held) != 0) {
                J3DModel* bombModel = static_cast<daNbomb_c*>(held)->mpModel;
                if (bombModel != nullptr && looks_like_live_data(bombModel)) {
                    add_attachment(kPuppetHeldBomb, bombModel, nearest_hand_joint(bombModel),
                        0xFFFF, 0.0f);
                }
            }
        }

        if (!modelsUnsafe && alink->mpKanteraModel != nullptr &&
            looks_like_live_data(alink->mpKanteraModel) && bodyData != nullptr &&
            alink->checkNoResetFlg2(daAlink_c::FLG2_UNK_1))
        {
            add_attachment(kPuppetHeldLantern, alink->mpKanteraModel, alink->mLeftItemJntNo, 0xFFFF,
                0.0f);

            if (alink->mpKanteraGlowModel != nullptr &&
                looks_like_live_data(alink->mpKanteraGlowModel))
            {
                const f32 burning = alink->checkNoResetFlg1(daAlink_c::FLG1_UNK_80) ? 1.0f : 0.0f;
                add_attachment(kPuppetHeldLanternGlow, alink->mpKanteraGlowModel,
                    kPuppetHeldJointRoot, 0xFFFF, 0.0f, burning);
            }
        }
    } else if (alink != nullptr && localIsWolf) {

        fill_slots(snapshot.under, alink->mUnderAnmHeap, alink->mUnderFrameCtrl,
            alink->mNowAnmPackUnder);
        fill_slots(snapshot.upper, alink->mUpperAnmHeap, alink->mUpperFrameCtrl,
            alink->mNowAnmPackUpper);
        snapshot.handL = 0xFE;
        snapshot.handR = 0xFE;
        snapshot.sword = kPuppetSwordNone;
        snapshot.sheath = kPuppetSheathNone;
        snapshot.swordVisible = 0;
        snapshot.shieldVisible = 0;

        for (int i = 0; i < kPuppetAttachSlots; ++i) {
            snapshot.attached[i].kind = kPuppetHeldNone;
            snapshot.attached[i].joint = kPuppetHeldJointRoot;
            snapshot.attached[i].bckResIdx = 0xFFFF;
            snapshot.attached[i].frame = 0.0f;
            for (int e = 0; e < 12; ++e) {
                snapshot.attached[i].mtx[e] = (e % 5 == 0) ? 1.0f : 0.0f;
            }
        }

        int wolfSlot = 4;
        if (!modelsUnsafe && alink->mEquipItem == kEquipTransformEffect &&
            alink->mHeldItemModel != nullptr && looks_like_live_data(alink->mHeldItemModel) &&
            alink->mpLinkModel != nullptr && looks_like_live_data(alink->mpLinkModel) &&
            wolfSlot < kPuppetAttachSlots)
        {
            J3DModelData* wolfBodyData = alink->mpLinkModel->getModelData();
            if (looks_like_live_data(wolfBodyData) &&
                kTransformEffectJoint < wolfBodyData->getJointNum())
            {
                Mtx inv;
                Mtx local;
                mDoMtx_inverse(alink->mpLinkModel->getAnmMtx(kTransformEffectJoint), inv);
                mDoMtx_concat(inv, alink->mHeldItemModel->getBaseTRMtx(), local);
                AttachedModelSnapshot& out = snapshot.attached[wolfSlot++];
                out.kind = kPuppetHeldTransform;
                out.joint = kTransformEffectJoint;
                out.bckResIdx = alink->mAnmHeap9.getIdx();
                out.frame = alink->field_0x33dc;
                for (int r = 0; r < 3; ++r) {
                    for (int c = 0; c < 4; ++c) out.mtx[r * 4 + c] = local[r][c];
                }
            }
        }

        J3DModel* wolfBody = alink->mpLinkModel;
        if (!modelsUnsafe && wolfBody != nullptr && looks_like_live_data(wolfBody)) {
            Mtx rootInv;
            mDoMtx_inverse(wolfBody->getBaseTRMtx(), rootInv);
            const cXyz* chainPos = alink->field_0x363c;
            const csXyz* chainAngle = alink->field_0x3142;
            for (int i = 0; i < 4 && i < wolfSlot; ++i) {
                mDoMtx_stack_c::transS(chainPos[i]);
                mDoMtx_stack_c::ZXYrotM(chainAngle[i]);
                Mtx world;
                mDoMtx_copy(mDoMtx_stack_c::get(), world);
                Mtx local;
                mDoMtx_concat(rootInv, world, local);

                AttachedModelSnapshot& out = snapshot.attached[i];
                out.kind = kPuppetHeldWolfChain;
                out.joint = kPuppetHeldJointRoot;
                out.bckResIdx = 0xFFFF;
                out.frame = 0.0f;
                for (int r = 0; r < 3; ++r) {
                    for (int c = 0; c < 4; ++c) out.mtx[r * 4 + c] = local[r][c];
                }
            }
        }
    } else {

        for (int i = 0; i < 3; ++i) {
            snapshot.under[i].resIdx = 0xFFFF;
            snapshot.under[i].frame = 0.0f;
            snapshot.under[i].ratio = 0.0f;
            snapshot.under[i].rate = 1.0f;
            snapshot.upper[i] = snapshot.under[i];
        }
        snapshot.under[0].resIdx = dRes_INDEX_ALANM_BCK_WAITS_e;
        snapshot.under[0].ratio = 1.0f;
        snapshot.handL = 0xFE;
        snapshot.handR = 0xFE;
        snapshot.sword = kPuppetSwordNone;
        snapshot.sheath = kPuppetSheathNone;
        snapshot.swordVisible = 0;
        snapshot.shieldVisible = 0;
    }

    snapshot.vfxSpin = kSpinVfxNone;
    snapshot.vfxWolfDig = 0;
    snapshot.vfxDigAngleX = 0;
    snapshot.vfxDigPos[0] = snapshot.vfxDigPos[1] = snapshot.vfxDigPos[2] = 0.0f;

    snapshot.warpOn = 0;
    snapshot.warpScroll = 0.0f;
    snapshot.warpDissolve = 0.0f;
    if (alink != nullptr &&
        (alink->mProcID == daAlink_c::PROC_WARP ||
         (alink->mProcID == daAlink_c::PROC_TOOL_DEMO && alink->mProcVar2.field_0x300c != 0)))
    {
        snapshot.warpOn = 1;
        snapshot.warpScroll = alink->field_0x3478;
        snapshot.warpDissolve = alink->field_0x347c;
    }
    if (alink != nullptr) {
        const u16 proc = alink->mProcID;
        if ((proc == daAlink_c::PROC_CUT_TURN || proc == daAlink_c::PROC_BOARD_CUT_TURN) &&
            alink->mUnderFrameCtrl[0].getFrame() >= alink->mProcVar1.field_0x300a)
        {
            const bool large = alink->mCutType == daAlink_c::CUT_TYPE_LARGE_TURN_RIGHT ||
                               alink->mCutType == daAlink_c::CUT_TYPE_LARGE_TURN_LEFT;
            uint8_t variant = kSpinVfxNone;
            uint8_t flags = 0;
            if (alink->checkNoResetFlg0(daAlink_c::FLG0_WATER_IN_MOVE)) {
                if (alink->checkZoraWearAbility()) {
                    variant = kSpinVfxWater;
                    if (large) flags |= kSpinVfxFlagWaterLarge;
                }
            } else if (large) {
                variant = kSpinVfxLarge;
            } else if (alink->checkNoResetFlg3(daAlink_c::FLG3_UNK_100000)) {
                variant = kSpinVfxLight;
            } else {
                variant = kSpinVfxNormal;
            }
            if (variant != kSpinVfxNone) {
                if (alink->mProcVar4.field_0x3010 != 0) flags |= kSpinVfxFlagNoLocalRot;
                snapshot.vfxSpin = static_cast<uint8_t>(variant | flags);
            }
        }

        if (proc == daAlink_c::PROC_WOLF_ROLL_ATTACK) {
            uint8_t flags = kSpinVfxWolf;
            if (alink->mProcVar2.field_0x300c == 0) flags |= kSpinVfxFlagWolfFlip;
            if (alink->mProcVar3.field_0x300e == 0) flags |= kSpinVfxFlagWolfEmitB;
            snapshot.vfxSpin = flags;
        }

        if (proc == daAlink_c::PROC_CUT_LARGE_JUMP_LAND && g_vfxLastProc != proc) {
            ++g_vfxJumpLandCounter;
        }
        g_vfxLastProc = proc;
    }
    snapshot.vfxJumpLand = g_vfxJumpLandCounter;

    if (alink != nullptr) {
        snapshot.rootClearMask = alink->field_0x2f99;
        snapshot.rootClearX = alink->field_0x3588.x;
        snapshot.rootClearY = alink->field_0x33b0;
        snapshot.rootClearZ = alink->field_0x3588.z;

        if (alink->field_0x2f99 == 0x60 && alink->field_0x384c != nullptr) {
            snapshot.rootClearX = alink->field_0x384c->x;
            snapshot.rootClearY = alink->field_0x384c->y;
            snapshot.rootClearZ = alink->field_0x384c->z;
        }
        for (int i = 0; i < 2; ++i) {
            snapshot.footAngles[i][0] = alink->mFootData1[i].field_0x6;
            snapshot.footAngles[i][1] = alink->mFootData1[i].field_0x4;
            snapshot.footAngles[i][2] = alink->mFootData1[i].field_0x2;
            snapshot.footAngles[i + 2][0] = alink->mFootData2[i].field_0x6;
            snapshot.footAngles[i + 2][1] = alink->mFootData2[i].field_0x4;
            snapshot.footAngles[i + 2][2] = alink->mFootData2[i].field_0x2;
        }
    }

    if (g_poseDiagLogs < 30 && alink != nullptr &&
        (snapshot.rootClearMask != 0 || snapshot.footAngles[0][0] != 0 ||
         snapshot.footAngles[0][1] != 0 || snapshot.footAngles[1][0] != 0))
    {
        ++g_poseDiagLogs;
        coop_log::trace("coop_mod: [POSEDIAG-TX] rootMask={:#x} clear=({},{},{}) "
                        "foot0=({},{},{}) foot1=({},{},{}) bodyRot=({},{},{})",
            snapshot.rootClearMask, snapshot.rootClearX, snapshot.rootClearY, snapshot.rootClearZ,
            snapshot.footAngles[0][0], snapshot.footAngles[0][1], snapshot.footAngles[0][2],
            snapshot.footAngles[1][0], snapshot.footAngles[1][1], snapshot.footAngles[1][2],
            snapshot.bodyRotX, snapshot.bodyRotY, snapshot.bodyRotZ);
    }

    snapshot.playerId = g_localId;
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        if (!g_links[i].used || !g_links[i].haveUdp) continue;
        if (!worth_sending(g_localId, i, snapshot.seq)) continue;
        g_udp.send_to(g_links[i].udpEndpoint,
            {reinterpret_cast<const std::byte*>(&snapshot), sizeof(snapshot)});
    }
    drive_fake_players(snapshot);
}

uint32_t g_midnaSeq = 0;

int g_midnaNoneToSend = 0;
const int kMidnaNoneRepeats = 3;

bool midna_ptr_live(const void* ptr) {
    return coop_ptr_looks_live(ptr);
}

void fill_mtx12(float* out, const Mtx m) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) out[r * 4 + c] = m[r][c];
    }
}

int16_t to_fixed_rot(f32 v) {
    f32 scaled = v * kMidnaRotFixed;
    if (scaled > 32767.0f) scaled = 32767.0f;
    if (scaled < -32768.0f) scaled = -32768.0f;
    return static_cast<int16_t>(scaled < 0.0f ? scaled - 0.5f : scaled + 0.5f);
}

uint8_t local_midna_mode(daAlink_c* alink, daMidna_c* midna) {
    if (alink == nullptr || midna == nullptr) return kMidnaModeNone;
    if (!midna_ptr_live(midna) || midna->mpShadowModel == nullptr) return kMidnaModeNone;
    if (midna->checkNoDrawState()) return kMidnaModeNone;
    if (!midna->checkStateFlg1(static_cast<daMidna_c::daMidna_FLG1>(
            daMidna_c::FLG1_SHADOW_MODEL_DRAW_DEMO_FORCE | daMidna_c::FLG1_UNK_1)) &&
        alink->checkPlayerNoDraw() &&
        !midna->checkStateFlg0(static_cast<daMidna_c::daMidna_FLG0>(
            daMidna_c::FLG0_TAG_WAIT | daMidna_c::FLG0_UNK_100)))
    {
        return kMidnaModeNone;
    }

    if (midna->checkStateFlg1(daMidna_c::FLG1_UNK_1)) return kMidnaModeNone;
    if (!midna->checkStateFlg0(daMidna_c::FLG0_NO_DRAW) &&
        !midna->checkStateFlg1(daMidna_c::FLG1_SHADOW_MODEL_DRAW_DEMO_FORCE) &&
        midna->mpModel != nullptr)
    {
        return kMidnaModeSolid;
    }
    return kMidnaModeShadow;
}

uint8_t local_midna_hair_shape(daMidna_c* midna) {
    if (midna->mpShadowHairhandBmd == nullptr) return 0;
    J3DModelData* data = midna->mpShadowHairhandBmd->getModelData();
    if (data == nullptr) return 0;
    const u16 n = data->getMaterialNum() < 3 ? data->getMaterialNum() : 3;
    for (u16 i = 0; i < n; ++i) {
        J3DMaterial* mat = data->getMaterialNodePointer(i);

        if (mat != nullptr && mat->getShape() != nullptr && !mat->getShape()->checkFlag(1)) {
            return static_cast<uint8_t>(i);
        }
    }
    return 0;
}

void send_midna_datagram(const MidnaSnapshot& snap) {
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        if (!g_links[i].used || !g_links[i].haveUdp) continue;
        if (!worth_sending(g_localId, i, snap.seq)) continue;
        g_udp.send_to(g_links[i].udpEndpoint,
            {reinterpret_cast<const std::byte*>(&snap), sizeof(snap)});
    }
}

void send_local_midna() {
    if (!g_udp || g_localId == kCoopNoPlayer) return;

    if (g_tickCounter % send_every_n_ticks() != 0) return;

    daAlink_c* alink = daAlink_getAlinkActorClass();
    daMidna_c* midna = daPy_py_c::getMidnaActor();
    uint8_t mode = kMidnaModeNone;

    if (!local_models_are_unsafe()) mode = local_midna_mode(alink, midna);

    J3DModel* body = (alink != nullptr && alink->mpLinkModel != nullptr &&
                      midna_ptr_live(alink->mpLinkModel))
                         ? alink->mpLinkModel
                         : nullptr;
    if (body == nullptr) mode = kMidnaModeNone;

    J3DModel* skel = (mode != kMidnaModeNone) ? midna->mpShadowModel : nullptr;
    if (skel != nullptr &&
        (skel->getModelData() == nullptr || skel->getModelData()->getJointNum() != kMidnaJoints))
    {

        mode = kMidnaModeNone;
    }

    if (mode == kMidnaModeNone) {
        if (g_midnaNoneToSend <= 0) return;
        --g_midnaNoneToSend;
        MidnaSnapshot gone{};
        gone.magic = kMidnaSnapshotMagic;
        gone.seq = ++g_midnaSeq;
        gone.playerId = g_localId;
        gone.mode = kMidnaModeNone;
        send_midna_datagram(gone);
        return;
    }
    g_midnaNoneToSend = kMidnaNoneRepeats;

    MidnaSnapshot snap{};
    snap.magic = kMidnaSnapshotMagic;
    snap.seq = ++g_midnaSeq;
    snap.playerId = g_localId;
    snap.mode = mode;

    const bool maskUp = !midna->checkStateFlg1(daMidna_c::FLG1_NO_MASK_DRAW);
    const bool hairUp =
        mode == kMidnaModeSolid
            ? (midna->mpHairhandBmd != nullptr &&
                  !midna->checkStateFlg1(static_cast<daMidna_c::daMidna_FLG1>(
                      daMidna_c::FLG1_UNK_40 | daMidna_c::FLG1_UNK_10)))
            : !midna->checkStateFlg1(daMidna_c::FLG1_UNK_40);
    if (maskUp) snap.flags |= kMidnaFlagMask;
    if (hairUp) snap.flags |= kMidnaFlagHairhand;

    if (midna->field_0x668 != nullptr) snap.flags |= kMidnaFlagTevColor;
    snap.hairShape = local_midna_hair_shape(midna);
    snap.leftHand = midna->mLeftHandShapeIdx == 0xFD ? 0xFE : midna->mLeftHandShapeIdx;
    snap.rightHand = midna->mRightHandShapeIdx == 0xFD ? 0xFE : midna->mRightHandShapeIdx;
    snap.baseScale = skel->getBaseScale()->x;

    Mtx rel;
    const bool riding =
        alink->checkWolf() && !midna->checkStateFlg0(daMidna_c::FLG0_WOLF_NO_POS);
    if (riding) {
        Mtx invBody;
        mDoMtx_inverse(body->getBaseTRMtx(), invBody);
        mDoMtx_concat(invBody, skel->getBaseTRMtx(), rel);
    } else {
        mDoMtx_copy(skel->getBaseTRMtx(), rel);
        snap.flags |= kMidnaFlagWorldBase;
    }
    fill_mtx12(snap.baseMtx, rel);

    Mtx invBase;
    mDoMtx_inverse(skel->getBaseTRMtx(), invBase);
    if (midna->mpShadowHairhandBmd != nullptr) {
        mDoMtx_concat(invBase, midna->mpShadowHairhandBmd->getBaseTRMtx(), rel);
        fill_mtx12(snap.hairMtx, rel);
    } else {
        snap.flags &= ~kMidnaFlagHairhand;
    }

    for (int j = 0; j < kMidnaJoints; ++j) {
        mDoMtx_concat(invBase, skel->getAnmMtx(j), rel);
        MidnaJointSnapshot& out = snap.joints[j];
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) out.rot[r * 3 + c] = to_fixed_rot(rel[r][c]);
            out.pos[r] = rel[r][3];
        }
    }

    const GXColorS10& tc = midna->tevStr.TevColor;
    snap.tevColor[0] = tc.r;
    snap.tevColor[1] = tc.g;
    snap.tevColor[2] = tc.b;
    snap.tevColor[3] = tc.a;
    snap.hairColor[0] = midna->field_0x6e0.r;
    snap.hairColor[1] = midna->field_0x6e0.g;
    snap.hairColor[2] = midna->field_0x6e0.b;
    snap.hairColor[3] = midna->field_0x6e0.a;
    snap.hairK1[0] = midna->field_0x6e8.r;
    snap.hairK1[1] = midna->field_0x6e8.g;
    snap.hairK1[2] = midna->field_0x6e8.b;
    snap.hairK1[3] = midna->field_0x6e8.a;
    snap.hairK2[0] = midna->field_0x6ec.r;
    snap.hairK2[1] = midna->field_0x6ec.g;
    snap.hairK2[2] = midna->field_0x6ec.b;
    snap.hairK2[3] = midna->field_0x6ec.a;

    send_midna_datagram(snap);
}

uint32_t g_horseSeq = 0;
int g_horseNoneToSend = 0;

void quat_from_mtx(const Mtx m, f32* q) {
    const f32 tr = m[0][0] + m[1][1] + m[2][2];
    if (tr > 0.0f) {
        const f32 s = std::sqrt(tr + 1.0f) * 2.0f;
        q[3] = 0.25f * s;
        q[0] = (m[2][1] - m[1][2]) / s;
        q[1] = (m[0][2] - m[2][0]) / s;
        q[2] = (m[1][0] - m[0][1]) / s;
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        const f32 s = std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2.0f;
        q[3] = (m[2][1] - m[1][2]) / s;
        q[0] = 0.25f * s;
        q[1] = (m[0][1] + m[1][0]) / s;
        q[2] = (m[0][2] + m[2][0]) / s;
    } else if (m[1][1] > m[2][2]) {
        const f32 s = std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2.0f;
        q[3] = (m[0][2] - m[2][0]) / s;
        q[0] = (m[0][1] + m[1][0]) / s;
        q[1] = 0.25f * s;
        q[2] = (m[1][2] + m[2][1]) / s;
    } else {
        const f32 s = std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2.0f;
        q[3] = (m[1][0] - m[0][1]) / s;
        q[0] = (m[0][2] + m[2][0]) / s;
        q[1] = (m[1][2] + m[2][1]) / s;
        q[2] = 0.25f * s;
    }
}

int16_t to_fixed(f32 v, f32 scale) {
    const f32 x = v * scale;
    return static_cast<int16_t>(x > 32767.0f ? 32767.0f : (x < -32767.0f ? -32767.0f : x));
}

void send_horse_datagram(const HorseSnapshot& snap) {
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        if (!g_links[i].used || !g_links[i].haveUdp) continue;
        if (!worth_sending(g_localId, i, snap.seq)) continue;
        g_udp.send_to(g_links[i].udpEndpoint,
            {reinterpret_cast<const std::byte*>(&snap), sizeof(snap)});
    }
}

void send_local_horse() {
    if (!g_udp || g_localId == kCoopNoPlayer) return;
    daAlink_c* alink = daAlink_getAlinkActorClass();
    auto* horse = static_cast<daHorse_c*>(dComIfGp_getHorseActor());
    const bool riding = alink != nullptr && alink->checkHorseRide() != 0;
    J3DModel* model = horse != nullptr ? horse->m_model : nullptr;

    const bool hidden = horse != nullptr &&
                        (horse->checkHorseCallWait() ||
                            (horse->actor_status & fopAcStts_NODRAW_e) != 0);
    const bool show = model != nullptr && model->getModelData() != nullptr && alink != nullptr &&
                      !hidden && !coop_local_models_unsafe() && alink->mpLinkModel != nullptr;

    if (!show) {
        if (g_horseNoneToSend <= 0) return;
        --g_horseNoneToSend;
        HorseSnapshot none{};
        none.magic = kHorseSnapshotMagic;
        none.seq = ++g_horseSeq;
        none.playerId = g_localId;
        send_horse_datagram(none);
        return;
    }

    const bool moving = riding || std::fabs(horse->speedF) > 0.5f;
    const bool idle = !riding && !moving;

    static s16 s_lastHorseYaw = 0;
    const s16 yawStep = static_cast<s16>(horse->shape_angle.y - s_lastHorseYaw);
    s_lastHorseYaw = horse->shape_angle.y;
    const bool turning = yawStep > 0x40 || yawStep < -0x40;
    const bool often = moving || turning;
    if (!often && g_tickCounter % 10 != 0) return;

    if (often && g_tickCounter % (send_every_n_ticks() * 2) != 0) return;
    g_horseNoneToSend = 5;

    HorseSnapshot snap{};
    snap.magic = kHorseSnapshotMagic;
    snap.seq = ++g_horseSeq;
    snap.playerId = g_localId;
    snap.room = static_cast<int8_t>(fopAcM_GetRoomNo(horse));
    MtxP base = model->getBaseTRMtx();
    Mtx rel;
    if (riding) {
        Mtx invBody;
        mDoMtx_inverse(alink->mpLinkModel->getBaseTRMtx(), invBody);
        mDoMtx_concat(invBody, base, rel);
        snap.flags |= kHorseFlagRiding;
    } else {
        mDoMtx_copy(base, rel);
        if (idle) {
            snap.flags |= kHorseFlagIdle;
            const u16 anm = horse->getAnmIdx(0);

            const bool own = anm < 0x100;
            snap.idleAnm = own ? anm : 27;
            snap.idleFrame = own ? horse->m_frameCtrl[0].getFrame() : 0.0f;
            snap.idleRate = own ? horse->m_frameCtrl[0].getRate() : 1.0f;
        }
    }
    fill_mtx12(snap.baseMtx, rel);

    Mtx invBase;
    mDoMtx_inverse(base, invBase);
    J3DModelData* horseData = model->getModelData();
    if (horseData == nullptr) return;
    const u16 joints = horseData->getJointNum();
    const int n = joints < kHorseJoints ? joints : kHorseJoints;
    static bool s_saidJoints = false;
    if (!s_saidJoints) {
        s_saidJoints = true;
        coop_log::info("coop_mod: [HORSE] our horse has {} joints (the datagram carries {})", joints,
            kHorseJoints);
    }
    snap.jointCount = static_cast<uint8_t>(n);
    for (int j = 0; j < n; ++j) {
        Mtx m;
        mDoMtx_concat(invBase, model->getAnmMtx(j), m);
        f32 q[4];
        quat_from_mtx(m, q);
        HorseJointSnapshot& out = snap.joints[j];
        for (int k = 0; k < 4; ++k) out.q[k] = to_fixed(q[k], kHorseQuatScale);
        for (int r = 0; r < 3; ++r) out.p[r] = to_fixed(m[r][3], kHorsePosScale);
    }

    const int reins = horse->field_0x1204 < kHorseReinPoints ? horse->field_0x1204
                                                               : kHorseReinPoints;
    cXyz* points = reins > 0 ? horse->m_reinLine.getPos(0) : nullptr;
    if (points != nullptr) {
        snap.reinCount = static_cast<uint8_t>(reins);
        for (int i = 0; i < reins; ++i) {
            cXyz local;
            mDoMtx_multVec(invBase, &points[i], &local);
            snap.reins[i][0] = to_fixed(local.x, kHorsePosScale);
            snap.reins[i][1] = to_fixed(local.y, kHorsePosScale);
            snap.reins[i][2] = to_fixed(local.z, kHorsePosScale);
        }
    }
    send_horse_datagram(snap);
}

void run_pending_auto_connect() {
    int64_t delayTicks = 600;
    svc_config->get_int(mod_ctx, g_autoConnectDelayTicksVar, &delayTicks);
    if (++g_autoConnectTicksWaited < static_cast<uint32_t>(delayTicks)) {
        return;
    }
    g_autoConnectPending = false;

    const std::string mode = cfg_string(g_modeVar, "host");
    if (mode == "join") {
        start_joining(cfg_string(g_joinAddressVar, "127.0.0.1:27716"));
    } else if (mode == "join_code") {
        start_joining_code(cfg_string(g_roomCodeVar, ""));
    } else {
        int64_t port = 27716;
        svc_config->get_int(mod_ctx, g_bindPortVar, &port);
        start_hosting(port);
    }
    coop_log::info("coop_mod: auto-connected as {} after {} ticks", mode, g_autoConnectTicksWaited);
}

}

bool coop_net_connected() {

    return g_peerConnected || (g_isHost && g_fakeMask != 0);
}

uint32_t coop_net_ticks_since_player(uint8_t playerId) {
    if (playerId == g_localId) return 0;
    if (playerId >= kCoopMaxPlayers) return 0xFFFFFFFFu;
    return g_playerQuiet[playerId];
}

int32_t coop_net_ping_ms(uint8_t playerId) {
    if (playerId >= kCoopMaxPlayers || !g_haveRttMs[playerId]) return -1;
    return static_cast<int32_t>(g_rttMs[playerId]);
}

uint32_t coop_net_rtt_ticks(uint8_t playerId) {
    if (playerId >= kCoopMaxPlayers) return 0;
    return g_rttTicks[playerId];
}

void update_pings() {
    ++g_pingTick;
    if (!coop_net_connected() || g_localId == kCoopNoPlayer) return;
    if (g_pingTick % 60 != 0) return;
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        if (i == g_localId || !coop_net_player_present(static_cast<uint8_t>(i))) continue;
        MsgPing msg{};
        msg.token = (static_cast<uint32_t>(i) << 24) | (g_pingTick & 0x00FFFFFF);
        g_pingToken[i] = msg.token;
        g_pingSentTick[i] = g_pingTick;
        g_pingSentAt[i] = std::chrono::steady_clock::now();
        coop_net_send_to(static_cast<uint8_t>(i), kMsgPing, &msg, sizeof(msg));
    }
}

uint32_t coop_ticks_since_world(uint8_t playerId) {
    if (playerId >= kCoopMaxPlayers) return 0xFFFFFFFFu;
    if (!g_playerWorldSeen[playerId]) return 0;
    return g_playerWorldStill[playerId];
}

const uint32_t kWorldStallTicks = 20;

bool g_localPausedSent = false;
uint32_t g_pauseResendTicks = 0;

bool coop_local_paused() {

    return daAlink_getAlinkActorClass() != nullptr && dComIfGp_isPauseFlag() != 0;
}

void coop_net_set_player_paused(uint8_t playerId, bool paused) {
    if (playerId >= kCoopMaxPlayers || playerId == g_localId) return;
    if (g_playerPaused[playerId] != paused) {
        coop_log::info("coop_mod: [PAUSE] player {} {}", playerId,
            paused ? "opened a menu - handing over what they own" : "is back");
    }
    g_playerPaused[playerId] = paused;
}

bool coop_player_paused(uint8_t playerId) {
    if (playerId >= kCoopMaxPlayers) return false;
    if (playerId == g_localId) return coop_local_paused() || coop_world_stalled(playerId);
    return g_playerPaused[playerId] || coop_world_stalled(playerId);
}

void announce_local_pause() {
    if (!coop_net_connected() || g_localId == kCoopNoPlayer) {
        g_localPausedSent = false;
        return;
    }
    const bool paused = coop_local_paused();
    const bool edge = paused != g_localPausedSent;
    if (!edge && (!paused || ++g_pauseResendTicks < 60)) return;
    g_pauseResendTicks = 0;
    g_localPausedSent = paused;
    MsgPause msg{};
    msg.paused = paused ? 1 : 0;
    coop_net_send(kMsgPause, &msg, sizeof(msg));
    if (edge) {
        coop_log::info("coop_mod: [PAUSE] we {}", paused ? "opened a menu - handing over"
                                                          : "closed the menu");

        if (!paused) enemies_on_local_unpause();
    }
}

bool coop_world_stalled(uint8_t playerId) {
    return coop_ticks_since_world(playerId) >= kWorldStallTicks;
}

uint32_t coop_net_ticks_since_rx() {
    return g_ticksSinceRx;
}

bool coop_net_connecting() {
    return g_connecting;
}

uint8_t coop_net_local_id() {
    return g_localId;
}

uint16_t coop_net_roster() {
    return g_roster;
}

bool coop_net_player_present(uint8_t playerId) {
    return playerId < kCoopMaxPlayers && ((g_roster | g_fakeMask) & (1u << playerId)) != 0;
}

void coop_net_set_local_id(uint8_t playerId, uint8_t hostMaxPlayers) {
    if (g_isHost) return;
    if (playerId >= kCoopMaxPlayers) {

        mods::log::error("coop_mod: host assigned us player {} but this build only supports {} - "
                         "disconnecting", playerId, kCoopMaxPlayers);
        coop_net_disconnect();
        return;
    }
    g_localId = playerId;
    coop_log::info("coop_mod: we are player {} of up to {}", playerId, hostMaxPlayers);
}

void coop_net_set_roster(uint16_t roster) {
    if (g_isHost) return;
    const uint16_t was = g_roster;
    const uint16_t before2 = g_roster;
    g_roster = static_cast<CoopRoster>(roster | (1u << kCoopHostId));
    if (g_roster != before2) features_on_roster_changed();

    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        const bool before = (was & (1u << i)) != 0;
        const bool now = (g_roster & (1u << i)) != 0;
        if (before && !now) puppet_hook_release_player(static_cast<uint8_t>(i));
    }
}

bool coop_net_is_host() {
    return g_isHost;
}

const char* coop_net_status() {

    static std::string online;
    if (!g_isHost && g_connecting && !g_peerConnected && online_active()) {
        online = online_status();
        if (!online.empty()) return online.c_str();
    }
    return g_statusText.c_str();
}

ConfigVarHandle coop_net_port_var() {
    return g_bindPortVar;
}

ConfigVarHandle coop_net_address_var() {
    return g_joinAddressVar;
}

ConfigVarHandle coop_net_join_port_var() {
    return g_joinPortVar;
}

std::string coop_net_join_address() {
    if (cfg_string(g_modeVar, "") == "join_code") {
        return "room " + normalize_room_code(cfg_string(g_roomCodeVar, ""));
    }
    return cfg_string(g_joinAddressVar, "");
}

ConfigVarHandle coop_net_autoconnect_var() {
    return g_autoConnectVar;
}

ConfigVarHandle coop_net_upnp_var() {
    return g_upnpVar;
}

ConfigVarHandle coop_net_room_code_var() {
    return g_roomCodeVar;
}

ConfigVarHandle coop_net_room_server_var() {
    return g_roomServerVar;
}

ConfigVarHandle coop_net_rooms_var() {
    return g_roomsVar;
}

ConfigVarHandle coop_net_host_key_var() {
    return g_hostKeyVar;
}

std::string normalize_room_code(const std::string& typed) {
    std::string code;
    for (const char c : typed) {
        if (c >= 'a' && c <= 'z') code += static_cast<char>(c - 'a' + 'A');
        else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) code += c;
    }
    return code;
}

void coop_net_join_code() {
    if (g_peerConnected || g_connecting) return;
    svc_config->set_string(mod_ctx, g_modeVar, "join_code");
    g_autoConnectPending = false;
    start_joining_code(cfg_string(g_roomCodeVar, ""));
}

void coop_udp_send_raw(const std::string& endpoint, const void* data, size_t size) {
    if (!g_udp) return;
    g_udp.send_to(endpoint, {static_cast<const std::byte*>(data), size});
}

void coop_online_punched(const std::string& endpoint, uint64_t token) {
    if (g_isHost || g_peerConnected) return;
    const uint64_t now = steady_ms();
    PeerLink& link = g_links[kCoopHostId];
    link = PeerLink{};
    link.used = true;
    link.viaUdp = true;
    link.udpEndpoint = endpoint;
    link.haveUdp = true;
    link.token = token;
    link.createdMs = now;

    uint32_t conn = 0;
    for (uint32_t salt = 0; conn == 0; ++salt) {
        conn = static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^
               static_cast<uint32_t>(token) ^ static_cast<uint32_t>(token >> 32) ^ salt;
    }
    link.rel.start(conn, now);
    g_statusText = "Found the host. Connecting...";
}

void coop_online_failed(const std::string& why, const std::string& upnpFallback) {
    if (g_isHost || g_peerConnected) return;
    if (!upnpFallback.empty()) {

        coop_log::info("coop_mod: room punch failed - trying the host's own address {}",
            upnpFallback);

        online_stop();
        if (start_joining(upnpFallback) == MOD_OK) {
            g_statusText = "Trying the host's address directly...";
            return;
        }
    }
    g_udp.close();
    for (int i = 0; i < kCoopMaxPlayers; ++i) g_links[i] = PeerLink{};
    g_connecting = false;
    g_localId = kCoopHostId;
    g_statusText = why;
}

void coop_net_host() {
    if (g_peerConnected || g_connecting) return;
    int64_t port = 27716;
    svc_config->get_int(mod_ctx, g_bindPortVar, &port);

    svc_config->set_string(mod_ctx, g_modeVar, "host");
    g_autoConnectPending = false;
    start_hosting(port);
}

void coop_net_join() {
    if (g_peerConnected || g_connecting) return;
    svc_config->set_string(mod_ctx, g_modeVar, "join");
    g_autoConnectPending = false;
    start_joining(cfg_string(g_joinAddressVar, "127.0.0.1:27716"));
}

void coop_net_disconnect() {
    upnp_release();
    online_stop();
    for (int i = 0; i < kCoopMaxPlayers; ++i) send_bye(g_links[i]);
    const bool wasConnected = g_peerConnected;
    g_peerConnected = false;
    g_connecting = false;
    g_autoConnectPending = false;
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        if (g_links[i].used) puppet_hook_release_player(static_cast<uint8_t>(i));
        g_links[i].sock.close();
        g_links[i] = PeerLink{};
    }
    g_localId = kCoopHostId;
    g_roster = 1u << kCoopHostId;
    g_listener.close();
    g_udp.close();
    g_statusText = "Not connected";
    if (wasConnected) features_on_disconnected();
    puppet_hook_request_release();
    coop_log::info("coop_mod: disconnected");
}

void coop_net_send(uint8_t type, const void* payload, size_t size) {
    if (size > kCoopMaxMessagePayload) return;
    const bool local = stage_local_message(type);
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        if (local && !worth_sending(g_localId, i, 0)) continue;
        send_frame_to(g_links[i], type, g_localId, payload, size);
    }
}

void coop_net_send_to(uint8_t playerId, uint8_t type, const void* payload, size_t size) {
    if (playerId >= kCoopMaxPlayers || size > kCoopMaxMessagePayload) return;

    PeerLink& link = g_isHost ? g_links[playerId] : g_links[kCoopHostId];
    send_frame_to(link, type, g_localId, payload, size);
}

void coop_debug_spawn_puppet() {
    puppet_hook_trigger_spawn();
}

void coop_debug_force_transform() {
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr) {
        coop_log::info("coop_mod: transform requested but there is no player actor");
        return;
    }

    if (alink->mEquipItem == dItemNo_IRONBALL_e) {
        coop_log::info("coop_mod: transform refused - put the Ball and Chain away first");
        return;
    }
    const u16 before = alink->mProcID;
    const bool wasWolf = alink->checkWolf();
    alink->procCoMetamorphoseInit();
    coop_log::info("coop_mod: transform requested: wasWolf={} proc {} -> {}",
        static_cast<int>(wasWolf), before, alink->mProcID);
}

void coop_debug_give_midna() {
    dComIfGs_onEventBit(dSv_event_flag_c::M_067);
    dComIfGs_onEventBit(0xD04);

    dComIfGs_onTransformLV(3);
    coop_log::info("coop_mod: [DEBUG] Midna flags set (riding + Shadow Crystal + real body)");
}

extern "C" {

MOD_EXPORT ModResult mod_initialize(ModError*) {
    ConfigVarDesc modeDesc = CONFIG_VAR_DESC_INIT;
    modeDesc.name = "mode";
    modeDesc.type = CONFIG_VAR_STRING;
    modeDesc.default_string = "host";
    svc_config->register_var(mod_ctx, &modeDesc, &g_modeVar);

    ConfigVarDesc portDesc = CONFIG_VAR_DESC_INIT;
    portDesc.name = "bind_port";
    portDesc.type = CONFIG_VAR_INT;
    portDesc.default_int = 27716;
    svc_config->register_var(mod_ctx, &portDesc, &g_bindPortVar);

    ConfigVarDesc joinDesc = CONFIG_VAR_DESC_INIT;
    joinDesc.name = "join_address";
    joinDesc.type = CONFIG_VAR_STRING;

    joinDesc.default_string = "127.0.0.1";
    svc_config->register_var(mod_ctx, &joinDesc, &g_joinAddressVar);

    ConfigVarDesc joinPortDesc = CONFIG_VAR_DESC_INIT;
    joinPortDesc.name = "join_port";
    joinPortDesc.type = CONFIG_VAR_INT;
    joinPortDesc.default_int = 27716;
    svc_config->register_var(mod_ctx, &joinPortDesc, &g_joinPortVar);

    ConfigVarDesc upnpDesc = CONFIG_VAR_DESC_INIT;
    upnpDesc.name = "open_port_automatically";
    upnpDesc.type = CONFIG_VAR_BOOL;
    upnpDesc.default_bool = true;
    svc_config->register_var(mod_ctx, &upnpDesc, &g_upnpVar);

    ConfigVarDesc roomCodeDesc = CONFIG_VAR_DESC_INIT;
    roomCodeDesc.name = "room_code";
    roomCodeDesc.type = CONFIG_VAR_STRING;
    roomCodeDesc.default_string = "";
    svc_config->register_var(mod_ctx, &roomCodeDesc, &g_roomCodeVar);

    ConfigVarDesc roomServerDesc = CONFIG_VAR_DESC_INIT;
    roomServerDesc.name = "room_server";
    roomServerDesc.type = CONFIG_VAR_STRING;
    roomServerDesc.default_string = "";
    svc_config->register_var(mod_ctx, &roomServerDesc, &g_roomServerVar);

    ConfigVarDesc twilightDesc = CONFIG_VAR_DESC_INIT;
    twilightDesc.name = "debug_clear_twilight";
    twilightDesc.type = CONFIG_VAR_BOOL;
    twilightDesc.default_bool = false;
    svc_config->register_var(mod_ctx, &twilightDesc, &g_clearTwilightVar);

    ConfigVarDesc eponaDesc = CONFIG_VAR_DESC_INIT;
    eponaDesc.name = "debug_epona_flags";
    eponaDesc.type = CONFIG_VAR_BOOL;
    eponaDesc.default_bool = false;
    svc_config->register_var(mod_ctx, &eponaDesc, &g_eponaFlagsVar);

    ConfigVarDesc kitDesc = CONFIG_VAR_DESC_INIT;
    kitDesc.name = "debug_give_kit";
    kitDesc.type = CONFIG_VAR_BOOL;
    kitDesc.default_bool = false;
    svc_config->register_var(mod_ctx, &kitDesc, &g_giveKitVar);

    ConfigVarDesc fakeDesc = CONFIG_VAR_DESC_INIT;
    fakeDesc.name = "debug_fake_players";
    fakeDesc.type = CONFIG_VAR_INT;
    fakeDesc.default_int = 0;
    svc_config->register_var(mod_ctx, &fakeDesc, &g_fakePlayersVar);

    ConfigVarDesc hostKeyDesc = CONFIG_VAR_DESC_INIT;
    hostKeyDesc.name = "room_host_key";
    hostKeyDesc.type = CONFIG_VAR_STRING;
    hostKeyDesc.default_string = "";
    svc_config->register_var(mod_ctx, &hostKeyDesc, &g_hostKeyVar);

    ConfigVarDesc roomsDesc = CONFIG_VAR_DESC_INIT;
    roomsDesc.name = "room_codes";
    roomsDesc.type = CONFIG_VAR_BOOL;
    roomsDesc.default_bool = true;
    svc_config->register_var(mod_ctx, &roomsDesc, &g_roomsVar);

    ConfigVarDesc autoDesc = CONFIG_VAR_DESC_INIT;
    autoDesc.name = "auto_connect";
    autoDesc.type = CONFIG_VAR_BOOL;
    autoDesc.default_bool = false;
    svc_config->register_var(mod_ctx, &autoDesc, &g_autoConnectVar);

    ConfigVarDesc autoDelayDesc = CONFIG_VAR_DESC_INIT;
    autoDelayDesc.name = "auto_connect_delay_ticks";
    autoDelayDesc.type = CONFIG_VAR_INT;
    autoDelayDesc.default_int = 600;
    svc_config->register_var(mod_ctx, &autoDelayDesc, &g_autoConnectDelayTicksVar);

    bool autoConnect = false;
    svc_config->get_bool(mod_ctx, g_autoConnectVar, &autoConnect);
    if (autoConnect) {
        g_autoConnectPending = true;
        g_autoConnectTicksWaited = 0;
        coop_log::info("coop_mod: auto_connect requested, deferring connect");
    }

    features_register_vars();
    features_init();
    skins_init();
    voices_init();
    game_mode_init();
    puppet_register_vars();
    horses_init();
    ui_init();
    puppet_hook_init();
    skipvote_init();
    drops_init();

    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    if (g_autoConnectPending) {
        run_pending_auto_connect();
    }

    if (g_ticksSinceRx < 0xFFFFFFFFu) ++g_ticksSinceRx;

    const char* hereStage = dComIfGp_getStartStageName();
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        if (g_playerQuiet[i] < 0xFFFFFFFFu) ++g_playerQuiet[i];
        bool expectSnapshots = true;
        if (i != g_localId) {
            const CoopPeer& them = features_peer_of(static_cast<uint8_t>(i));
            expectSnapshots = them.present && them.inGame && hereStage != nullptr &&
                              them.stage[0] != 0 &&
                              std::strncmp(them.stage, hereStage, 8) == 0;
        }
        if (!expectSnapshots) {
            g_playerWorldStill[i] = 0;
            continue;
        }
        if (g_playerWorldStill[i] < 0xFFFFFFFFu) ++g_playerWorldStill[i];
    }

    {
        const uint32_t mine = coop_local_world_frames();
        const uint8_t me = g_localId;
        if (me < kCoopMaxPlayers) {
            if (!g_playerWorldSeen[me] || g_playerWorldTick[me] != mine) {
                g_playerWorldTick[me] = mine;
                g_playerWorldSeen[me] = true;
                g_playerWorldStill[me] = 0;
            }
        }
    }

    mods::net::Event event;
    while (mods::net::poll(event)) {
        if (event.handle == g_udp.handle()) {
            handle_udp_event(event);
        } else if (upnp_owns(event.handle)) {
            upnp_on_net_event(event);
        } else {
            handle_tcp_event(event);
        }
    }

    upnp_update();
    online_update();
    update_pings();
    announce_local_pause();
    features_update();

    apply_debug_clear_twilight();
    apply_debug_epona_flags();
    apply_debug_give_kit();
    send_local_snapshot();
    send_local_midna();
    send_local_horse();

    flush_udp_links();

    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    online_stop();
    for (int i = 0; i < kCoopMaxPlayers; ++i) {
        send_bye(g_links[i]);
        g_links[i].sock.close();
    }
    g_listener.close();
    g_udp.close();
    coop_log::info("coop_mod shut down");
    return MOD_OK;
}
}
