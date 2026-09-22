

#include "mod.hpp"
#include "print.hpp"

#include "mods/service.hpp"
#include "mods/svc/hook.hpp"

#include "JSystem/JAudio2/JAISe.h"
#include "JSystem/JAudio2/JASBasicWaveBank.h"
#include "JSystem/JAudio2/JASTrack.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace {

#pragma pack(push, 1)
struct VoiceEntry {
    uint32_t bank;
    uint32_t waveId;
    uint8_t format;
    uint8_t baseKey;
    uint8_t pad[2];
    float sampleRate;
    uint32_t loopFlag;
    uint32_t loopStart;
    uint32_t loopEnd;
    uint32_t sampleCount;
    int16_t last;
    int16_t penult;
    uint32_t dataSize;

    uint32_t gameSampleCount;
};
#pragma pack(pop)

struct Voice : public JASBasicWaveBank::TWaveHandle {
    void const* getAramBaseAddress() const override { return data.data(); }

    uint32_t waveId = 0;
    VoiceEntry entry{};
    std::vector<uint8_t> data;
};

struct VoiceSet {
    std::string skin;
    std::vector<Voice*> voices;
    JASHeap zeroHeap;
    bool failed = false;
};

std::vector<VoiceSet*> s_sets;

enum class Starter { None, Local, Remote };
Starter s_starter = Starter::None;
VoiceSet* s_starterSet = nullptr;

struct Tag {
    const JASTrack* root = nullptr;
    VoiceSet* set = nullptr;
};
const int kMaxTags = 128;
Tag s_tags[kMaxTags];
int s_tagNext = 0;
std::mutex s_tagMutex;

thread_local VoiceSet* t_noteSet = nullptr;

void tag_sound(const JASTrack* root, VoiceSet* set) {
    std::lock_guard<std::mutex> lock(s_tagMutex);
    for (Tag& t : s_tags) {
        if (t.root == root) {
            t.set = set;
            if (set == nullptr) t.root = nullptr;
            return;
        }
    }
    if (set == nullptr) return;
    s_tags[s_tagNext] = Tag{root, set};
    s_tagNext = (s_tagNext + 1) % kMaxTags;
}

VoiceSet* tag_of(const JASTrack* root) {
    std::lock_guard<std::mutex> lock(s_tagMutex);
    for (const Tag& t : s_tags) {
        if (t.root == root) return t.set;
    }
    return nullptr;
}

std::string s_wanted;

int s_swapped = 0;

VoiceSet* load_set(const std::string& skin) {
    for (VoiceSet* set : s_sets) {
        if (set->skin == skin) return set->failed ? nullptr : set;
    }
    VoiceSet* set = new VoiceSet();
    set->skin = skin;
    s_sets.push_back(set);

    set->zeroHeap.initRootHeap(nullptr, 0);

    const std::string path = skins_folder_path() + "/" + skin + "/voices.bin";
    std::ifstream file(path_ci(path), std::ios::binary);
    if (!file) {
        set->failed = true;
        return nullptr;
    }
    char magic[8] = {};
    uint32_t count = 0;
    file.read(magic, sizeof(magic));
    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (std::memcmp(magic, "COOPVOX3", 8) != 0 || count == 0 || count > 20000) {
        coop_log::warn("coop_mod: [VOICE] '{}' voices.bin is not one of ours", skin);
        set->failed = true;
        return nullptr;
    }

    uint32_t total = 0;
    for (uint32_t i = 0; i < count; ++i) {
        VoiceEntry entry{};
        if (!file.read(reinterpret_cast<char*>(&entry), sizeof(entry))) break;
        if (entry.dataSize == 0 || entry.dataSize > 1024 * 1024) break;
        Voice* voice = new Voice();
        voice->data.resize(entry.dataSize);
        if (!file.read(reinterpret_cast<char*>(voice->data.data()), entry.dataSize)) {
            delete voice;
            break;
        }
        voice->waveId = entry.waveId;
        voice->entry = entry;
        voice->mWaveInfo.mWaveFormat = entry.format;
        voice->mWaveInfo.mBaseKey = entry.baseKey;
        voice->mWaveInfo.mLoopFlag = entry.loopFlag != 0 ? 0xFF : 0;
        voice->mWaveInfo.mSampleRate = entry.sampleRate;
        voice->mWaveInfo.mOffsetStart = 0;
        voice->mWaveInfo.mOffsetLength = static_cast<int>(entry.dataSize);
        voice->mWaveInfo.mLoopStartSample = entry.loopStart;
        voice->mWaveInfo.mLoopEndSample = static_cast<int>(entry.loopEnd);
        voice->mWaveInfo.mSampleCount = static_cast<int>(entry.sampleCount);
        voice->mWaveInfo.mpLast = entry.last;
        voice->mWaveInfo.mpPenult = entry.penult;
        voice->mHeap = &set->zeroHeap;
        total += entry.dataSize;
        set->voices.push_back(voice);
    }
    if (set->voices.empty()) {
        set->failed = true;
        return nullptr;
    }
    coop_log::info("coop_mod: [VOICE] '{}': all {} sounds loaded ({} KB)", skin,
        set->voices.size(), total / 1024);
    return set;
}

}

