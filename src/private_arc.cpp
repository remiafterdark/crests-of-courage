

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

#include <algorithm>
#include <chrono>
#include "JSystem/JKernel/JKRSolidHeap.h"
#include "dolphin/gx/GXExtra.h"
#include "helpers/gx_helper.h"
#include "JSystem/J3DGraphAnimator/J3DModelData.h"
#include "JSystem/J3DGraphLoader/J3DAnmLoader.h"

#include <cstring>

namespace {

const int kMaxPrivateArcs = 12;

struct PrivateArc {
    char name[16] = {};
    mDoDvdThd_mountArchive_c* job = nullptr;
    JKRArchive* archive = nullptr;
    int users = 0;
    bool failed = false;
    std::chrono::steady_clock::time_point failedAt{};
};

const auto kArcRetryAfter = std::chrono::seconds(3);

void mark_failed(PrivateArc& a) {
    a.failed = true;
    a.failedAt = std::chrono::steady_clock::now();
}

PrivateArc s_arcs[kMaxPrivateArcs];

JKRExpHeap* s_arcHeap = nullptr;
const u32 kArcHeapWanted = 40u * 1024u * 1024u;
const u32 kArcHeapMin = 8u * 1024u * 1024u;

std::chrono::steady_clock::time_point s_arcHeapNextTry{};
bool s_arcHeapLowLogged = false;

JKRExpHeap* s_modelHeap = nullptr;
const u32 kModelHeapWanted = 16u * 1024u * 1024u;
const u32 kModelHeapMin = 4u * 1024u * 1024u;

void create_model_heap(JKRHeap* parent, u32 reserve) {
    if (s_modelHeap != nullptr || s_arcHeap == nullptr) return;
    const u32 parentFree = parent != nullptr ? parent->getFreeSize() : 0;
    u32 size = parentFree > reserve ? std::min(kModelHeapWanted, parentFree - reserve) : 0;
    const char* from = "beside";
    if (size >= kModelHeapMin) {
        s_modelHeap = JKRExpHeap::create(size, parent, false);
    }
    if (s_modelHeap == nullptr) {
        size = s_arcHeap->getFreeSize() * 3u / 8u;
        from = "inside";
        if (size >= kModelHeapMin) s_modelHeap = JKRExpHeap::create(size, s_arcHeap, false);
    }
    if (s_modelHeap == nullptr) {
        coop_log::warn("coop_mod: [ARC] no model heap ({} KB free beside, {} KB inside)", parentFree / 1024,
            s_arcHeap->getFreeSize() / 1024);
        return;
    }
    s_modelHeap->setName("CoopModelHeap");
    coop_log::info("coop_mod: [ARC] {} MB model heap {} the archive heap", size >> 20, from);
}

JKRHeap* arc_heap() {
    if (s_arcHeap != nullptr) return s_arcHeap;
    const auto now = std::chrono::steady_clock::now();
    if (now < s_arcHeapNextTry) return nullptr;
    s_arcHeapNextTry = now + std::chrono::seconds(5);
    JKRHeap* root = JKRHeap::getRootHeap();
    if (root == nullptr) return nullptr;
    const u32 rootFree = root->getFreeSize();

    u32 size = 0;
    for (u32 reserve : {32u * 1024u * 1024u, 16u * 1024u * 1024u}) {
        if (rootFree <= reserve) continue;
        size = std::min(kArcHeapWanted, rootFree - reserve);
        if (size >= kArcHeapMin) break;
        size = 0;
    }

    JKRHeap* parent = root;
    if (size < kArcHeapMin) {
        JKRHeap* game = mDoExt_getGameHeap();
        const u32 gameFree = game != nullptr ? game->getFreeSize() : 0;
        const u32 kGameReserve = 40u * 1024u * 1024u;
        if (gameFree > kGameReserve + kArcHeapMin) {
            size = std::min(24u * 1024u * 1024u, gameFree - kGameReserve);
            parent = game;
        }
    }
    if (size < kArcHeapMin) {
        if (!s_arcHeapLowLogged) {
            s_arcHeapLowLogged = true;
            coop_log::warn("coop_mod: [ARC] root heap low ({} KB), using game archive heap", rootFree / 1024);
        }
        return nullptr;
    }
    s_arcHeap = JKRExpHeap::create(size, parent, false);
    if (s_arcHeap != nullptr) {
        s_arcHeap->setName("CoopArcHeap");
        coop_log::info("coop_mod: [ARC] {} MB reserved for peer archives from {} heap ({} KB free in root)", size >> 20,
            parent == root ? "root" : "game", rootFree / 1024);

        create_model_heap(parent, parent == root ? 32u * 1024u * 1024u : 40u * 1024u * 1024u);
    }
    return s_arcHeap;
}

bool mount_in_flight() {
    for (PrivateArc& a : s_arcs) {
        if (a.job != nullptr && !a.job->sync()) return true;
    }
    return false;
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
        coop_log::info("coop_mod: [ARC] unmounted private '{}'", a.name);
    }
    a = PrivateArc{};
}

