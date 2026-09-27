

#include "mod.hpp"
#include "net/messages.hpp"
#include "net/protocol.hpp"
#include "print.hpp"
#include "util.hpp"

#include "mods/svc/config.h"
#include "mods/svc/flow.hpp"
#include "mods/svc/hook.hpp"
#include "mods/svc/message.h"

#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_npc_bouS.h"
#include "d/actor/d_a_npc_wrestler.h"
#include "d/actor/d_a_tag_arena.h"
#include "d/d_com_inf_game.h"
#include "d/d_meter2_draw.h"
#include "d/d_meter2_info.h"
#include "d/d_msg_flow.h"
#include "f_op/f_op_actor_mng.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <string>

IMPORT_OPTIONAL_SERVICE(MessageService, svc_message);
IMPORT_OPTIONAL_SERVICE(FlowService, svc_flow);

DEFINE_HOOK_SYMBOL("daNpcWrestler_c::sumouWait", bool(daNpcWrestler_c*, void*), SumoWaitHook);
DEFINE_HOOK_SYMBOL("daNpcWrestler_c::sumouAI", void(daNpcWrestler_c*), SumoAIHook);
DEFINE_HOOK_SYMBOL("daNpcWrestler_c::checkOutOfArenaP", bool(daNpcWrestler_c*), SumoOutPHook);
DEFINE_HOOK_SYMBOL("daNpcWrestler_c::checkOutOfArenaW", bool(daNpcWrestler_c*), SumoOutWHook);
DEFINE_HOOK_SYMBOL("daNpcWrestler_c::Draw", int(daNpcWrestler_c*), SumoWrestlerDrawHook);
DEFINE_HOOK_SYMBOL("daAlink_c::procSumouMove", int(daAlink_c*), SumoLinkMoveHook);
DEFINE_HOOK_SYMBOL("daAlink_c::procSumouSideMove", int(daAlink_c*), SumoLinkSideHook);
DEFINE_HOOK_SYMBOL("daAlink_c::procSumouAction", int(daAlink_c*), SumoLinkActionHook);
DEFINE_HOOK_SYMBOL("daAlink_c::procSumouStagger", int(daAlink_c*), SumoLinkStaggerHook);
DEFINE_HOOK_SYMBOL("daNpcWrestler_c::setStepAngle", void(daNpcWrestler_c*), SumoStepAngleHook);
DEFINE_HOOK_SYMBOL("daNpcWrestler_c::sumouSideStep", bool(daNpcWrestler_c*, void*), SumoSideStepHook);
DEFINE_HOOK_SYMBOL("daNpcWrestler_c::sumouTackleStagger", bool(daNpcWrestler_c*, void*), SumoStaggerHook);
DEFINE_HOOK_SYMBOL("daNpcWrestler_c::setBackToLiving", void(daNpcWrestler_c*), SumoBackToLivingHook);
DEFINE_HOOK_SYMBOL("daNpcWrestler_c::gotoLiving", bool(daNpcWrestler_c*, void*), SumoGoHomeHook);
DEFINE_HOOK_SYMBOL("daNpcWrestler_c::setWrestlerVoice", void(daNpcWrestler_c*), SumoVoiceHook);
DEFINE_HOOK_SYMBOL("daAlink_c::setClothesChange", void(daAlink_c*, int), SumoClothesHook);
DEFINE_HOOK_SYMBOL("dMeter2Draw_c::getActionString", char*(dMeter2Draw_c*, u8, u8, u8*), SumoLabelHook);
DEFINE_HOOK_SYMBOL("daNpcWrestler_c::setNextAction", bool(daNpcWrestler_c*), SumoNextActionHook);

extern const ConfigService* svc_config;

namespace {

const u16 kBeatBo = 0x1C10;
const u16 kFirstMatch = 0x1C40;

const int kMoveSide = 1;
const int kMoveSlap = 2;
const int kMoveTackle = 3;
const int kMoveWait = 4;
const u8 kSideRight = 1;
const u8 kSideLeft = 2;
const int kEscapePresses = 10;

const int kFps = 30;
const u32 kAskTimeout = 25 * kFps;
const u32 kCallTimeout = 8 * kFps;
const u32 kMoveStale = kFps * 3 / 2;
const u32 kPeerSilence = 5 * kFps;
const u32 kRematchWait = 20 * kFps;
const u32 kPushedMax = 12 * kFps;
const u32 kSideRepeat = 10;

enum Phase : u8 {
    kIdle,
    kAsking,
    kAsked,
    kStarting,
    kMatch,
};

struct State {
    Phase phase = kIdle;
    u8 opponent = kCoopNoPlayer;
    bool authority = false;
    u32 since = 0;
    u32 lastHeard = 0;
    int move = 0;
    u32 moveAt = 0;
    u8 result = 0;
    u32 resultAt = 0;
    bool ended = false;
    bool fight = false;

    int stepDir = 0;
    u32 mashes = 0;
    u32 mashBase = 0;
    u32 pushedSince = 0;
    f32 pushRate = 0.0f;
    f32 ownPushRate = 0.0f;

    u8 mirrorAct = 0xFF;
    u8 mirrorBck = 0xFF;
    f32 mirrorFrame = 0.0f;
    cXyz mirrorPos;
    s16 mirrorAngle = 0;
    u32 mirrorAt = 0;
    bool mirrorHeard = false;
    u16 mirrorAnm = 0;
    u32 sideSentAt = 0;
    u8 voicedBck = 0xFF;
    f32 voicedFrame = -1.0f;

    bool home = false;
    cXyz homePos;
    s16 homeAngle = 0;
    bool sawTalk = false;
    bool sawWrestler = false;
    bool rematch = false;
    std::string name;
};
State s;
u32 s_frame = 0;
ConfigVarHandle s_testVar = 0;
bool s_testDone = false;
u8 s_promptFor = kCoopNoPlayer;
u8 s_waitingFrom = kCoopNoPlayer;
u32 s_waitingSince = 0;
u8 s_lastOpponent = kCoopNoPlayer;
u32 s_lastEnded = 0;
bool s_labelActive = false;

void set_phase(Phase phase) {
    s.phase = phase;
    s.since = s_frame;
}

void reset() {
    s = State{};
    s.since = s_frame;
}

void send(u8 to, u8 kind, u8 value, u8 pad = 0) {
    MsgSumo msg{};
    msg.kind = kind;
    msg.to = to;
    msg.value = value;
    msg.pad = pad;
    coop_net_send(kMsgSumo, &msg, sizeof(msg));
}

struct Find {
    s16 name;
    fopAc_ac_c* found;
};

void* find_by_name(void* proc, void* data) {
    auto* want = static_cast<Find*>(data);
    auto* actor = static_cast<fopAc_ac_c*>(proc);
    if (want->found == nullptr && actor != nullptr && fopAcM_GetName(actor) == want->name) {
        want->found = actor;
    }
    return nullptr;
}

fopAc_ac_c* find_actor(s16 name) {
    Find want{name, nullptr};
    fopAcM_Search(find_by_name, &want);
    return want.found;
}

daNpcBouS_c* find_bo() { return static_cast<daNpcBouS_c*>(find_actor(fpcNm_NPC_BOU_S_e)); }
daNpcWrestler_c* find_wrestler() {
    return static_cast<daNpcWrestler_c*>(find_actor(fpcNm_NPC_WRESTLER_e));
}
daTagArena_c* find_arena() { return static_cast<daTagArena_c*>(find_actor(fpcNm_Tag_Arena_e)); }

bool in_ring(const daTagArena_c* arena, f32 x, f32 z) {
    const cXyz at = const_cast<daTagArena_c*>(arena)->getArenaPos();
    const f32 r = const_cast<daTagArena_c*>(arena)->getArenaExtent();
    const f32 dx = x - at.x;
    const f32 dz = z - at.z;
    return dx * dx + dz * dz < r * r;
}

bool beat_bo() {
    return dComIfGs_isEventBit(kBeatBo);
}

bool free_to_wrestle(daAlink_c* alink) {
    return alink != nullptr && !alink->checkWolf() && !dComIfGp_event_runCheck() &&
           alink->mClothesChangeWaitTimer == 0 && beat_bo();
}

u8 prompt_target(daAlink_c* alink) {
    if (!free_to_wrestle(alink) || alink->speedF > 0.5f) return kCoopNoPlayer;
    daTagArena_c* arena = find_arena();
    if (arena == nullptr || find_bo() == nullptr || find_wrestler() != nullptr) return kCoopNoPlayer;
    const cXyz& me = alink->current.pos;
    if (!in_ring(arena, me.x, me.z)) return kCoopNoPlayer;
    u8 best = kCoopNoPlayer;
    f32 bestD = 0.0f;
    for (u8 id = 0; id < kCoopMaxPlayers; ++id) {
        if (id == coop_net_local_id()) continue;
        f32 x, y, z;
        short angle;
        if (!puppet_hook_get_pose_of(id, &x, &y, &z, &angle, nullptr, nullptr)) continue;
        if (puppet_hook_is_wolf_of(id) || !in_ring(arena, x, z) || std::abs(y - me.y) > 60.0f) continue;
        const f32 dx = x - me.x;
        const f32 dz = z - me.z;
        const f32 d = dx * dx + dz * dz;
        if (d > 200.0f * 200.0f) continue;
        const s16 toThem = cM_atan2s(dx, dz);
        if (std::abs(static_cast<s16>(toThem - alink->shape_angle.y)) > 0x2400) continue;
        if (best == kCoopNoPlayer || d < bestD) {
            best = id;
            bestD = d;
        }
    }
    return best;
}

void remember_home() {
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink == nullptr) return;
    s.home = true;
    s.homePos = alink->current.pos;
    s.homeAngle = alink->shape_angle.y;
}

