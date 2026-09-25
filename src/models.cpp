#include "util.hpp"
#include "models.hpp"
#include "print.hpp"
#include "d/d_com_inf_game.h"
#include "d/actor/d_a_alink.h"
#include "m_Do/m_Do_ext.h"
#include "JSystem/JKernel/JKRSolidHeap.h"
#include "m_Do/m_Do_mtx.h"
#include "d/d_kankyo.h"
#include "JSystem/JKernel/JKRExpHeap.h"
#include "JSystem/J3DGraphBase/J3DPacket.h"

#include "JSystem/J3DGraphAnimator/J3DMaterialAnm.h"
#include "JSystem/J3DGraphLoader/J3DModelLoader.h"

#include "d/d_resorce.h"
#include "mods/svc/hook.hpp"

#include <cstdio>
#include <string>
#include <fstream>
#include <cstring>
#include <unordered_set>

static const u32 kCoopDifferedDlistFlags = 0x11000284u | J3DDiffFlag_KonstColor | J3DDiffFlag_TexGen;

static const int kMaxClaimedArcs = 64;
static char s_claimedArcNames[kMaxClaimedArcs][64];
static int s_claimedArcCount = 0;

static bool haveClaimedRef(const char* name) {
    for (int i = 0; i < s_claimedArcCount; ++i) {
        if (std::strcmp(s_claimedArcNames[i], name) == 0) return true;
    }
    return false;
}

static bool claimTableFull() {
    return s_claimedArcCount >= kMaxClaimedArcs;
}

static void markClaimedRef(const char* name) {
    if (claimTableFull()) return;
    std::strncpy(s_claimedArcNames[s_claimedArcCount], name, 63);
    s_claimedArcNames[s_claimedArcCount][63] = '\0';
    ++s_claimedArcCount;
}

static void clearClaimedRef(const char* name) {
    for (int i = 0; i < s_claimedArcCount; ++i) {
        if (std::strcmp(s_claimedArcNames[i], name) == 0) {
            s_claimedArcNames[i][0] = '\0';
            std::memmove(&s_claimedArcNames[i], &s_claimedArcNames[i + 1],
                sizeof(s_claimedArcNames[0]) * (s_claimedArcCount - i - 1));
            --s_claimedArcCount;
            return;
        }
    }
}

static void normalizeArcName(const char* src, char* dst, size_t dstSize) {
    if (src == nullptr || dst == nullptr || dstSize == 0) return;
    std::strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
    char* dot = std::strstr(dst, ".arc");
    if (dot != nullptr) {
        *dot = '\0';
    }
}

int loadObjectArchive(const char* arcName) {
    if (arcName == nullptr) return -1;

    char cleanName[64];
    normalizeArcName(arcName, cleanName, sizeof(cleanName));

    dRes_info_c* info = dComIfG_getObjectResInfo(cleanName);
    const bool alreadyMounted = info != nullptr && info->getArchive() != nullptr;

    ensure_system_heap_capacity();

    if (alreadyMounted && !haveClaimedRef(cleanName)) {
        daAlink_c* player = daAlink_getAlinkActorClass();
        if (player != nullptr &&
            ((player->mArcName != nullptr && std::strcmp(player->mArcName, cleanName) == 0) ||
                (player->mShieldArcName != nullptr &&
                    std::strcmp(player->mShieldArcName, cleanName) == 0))) {
            return 0;
        }
    }

    if (!haveClaimedRef(cleanName)) {

        if (claimTableFull()) {
            coop_log::warn("coop_mod: [models] REFUSING to mount '{}' - {} archives already claimed",
                cleanName, s_claimedArcCount);
            return -1;
        }
        ensure_system_heap_capacity();
        JKRHeap* targetHeap = nullptr;
        JKRExpHeap* arcHeap = mDoExt_getArchiveHeap();
        JKRExpHeap* zeldaHeap = mDoExt_getZeldaHeap();
        JKRHeap* rootHeap = JKRHeap::getRootHeap();
        if (rootHeap != nullptr && rootHeap->getFreeSize() > 10000000) {
            targetHeap = rootHeap;
        } else if (arcHeap != nullptr && arcHeap->getFreeSize() > 6000000) {
            targetHeap = arcHeap;
        } else if (zeldaHeap != nullptr && zeldaHeap->getFreeSize() > 6000000) {
            targetHeap = zeldaHeap;
        }
        dComIfG_setObjectRes(cleanName, 0, targetHeap);
        markClaimedRef(cleanName);
        dRes_info_c* after = dComIfG_getObjectResInfo(cleanName);
        coop_log::info("coop_mod: [models] claimed '{}' ({}, count now {})", cleanName,
            alreadyMounted ? "shared an existing mount" : "our own mount",
            after != nullptr ? after->getCount() : -1);
    }

    if (alreadyMounted) {
        return 0;
    }

    const int sync = dComIfG_syncObjectRes(cleanName);
    if (sync == 0) {
        return 0;
    }
    if (sync < 0) {
        coop_log::warn("coop_mod: [models] FAILED to mount archive '{}' (syncObjectRes={})", cleanName, sync);
        return -1;
    }

    return 1;
}

