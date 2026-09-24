

#include "mod.hpp"
#include "models.hpp"
#include "print.hpp"

#include "d/d_resorce.h"
#include "d/d_com_inf_game.h"
#include "m_Do/m_Do_dvd_thread.h"
#include "m_Do/m_Do_ext.h"
#include "JSystem/JKernel/JKRArchive.h"
#include "JSystem/JKernel/JKRExpHeap.h"
#include "JSystem/JKernel/JKRMemArchive.h"
#include "JSystem/J3DGraphAnimator/J3DModelData.h"

#include <cstring>

namespace {

const int kMaxPrivateArcs = 12;

struct PrivateArc {
    char name[16] = {};
    mDoDvdThd_mountArchive_c* job = nullptr;
    JKRArchive* archive = nullptr;
    int users = 0;
    bool failed = false;
};

PrivateArc s_arcs[kMaxPrivateArcs];

JKRExpHeap* s_arcHeap = nullptr;
bool s_arcHeapTried = false;
const u32 kArcHeapWanted = 40u * 1024u * 1024u;

JKRHeap* arc_heap() {
    if (s_arcHeap != nullptr || s_arcHeapTried) return s_arcHeap;
    s_arcHeapTried = true;
    JKRHeap* root = JKRHeap::getRootHeap();
    if (root == nullptr) return nullptr;
    const u32 rootFree = root->getFreeSize();

    u32 size = kArcHeapWanted;
    if (rootFree < size + 32u * 1024u * 1024u) {
        size = rootFree > 40u * 1024u * 1024u ? rootFree - 32u * 1024u * 1024u : 0;
    }
    if (size < 8u * 1024u * 1024u) {
        coop_log::warn("coop_mod: [ARC] only {} KB free in the root heap - using the game's archive "
                       "heap as before", rootFree / 1024);
        return nullptr;
    }
    s_arcHeap = JKRExpHeap::create(size, root, false);
    if (s_arcHeap != nullptr) {
        s_arcHeap->setName("CoopArcHeap");
        coop_log::info("coop_mod: [ARC] {} MB of our own for other players' archives", size >> 20);
    }
    return s_arcHeap;
}

void unmount(PrivateArc& a) {
    if (a.job != nullptr) {
        if (!a.job->sync()) return;
        a.archive = static_cast<JKRArchive*>(a.job->getArchive());
        a.job->destroy();
        a.job = nullptr;
    }
    if (a.archive != nullptr) {
        a.archive->unmount();
        coop_log::info("coop_mod: [ARC] unmounted our copy of '{}'", a.name);
    }
    a = PrivateArc{};
}

PrivateArc* free_slot() {
    for (PrivateArc& a : s_arcs) {
        if (a.name[0] == '\0') return &a;
    }
    for (PrivateArc& a : s_arcs) {
        if (a.users == 0 && a.job == nullptr) {
            unmount(a);
            return &a;
        }
    }
    return nullptr;
}

void make_room() {
    JKRHeap* heap = arc_heap();
    if (heap == nullptr) return;
    for (PrivateArc& a : s_arcs) {
        if (heap->getFreeSize() > 4u * 1024u * 1024u) return;
        if (a.name[0] != '\0' && a.users == 0 && a.job == nullptr) unmount(a);
    }
}

PrivateArc* find_arc(const char* name) {
    for (PrivateArc& a : s_arcs) {
        if (a.name[0] != '\0' && std::strcmp(a.name, name) == 0) return &a;
    }
    return nullptr;
}

u32 node_type_for_index(JKRArchive* archive, u32 index) {
    for (int i = 0; i < archive->countDirectory(); ++i) {
        const JKRArchive::SDIDirEntry& dir = archive->mNodes[i];
        if (index >= dir.first_file_index &&
            index < dir.first_file_index + dir.num_entries) {
            return dir.type;
        }
    }
    return 0;
}

}

namespace {
J3DModelData* private_arc_build(JKRArchive* archive, const char* name, u32 index, void* raw);
}

bool private_arc_request(const char* name) {
    if (name == nullptr || name[0] == '\0') return false;
    if (PrivateArc* have = find_arc(name)) {
        ++have->users;
        return !have->failed;
    }
    make_room();
    PrivateArc* slot = free_slot();
    if (slot == nullptr) {
        coop_log::warn("coop_mod: [ARC] no free slot for a private mount of '{}'", name);
        return false;
    }
    PrivateArc& a = *slot;
    std::strncpy(a.name, name, sizeof(a.name) - 1);
    a.name[sizeof(a.name) - 1] = '\0';
    a.users = 1;
    char path[64];
    std::snprintf(path, sizeof(path), "/res/Object/%s.arc", a.name);

    a.job = mDoDvdThd_mountArchive_c::create(path, mDoDvd_MOUNT_DIRECTION_HEAD, arc_heap());
    if (a.job == nullptr) {
        a.failed = true;
        coop_log::warn("coop_mod: [ARC] could not start a private mount of '{}'", a.name);
        return false;
    }
    coop_log::info("coop_mod: [ARC] mounting our own copy of '{}'", a.name);
    return true;
}