void challenge(u8 target) {
    s.opponent = target;
    remember_home();
    s.authority = true;
    s.name = features_peer_name(target);
    s.lastHeard = s_frame;
    set_phase(kAsking);
    send(target, kSumoChallenge, 0);
    features_toast("Sumo", ("Waiting for " + s.name + " to answer...").c_str());
    coop_log::info("coop_mod: [SUMO] challenged {}", target);
}

bool free_to_be_asked(daAlink_c* alink) {
    return s.phase == kIdle && free_to_wrestle(alink) && find_bo() != nullptr && find_arena() != nullptr &&
           find_wrestler() == nullptr;
}

void take_challenge(u8 from) {
    s.opponent = from;
    remember_home();
    s.authority = false;
    s.name = features_peer_name(from);
    s.lastHeard = s_frame;
    set_phase(kAsked);
    coop_log::info("coop_mod: [SUMO] {} challenged us", from);
}

void spawn_wrestler(daNpcBouS_c* bo) {

    const u32 params = (2u << 24) | 0x700u | static_cast<u8>(bo->getArenaNo());
    bo->parentActorID = fopAcM_createChild(fpcNm_NPC_WRESTLER_e, fopAcM_GetID(bo), params,
        &bo->current.pos, fopAcM_GetRoomNo(bo), nullptr, nullptr, bo->getType(), nullptr);
    coop_log::info("coop_mod: [SUMO] wrestler spawned for the match against {}", s.opponent);
}

const u8 kChoicesFollow[] = {0x1A, 0x05, 0x00, 0x00, 0x20};
const u8 kChoice1[] = {0x1A, 0x06, 0x00, 0x00, 0x08, 0x01};
const u8 kChoice2[] = {0x1A, 0x06, 0x00, 0x00, 0x08, 0x02};

std::string s_text;

void append(std::string& out, const u8* bytes, size_t n) {
    out.append(reinterpret_cast<const char*>(bytes), n);
}

std::string plain(const std::string& in) {
    std::string out;
    for (char ch : in) {
        const u8 c = static_cast<u8>(ch);
        if (c >= 0x20 && c < 0x7F) out.push_back(ch);
    }
    return out.empty() ? std::string("Someone") : out;
}

enum Line : u8 { kLineAsk };

struct Reword {
    u16 id;
    u16 entry;
    Line line;
};
const Reword kRewords[] = {
    {5079, 78, kLineAsk},
};
const u16 kBoGroup = 1;

bool reword(ModContext*, const MessageOverrideContext* message, MessageTextData* out, void* user) {
    const Line line = static_cast<Line>(reinterpret_cast<uintptr_t>(user));
    if (s.phase != kAsked) return false;
    s_text.clear();
    switch (line) {
    case kLineAsk:
        s_text = "Hoho! " + plain(s.name) + " wants to wrestle you!";
        append(s_text, kChoicesFollow, sizeof(kChoicesFollow));
        break;
    }
    static u32 s_logged[sizeof(kRewords) / sizeof(kRewords[0])] = {};
    for (size_t i = 0; i < sizeof(kRewords) / sizeof(kRewords[0]); ++i) {
        if (kRewords[i].id == message->message_id && s_logged[i] != s.since + 1) {
            s_logged[i] = s.since + 1;
            coop_log::info("coop_mod: [SUMO] our line for message {}", message->message_id);
        }
    }
    s_text.push_back('\0');
    out->text = reinterpret_cast<const u8*>(s_text.data());
    out->text_size = s_text.size();
    return true;
}

void register_rewords() {
    if (svc_message == nullptr) {
        coop_log::warn("coop_mod: [SUMO] no message service - Bo keeps his own lines");
        return;
    }
    const u8 languages[] = {MESSAGE_LANGUAGE_ENGLISH, MESSAGE_LANGUAGE_GERMAN, MESSAGE_LANGUAGE_FRENCH,
        MESSAGE_LANGUAGE_SPANISH, MESSAGE_LANGUAGE_ITALIAN, MESSAGE_LANGUAGE_JAPANESE};
    int ok = 0;
    for (const Reword& r : kRewords) {
        for (u8 language : languages) {
            MessageOverrideHandle handle = 0;
            if (svc_message->override_message_fn(mod_ctx, kBoGroup, r.id, language, reword,
                    reinterpret_cast<void*>(static_cast<uintptr_t>(r.line)), &handle) == MOD_OK) {
                ++ok;
            }
        }
    }
    coop_log::info("coop_mod: [SUMO] {} of Bo's lines can be reworded", ok);
}

const u16 kBoTalkStart = 294;
const FlowNodeData kBoTalkStartNode = {{0x02, 0x02, 0x00, 0x01, 0x00, 0xE8, 0x00, 0xBB}};
const u16 kWrestlerTalkStart = 218;
const FlowNodeData kWrestlerTalkStartNode = {{0x02, 0x02, 0x00, 0x0A, 0x00, 0x30, 0x00, 0x7E}};

enum TalkPath : u16 { kTalkTheirOwn, kTalkChallenge, kTalkWeWon, kTalkWeLost, kTalkPaths };

mods::flow::Query s_talkQuery;
mods::flow::Graph s_talkGraph;
std::vector<mods::flow::RegisteredMessage> s_ourMessages;

bool we_won(u8 result);

uint16_t talk_query(ModContext*, const FlowQueryContext* query, void*) {
    if (query == nullptr || query->speaker_actor == nullptr) return kTalkTheirOwn;
    auto* speaker = static_cast<fopAc_ac_c*>(const_cast<void*>(query->speaker_actor));
    const s16 name = fopAcM_GetName(speaker);
    TalkPath path = kTalkTheirOwn;
    if (s.phase == kAsked && name == fpcNm_NPC_BOU_S_e) {
        path = kTalkChallenge;
    } else if (s.phase == kMatch && s.result != 0 && name == fpcNm_NPC_WRESTLER_e) {
        path = we_won(s.result) ? kTalkWeWon : kTalkWeLost;
    }
    if (query->phase == FLOW_QUERY_PHASE_EXECUTE && path != kTalkTheirOwn) {
        coop_log::info("coop_mod: [SUMO] talk: our path {}", static_cast<int>(path));
    }
    return path;
}