void unloadObjectArchive(const char* arcName) {
    if (arcName == nullptr) return;
    char cleanName[64];
    normalizeArcName(arcName, cleanName, sizeof(cleanName));

    if (haveClaimedRef(cleanName)) {

        dRes_info_c* before = dComIfG_getObjectResInfo(cleanName);
        const int countBefore = before != nullptr ? before->getCount() : -1;
        dComIfG_deleteObjectResMain(cleanName);
        clearClaimedRef(cleanName);
        coop_log::info("coop_mod: [models] released '{}' (count was {})", cleanName, countBefore);
    }
}

J3DModel* loadBmdFromArc(const char* arcName, const char* bmdName, cXyz scale) {
    if (arcName == nullptr || bmdName == nullptr) return nullptr;

    char cleanArc[64];
    normalizeArcName(arcName, cleanArc, sizeof(cleanArc));

    if (loadObjectArchive(cleanArc) != 0) {
        return nullptr;
    }

    char cleanBmd[64];
    std::strncpy(cleanBmd, bmdName, sizeof(cleanBmd) - 1);
    cleanBmd[sizeof(cleanBmd) - 1] = '\0';
    char* dot = std::strstr(cleanBmd, ".bmd");
    if (dot != nullptr) {
        *dot = '\0';
    }

    void* res = nullptr;
    dRes_info_c* info = dComIfG_getObjectResInfo(cleanArc);
    JKRArchive* archive = (info != nullptr) ? info->getArchive() : nullptr;
    const char* foundVia = nullptr;
    if (archive != nullptr) {
        res = archive->getResource('BMD ', bmdName);
        if (res != nullptr) foundVia = "type-tagged (full name)";
        if (res == nullptr) {
            res = archive->getResource('BMD ', cleanBmd);
            if (res != nullptr) foundVia = "type-tagged (stripped name)";
        }
    }

    if (res == nullptr) {
        res = dComIfG_getObjectRes(cleanArc, bmdName);
        if (res != nullptr) foundVia = "untyped (full name)";
    }
    if (res == nullptr) {
        res = dComIfG_getObjectRes(cleanArc, cleanBmd);
        if (res != nullptr) foundVia = "untyped (stripped name)";
    }
    if (res == nullptr && std::strcmp(cleanArc, "E_fm") == 0 && std::strcmp(cleanBmd, "fm_core") == 0) {
        res = dComIfG_getObjectRes("E_fm", 0x27);
        if (res != nullptr) foundVia = "id (BMDE_FM_CORE 0x27)";
    }
    if (res == nullptr && std::strcmp(cleanArc, "E_th_ball") == 0) {
        if (std::strcmp(cleanBmd, "ib") == 0) {
            res = dComIfG_getObjectRes("E_th_ball", 4);
            if (res != nullptr) foundVia = "id (ib 4)";
        } else if (std::strcmp(cleanBmd, "tc") == 0) {
            res = dComIfG_getObjectRes("E_th_ball", 7);
            if (res != nullptr) foundVia = "id (tc 7)";
        }
    }
    if (res == nullptr && std::strcmp(cleanArc, "B_yo") == 0 && std::strcmp(cleanBmd, "yo_ice") == 0) {
        res = dComIfG_getObjectRes("B_yo", 0x21);
        if (res != nullptr) foundVia = "id (BMDE_YO_ICE 0x21)";
    }

    if (res == nullptr) {
        coop_log::warn("coop_mod: [models] '{}' not found in '{}'", bmdName, cleanArc);
        return nullptr;
    }
    (void)foundVia;

    J3DModelData* modelData = static_cast<J3DModelData*>(res);
    if (modelData->getMaterialNum() == 0 || modelData->getShapeTable() == nullptr ||
        modelData->getShapeTable()->getShapeNum() == 0 ||
        modelData->getMaterialNodePointer(0) == nullptr) {
        coop_log::warn("coop_mod: [models] '{}' failed material/shape sanity check", bmdName);
        return nullptr;
    }

    J3DModel* model = coop_create_model(modelData, 0x80000, kCoopDifferedDlistFlags);
    if (model != nullptr) {
        model->setBaseScale(scale);
    } else {
        coop_log::warn("coop_mod: [models] '{}': mDoExt_J3DModel__create returned null", bmdName);
    }
    return model;
}