int private_arc_poll(const char* name) {
    PrivateArc* a = find_arc(name);
    if (a == nullptr) return -1;
    if (a->failed) return -1;
    if (a->job != nullptr) {

        if (!a->job->sync()) return 0;
        a->archive = static_cast<JKRArchive*>(a->job->getArchive());
        a->job->destroy();
        a->job = nullptr;
        if (a->archive == nullptr) {
            a->failed = true;
            coop_log::warn("coop_mod: [ARC] private mount of '{}' produced no archive", a->name);
            return -1;
        }
        coop_log::info("coop_mod: [ARC] '{}' is ours now, {} files", a->name,
            static_cast<int>(a->archive->countFile()));
    }
    return a->archive != nullptr ? 1 : -1;
}

void private_arc_release(const char* name) {
    PrivateArc* a = find_arc(name);
    if (a == nullptr) return;
    if (a->users > 0) --a->users;
    if (a->users > 0) return;

    if (a->failed && a->job == nullptr) *a = PrivateArc{};
}

J3DModelData* private_arc_load_idx(const char* name, u32 index) {
    PrivateArc* a = find_arc(name);
    if (a == nullptr || a->archive == nullptr) return nullptr;
    JKRArchive* archive = a->archive;
    void* raw = archive->getIdxResource(index);
    if (raw == nullptr) return nullptr;
    return private_arc_build(archive, name, index, raw);
}

const char* private_arc_file_name(const char* name, u32 index) {
    PrivateArc* a = find_arc(name);
    if (a == nullptr || a->archive == nullptr) return nullptr;
    JKRArchive* archive = a->archive;
    if (index >= archive->countFile() || archive->mStringTable == nullptr) return nullptr;
    return archive->mStringTable + archive->mFiles[index].getNameOffset();
}

J3DModelData* private_arc_load(const char* name, const char* file) {
    PrivateArc* a = find_arc(name);
    if (a == nullptr || a->archive == nullptr || file == nullptr) return nullptr;
    JKRArchive* archive = a->archive;
    JKRArchive::SDIFileEntry* entry = archive->findNameResource(file);
    if (entry == nullptr) {
        coop_log::warn("coop_mod: [ARC] '{}' has no '{}'", name, file);
        return nullptr;
    }
    const u32 index = static_cast<u32>(entry - archive->mFiles);
    void* raw = archive->getIdxResource(index);
    if (raw == nullptr) return nullptr;
    return private_arc_build(archive, name, index, raw);
}

namespace {
J3DModelData* private_arc_build(JKRArchive* archive, const char* name, u32 index, void* raw) {
    const u32 size = archive->getExpandedResSize(raw);
    if (size < 32 || size > 16u * 1024u * 1024u) {
        coop_log::warn("coop_mod: [ARC] '{}' idx {} has an implausible size {}", name, index, size);
        return nullptr;
    }
    const u32 type = node_type_for_index(archive, index);
    if (type != 'BMDR' && type != 'BMWR' && type != 'BMDE' && type != 'BMWE' && type != 'BMDV') {
        coop_log::warn("coop_mod: [ARC] '{}' idx {} is not a model node ({:#x})", name, index, type);
        return nullptr;
    }

    JKRHeap* heap = arc_heap();
    JKRHeap* previous = heap != nullptr ? heap->becomeCurrentHeap() : nullptr;
    struct Restore {
        JKRHeap* heap;
        ~Restore() {
            if (heap != nullptr) heap->becomeCurrentHeap();
        }
    } restore{previous};
    u8* copy = JKR_NEW_ARRAY_ARGS(u8, size, 32);
    if (copy == nullptr) return nullptr;
    std::memcpy(copy, raw, size);

    J3DModelData* data = dRes_info_c::loaderBasicBmd(type, copy);
    if (data == nullptr) {
        JKR_DELETE_ARRAY(copy);
        return nullptr;
    }
    if (type == 'BMWR' || type == 'BMWE') {

        dRes_info_c::offWarpMaterial(data);
        data->simpleCalcMaterial(const_cast<MtxP>(j3dDefaultMtx));
        data->makeSharedDL();
    }
    return data;
}
}

void private_arc_free_data(J3DModelData* data) {
    if (data == nullptr) return;

    JKR_DELETE_ARRAY(reinterpret_cast<u8*>(data));
}