u16 entry_of(Line line) {
    for (const Reword& r : kRewords) {
        if (r.line == line) return r.entry;
    }
    return 0;
}

const u8 kBoVoice = 0x1A;
const mods::flow::MessageStyle kBoStyle = mods::flow::MessageStyle{}
                                              .speaker(kBoVoice)
                                              .box_position(MESSAGE_POSITION_AUTO)
                                              .trailing_data(0x0400);
const mods::flow::MessageStyle kTheirStyle =
    mods::flow::MessageStyle{}.box_position(MESSAGE_POSITION_AUTO).trailing_data(0x0400);

u16 our_message(const mods::flow::MessageBuilder& builder) {
    const MessageLanguage languages[] = {MESSAGE_LANGUAGE_ENGLISH, MESSAGE_LANGUAGE_GERMAN,
        MESSAGE_LANGUAGE_FRENCH, MESSAGE_LANGUAGE_SPANISH, MESSAGE_LANGUAGE_ITALIAN,
        MESSAGE_LANGUAGE_JAPANESE};
    std::vector<mods::flow::MessageVariant> variants;
    for (MessageLanguage language : languages) variants.push_back(builder.build(language));
    mods::flow::RegisteredMessage message =
        mods::flow::register_message(kBoGroup, std::span<const mods::flow::MessageVariant>(variants));
    if (!message) return 0;
    const u16 id = message.id();
    s_ourMessages.push_back(std::move(message));
    return id;
}

u16 line_of(const mods::flow::MessageStyle& style, const char* text) {
    return our_message(mods::flow::MessageBuilder{style}.text(text));
}

void build_talks() {
    if (svc_flow == nullptr || svc_message == nullptr) {
        coop_log::warn("coop_mod: [SUMO] no flow service - Bo keeps his own talk");
        return;
    }
    s_talkQuery = mods::flow::register_query("coop sumo talk", talk_query);
    if (!s_talkQuery) {
        coop_log::warn("coop_mod: [SUMO] flow query refused ({})", static_cast<int>(s_talkQuery.result()));
        return;
    }
    const u16 choiceMsg = our_message(mods::flow::MessageBuilder{}.options("Sure!", "Maybe later..."));
    const u16 yes1Msg = line_of(kBoStyle, "Now this I've gotta see!");
    const u16 yes2Msg = line_of(kBoStyle, "All righty! Into the ring, you two!");
    const u16 noMsg = line_of(kBoStyle, "Maybe next time, eh?");
    const u16 lost1Msg = line_of(kTheirStyle, "Ugh... you got me!");
    const u16 lost2Msg = line_of(kTheirStyle, "Good match!");
    const u16 won1Msg = line_of(kTheirStyle, "Hah! Out you go!");
    const u16 won2Msg = line_of(kTheirStyle, "Better luck next time!");
    if (choiceMsg == 0 || yes1Msg == 0 || yes2Msg == 0 || noMsg == 0 || lost1Msg == 0 || lost2Msg == 0 ||
        won1Msg == 0 || won2Msg == 0) {
        coop_log::warn("coop_mod: [SUMO] our lines refused - Bo keeps his own talk");
        return;
    }
    using mods::flow::kEnd;
    mods::flow::GraphBuilder g{kBoGroup};

    const auto start = g.add_event(FLOW_EVENT_START_EVENT, {0x00, 0x06, 0x00, 0x02}).next(kEnd);
    const auto yes2 = g.add_message(yes2Msg).next(start);
    const auto yes1 = g.add_message(yes1Msg).next(yes2);
    const auto no = g.add_message(noMsg).next(kEnd);
    const auto answer = g.add_branch(FLOW_QUERY_SELECT_2, 0).results({yes1, no});
    const auto choice = g.add_message(choiceMsg).next(answer);
    const auto ask = g.add_message(entry_of(kLineAsk)).next(choice);

    const auto home = g.add_event(FLOW_EVENT_START_EVENT, {0x00, 0x06, 0x00, 0x04}).next(kEnd);
    const auto lost2 = g.add_message(lost2Msg).next(home);
    const auto lost1 = g.add_message(lost1Msg).next(lost2);
    const auto won2 = g.add_message(won2Msg).next(home);
    const auto won1 = g.add_message(won1Msg).next(won2);

    const auto boOwn = g.add_node(kBoTalkStartNode);
    const auto wrestlerOwn = g.add_node(kWrestlerTalkStartNode);
    const std::array<uint16_t, kTalkPaths> boPaths{boOwn, ask, lost1, won1};
    const std::array<uint16_t, kTalkPaths> wrestlerPaths{wrestlerOwn, ask, lost1, won1};
    g.patch_branch(kBoTalkStart, s_talkQuery.id(), 0, boPaths);
    g.patch_branch(kWrestlerTalkStart, s_talkQuery.id(), 0, wrestlerPaths);
    s_talkGraph = g.commit();
    coop_log::info("coop_mod: [SUMO] our talks: {}", s_talkGraph ? "in" : "refused");
}

enum Act : u8 {
    kActSideStep, kActPunchHit, kActPunchChaseHit, kActPunchChaseShock, kActPunchShock, kActPunchDraw,
    kActPunchMiss, kActTackleHit, kActTackleMiss, kActTackleDraw, kActTackleShock,
    kActWin, kActWin2, kActLose, kActLose2,
    kActWait, kActTackleStagger, kActTacklePush, kActTackleRelease, kActTackleStaggerRelease,
    kActLostBalance, kActPunchStagger, kActIdle, kActGoHome, kActCount,
};
const char* const kActNames[kActCount] = {
    "daNpcWrestler_c::sumouSideStep", "daNpcWrestler_c::sumouPunchHit",
    "daNpcWrestler_c::sumouPunchChaseHit", "daNpcWrestler_c::sumouPunchChaseShock",
    "daNpcWrestler_c::sumouPunchShock", "daNpcWrestler_c::sumouPunchDraw",
    "daNpcWrestler_c::sumouPunchMiss", "daNpcWrestler_c::sumouTackleHit",
    "daNpcWrestler_c::sumouTackleMiss", "daNpcWrestler_c::sumouTackleDraw",
    "daNpcWrestler_c::sumouTackleShock", "daNpcWrestler_c::demoSumouWin",
    "daNpcWrestler_c::demoSumouWin2", "daNpcWrestler_c::demoSumouLose",
    "daNpcWrestler_c::demoSumouLose2",
    "daNpcWrestler_c::sumouWait", "daNpcWrestler_c::sumouTackleStagger",
    "daNpcWrestler_c::sumouTacklePush", "daNpcWrestler_c::sumouTackleRelease",
    "daNpcWrestler_c::sumouTackleStaggerRelease", "daNpcWrestler_c::sumouLostBalance",
    "daNpcWrestler_c::sumouPunchStagger", "daNpcWrestler_c::wait", "daNpcWrestler_c::gotoArena",
};
void* s_act[kActCount] = {};

using ActionFunc = daNpcWrestler_c::actionFunc;

static_assert(sizeof(ActionFunc) >= sizeof(void*), "a wrestler action starts with its code address");

ActionFunc action_at(void* at) {
    ActionFunc action;
    std::memset(&action, 0, sizeof(action));
    std::memcpy(&action, &at, sizeof(at));
    return action;
}

void* current_action(daNpcWrestler_c* w) {
    void* at = nullptr;
    std::memcpy(&at, &w->field_0xdcc, sizeof(at));
    return at;
}