#include "JSystem/J3DGraphLoader/J3DAnmLoader.h"

std::unordered_set<J3DModelData*> s_looseData;

DEFINE_HOOK(&dRes_info_c::onWarpMaterial, WarpOnHook);
DEFINE_HOOK(&dRes_info_c::offWarpMaterial, WarpOffHook);

DEFINE_HOOK_SYMBOL("dRes_info_c::setWarpSRT", void(J3DModelData*, cXyz&, f32, f32), WarpSrtHook);

HookAction skip_if_ours(ModContext*, void* args, void*, void*) {
    return s_looseData.count(mods::arg<J3DModelData*>(args, 0)) != 0 ? HOOK_SKIP_ORIGINAL
                                                                     : HOOK_CONTINUE;
}

void models_warp_guard_init() {
    const bool on = mods::hook::add_pre<WarpOnHook>(skip_if_ours) == MOD_OK;
    const bool off = mods::hook::add_pre<WarpOffHook>(skip_if_ours) == MOD_OK;
    const bool srt = mods::hook::add_pre<WarpSrtHook>(skip_if_ours) == MOD_OK;
    coop_log::info("coop_mod: [models] warp guard {}", on && off && srt ? "attached" : "FAILED - "
        "warping in a custom model can crash");
}

J3DModelData* loadBmdDataFromFile(const char* path) {
    if (path == nullptr) return nullptr;

    std::ifstream file(path_ci(path), std::ios::binary | std::ios::ate);
    if (!file) {
        coop_log::warn("coop_mod: [models] cannot open '{}'", path);
        return nullptr;
    }
    const std::streamoff size = file.tellg();
    if (size <= 64 || size > 64 * 1024 * 1024) {
        coop_log::warn("coop_mod: [models] '{}' is not a sensible size ({} bytes)", path,
            static_cast<long long>(size));
        return nullptr;
    }
    file.seekg(0);

    ensure_system_heap_capacity();
    JKRHeap* heap = JKRHeap::getRootHeap();
    if (heap == nullptr) heap = JKRHeap::getSystemHeap();
    if (heap == nullptr) return nullptr;
    void* buffer = heap->alloc(static_cast<u32>(size), 32);
    if (buffer == nullptr) {
        coop_log::warn("coop_mod: [models] no room for '{}' ({} bytes)", path,
            static_cast<long long>(size));
        return nullptr;
    }
    if (!file.read(static_cast<char*>(buffer), size)) {
        coop_log::warn("coop_mod: [models] short read on '{}'", path);
        return nullptr;
    }

    if (std::memcmp(buffer, "J3D2", 4) != 0) {
        coop_log::warn("coop_mod: [models] '{}' is not a J3D model file", path);
        return nullptr;
    }

    const u32 kBmwrLoadFlags = 0x59040030;

    struct CurrentHeapFor {
        explicit CurrentHeapFor(JKRHeap* heap) : mPrevious(heap->becomeCurrentHeap()) {}
        ~CurrentHeapFor() {
            if (mPrevious != nullptr) mPrevious->becomeCurrentHeap();
        }
        JKRHeap* mPrevious;
    } heapScope(heap);
    J3DModelData* data = J3DModelLoaderDataBase::load(buffer, kBmwrLoadFlags);
    if (data == nullptr || data->getMaterialNum() == 0 || data->getShapeTable() == nullptr ||
        data->getShapeTable()->getShapeNum() == 0 || data->getMaterialNodePointer(0) == nullptr) {
        coop_log::warn("coop_mod: [models] '{}' loaded but has no usable materials or shapes", path);
        return nullptr;
    }

    for (u16 i = 0; i < data->getMaterialNum(); ++i) {
        J3DMaterial* material = data->getMaterialNodePointer(i);
        if (material == nullptr) continue;
        material->change();
        J3DMaterialAnm* anm = new J3DMaterialAnm();
        if (anm == nullptr) return nullptr;
        material->setMaterialAnm(anm);
    }
    if (data->newSharedDisplayList(J3DMdlFlag_UseSingleDL) != kJ3DError_Success) {
        coop_log::warn("coop_mod: [models] '{}': no room for its display list", path);
        return nullptr;
    }
    data->simpleCalcMaterial(const_cast<MtxP>(j3dDefaultMtx));
    data->makeSharedDL();

    coop_log::info("coop_mod: [models] loaded '{}' ({} joints, {} materials)", path,
        data->getJointNum(), data->getMaterialNum());
    s_looseData.insert(data);
    return data;
}

