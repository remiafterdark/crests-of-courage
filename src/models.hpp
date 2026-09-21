#pragma once

#include "mods/api.h"

#include "m_Do/m_Do_ext.h"

int loadObjectArchive(const char* arcName);
void unloadObjectArchive(const char* arcName);

J3DModel* loadBmdFromArc(const char* arcName, const char* bmdName, cXyz scale);
J3DModel* loadBmdFromArcIdx(const char* arcName, int resIndex, cXyz scale);

J3DModel* loadBmdFromFile(const char* path, cXyz scale);
J3DModelData* loadBmdDataFromFile(const char* path);
J3DModel* modelFromData(J3DModelData* data, cXyz scale);

void renderModelAtMtx(J3DModel* model, MtxP mtx, mDoExt_bckAnm* bck);

void ensure_system_heap_capacity();