void* landing_of(void* at) {
#if defined(_MSC_VER) && defined(_M_X64)

    const u8* code = static_cast<const u8*>(at);
    for (int hop = 0; hop < 2 && code != nullptr && code[0] == 0xE9; ++hop) {
        s32 rel = 0;
        std::memcpy(&rel, code + 1, sizeof(rel));
        code = code + 5 + rel;
    }
    return const_cast<u8*>(code);
#else
    return at;
#endif
}

void* s_actGame[kActCount] = {};

bool set_action(daNpcWrestler_c* w, Act act) {
    void* at = s_actGame[act] != nullptr ? s_actGame[act] : s_act[act];
    if (at == nullptr) return false;
    ActionFunc action = action_at(at);
    w->field_0xe96 = 3;
    if (w->field_0xdcc != nullptr) (w->*(w->field_0xdcc))(nullptr);
    w->field_0xe96 = 0;
    w->field_0xdcc = action;
    (w->*action)(nullptr);
    return true;
}

u8 act_of(daNpcWrestler_c* w) {
    void* raw = current_action(w);
    if (raw == nullptr) return 0xFF;
    void* at = landing_of(raw);
    for (u8 i = 0; i < kActCount; ++i) {
        if (s_act[i] != nullptr && at == landing_of(s_act[i])) {
            if (raw != s_act[i]) s_actGame[i] = raw;
            return i;
        }
    }
    return 0xFF;
}

struct WrestlerAccess : daNpcWrestler_c {
    static mDoExt_McaMorfSO* anm(daNpcWrestler_c* w) { return static_cast<WrestlerAccess*>(w)->mAnm_p; }
    static csXyz& cur_angle(daNpcWrestler_c* w) { return static_cast<WrestlerAccess*>(w)->mCurAngle; }
};

const char kWrestlerArc[] = "Bou3";

struct AnimPair {
    u8 bck;
    u16 anm;
    u8 boLen;
    u8 linkLen;
    bool loop;
};
const AnimPair kAnimPairs[] = {
    {0x06, daAlink_c::ANM_SUMOU_KNOCKED_DOWN, 40, 40, false},
    {0x1D, daAlink_c::ANM_SUMOU_FIGHT_STEP, 14, 14, false},
    {0x1E, daAlink_c::ANM_SUMOU_FIGHT_STEP_LEFT, 24, 26, true},
    {0x1F, daAlink_c::ANM_SUMOU_FIGHT_STEP_RIGHT, 24, 24, true},
    {0x20, daAlink_c::ANM_SUMOU_FIGHT_WAIT, 30, 30, true},
    {0x21, daAlink_c::ANM_SUMOU_HAKEYOI, 11, 11, false},
    {0x22, daAlink_c::ANM_SUMOU_GET_UP, 49, 49, false},
    {0x23, daAlink_c::ANM_SUMOU_LOSE, 46, 46, false},
    {0x24, daAlink_c::ANM_SUMOU_LOSE, 26, 46, false},
    {0x25, daAlink_c::ANM_SUMOU_MIAU, 30, 30, true},
    {0x26, daAlink_c::ANM_SUMOU_PULL_BACK, 39, 39, false},
    {0x27, daAlink_c::ANM_SUMOU_PUNCH, 32, 32, false},
    {0x28, daAlink_c::ANM_SUMOU_PUNCH_SHOCK, 32, 32, false},
    {0x29, daAlink_c::ANM_SUMOU_PUNCH_MISS_SHOCK, 44, 44, false},
    {0x2A, daAlink_c::ANM_SUMOU_PUNCH_MISS_SHOCK_RETURN, 46, 46, false},
    {0x2B, daAlink_c::ANM_SUMOU_PUSH_BACK, 39, 39, false},
    {0x2C, daAlink_c::ANM_SUMOU_TACKLE_WAIT, 41, 21, false},
    {0x2D, daAlink_c::ANM_SUMOU_SHIKO, 149, 146, false},
    {0x2E, daAlink_c::ANM_SUMOU_STAGGER, 25, 25, false},
    {0x2F, daAlink_c::ANM_SUMOU_FIGHT_STEP, 14, 14, false},
    {0x30, daAlink_c::ANM_SUMOU_TACKLE, 14, 14, false},
    {0x31, daAlink_c::ANM_SUMOU_DODGE_TACKLE, 44, 44, false},
    {0x32, daAlink_c::ANM_SUMOU_TACKLE_MISS, 29, 29, false},
    {0x33, daAlink_c::ANM_SUMOU_TACKLE_SHOCK, 14, 14, false},
    {0x34, daAlink_c::ANM_SUMOU_TACKLE_STAGGER, 22, 21, false},
    {0x35, daAlink_c::ANM_SUMOU_TACKLE_WAIT, 21, 21, false},
    {0x36, daAlink_c::ANM_SUMOU_TACKLE_SHOCK_RETURN, 31, 31, false},
    {0x37, daAlink_c::ANM_SUMOU_FIGHT_WAIT, 60, 30, true},
    {0x38, daAlink_c::ANM_SUMOU_FIGHT_WAIT, 46, 30, true},
    {0x39, daAlink_c::ANM_SUMOU_WIN, 46, 46, false},
    {0x3A, daAlink_c::ANM_SUMOU_WIN, 37, 46, false},
};

const AnimPair* pair_for(u8 bck) {
    for (const AnimPair& a : kAnimPairs) {
        if (a.bck == bck) return &a;
    }
    return nullptr;
}

f32 shiko_frame(f32 f) {
    static const f32 bo[] = {0.0f, 37.0f, 103.0f, 149.0f};
    static const f32 link[] = {0.0f, 65.0f, 127.0f, 146.0f};
    for (int i = 0; i < 3; ++i) {
        if (f <= bo[i + 1]) return link[i] + (f - bo[i]) * (link[i + 1] - link[i]) / (bo[i + 1] - bo[i]);
    }
    return link[3];
}

f32 link_frame(const AnimPair& a, f32 f) {
    if (a.bck == 0x2D) return shiko_frame(f);
    if (a.loop) {
        const f32 len = static_cast<f32>(a.linkLen);
        return f - len * static_cast<f32>(static_cast<int>(f / len));
    }
    return a.boLen == 0 ? f : f * static_cast<f32>(a.linkLen) / static_cast<f32>(a.boLen);
}

u16 link_anim_for(u8 bck) {
    const AnimPair* a = pair_for(bck);
    return a != nullptr ? a->anm : 0;
}

u8 bck_of(daNpcWrestler_c* w) {
    mDoExt_McaMorfSO* morf = WrestlerAccess::anm(w);
    J3DAnmTransform* now = morf != nullptr ? morf->getAnm() : nullptr;
    if (now == nullptr) return 0xFF;
    for (const AnimPair& a : kAnimPairs) {
        if (dComIfG_getObjectRes(kWrestlerArc, a.bck) == now) return a.bck;
    }
    return 0xFF;
}

f32 frame_of(daNpcWrestler_c* w) {
    mDoExt_McaMorfSO* morf = WrestlerAccess::anm(w);
    return morf != nullptr ? morf->getFrame() : 0.0f;
}