PrivateArc* free_slot() {
    for (PrivateArc& a : s_arcs) {
        if (a.name[0] == '\0') return &a;
    }
    if (mount_in_flight()) return nullptr;
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
    if (heap == nullptr || mount_in_flight()) return;
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

JKRHeap* private_arc_heap_peek() {
    return s_arcHeap;
}

JKRHeap* private_arc_heap() {
    return arc_heap();
}

JKRHeap* private_model_heap() {
    arc_heap();
    return s_modelHeap;
}

JKRHeap* private_model_heap_peek() {
    return s_modelHeap;
}

bool private_arc_request(const char* name) {
    if (name == nullptr || name[0] == '\0') return false;
    int keepUsers = 0;
    PrivateArc* slot = find_arc(name);
    if (slot != nullptr) {
        if (!slot->failed) {
            ++slot->users;
            return true;
        }

        if (slot->job != nullptr ||
            std::chrono::steady_clock::now() - slot->failedAt < kArcRetryAfter) {
            return false;
        }

        keepUsers = slot->users;
        make_room();
    } else {
        make_room();
        slot = free_slot();
    }
    if (slot == nullptr) {
        if (mount_in_flight()) return false;
        coop_log::warn("coop_mod: [ARC] no free slot for a private mount of '{}'", name);
        return false;
    }

    if (JKRHeap* heap = arc_heap(); heap != nullptr && heap->getFreeSize() < 4u * 1024u * 1024u) {
        static int s_mapFullLogged = 0;
        if (s_mapFullLogged++ % 60 == 0) {
            coop_log::warn("coop_mod: [ARC] mount '{}' deferred, {} KB left", name,
                heap->getFreeSize() / 1024);
        }
        if (slot != nullptr && slot->failed) slot->failedAt = std::chrono::steady_clock::now();
        return false;
    }
    PrivateArc& a = *slot;
    a = PrivateArc{};
    std::strncpy(a.name, name, sizeof(a.name) - 1);
    a.name[sizeof(a.name) - 1] = '\0';
    a.users = keepUsers + 1;
    char path[64];
    std::snprintf(path, sizeof(path), "/res/Object/%s.arc", a.name);

    a.job = mDoDvdThd_mountArchive_c::create(path, mDoDvd_MOUNT_DIRECTION_HEAD, arc_heap());
    if (a.job == nullptr) {
        mark_failed(a);
        a.users = keepUsers;
        coop_log::warn("coop_mod: [ARC] could not start a private mount of '{}'", a.name);
        return false;
    }
    coop_log::info("coop_mod: [ARC] mounting private '{}'", a.name);
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
            mark_failed(*a);
            coop_log::warn("coop_mod: [ARC] private mount of '{}' produced no archive", a->name);
            return -1;
        }
        coop_log::info("coop_mod: [ARC] '{}' private, {} files", a->name,
            static_cast<int>(a->archive->countFile()));
    }
    return a->archive != nullptr ? 1 : -1;
}