DEFINE_HOOK_SYMBOL("JASBasicWaveBank::getWaveHandle",
    JASWaveHandle*(const JASBasicWaveBank*, u32), VoicesGetWaveHandleHook);
DEFINE_HOOK_SYMBOL("JASSimpleWaveBank::getWaveHandle",
    JASWaveHandle*(const void*, u32), VoicesGetWaveHandleSimpleHook);

DEFINE_HOOK_SYMBOL("JAISeMgr::newSe_", JAISe*(JAISeMgr*, int, u32),
    VoicesNewSeHook);

DEFINE_HOOK_SYMBOL("JASTrack::noteOn", int(JASTrack*, u32, u32, u32),
    VoicesNoteOnHook);

DEFINE_HOOK_SYMBOL("JASTrack::gateOn", int(JASTrack*, u32, u32, f32, u32),
    VoicesGateOnHook);
DEFINE_HOOK_SYMBOL("JASTrack::channelStart",
    JASChannel*(JASTrack*, void*, u32, u32, u32), VoicesChannelStartHook);

void on_new_se(ModContext*, void*, void* retval, void*) {
    JAISe** result = static_cast<JAISe**>(retval);
    if (result == nullptr || *result == nullptr) return;
    VoiceSet* set = s_starter == Starter::None ? nullptr : s_starterSet;
    tag_sound(&(*result)->inner_.track, set);
}

thread_local int t_noteDepth = 0;

VoiceSet* set_of_track(const JASTrack* track) {
    std::lock_guard<std::mutex> lock(s_tagMutex);
    for (int depth = 0; depth < 12 && track != nullptr; ++depth, track = track->mParent) {
        for (const Tag& t : s_tags) {
            if (t.root == track) return t.set;
        }
    }
    return nullptr;
}

HookAction on_note_on_pre(ModContext*, void* args, void*, void*) {
    if (t_noteDepth++ > 0) return HOOK_CONTINUE;
    const JASTrack* track = mods::arg<JASTrack*>(args, 0);
    t_noteSet = track != nullptr ? set_of_track(track) : nullptr;
    return HOOK_CONTINUE;
}

void on_note_on_post(ModContext*, void*, void*, void*) {
    if (t_noteDepth > 0 && --t_noteDepth == 0) t_noteSet = nullptr;
}