struct VoiceCue {
    u8 bck;
    f32 frame;
    u32 voice;
};
const VoiceCue kVoiceCues[] = {
    {0x27, 11.0f, Z2SE_AL_V_SUMO_HARITE_ATK},
    {0x29, 11.0f, Z2SE_AL_V_SUMO_HARITE_ATK},
    {0x29, 35.0f, Z2SE_AL_V_SUMO_HOLDED},
    {0x28, 15.0f, Z2SE_AL_V_SUMO_HARITE_DMG},
    {0x2A, 15.0f, Z2SE_AL_V_SUMO_HARITE_DMG},
    {0x30, 2.0f, Z2SE_AL_V_SUMO_TUCKLE_ATK},
    {0x31, 32.0f, Z2SE_AL_V_SUMO_TUCKLE_ATK},
    {0x32, 2.0f, Z2SE_AL_V_SUMO_TUCKLE_ATK},
    {0x33, 7.0f, Z2SE_AL_V_SUMO_HOLDED},
    {0x36, 7.0f, Z2SE_AL_V_SUMO_HOLDED},
    {0x2B, 5.0f, Z2SE_AL_V_SUMO_HOLD_BACK},
    {0x35, 5.0f, Z2SE_AL_V_SUMO_PUSH},
    {0x39, 10.0f, Z2SE_AL_V_SUMO_PUSH_LAST},
    {0x23, 23.0f, Z2SE_AL_V_SUMO_FALL_LOSE},
    {0x24, 3.0f, Z2SE_AL_V_SUMO_FALL_LOSE},
    {0x2D, 37.0f, Z2SE_AL_V_SUMO_SHIKO},
    {0x26, 13.0f, Z2SE_AL_V_SUMO_PUSHED_BACK},
};

bool in_end_scene(daNpcWrestler_c* w) {
    const u8 act = act_of(w);
    return act == kActWin || act == kActWin2 || act == kActLose || act == kActLose2;
}

bool in_match() {
    return (s.phase == kStarting || s.phase == kMatch) && s.opponent != kCoopNoPlayer;
}

bool ours(daNpcWrestler_c* w) {
    return in_match() && w != nullptr;
}

bool authority_fight() {
    return in_match() && s.authority && s.fight && !s.ended;
}

bool mirror_fight() {
    return in_match() && !s.authority && s.fight && !s.ended;
}

void on_ai_post(ModContext*, void* args, void*, void*) {
    auto* w = mods::arg<daNpcWrestler_c*>(args, 0);
    if (ours(w)) w->mWrestlerAction = kMoveWait;
}

HookAction on_wait_pre(ModContext*, void* args, void* r, void*) {
    auto* w = mods::arg<daNpcWrestler_c*>(args, 0);
    if (!ours(w)) return HOOK_CONTINUE;
    if (!s.fight) {

        s.fight = true;
        coop_log::info("coop_mod: [SUMO] the match begins here ({})", s.authority ? "played here" : "shown here");
    }
    if (!s.authority) {

        if (r != nullptr) *static_cast<bool*>(r) = true;
        return HOOK_SKIP_ORIGINAL;
    }
    if (w->field_0xe96 != 2) return HOOK_CONTINUE;
    if (s.move != 0 && s_frame - s.moveAt <= kMoveStale) {
        w->mWrestlerAction = s.move;
        w->field_0xe80 = -1;
        coop_log::info("coop_mod: [SUMO] the wrestler makes their move {}", s.move);
        s.move = 0;
    } else {

        s.move = 0;
        w->mWrestlerAction = kMoveWait;
        if (w->field_0xe80 < 5) w->field_0xe80 = 5;
    }
    return HOOK_CONTINUE;
}

void on_step_angle_post(ModContext*, void* args, void*, void*) {
    auto* w = mods::arg<daNpcWrestler_c*>(args, 0);
    if (!ours(w) || !s.authority || s.stepDir == 0) return;
    const s16 speed = static_cast<s16>(std::abs(w->mStepAngle));
    w->mStepAngle = s.stepDir > 0 ? speed : static_cast<s16>(-speed);
}

HookAction on_side_step_pre(ModContext*, void* args, void* r, void*) {
    auto* w = mods::arg<daNpcWrestler_c*>(args, 0);
    if (!ours(w) || !s.authority || w->field_0xe96 != 2 || w->field_0xe80 > 0) return HOOK_CONTINUE;
    daPy_py_c* player = daPy_getPlayerActorClass();
    if (player == nullptr || !player->checkSumouTackleMiss() || s.move == kMoveTackle) return HOOK_CONTINUE;
    set_action(w, kActWait);
    if (r != nullptr) *static_cast<bool*>(r) = true;
    return HOOK_SKIP_ORIGINAL;
}

HookAction on_stagger_pre(ModContext*, void* args, void*, void*) {
    auto* w = mods::arg<daNpcWrestler_c*>(args, 0);
    if (!ours(w) || !s.authority) return HOOK_CONTINUE;
    if (w->field_0xe96 == 0) {
        s.mashBase = s.mashes;
        s.pushedSince = s_frame;
    } else if (w->field_0xe96 == 2) {
        const bool free = s.mashes - s.mashBase >= static_cast<u32>(kEscapePresses) ||
                          s_frame - s.pushedSince > kPushedMax;
        if (free) {
            w->field_0xe80 = 0;
        } else if (w->field_0xe80 < 2) {
            w->field_0xe80 = 2;
        }
    }
    return HOOK_CONTINUE;
}

HookAction on_wrestler_voice_pre(ModContext*, void* args, void*, void*) {
    auto* w = mods::arg<daNpcWrestler_c*>(args, 0);
    if (!ours(w)) return HOOK_CONTINUE;

    if (mirror_fight()) return HOOK_SKIP_ORIGINAL;
    const u8 bck = bck_of(w);
    const f32 frame = frame_of(w);
    for (const VoiceCue& cue : kVoiceCues) {
        if (cue.bck == bck && cue.frame == frame) fx_play_for(s.opponent, cue.voice, &w->eyePos.x);
    }
    return HOOK_SKIP_ORIGINAL;
}

bool s_goingHomeStep = false;

void clear_match_marks() {
    daNpcF_offTmpBit(0x2E);
    daNpcF_offTmpBit(0x2F);
    daNpcF_offTmpBit(0x30);
}

void back_where_we_stood() {
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink != nullptr && s.home) {
        cXyz at = s.homePos;
        alink->setPlayerPosAndAngle(&at, s.homeAngle, TRUE);
    }
}

HookAction on_go_home_pre(ModContext*, void* args, void*, void*) {
    auto* w = mods::arg<daNpcWrestler_c*>(args, 0);
    if (!ours(w) || w->field_0xe96 != 0) return HOOK_CONTINUE;
    clear_match_marks();
    s_goingHomeStep = true;
    return HOOK_CONTINUE;
}

void on_go_home_post(ModContext*, void* args, void*, void*) {
    if (!s_goingHomeStep) return;
    s_goingHomeStep = false;
    auto* w = mods::arg<daNpcWrestler_c*>(args, 0);
    if (auto* bo = static_cast<daNpcBouS_c*>(fpcM_SearchByID(w->parentActorID))) {
        bo->mForcibleTalk = 0;
        bo->setMessageNo(bo->getMessageNo());
        bo->onDispFlag();
    }
    back_where_we_stood();
    coop_log::info("coop_mod: [SUMO] home: Bo left to his own talk, us where we stood");
}

HookAction on_back_to_living_pre(ModContext*, void* args, void*, void*) {
    auto* w = mods::arg<daNpcWrestler_c*>(args, 0);
    if (!ours(w)) return HOOK_CONTINUE;
    if (auto* bo = static_cast<daNpcBouS_c*>(fpcM_SearchByID(w->parentActorID))) bo->onDispFlag();
    back_where_we_stood();

    clear_match_marks();
    w->field_0xe99 = 1;
    coop_log::info("coop_mod: [SUMO] Bo is back, with nothing to say");
    return HOOK_SKIP_ORIGINAL;
}

HookAction on_link_sumo_pre(ModContext*, void* args, void* r, void*) {
    if (!mirror_fight() || mods::arg<daAlink_c*>(args, 0) != daAlink_getAlinkActorClass()) {
        return HOOK_CONTINUE;
    }
    if (r != nullptr) *static_cast<int*>(r) = 1;
    return HOOK_SKIP_ORIGINAL;
}

bool pushed(u8 act) { return act == kActTackleShock || act == kActTackleStagger || act == kActPunchMiss; }
bool pushing(u8 act) { return act == kActTackleHit || act == kActTacklePush; }