void private_arc_release(const char* name) {
    PrivateArc* a = find_arc(name);
    if (a == nullptr) return;
    if (a->users > 0) --a->users;
    if (a->users > 0) return;

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

void* private_arc_load_anm_idx(const char* name, uint32_t index) {
    PrivateArc* a = find_arc(name);
    if (a == nullptr || a->archive == nullptr) return nullptr;
    JKRArchive* archive = a->archive;
    if (index >= archive->countFile() || !archive->isFileEntry(index)) return nullptr;
    void* raw = archive->getIdxResource(index);
    if (raw == nullptr) return nullptr;
    const u32 size = archive->getExpandedResSize(raw);
    JKRHeap* heap = arc_heap();
    if (size < 32 || size > 1024u * 1024u || heap == nullptr ||
        heap->getFreeSize() < size * 2u + 2u * 1024u * 1024u) {
        return nullptr;
    }
    JKRHeap* previous = heap->becomeCurrentHeap();
    u8* copy = JKR_NEW_ARRAY_ARGS(u8, size, 32);
    void* anm = nullptr;
    if (copy != nullptr) {
        std::memcpy(copy, raw, size);
        anm = J3DAnmLoaderDataBase::load(copy);
    }
    if (previous != nullptr) previous->becomeCurrentHeap();
    return anm;
}

void* private_arc_load_anm(const char* name, const char* file) {
    PrivateArc* a = find_arc(name);
    if (a == nullptr || a->archive == nullptr || file == nullptr) return nullptr;
    JKRArchive* archive = a->archive;
    JKRArchive::SDIFileEntry* entry = archive->findNameResource(file);
    if (entry == nullptr) {
        coop_log::warn("coop_mod: [ARC] '{}' has no '{}'", name, file);
        return nullptr;
    }
    void* raw = archive->getIdxResource(static_cast<u32>(entry - archive->mFiles));
    if (raw == nullptr) return nullptr;
    const u32 size = archive->getExpandedResSize(raw);
    JKRHeap* heap = arc_heap();
    if (size < 32 || size > 1024u * 1024u || heap == nullptr ||
        heap->getFreeSize() < size * 2u + 2u * 1024u * 1024u) {
        return nullptr;
    }
    JKRHeap* previous = heap->becomeCurrentHeap();
    u8* copy = JKR_NEW_ARRAY_ARGS(u8, size, 32);
    void* anm = nullptr;
    if (copy != nullptr) {
        std::memcpy(copy, raw, size);
        anm = J3DAnmLoaderDataBase::load(copy);
    }
    if (previous != nullptr) previous->becomeCurrentHeap();
    return anm;
}

namespace {

struct PartHeap {
    J3DModelData* data = nullptr;
    JKRSolidHeap* heap = nullptr;
    int donor = -1;
    bool isDonor = false;
};

struct TexDonor {
    char key[48] = {};
    J3DModelData* data = nullptr;
    int borrowers = 0;
    bool freeWhenUnused = false;
};
const int kTexDonorMax = 128;
TexDonor s_texDonors[kTexDonorMax];

struct J3DTextureFields {
    void* vtable;
    u16 num;
    u16 unk;
    ResTIMG* res;
    void* tlutObj;
    TGXTexObj* texObj;
    u8** imgData;
    u8** tlutData;
};
static_assert(sizeof(J3DTextureFields) == sizeof(J3DTexture), "J3DTexture layout changed");

bool same_textures(J3DModelData* a, J3DModelData* b) {
    J3DTexture* ta = a->getTexture();
    J3DTexture* tb = b->getTexture();
    if (ta == nullptr || tb == nullptr || ta->getNum() != tb->getNum()) return false;
    for (u16 i = 0; i < ta->getNum(); ++i) {
        const ResTIMG* ra = ta->getResTIMG(i);
        const ResTIMG* rb = tb->getResTIMG(i);
        if (ra->format != rb->format || ra->width != rb->width || ra->height != rb->height ||
            ra->indexTexture != rb->indexTexture || ra->mipmapCount != rb->mipmapCount) {
            return false;
        }
    }
    return true;
}

int share_textures(const char* key, J3DModelData* data, bool* lends) {
    *lends = false;
    if (data == nullptr || data->getTexture() == nullptr || data->getTexture()->getNum() == 0) return -1;
    int freeSlot = -1;
    for (int i = 0; i < kTexDonorMax; ++i) {
        TexDonor& d = s_texDonors[i];
        if (d.data == nullptr) {
            if (freeSlot < 0) freeSlot = i;
            continue;
        }
        if (d.freeWhenUnused || std::strcmp(d.key, key) != 0) continue;
        if (!same_textures(d.data, data)) return -1;
        auto* from = reinterpret_cast<J3DTextureFields*>(d.data->getTexture());
        auto* to = reinterpret_cast<J3DTextureFields*>(data->getTexture());
        for (u16 t = 0; t < to->num; ++t) {
            if (to->res[t].indexTexture) continue;

            GXDestroyTexObj(&to->texObj[t]);
            std::memcpy(static_cast<void*>(&to->texObj[t]), static_cast<const void*>(&from->texObj[t]),
                sizeof(TGXTexObj));
            to->imgData[t] = from->imgData[t];
        }
        ++d.borrowers;
        return i;
    }
    if (freeSlot < 0) return -1;
    TexDonor& d = s_texDonors[freeSlot];
    d = TexDonor{};
    std::strncpy(d.key, key, sizeof(d.key) - 1);
    d.data = data;
    *lends = true;
    return freeSlot;
}

void destroy_part(PartHeap& p) {
    if (p.data != nullptr && p.data->getTexture() != nullptr) {
        auto* tex = reinterpret_cast<J3DTextureFields*>(p.data->getTexture());
        for (u16 t = 0; t < tex->num; ++t) {
            const bool borrowed = p.donor >= 0 && !tex->res[t].indexTexture;
            if (!borrowed) GXDestroyTexObj(&tex->texObj[t]);
            if (tex->res[t].indexTexture) GXDestroyTlutObj(reinterpret_cast<GXTlutObj*>(tex->tlutObj) + t);
        }
    }
    p.heap->destroy();
    p = PartHeap{};
}
const int kPartHeapMax = 512;
PartHeap s_partHeaps[kPartHeapMax];

bool remember_part_heap(J3DModelData* data, JKRSolidHeap* heap, int donor, bool lends) {
    for (PartHeap& p : s_partHeaps) {
        if (p.data != nullptr) continue;
        p.data = data;
        p.donor = donor;
        p.isDonor = lends;
        p.heap = heap;
        return true;
    }
    return false;
}

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

    arc_heap();
    JKRHeap* parent = s_modelHeap;
    if (parent == nullptr) return nullptr;

    const u32 want = size * 4u + 1024u * 1024u;
    const u32 kMountReserve = 2u * 1024u * 1024u;
    if (parent->getFreeSize() < want + kMountReserve) {
        static int s_noRoomLogged = 0;
        if (s_noRoomLogged++ < 8) {
            coop_log::warn("coop_mod: [ARC] no room for '{}' idx={} ({} KB wanted, {} KB free), using game part", name, index, want / 1024,
                parent->getFreeSize() / 1024);
        }
        return nullptr;
    }
    JKRSolidHeap* heap = JKRSolidHeap::create(want, parent, false);
    if (heap == nullptr) return nullptr;
    JKRHeap* previous = heap->becomeCurrentHeap();
    u8* copy = JKR_NEW_ARRAY_ARGS(u8, size, 32);
    J3DModelData* data = nullptr;
    if (copy != nullptr) {
        std::memcpy(copy, raw, size);
        data = dRes_info_c::loaderBasicBmd(type, copy);
    }
    if (data == nullptr) {
        if (previous != nullptr) previous->becomeCurrentHeap();
        heap->destroy();
        return nullptr;
    }
    char key[48];
    std::snprintf(key, sizeof(key), "%s#%u", name, static_cast<unsigned>(index));
    bool lends = false;
    const int donor = share_textures(key, data, &lends);
    if (type == 'BMWR' || type == 'BMWE') {

        dRes_info_c::offWarpMaterial(data);
        data->simpleCalcMaterial(const_cast<MtxP>(j3dDefaultMtx));
        data->makeSharedDL();
    }

    if (previous != nullptr) previous->becomeCurrentHeap();
    heap->adjustSize();
    if (!remember_part_heap(data, heap, lends ? -1 : donor, lends)) {

        coop_log::warn("coop_mod: [ARC] {} parts loading, deferred '{}' idx={}",
            kPartHeapMax, name, index);
        if (donor >= 0) {
            if (lends) s_texDonors[donor] = TexDonor{};
            else --s_texDonors[donor].borrowers;
        }
        PartHeap gone;
        gone.data = data;
        gone.heap = heap;
        gone.donor = lends ? -1 : donor;
        destroy_part(gone);
        return nullptr;
    }
    return data;
}
}

void private_arc_free_data(J3DModelData* data) {
    if (data == nullptr) return;
    for (PartHeap& p : s_partHeaps) {
        if (p.data != data) continue;

        if (p.isDonor) {
            for (TexDonor& d : s_texDonors) {
                if (d.data != data) continue;
                if (d.borrowers > 0) {
                    d.freeWhenUnused = true;
                    return;
                }
                d = TexDonor{};
            }
        }
        const int donor = p.donor;

        destroy_part(p);
        if (donor >= 0 && donor < kTexDonorMax && s_texDonors[donor].data != nullptr) {
            TexDonor& d = s_texDonors[donor];
            if (--d.borrowers <= 0 && d.freeWhenUnused) {
                J3DModelData* lender = d.data;
                d = TexDonor{};
                for (PartHeap& q : s_partHeaps) {
                    if (q.data == lender) {
                        destroy_part(q);
                        break;
                    }
                }
            }
        }
        return;
    }

    JKR_DELETE_ARRAY(reinterpret_cast<u8*>(data));
}
