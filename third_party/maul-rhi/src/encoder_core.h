// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the encoder's commands share (mrhi-0011): a recording pass found
// by id, what it records, and records taken from the frame's arena.

#ifndef MAUL_RHI_SRC_ENCODER_CORE_H
#define MAUL_RHI_SRC_ENCODER_CORE_H

#include "device_core.h"

#include "maul-rhi/encoder.h"

// What a pass records: a render pass draws, a pass without targets
// dispatches, a transfer pass copies.
typedef enum mrhiPassWork
{
    mrhiWorkRender,
    mrhiWorkCompute,
    mrhiWorkTransfer,
} mrhiPassWork;

mrhiPassWork mrhiWorkOf(const mrhiFramePass* pass);

// The pass an id names in the open, compiled frame, recording: or NULL
// with the refusal.
mrhiFramePass* mrhiRecordingPass(mrhiDevice* device, mrhiPassId id, mrhiResult* statusOut);

// Takes count records, at most a chunk's, in the pass's last chunk or a
// new one: the records, or NULL when the arena is full, which marks the
// pass so that its frame is never submitted without the command.
mrhiCommand* mrhiTakeCommands(mrhiDevice* device, mrhiFramePass* pass, uint32_t count);

// The access kinds as bits, for the kinds a command accepts.
#define MRHI_KIND(kind) (1u << (kind))

// Whether the pass declares a use of a frame resource (its slot plus
// one) of one of the kinds, covering a part of it: the part's planes,
// mips and layers, or any use for a buffer (part NULL). Target uses are
// of no access kind.
bool mrhiPassDeclares(const mrhiDevice* device, const mrhiFramePass* pass, uint32_t object,
                      uint32_t kinds, const mrhiFrameUse* part);

// The bytes of a buffer of the open frame, declared or imported.
uint64_t mrhiBufferBytesOf(const mrhiFrameResource* resource);

#endif // MAUL_RHI_SRC_ENCODER_CORE_H