void mirror_frame(daAlink_c* alink, daNpcWrestler_c* w) {

    f32 x, y, z;
    short angle = 0;
    if (w != nullptr && puppet_hook_get_pose_of(s.opponent, &x, &y, &z, &angle, nullptr, nullptr)) {
        w->current.pos.set(x, y, z);
        w->old.pos = w->current.pos;
        w->shape_angle.y = angle;
        w->current.angle.y = angle;
        WrestlerAccess::cur_angle(w).y = angle;
    }
    if (!s.mirrorHeard) return;

    const AnimPair* pair = pair_for(s.mirrorBck);
    const u16 anm = pair != nullptr ? pair->anm : 0;
    const f32 mapped = pair != nullptr ? link_frame(*pair, s.mirrorFrame) : 0.0f;
    if (anm != 0 && anm != s.mirrorAnm) {
        alink->setSingleAnime(static_cast<daAlink_c::daAlink_ANM>(anm), 0.0f, mapped, -1, 3.0f);
        s.mirrorAnm = anm;
    }
    if (s.mirrorAnm != 0) {

        f32 frame = mapped;
        if (J3DAnmTransform* now = alink->getNowAnmPackUnder(daAlink_c::UNDER_0)) {
            const f32 last = static_cast<f32>(now->getFrameMax());
            if (frame > last) frame = last;
            now->setFrame(frame);
        }
        if (J3DAnmTransform* now = alink->getNowAnmPackUpper(daAlink_c::UPPER_0)) now->setFrame(frame);
        alink->mUnderFrameCtrl[0].setFrame(frame);
        alink->mUpperFrameCtrl[0].setFrame(frame);
        alink->mUnderFrameCtrl[0].setRate(0.0f);
        alink->mUpperFrameCtrl[0].setRate(0.0f);
    }
    cXyz at = s.mirrorPos;
    alink->setPlayerPosAndAngle(&at, s.mirrorAngle, TRUE);

    for (const VoiceCue& cue : kVoiceCues) {
        if (cue.bck != s.mirrorBck || s.mirrorFrame < cue.frame) continue;
        const bool already = s.voicedBck == s.mirrorBck && s.voicedFrame >= cue.frame;
        if (!already && s.mirrorFrame - cue.frame < 3.0f) {
            fx_play_for(coop_net_local_id(), cue.voice, &alink->eyePos.x);
        }
    }
    s.voicedBck = s.mirrorBck;
    s.voicedFrame = s.mirrorFrame;

    const u8 act = s.mirrorAct;
    if (pushed(act) || pushing(act)) {
        alink->setDoStatusContinuation(0x64);
        dMeter2Info_onBlinkButton(1);
    } else {
        alink->setSumouPunchStatus();
        alink->setDoStatusEmphasys(BUTTON_STATUS_GRAB);
    }
    if (alink->doTrigger()) {
        if (pushed(act) || pushing(act)) {
            send(s.opponent, kSumoMash, 1);
        } else {
            send(s.opponent, kSumoMove, kMoveTackle);
            coop_log::info("coop_mod: [SUMO] our move: tackle");
        }
    } else if (alink->swordTrigger()) {
        if (!pushed(act) && !pushing(act)) {
            send(s.opponent, kSumoMove, kMoveSlap);
            coop_log::info("coop_mod: [SUMO] our move: slap");
        }
    } else if (!pushed(act) && !pushing(act) && alink->checkInputOnR() &&
               s_frame - s.sideSentAt >= kSideRepeat) {
        const int dir = alink->getDirectionFromShapeAngle();
        if (dir == daAlink_c::DIR_LEFT || dir == daAlink_c::DIR_RIGHT) {
            send(s.opponent, kSumoMove, kMoveSide, dir == daAlink_c::DIR_RIGHT ? kSideRight : kSideLeft);
            s.sideSentAt = s_frame;
        }
    }
}

void send_state(daNpcWrestler_c* w) {
    MsgSumoState msg{};
    msg.to = s.opponent;
    msg.act = act_of(w);
    msg.bck = bck_of(w);
    msg.frame = frame_of(w);
    msg.x = w->current.pos.x;
    msg.y = w->current.pos.y;
    msg.z = w->current.pos.z;
    msg.angle = w->shape_angle.y;
    coop_net_send(kMsgSumoState, &msg, sizeof(msg));
}

const f32 kPushPerPress = 0.35f;
const f32 kPushFade = 0.04f;
const f32 kPushMax = 1.5f;
const f32 kPushBack = 0.6f;
const u8 kModePushing = 7;
const u8 kModePushed = 8;

void press_push(f32& rate) {
    rate = rate + kPushPerPress > kPushMax ? kPushMax : rate + kPushPerPress;
}

bool pushed(u8 act);
bool pushing(u8 act);

void authority_frame(daAlink_c* alink, daNpcWrestler_c* w) {
    const u8 act = act_of(w);
    const bool clinch = alink->mMode == kModePushing || alink->mMode == kModePushed;
    if (clinch && alink->doTrigger()) press_push(s.ownPushRate);
    cLib_chaseF(&s.pushRate, 0.0f, kPushFade);
    cLib_chaseF(&s.ownPushRate, 0.0f, kPushFade);
    f32 rate = -1.0f;
    if (alink->mMode == kModePushing && pushed(act)) {
        rate = s.ownPushRate - s.pushRate * kPushBack;
    } else if (alink->mMode == kModePushed && act == kActTacklePush) {
        rate = s.pushRate - s.ownPushRate * kPushBack;
    }
    if (rate != -1.0f) {
        if (rate < 0.0f) rate = 0.0f;
        alink->mUnderFrameCtrl[0].setRate(rate);
        alink->mUpperFrameCtrl[0].setRate(rate);
    }
    send_state(w);
}

bool pvp_next_action(daNpcWrestler_c* w) {
    daPy_py_c* player = daPy_getPlayerActorClass();
    if (player == nullptr) return false;
    const bool sliding = player->checkSumouSlideLeft() || player->checkSumouSlideRight() ||
                         player->checkSumouLeftMove() || player->checkSumouRightMove();

    if ((player->checkSumouPunchStagger() || player->checkSumouTackleStagger()) &&
        w->mWrestlerAction == kMoveTackle) {
        set_action(w, kActTackleHit);
        player->setSumouForceTackle();
        return true;
    }
    switch (w->mWrestlerAction) {
    case kMoveSide:
        if (player->checkSumouTackleMiss()) return set_action(w, kActSideStep);
        if (player->checkSumouPunchSuccess()) return set_action(w, kActPunchChaseShock);
        if (sliding) return set_action(w, kActSideStep);
        break;
    case kMoveSlap:
        if (sliding) return set_action(w, kActPunchChaseHit);
        if (player->checkSumouPunchDraw()) return set_action(w, kActPunchDraw);
        if (player->checkSumouTackleSuccess() || player->checkSumouTackleSuccessPunch()) {
            return set_action(w, kActPunchMiss);
        }
        break;
    case kMoveTackle:
        if (player->checkSumouSlideLeft() || player->checkSumouSlideRight()) {
            return set_action(w, kActTackleMiss);
        }
        if (player->checkSumouPunchMiss()) return set_action(w, kActTackleHit);
        if (player->checkSumouTackleDraw()) return set_action(w, kActTackleDraw);
        break;
    default:

        if (player->checkSumouTackleSuccess()) return set_action(w, kActTackleShock);
        if (player->checkSumouPunchSuccess()) return set_action(w, kActPunchShock);
        break;
    }
    return false;
}

HookAction on_next_action_pre(ModContext*, void* args, void* r, void*) {
    auto* w = mods::arg<daNpcWrestler_c*>(args, 0);
    if (!ours(w)) return HOOK_CONTINUE;
    const bool acted = pvp_next_action(w);
    if (r != nullptr) *static_cast<bool*>(r) = acted;
    return HOOK_SKIP_ORIGINAL;
}