J3DModelData* loadBmdDataForLink(const char* path) {
    if (path == nullptr) return nullptr;
    std::ifstream file(path_ci(path), std::ios::binary | std::ios::ate);
    if (!file) return nullptr;
    const std::streamoff size = file.tellg();
    if (size <= 64 || size > 64 * 1024 * 1024) return nullptr;
    file.seekg(0);
    ensure_system_heap_capacity();
    JKRHeap* heap = JKRHeap::getRootHeap();
    if (heap == nullptr) heap = JKRHeap::getSystemHeap();
    if (heap == nullptr) return nullptr;
    void* buffer = heap->alloc(static_cast<u32>(size), 32);
    if (buffer == nullptr) return nullptr;
    if (!file.read(static_cast<char*>(buffer), size) || std::memcmp(buffer, "J3D2", 4) != 0) {
        return nullptr;
    }
    JKRHeap* previous = heap->becomeCurrentHeap();
    J3DModelData* data = dRes_info_c::loaderBasicBmd('BMWR', buffer);
    if (previous != nullptr) previous->becomeCurrentHeap();
    if (data == nullptr || data->getMaterialNum() == 0 || data->getMaterialNodePointer(0) == nullptr) {
        coop_log::warn("coop_mod: [models] '{}' did not load as Link's own - using the plain copy",
            path);
        return nullptr;
    }
    coop_log::info("coop_mod: [models] loaded '{}' for you, with the warp material", path);
    return data;
}

J3DModel* modelFromData(J3DModelData* data, cXyz scale) {
    if (data == nullptr) return nullptr;
    J3DModel* model = coop_create_model(data, 0x80000, kCoopDifferedDlistFlags);
    if (model == nullptr) {
        coop_log::warn("coop_mod: [models] mDoExt_J3DModel__create returned null");
        return nullptr;
    }
    model->setBaseScale(scale);
    return model;
}

J3DModel* loadBmdFromFile(const char* path, cXyz scale) {
    return modelFromData(loadBmdDataFromFile(path), scale);
}

J3DModel* loadBmdFromArcIdx(const char* arcName, int resIndex, cXyz scale) {
    if (arcName == nullptr) return nullptr;

    char cleanArc[64];
    normalizeArcName(arcName, cleanArc, sizeof(cleanArc));

    if (loadObjectArchive(cleanArc) != 0) {
        return nullptr;
    }

    void* res = dComIfG_getObjectRes(cleanArc, resIndex);
    if (res == nullptr) {
        coop_log::warn("coop_mod: [models] index {} not found in '{}'", resIndex, cleanArc);
        return nullptr;
    }

    J3DModelData* modelData = static_cast<J3DModelData*>(res);
    if (modelData->getMaterialNum() == 0 || modelData->getShapeTable() == nullptr ||
        modelData->getShapeTable()->getShapeNum() == 0 ||
        modelData->getMaterialNodePointer(0) == nullptr) {
        coop_log::warn("coop_mod: [models] index {} in '{}' failed sanity check", resIndex, cleanArc);
        return nullptr;
    }

    J3DModel* model = coop_create_model(modelData, 0x80000, kCoopDifferedDlistFlags);
    if (model != nullptr) {
        model->setBaseScale(scale);
    }
    return model;
}

void renderModelAtMtx(J3DModel* model, MtxP mtx, mDoExt_bckAnm* bck) {
    if (model == nullptr) return;

    if (bck != nullptr) {
        bck->play();
        bck->entry(model->getModelData());
    }

    model->setBaseTRMtx(mtx);
    model->calc();

    daAlink_c* alink = daAlink_getAlinkActorClass();
    if (alink != nullptr) {
        g_env_light.settingTevStruct_colget_player(&alink->tevStr);
        g_env_light.setLightTevColorType_MAJI(model, &alink->tevStr);
    }

    mDoExt_modelUpdateDL(model);
}

JKRHeap* private_arc_heap_if_any();
JKRHeap* private_arc_heap();
std::string coop_mem_status();

namespace {
struct ModelHeap {
    J3DModel* model = nullptr;
    JKRSolidHeap* heap = nullptr;
};
const int kModelHeapMax = 1024;
ModelHeap s_modelHeaps[kModelHeapMax];
int s_modelHeapFallbacks = 0;
}