void on_get_wave_handle(ModContext*, void* args, void* retval, void*) {
    VoiceSet* active = t_noteSet;
    if (active == nullptr) return;
    JASWaveHandle** result = static_cast<JASWaveHandle**>(retval);

    if (result == nullptr || *result == nullptr) return;
    const u32 waveId = mods::arg<u32>(args, 1);
    for (Voice* voice : active->voices) {
        if (voice->waveId != waveId) continue;

        const JASWaveInfo* theirs = (*result)->getWaveInfo();
        if (theirs == nullptr ||
            static_cast<uint32_t>(theirs->mSampleCount) != voice->entry.gameSampleCount) {
            return;
        }
        *result = voice;
        ++s_swapped;
        return;
    }
}

void voices_init() {
    const ModResult basic = mods::hook::add_post<VoicesGetWaveHandleHook>(on_get_wave_handle);
    const ModResult simple = mods::hook::add_post<VoicesGetWaveHandleSimpleHook>(on_get_wave_handle);
    const ModResult start = mods::hook::add_post<VoicesNewSeHook>(on_new_se);
    const ModResult notePre = mods::hook::add_pre<VoicesNoteOnHook>(on_note_on_pre);
    const ModResult notePost = mods::hook::add_post<VoicesNoteOnHook>(on_note_on_post);
    const ModResult gatePre = mods::hook::add_pre<VoicesGateOnHook>(on_note_on_pre);
    const ModResult gatePost = mods::hook::add_post<VoicesGateOnHook>(on_note_on_post);
    const ModResult chanPre = mods::hook::add_pre<VoicesChannelStartHook>(on_note_on_pre);
    const ModResult chanPost = mods::hook::add_post<VoicesChannelStartHook>(on_note_on_post);
    coop_log::info("coop_mod: [VOICE] hooks: gateOn={}/{} channelStart={}/{}",
        static_cast<int>(gatePre), static_cast<int>(gatePost), static_cast<int>(chanPre),
        static_cast<int>(chanPost));
    coop_log::info("coop_mod: [VOICE] hooks: basic={} simple={} seStart={} noteOn={}/{}",
        static_cast<int>(basic), static_cast<int>(simple), static_cast<int>(start),
        static_cast<int>(notePre), static_cast<int>(notePost));
}

VoiceSet* set_for(const char* skinName) {
    if (skinName == nullptr || skinName[0] == '\0') return nullptr;
    for (VoiceSet* set : s_sets) {
        if (set->skin != skinName) continue;
        return (set->failed || set->voices.empty()) ? nullptr : set;
    }
    s_wanted = skinName;
    return nullptr;
}

void voices_begin(const char* skinName) {
    voices_begin_remote(skinName);
}

void voices_begin_remote(const char* skinName) {
    s_starter = Starter::Remote;
    s_starterSet = set_for(skinName);
}

void voices_end_remote() {
    if (s_starter == Starter::Remote) {
        s_starter = Starter::None;
        s_starterSet = nullptr;
    }
}

void voices_begin_local() {

    if (s_starter == Starter::Remote) return;

    const std::string mine = skins_local_slot(kSkinChoiceVoice);
    s_starter = Starter::Local;
    s_starterSet = set_for(mine.c_str());
}

void voices_end_local() {
    if (s_starter == Starter::Local) {
        s_starter = Starter::None;
        s_starterSet = nullptr;
    }
}

void voices_update() {

    if (!s_wanted.empty()) {
        const std::string wanted = s_wanted;
        s_wanted.clear();
        load_set(wanted);
    }
    const std::string mine = skins_local_slot(kSkinChoiceVoice);
    if (!mine.empty()) {
        bool known = false;
        for (VoiceSet* set : s_sets) known = known || set->skin == mine;
        if (!known) load_set(mine);
    }

    static int s_reported = -1;
    if (s_swapped != s_reported && (s_reported < 0 || s_swapped / 100 != s_reported / 100 ||
                                       (s_reported == 0 && s_swapped > 0))) {
        s_reported = s_swapped;
        coop_log::info("coop_mod: [VOICE] {} sound(s) played in a player's own voice", s_swapped);
    }
}

void voices_on_disconnected() {
    s_starter = Starter::None;
    s_starterSet = nullptr;
}