HookAction on_wrestler_draw_pre(ModContext*, void* args, void* r, void*) {
    if (!ours(mods::arg<daNpcWrestler_c*>(args, 0))) return HOOK_CONTINUE;
    if (r != nullptr) *static_cast<int*>(r) = 1;
    return HOOK_SKIP_ORIGINAL;
}

bool we_won(u8 result) {
    return result == (s.authority ? kSumoChallengerWon : kSumoChallengedWon);
}

u8 result_for(bool weWon) {
    return (weWon == s.authority) ? kSumoChallengerWon : kSumoChallengedWon;
}

void ring_out(void* args, void* r, bool player) {
    if (r == nullptr || !ours(mods::arg<daNpcWrestler_c*>(args, 0))) return;
    bool& out = *static_cast<bool*>(r);
    if (!s.authority && s.result == 0) {
        out = false;
        return;
    }
    if (s.result == 0 && out) {

        s.result = result_for(!player);
        s.resultAt = s_frame;
        send(s.opponent, kSumoResult, s.result);
        coop_log::info("coop_mod: [SUMO] ring-out seen here: {}", player ? "we lost" : "we won");
    }
    if (s.result != 0) {
        out = player ? !we_won(s.result) : we_won(s.result);
    }
}

void on_out_p_post(ModContext*, void* args, void* r, void*) { ring_out(args, r, true); }
void on_out_w_post(ModContext*, void* args, void* r, void*) { ring_out(args, r, false); }

HookAction on_clothes_pre(ModContext*, void* args, void*, void*) {
    if (mods::arg<daAlink_c*>(args, 0) != daAlink_getAlinkActorClass()) return HOOK_CONTINUE;

    return in_match() ? HOOK_SKIP_ORIGINAL : HOOK_CONTINUE;
}

char s_label[] = "Sumo";

void on_label_post(ModContext*, void* args, void* r, void*) {
    if (!s_labelActive || r == nullptr) return;
    if (mods::arg<u8>(args, 1) == BUTTON_STATUS_SPEAK) *static_cast<char**>(r) = s_label;
}

}

void sumo_after_player(daAlink_c* alink) {
    ++s_frame;
    if (alink == nullptr) return;
    if (!s_testDone && cfg_bool(s_testVar, false)) {
        s_testDone = true;
        dComIfGs_onEventBit(kFirstMatch);
        dComIfGs_onEventBit(kBeatBo);
        coop_log::info("coop_mod: [SUMO] debug: Bo counts as beaten");
    }

    const u8 target = s.phase == kIdle ? prompt_target(alink) : kCoopNoPlayer;
    if (s.phase == kIdle && s_promptFor != kCoopNoPlayer && s_promptFor == target &&
        alink->doTrigger()) {
        challenge(target);
    }

    if (s_waitingFrom != kCoopNoPlayer) {
        if (free_to_be_asked(alink)) {
            take_challenge(s_waitingFrom);
            s_waitingFrom = kCoopNoPlayer;
        } else if (s_frame - s_waitingSince > kRematchWait) {
            send(s_waitingFrom, kSumoAnswer, 0);
            s_waitingFrom = kCoopNoPlayer;
        }
    }
    s_promptFor = s.phase == kIdle ? target : kCoopNoPlayer;
    s_labelActive = s_promptFor != kCoopNoPlayer;
    if (s_labelActive && dComIfGp_getDoStatus() == 0) {
        dComIfGp_setDoStatus(BUTTON_STATUS_SPEAK, BUTTON_STATUS_FLAG_NONE);
    }

    daNpcWrestler_c* wrestler = find_wrestler();
    switch (s.phase) {
    case kIdle:
        break;
    case kAsking:
        if (s_frame - s.since > kAskTimeout) {
            send(s.opponent, kSumoCancel, 0);
            features_toast("Sumo", (s.name + " didn't answer.").c_str());
            reset();
        }
        break;
    case kAsked: {

        if (dComIfGp_event_runCheck()) {
            f32 x, y, z;
            if (puppet_hook_get_pose_of(s.opponent, &x, &y, &z, nullptr, nullptr, nullptr)) {
                const cXyz them(x, y, z);
                const s16 toThem = cLib_targetAngleY(&alink->current.pos, &them);
                alink->shape_angle.y = toThem;
                alink->current.angle.y = toThem;
            }
        }

        if (wrestler != nullptr) {
            send(s.opponent, kSumoAnswer, 1);
            s.lastHeard = s_frame;
            set_phase(kStarting);
            coop_log::info("coop_mod: [SUMO] accepted {}'s challenge", s.opponent);
            break;
        }
        if (dComIfGp_event_runCheck()) {
            s.sawTalk = true;
        } else if (s.sawTalk || s_frame - s.since > kCallTimeout) {

            if (daNpcBouS_c* bo = find_bo()) bo->mForcibleTalk = 0;
            send(s.opponent, kSumoAnswer, 0);
            coop_log::info("coop_mod: [SUMO] declined {}'s challenge", s.opponent);
            reset();
        } else if (daNpcBouS_c* bo = find_bo()) {

            bo->mForcibleTalk = 1;
            bo->eventInfo.onCondition(dEvtCnd_CANTALK_e);
            fopAcM_orderSpeakEvent(bo, 0, 0);
        }
        break;
    }
    case kStarting:
    case kMatch:
        if (wrestler != nullptr) {
            s.sawWrestler = true;
            if (s.phase == kStarting) set_phase(kMatch);
        } else if (s.sawWrestler || s_frame - s.since > 10 * kFps) {
            coop_log::info("coop_mod: [SUMO] the match is over");
            const bool rematch = s.rematch;
            const u8 opponent = s.opponent;
            s_lastOpponent = opponent;
            s_lastEnded = s_frame;
            reset();
            if (rematch && coop_net_player_present(opponent)) challenge(opponent);
            break;
        }
        if (wrestler != nullptr && authority_fight()) authority_frame(alink, wrestler);
        if (mirror_fight()) mirror_frame(alink, wrestler);

        if (wrestler != nullptr && s.result != 0 && !s.ended) {
            if (in_end_scene(wrestler)) {
                s.ended = true;
            } else if (s_frame - s.resultAt >= 2) {
                const bool won = we_won(s.result);
                s.ended = set_action(wrestler, won ? kActLose : kActWin);
                coop_log::info("coop_mod: [SUMO] ending the match here: {} ({})", won ? "we won" : "we lost",
                    s.ended ? "done" : "no scene to play");
            }
        }

        if (coop_net_player_present(s.opponent)) {
            s.lastHeard = s_frame;
        } else if (s_frame - s.lastHeard > kPeerSilence) {

            features_toast("Sumo", (s.name + " left the match.").c_str());
            coop_log::info("coop_mod: [SUMO] {} left the session mid-match", s.opponent);
            reset();
        }
        break;
    }
}

