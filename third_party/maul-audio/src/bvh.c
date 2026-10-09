// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The hierarchy (bvh.h). The build is iterative: a node's range of
// triangles is split at the binned SAH's best plane (32 bins per axis,
// ties to the lower axis and bin) until 4 or fewer remain; a range whose
// centroids coincide, or one deeper than 64, is halved by position. Box
// tests scale the exit distance by 1 + 2 gamma(3) (Ize, "Robust BVH ray
// traversal", JCGT 2013) so that rounding never misses a box a
// watertight triangle test would hit.

#include "bvh.h"

#include "watertight.h"

#include <math.h>
#include <string.h>

#define BINS       32
#define LEAF       4
#define DEEPEST    64
#define STACK      128
#define EXIT_SCALE 1.0000004f

// Plain comparisons, which compile to single instructions where fminf
// and fmaxf, keeping NaN's rules, become library calls. No NaN reaches
// them: vertices are finite and parallel slabs are handled apart.
static float Min(float a, float b)
{
    return a < b ? a : b;
}

static float Max(float a, float b)
{
    return a > b ? a : b;
}

typedef struct Box
{
    float min[3];
    float max[3];
} Box;

static Box Empty(void)
{
    return (Box){{INFINITY, INFINITY, INFINITY}, {-INFINITY, -INFINITY, -INFINITY}};
}

static void Grow(Box* box, const float* p)
{
    for (int i = 0; i < 3; ++i)
    {
        box->min[i] = Min(box->min[i], p[i]);
        box->max[i] = Max(box->max[i], p[i]);
    }
}

static void Merge(Box* box, const Box* other)
{
    // An empty box's corners are infinite: it adds nothing.
    if (other->min[0] > other->max[0])
    {
        return;
    }
    Grow(box, other->min);
    Grow(box, other->max);
}

static float Area(const Box* box)
{
    float d[3];
    for (int i = 0; i < 3; ++i)
    {
        d[i] = Max(box->max[i] - box->min[i], 0.0f);
    }
    return 2.0f * (d[0] * d[1] + d[1] * d[2] + d[2] * d[0]);
}

static float Centroid(const maudTriangle* t, int axis)
{
    return (t->a[axis] + t->b[axis] + t->c[axis]) * (1.0f / 3.0f);
}

static Box TriangleBox(const maudTriangle* t)
{
    Box box = Empty();
    Grow(&box, t->a);
    Grow(&box, t->b);
    Grow(&box, t->c);
    return box;
}

uint32_t maudBvhCapacity(uint32_t count)
{
    return count > 0 ? 2 * count - 1 : 1;
}

typedef struct Split
{
    int axis;
    int bin;
    float low;
    float scale;
} Split;

static int BinOf(const maudTriangle* t, const Split* split)
{
    int k = (int)((Centroid(t, split->axis) - split->low) * split->scale);
    return k < 0 ? 0 : k >= BINS ? BINS - 1 : k;
}

// The best binned split of one axis into *best, if better than its cost.
static void TryAxis(const maudTriangle* tris, uint32_t count, int axis, float low, float high,
                    Split* best, float* bestCost)
{
    if (!(high > low))
    {
        return;
    }
    Split split = {axis, 0, low, (float)BINS / (high - low)};
    uint32_t counts[BINS] = {0};
    Box boxes[BINS];
    for (int k = 0; k < BINS; ++k)
    {
        boxes[k] = Empty();
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        int k = BinOf(&tris[i], &split);
        counts[k] += 1;
        Box box = TriangleBox(&tris[i]);
        Merge(&boxes[k], &box);
    }
    float rightCost[BINS];
    Box right = Empty();
    uint32_t rightCount = 0;
    for (int k = BINS - 1; k > 0; --k)
    {
        Merge(&right, &boxes[k]);
        rightCount += counts[k];
        rightCost[k] = Area(&right) * (float)rightCount;
    }
    Box left = Empty();
    uint32_t leftCount = 0;
    for (int k = 0; k < BINS - 1; ++k)
    {
        Merge(&left, &boxes[k]);
        leftCount += counts[k];
        if (leftCount == 0 || leftCount == count)
        {
            continue;
        }
        float cost = Area(&left) * (float)leftCount + rightCost[k + 1];
        if (cost < *bestCost)
        {
            *bestCost = cost;
            split.bin = k;
            *best = split;
        }
    }
}

