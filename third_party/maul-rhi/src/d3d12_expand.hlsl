// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Expands counted multi-draw records for a D3D12 pipeline that reads its
// first vertex or instance (mrhi-0020): each record of `words` 32-bit
// words is written after a copy of the two words at `info` in it, the
// base vertex and first instance its command signature sets as root
// constants. tools/gen_kernels.py compiles it to
// src/generated/d3d12_expand.h.

#define EXPAND_ROOT                                                                                \
    "RootConstants(num32BitConstants=3, b0), SRV(t0), UAV(u0)"

cbuffer Shape : register(b0)
{
    uint most;
    uint words;
    uint info;
};

ByteAddressBuffer records : register(t0);
RWByteAddressBuffer expanded : register(u0);

[RootSignature(EXPAND_ROOT)]
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint record = id.x;
    if (record >= most)
    {
        return;
    }
    uint from = record * words * 4;
    uint to = record * (words + 2) * 4;
    expanded.Store2(to, records.Load2(from + info * 4));
    for (uint word = 0; word < words; ++word)
    {
        expanded.Store(to + 8 + word * 4, records.Load(from + word * 4));
    }
}
