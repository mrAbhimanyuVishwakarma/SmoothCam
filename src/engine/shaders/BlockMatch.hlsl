// One level of coarse-to-fine block matching.
//
// For every 8x8 block of `From` we look for the displacement into `To` with the
// lowest sum of absolute differences, plus a small penalty for disagreeing with
// the prediction. Candidates come from the coarser level (upsampled), the
// previous frame pair (temporal), and zero; the best one is then refined with a
// small local search. This is the classic predictive-search idea behind 3-D
// recursive search, kept simple enough to run per-block in parallel.
//
// Output vectors are in pixels of *this* level.

#include "Common.hlsli"

Texture2D<float> From : register(t0);
Texture2D<float> To : register(t1);
Texture2D<float2> Coarse : register(t2);   // previous (coarser) level, its own pixel units
Texture2D<float2> Temporal : register(t3); // last pair's field, full-res pixels
RWTexture2D<float2> Out : register(u0);

cbuffer Params : register(b0)
{
    uint2 LevelSize;
    uint2 Blocks;
    uint2 CoarseBlocks;
    float TemporalScale; // full-res pixels -> this level's pixels
    uint Flags;          // 1 = has coarse, 2 = has temporal
    int SearchRadius;
    float SearchStep;
    float Lambda;
    int FineRadius;      // extra half-step refinement, 0 = off
    uint BlockStride;    // distance between block origins (8, or 4 for overlapped blocks)
    uint CoarseStride;   // BlockStride used on the coarser level
    float2 Pad;
};

static float g_block[BLOCK * BLOCK];

float Cost(float2 origin, float2 v, float2 pred)
{
    const float2 invSize = 1.0 / float2(LevelSize);
    float sad = 0;
    [unroll]
    for (int y = 0; y < BLOCK; ++y)
    {
        [unroll]
        for (int x = 0; x < BLOCK; ++x)
        {
            const float2 p = origin + float2(x, y) + 0.5 + v;
            sad += abs(g_block[y * BLOCK + x] - To.SampleLevel(LinearClamp, p * invSize, 0));
        }
    }
    const float2 d = v - pred;
    return sad * (1.0 / (BLOCK * BLOCK)) + Lambda * (abs(d.x) + abs(d.y));
}

void Try(float2 origin, float2 v, float2 pred, inout float bestCost, inout float2 best)
{
    const float c = Cost(origin, v, pred);
    if (c < bestCost)
    {
        bestCost = c;
        best = v;
    }
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= Blocks))
        return;

    // Each 8x8 window is centered on its cell; with a stride of 4 the windows overlap.
    const float2 origin = float2(id.xy * BlockStride) + BlockStride * 0.5 - BLOCK * 0.5;
    const int2 maxPos = int2(LevelSize) - 1;
    [unroll]
    for (int y = 0; y < BLOCK; ++y)
    {
        [unroll]
        for (int x = 0; x < BLOCK; ++x)
            g_block[y * BLOCK + x] = From[clamp(int2(origin) + int2(x, y), 0, maxPos)];
    }

    // Cell of the coarser level that covers this window's center.
    const float2 centerHere = origin + BLOCK * 0.5;
    const int2 coarseCell = int2(centerHere * 0.5 / float(CoarseStride));
    const int2 coarseLim = int2(CoarseBlocks) - 1;

    float2 pred = 0;
    if (Flags & 1)
    {
        pred = Coarse[clamp(coarseCell, 0, coarseLim)] * 2.0;
    }
    else if (Flags & 2)
    {
        const float2 uv = centerHere / float2(LevelSize);
        pred = Temporal.SampleLevel(LinearClamp, uv, 0) * TemporalScale;
    }

    float2 best = 0;
    float bestCost = Cost(origin, 0, pred);
    Try(origin, pred, pred, bestCost, best);

    if (Flags & 1)
    {
        Try(origin, Coarse[clamp(coarseCell + int2(-1, 0), 0, coarseLim)] * 2.0, pred, bestCost, best);
        Try(origin, Coarse[clamp(coarseCell + int2(1, 0), 0, coarseLim)] * 2.0, pred, bestCost, best);
        Try(origin, Coarse[clamp(coarseCell + int2(0, -1), 0, coarseLim)] * 2.0, pred, bestCost, best);
        Try(origin, Coarse[clamp(coarseCell + int2(0, 1), 0, coarseLim)] * 2.0, pred, bestCost, best);
    }
    if (Flags & 2)
    {
        const float2 uv = centerHere / float2(LevelSize);
        Try(origin, Temporal.SampleLevel(LinearClamp, uv, 0) * TemporalScale, pred, bestCost, best);
    }

    // Local search around the winner.
    const float2 center = best;
    for (int sy = -SearchRadius; sy <= SearchRadius; ++sy)
    {
        for (int sx = -SearchRadius; sx <= SearchRadius; ++sx)
        {
            if (sx == 0 && sy == 0)
                continue;
            Try(origin, center + float2(sx, sy) * SearchStep, pred, bestCost, best);
        }
    }

    // Optional sub-step refinement.
    if (FineRadius > 0)
    {
        const float2 c2 = best;
        const float fine = SearchStep * 0.5;
        for (int fy = -FineRadius; fy <= FineRadius; ++fy)
        {
            for (int fx = -FineRadius; fx <= FineRadius; ++fx)
            {
                if (fx == 0 && fy == 0)
                    continue;
                Try(origin, c2 + float2(fx, fy) * fine, pred, bestCost, best);
            }
        }
    }

    Out[id.xy] = best;
}