// Splits a range in place; returns the left part's count.
static uint32_t Partition(maudTriangle* tris, uint32_t count, uint32_t depth)
{
    Box centroids = Empty();
    for (uint32_t i = 0; i < count; ++i)
    {
        float c[3] = {Centroid(&tris[i], 0), Centroid(&tris[i], 1), Centroid(&tris[i], 2)};
        Grow(&centroids, c);
    }
    Split best = {-1, 0, 0.0f, 0.0f};
    float bestCost = INFINITY;
    for (int axis = 0; axis < 3 && depth <= DEEPEST; ++axis)
    {
        TryAxis(tris, count, axis, centroids.min[axis], centroids.max[axis], &best, &bestCost);
    }
    if (best.axis < 0)
    {
        return count / 2;
    }
    uint32_t i = 0;
    uint32_t j = count;
    while (i < j)
    {
        if (BinOf(&tris[i], &best) <= best.bin)
        {
            ++i;
            continue;
        }
        --j;
        maudTriangle swap = tris[i];
        tris[i] = tris[j];
        tris[j] = swap;
    }
    return i;
}

typedef struct Work
{
    uint32_t first;
    uint32_t count;
    uint32_t depth;
    // The parent whose right child this is, or UINT32_MAX.
    uint32_t parent;
} Work;

static void Bound(const maudTriangle* tris, uint32_t count, maudBvhNode* node)
{
    Box box = Empty();
    for (uint32_t i = 0; i < count; ++i)
    {
        Box t = TriangleBox(&tris[i]);
        Merge(&box, &t);
    }
    memcpy(node->min, box.min, sizeof(node->min));
    memcpy(node->max, box.max, sizeof(node->max));
}

uint32_t maudBuildBvh(maudTriangle* triangles, uint32_t count, maudBvhNode* nodes)
{
    Work stack[STACK];
    uint32_t top = 0;
    uint32_t used = 0;
    stack[top++] = (Work){0, count, 0, UINT32_MAX};
    while (top > 0)
    {
        Work work = stack[--top];
        uint32_t index = used++;
        maudBvhNode* node = &nodes[index];
        if (work.parent != UINT32_MAX)
        {
            nodes[work.parent].offset = index;
        }
        Bound(triangles + work.first, work.count, node);
        if (work.count <= LEAF)
        {
            node->offset = work.first;
            node->count = work.count;
            continue;
        }
        uint32_t left = Partition(triangles + work.first, work.count, work.depth);
        node->count = 0;
        stack[top++] = (Work){work.first + left, work.count - left, work.depth + 1, index};
        stack[top++] = (Work){work.first, left, work.depth + 1, UINT32_MAX};
    }
    return used;
}

typedef struct Ray
{
    maudShearedRay sheared;
    float origin[3];
    float inverse[3];
    bool parallel[3];
} Ray;

static void Prepare(const float* origin, const float* direction, Ray* ray)
{
    maudShearRay(origin, direction, &ray->sheared);
    for (int i = 0; i < 3; ++i)
    {
        ray->origin[i] = origin[i];
        ray->parallel[i] = direction[i] == 0.0f;
        ray->inverse[i] = ray->parallel[i] ? 0.0f : 1.0f / direction[i];
    }
}

// The distance at which a ray enters a node's box, or INFINITY if it
// misses it within [tMin, tMax].
static float Enter(const Ray* ray, const maudBvhNode* node, float tMin, float tMax)
{
    float near = tMin;
    float far = tMax;
    for (int i = 0; i < 3; ++i)
    {
        // A ray parallel to a slab is inside it all along or never; the
        // general form would meet 0 times infinity on the slab's faces.
        if (ray->parallel[i])
        {
            if (ray->origin[i] < node->min[i] || ray->origin[i] > node->max[i])
            {
                return INFINITY;
            }
            continue;
        }
        float t0 = (node->min[i] - ray->origin[i]) * ray->inverse[i];
        float t1 = (node->max[i] - ray->origin[i]) * ray->inverse[i];
        near = Max(near, Min(t0, t1));
        far = Min(far, Max(t0, t1) * EXIT_SCALE);
    }
    return near <= far ? near : INFINITY;
}

typedef struct Pending
{
    uint32_t node;
    // Where the ray enters the node's box.
    float enter;
} Pending;

