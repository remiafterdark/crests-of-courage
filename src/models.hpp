#pragma once

#include "mods/api.h"

#include "m_Do/m_Do_ext.h"

int loadObjectArchive(const char* arcName);
void unloadObjectArchive(const char* arcName);

J3DModel* loadBmdFromArc(const char* arcName, const char* bmdName, cXyz scale);
J3DModel* loadBmdFromArcIdx(const char* arcName, int resIndex, cXyz scale);

J3DModel* loadBmdFromFile(const char* path, cXyz scale);
J3DModelData* loadBmdDataFromFile(const char* path);

J3DModelData* loadBmdDataForLink(const char* path);
J3DModel* modelFromData(J3DModelData* data, cXyz scale);

J3DModel* coop_create_model(J3DModelData* data, u32 modelFlag, u32 differedDlistFlag);
void coop_free_model(J3DModel* model);

void models_warp_guard_init();

void renderModelAtMtx(J3DModel* model, MtxP mtx, mDoExt_bckAnm* bck);

void ensure_system_heap_capacity();