void sumo_on_message(const uint8_t* payload, size_t size, uint8_t from) {
    if (size < sizeof(MsgSumo)) return;
    MsgSumo msg;
    std::memcpy(&msg, payload, sizeof(msg));
    if (msg.to != coop_net_local_id()) return;
    daAlink_c* alink = daAlink_getAlinkActorClass();
    switch (msg.kind) {
    case kSumoChallenge: {
        if (free_to_be_asked(alink)) {
            take_challenge(from);
            return;
        }
        const bool justWrestled = s_lastOpponent == from && s_frame - s_lastEnded < kRematchWait;
        if ((in_match() && from == s.opponent) || (s.phase == kIdle && justWrestled)) {

            s_waitingFrom = from;
            s_waitingSince = s_frame;
            coop_log::info("coop_mod: [SUMO] {} wants a rematch - once this match is over", from);
            return;
        }
        send(from, kSumoAnswer, 0);
        coop_log::info("coop_mod: [SUMO] {} challenged us - not free, said no", from);
        return;
    }
    case kSumoAnswer:
        if (s.phase != kAsking || from != s.opponent) return;
        s.lastHeard = s_frame;
        if (msg.value == 0) {
            features_toast("Sumo", (s.name + " said maybe later.").c_str());
            reset();
            return;
        }
        if (daNpcBouS_c* bo = find_bo()) {
            spawn_wrestler(bo);
            set_phase(kStarting);
        } else {
            send(from, kSumoCancel, 0);
            reset();
        }
        return;
    case kSumoCancel:
        if (from == s_waitingFrom) s_waitingFrom = kCoopNoPlayer;
        if (from != s.opponent) return;
        if (s.phase == kAsked) {
            if (daNpcBouS_c* bo = find_bo()) bo->mForcibleTalk = 0;
            reset();
        }
        return;
    case kSumoMove:
        if (!in_match() || from != s.opponent || !s.authority) return;
        s.lastHeard = s_frame;
        s.move = msg.value;
        s.moveAt = s_frame;
        s.stepDir = msg.value == kMoveSide ? (msg.pad == kSideLeft ? -1 : 1) : 0;
        return;
    case kSumoMash:
        if (!in_match() || from != s.opponent || !s.authority) return;
        s.lastHeard = s_frame;
        ++s.mashes;
        press_push(s.pushRate);
        return;
    case kSumoResult:
        if (!in_match() || from != s.opponent) return;
        s.lastHeard = s_frame;
        if (s.result == 0) {
            s.result = msg.value;
            s.resultAt = s_frame;
            coop_log::info("coop_mod: [SUMO] ring-out seen there: {}", we_won(s.result) ? "we won" : "we lost");
        } else if (s.result != msg.value) {

            coop_log::info("coop_mod: [SUMO] both screens saw a ring-out at once - {}",
                s.authority ? "ours stands" : "the challenger's stands");
            if (s.authority) {
                send(s.opponent, kSumoResult, s.result);
            } else {
                s.result = msg.value;
            }
        }
        return;
    default:
        return;
    }
}

void sumo_on_state(const uint8_t* payload, size_t size, uint8_t from) {
    if (size < sizeof(MsgSumoState)) return;
    MsgSumoState msg;
    std::memcpy(&msg, payload, sizeof(msg));
    if (msg.to != coop_net_local_id() || !in_match() || s.authority || from != s.opponent) return;
    s.lastHeard = s_frame;
    s.mirrorAct = msg.act;
    s.mirrorBck = msg.bck;
    s.mirrorFrame = msg.frame;
    s.mirrorPos.set(msg.x, msg.y, msg.z);
    s.mirrorAngle = msg.angle;
    s.mirrorAt = s_frame;
    s.mirrorHeard = true;
}

bool sumo_shows_in_cutscene(uint8_t playerId) {
    return in_match() && playerId == s.opponent;
}

bool sumo_puppet_anim(uint8_t playerId, uint16_t* underIdx, uint16_t* upperIdx, float* frame) {

    if (!in_match() || playerId != s.opponent || mirror_fight()) return false;

    if (s.result != 0) return false;
    daNpcWrestler_c* w = find_wrestler();
    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (w == nullptr || alink == nullptr) return false;
    const AnimPair* pair = pair_for(bck_of(w));
    if (pair == nullptr) return false;
    const daAlink_BckData* data = alink->getMainBckData(static_cast<daAlink_c::daAlink_ANM>(pair->anm));
    if (data == nullptr) return false;

    auto pack = [](u16 id) { return anm_pack((id >> 12) & 0xF ? (id >> 12) & 0xF : 0xFFFF, id & 0xFFF); };
    *underIdx = pack(data->m_underID);
    *upperIdx = pack(data->m_upperID);
    *frame = link_frame(*pair, frame_of(w));
    return true;
}

bool sumo_hides_equipment(uint8_t playerId) {
    return in_match() && (playerId == s.opponent || playerId == coop_net_local_id());
}

bool sumo_local_demo_running() {
    return in_match() && dComIfGp_event_runCheck();
}

bool sumo_puppet_transform(uint8_t playerId, cXyz* pos, csXyz* angle) {
    if (!in_match() || playerId != s.opponent) return false;
    daNpcWrestler_c* w = find_wrestler();
    if (w == nullptr) return false;
    if (pos != nullptr) *pos = w->current.pos;
    if (angle != nullptr) *angle = w->shape_angle;
    return true;
}

void sumo_init() {
    ConfigVarDesc testDesc = CONFIG_VAR_DESC_INIT;
    testDesc.name = "debug_sumo";
    testDesc.type = CONFIG_VAR_BOOL;
    testDesc.default_bool = false;
    if (svc_config->register_var(mod_ctx, &testDesc, &s_testVar) != MOD_OK) s_testVar = 0;
    register_rewords();
    build_talks();
    int resolved = 0;
    for (int i = 0; i < kActCount; ++i) {
        void* at = nullptr;
        if (svc_hook->resolve(mod_ctx, kActNames[i], &at, nullptr) == MOD_OK && at != nullptr) {
            s_act[i] = at;
            ++resolved;
        } else {
            coop_log::warn("coop_mod: [SUMO] no {}", kActNames[i]);
        }
    }
    const int results[] = {
        static_cast<int>(mods::hook::add_post<SumoAIHook>(on_ai_post)),
        static_cast<int>(mods::hook::add_pre<SumoWaitHook>(on_wait_pre)),
        static_cast<int>(mods::hook::add_pre<SumoWrestlerDrawHook>(on_wrestler_draw_pre)),
        static_cast<int>(mods::hook::add_pre<SumoLinkMoveHook>(on_link_sumo_pre)),
        static_cast<int>(mods::hook::add_pre<SumoLinkSideHook>(on_link_sumo_pre)),
        static_cast<int>(mods::hook::add_post<SumoOutPHook>(on_out_p_post)),
        static_cast<int>(mods::hook::add_post<SumoOutWHook>(on_out_w_post)),
        static_cast<int>(mods::hook::add_pre<SumoClothesHook>(on_clothes_pre)),
        static_cast<int>(mods::hook::add_post<SumoLabelHook>(on_label_post)),
        static_cast<int>(mods::hook::add_pre<SumoNextActionHook>(on_next_action_pre)),
        static_cast<int>(mods::hook::add_pre<SumoLinkActionHook>(on_link_sumo_pre)),
        static_cast<int>(mods::hook::add_pre<SumoLinkStaggerHook>(on_link_sumo_pre)),
        static_cast<int>(mods::hook::add_post<SumoStepAngleHook>(on_step_angle_post)),
        static_cast<int>(mods::hook::add_pre<SumoSideStepHook>(on_side_step_pre)),
        static_cast<int>(mods::hook::add_pre<SumoStaggerHook>(on_stagger_pre)),
        static_cast<int>(mods::hook::add_pre<SumoBackToLivingHook>(on_back_to_living_pre)),
        static_cast<int>(mods::hook::add_pre<SumoVoiceHook>(on_wrestler_voice_pre)),
        static_cast<int>(mods::hook::add_pre<SumoGoHomeHook>(on_go_home_pre)),
        static_cast<int>(mods::hook::add_post<SumoGoHomeHook>(on_go_home_post)),
    };
    int failed = 0;
    for (int result : results) failed += result != 0 ? 1 : 0;
    coop_log::info("coop_mod: [SUMO] {} of {} hooks in; {} of {} wrestler actions",
        static_cast<int>(sizeof(results) / sizeof(results[0])) - failed,
        static_cast<int>(sizeof(results) / sizeof(results[0])), resolved, static_cast<int>(kActCount));
}
