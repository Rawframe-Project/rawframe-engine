// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Comparing two records of a node (record mui-0008).

#include "access_record.h"

#include <string.h>

bool muiRecordTextDiffers(const muiAccessNode* old, const muiAccessNode* now,
                          muiAccessTextKind kind)
{
    const char* a = old->text[kind] != nullptr ? old->text[kind] : "";
    const char* b = now->text[kind] != nullptr ? now->text[kind] : "";
    return strcmp(a, b) != 0;
}

muiAccessTextKind muiRecordNameKindOf(const muiAccessNode* node)
{
    return node->text[mui_accessLabel] == nullptr && node->role == mui_roleLabel ? mui_accessValue
                                                                                 : mui_accessLabel;
}

bool muiRecordNameDiffers(const muiAccessNode* old, const muiAccessNode* now)
{
    muiAccessTextKind kind = muiRecordNameKindOf(now);
    return muiRecordNameKindOf(old) != kind || muiRecordTextDiffers(old, now, kind);
}