// Pushes an inner node's children that the ray enters before limit,
// the nearer last so that it is visited first.
static void PushChildren(const Ray* ray, const maudBvhNode* nodes, uint32_t index, float tMin,
                         float limit, Pending* stack, uint32_t* top)
{
    uint32_t left = index + 1;
    uint32_t right = nodes[index].offset;
    float l = Enter(ray, &nodes[left], tMin, limit);
    float r = Enter(ray, &nodes[right], tMin, limit);
    Pending near = l <= r ? (Pending){left, l} : (Pending){right, r};
    Pending far = l <= r ? (Pending){right, r} : (Pending){left, l};
    if (far.enter != INFINITY)
    {
        stack[(*top)++] = far;
    }
    if (near.enter != INFINITY)
    {
        stack[(*top)++] = near;
    }
}

bool maudBvhAnyHit(const maudBvhNode* nodes, const maudTriangle* triangles, const float* origin,
                   const float* direction, float tMin, float tMax)
{
    Ray ray;
    Prepare(origin, direction, &ray);
    Pending stack[STACK];
    uint32_t top = 0;
    float root = Enter(&ray, &nodes[0], tMin, tMax);
    if (root != INFINITY)
    {
        stack[top++] = (Pending){0, root};
    }
    while (top > 0)
    {
        const maudBvhNode* node = &nodes[stack[--top].node];
        if (node->count == 0)
        {
            PushChildren(&ray, nodes, (uint32_t)(node - nodes), tMin, tMax, stack, &top);
            continue;
        }
        for (uint32_t i = 0; i < node->count; ++i)
        {
            const maudTriangle* t = &triangles[node->offset + i];
            float distance;
            if (maudHitTriangle(&ray.sheared, t->a, t->b, t->c, tMin, tMax, &distance))
            {
                return true;
            }
        }
    }
    return false;
}

const maudTriangle* maudBvhClosestHit(const maudBvhNode* nodes, const maudTriangle* triangles,
                                      const float* origin, const float* direction, float tMin,
                                      float tMax, float* t)
{
    Ray ray;
    Prepare(origin, direction, &ray);
    const maudTriangle* best = nullptr;
    float bestT = tMax;
    Pending stack[STACK];
    uint32_t top = 0;
    float root = Enter(&ray, &nodes[0], tMin, tMax);
    if (root != INFINITY)
    {
        stack[top++] = (Pending){0, root};
    }
    while (top > 0)
    {
        Pending pending = stack[--top];
        // A box entered beyond the best hit so far cannot hold a nearer
        // one (at the same distance it may hold a tie).
        if (pending.enter > bestT)
        {
            continue;
        }
        const maudBvhNode* node = &nodes[pending.node];
        if (node->count == 0)
        {
            PushChildren(&ray, nodes, pending.node, tMin, bestT, stack, &top);
            continue;
        }
        for (uint32_t i = 0; i < node->count; ++i)
        {
            const maudTriangle* tri = &triangles[node->offset + i];
            float distance;
            if (maudHitTriangle(&ray.sheared, tri->a, tri->b, tri->c, tMin, bestT, &distance) &&
                (best == nullptr || distance < bestT ||
                 (distance == bestT && tri->index < best->index)))
            {
                best = tri;
                bestT = distance;
            }
        }
    }
    *t = bestT;
    return best;
}

bool maudBvhVisit(const maudBvhNode* nodes, const maudTriangle* entries, const float* origin,
                  const float* direction, float tMin, float* limit, maudBvhVisitFn* visit,
                  void* context)
{
    Ray ray;
    Prepare(origin, direction, &ray);
    Pending stack[STACK];
    uint32_t top = 0;
    float root = Enter(&ray, &nodes[0], tMin, *limit);
    if (root != INFINITY)
    {
        stack[top++] = (Pending){0, root};
    }
    while (top > 0)
    {
        Pending pending = stack[--top];
        if (pending.enter > *limit)
        {
            continue;
        }
        const maudBvhNode* node = &nodes[pending.node];
        if (node->count == 0)
        {
            PushChildren(&ray, nodes, pending.node, tMin, *limit, stack, &top);
            continue;
        }
        for (uint32_t i = 0; i < node->count; ++i)
        {
            if (visit(&entries[node->offset + i], limit, context))
            {
                return true;
            }
        }
    }
    return false;
}