J3DModel* coop_create_model(J3DModelData* data, u32 modelFlag, u32 differedDlistFlag) {
    if (data == nullptr) return nullptr;

    const u32 kModelHeapSize = 2u * 1024u * 1024u;
    const u32 kReserve = 2u * 1024u * 1024u;
    JKRHeap* parent = private_arc_heap();
    int slot = -1;
    for (int i = 0; i < kModelHeapMax; ++i) {
        if (s_modelHeaps[i].model == nullptr) {
            slot = i;
            break;
        }
    }
    if (parent == nullptr || slot < 0 || parent->getFreeSize() < kModelHeapSize + kReserve) {

        if (s_modelHeapFallbacks++ < 4) {
            coop_log::warn("coop_mod: [models] no room for a model heap ({}) - building it the "
                           "old way | [MEM] {}", slot < 0 ? "table full" : "heap low",
                coop_mem_status());
        }
        return mDoExt_J3DModel__create(data, modelFlag, differedDlistFlag);
    }

    const u32 kSizes[] = {512u * 1024u, kModelHeapSize};
    JKRSolidHeap* heap = nullptr;
    J3DModel* model = nullptr;
    for (u32 size : kSizes) {
        heap = JKRSolidHeap::create(size, parent, false);
        if (heap == nullptr) continue;
        JKRHeap* previous = heap->becomeCurrentHeap();
        model = mDoExt_J3DModel__create(data, modelFlag, differedDlistFlag);
        if (previous != nullptr) previous->becomeCurrentHeap();
        if (model != nullptr) break;
        heap->destroy();
        heap = nullptr;
    }
    if (model == nullptr) return nullptr;
    heap->adjustSize();
    s_modelHeaps[slot].model = model;
    s_modelHeaps[slot].heap = heap;
    return model;
}

void coop_free_model(J3DModel* model) {
    if (model == nullptr) return;
    for (ModelHeap& m : s_modelHeaps) {
        if (m.model != model) continue;

        m.heap->destroy();
        m = ModelHeap{};
        return;
    }
    JKR_DELETE(model);
}

std::string coop_mem_status() {

    const auto kb = [](JKRHeap* h, char* out, size_t n) {
        if (h == nullptr) {
            std::snprintf(out, n, "-");
            return;
        }
        std::snprintf(out, n, "%ld/%ldK", static_cast<long>(h->getTotalFreeSize() / 1024),
            static_cast<long>(h->getFreeSize() / 1024));
    };
    char cur[32], sys[32], zel[32], game[32], arc[32], ours[32], root[32];
    JKRHeap* current = JKRHeap::getCurrentHeap();
    kb(current, cur, sizeof(cur));
    kb(JKRHeap::getSystemHeap(), sys, sizeof(sys));
    kb(mDoExt_getZeldaHeap(), zel, sizeof(zel));
    kb(mDoExt_getGameHeap(), game, sizeof(game));
    kb(mDoExt_getArchiveHeap(), arc, sizeof(arc));
    kb(private_arc_heap_if_any(), ours, sizeof(ours));
    kb(JKRHeap::getRootHeap(), root, sizeof(root));
    char buf[320];
    std::snprintf(buf, sizeof(buf),
        "current=%s:%s system=%s zelda=%s game=%s archive=%s ours=%s root=%s",
        current != nullptr && current->getName() != nullptr ? current->getName() : "?", cur, sys,
        zel, game, arc, ours, root);
    return buf;
}

void ensure_system_heap_capacity() {
    static bool s_done = false;
    if (s_done) return;

    JKRHeap* sysHeap = JKRHeap::getSystemHeap();
    JKRHeap* rootHeap = JKRHeap::getRootHeap();
    if (sysHeap == nullptr || rootHeap == nullptr) {
        return;
    }

    if (sysHeap->getFreeSize() < 4 * 1024 * 1024) {
        u32 targetSize = 32 * 1024 * 1024;
        u32 rootFree = rootHeap->getFreeSize();
        if (rootFree < targetSize + 2 * 1024 * 1024) {
            targetSize = (rootFree > 4 * 1024 * 1024) ? (rootFree - 2 * 1024 * 1024) : 0;
        }

        if (targetSize >= 4 * 1024 * 1024) {
            JKRExpHeap* newSysHeap = JKRExpHeap::create(targetSize, rootHeap, false);
            if (newSysHeap != nullptr) {
                newSysHeap->setName("ExpandedSysHeap");
                JKRHeap::setSystemHeap(newSysHeap);
                s_done = true;
                coop_log::info("coop_mod: [models] expanded system heap to {} MB (prev free: {} KB)",
                    targetSize / (1024 * 1024), sysHeap->getFreeSize() / 1024);
            }
        }
    }
}
