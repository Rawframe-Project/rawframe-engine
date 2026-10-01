// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Inherited text style (record mui-0004). The size scales the parent's
// computed size; the line height and letter spacing scale the node's own
// size and are inherited as written, as a unitless CSS line-height is.

#include "inherit.h"

#include "property.h"

static muiTextRecord Compute(const muiTextStyle* own, muiPropertyMask given,
                             const muiTextRecord* parent)
{
    const muiComputedTextStyle* above = &parent->computed;
    muiTextRecord record = {.given = given};
    muiComputedTextStyle* out = &record.computed;
#define GIVEN(property) ((given & MUI_PROPERTY_BIT(property)) != 0)
    out->color = GIVEN(mui_propertyTextColor) ? own->color : above->color;
    out->font = GIVEN(mui_propertyFont) ? own->font : above->font;
    out->size = GIVEN(mui_propertyFontSize) ? own->size.scale * above->size + own->size.offset
                                            : above->size;
    out->weight = GIVEN(mui_propertyFontWeight) ? own->weight : above->weight;
    out->slant = GIVEN(mui_propertyFontSlant) ? own->slant : above->slant;
    out->align = GIVEN(mui_propertyTextAlign) ? own->align : above->align;
    out->wrap = GIVEN(mui_propertyTextWrap) ? own->wrap : above->wrap;
    record.lineHeight = GIVEN(mui_propertyLineHeight) ? own->lineHeight : parent->lineHeight;
    record.letterSpacing =
        GIVEN(mui_propertyLetterSpacing) ? own->letterSpacing : parent->letterSpacing;
#undef GIVEN
    out->automaticLineHeight = record.lineHeight.kind == mui_dimensionAuto;
    out->lineHeight = out->automaticLineHeight
                          ? 0.0f
                          : record.lineHeight.scale * out->size + record.lineHeight.offset;
    out->letterSpacing = record.letterSpacing.scale * out->size + record.letterSpacing.offset;
    return record;
}

// muiDefaultTextStyle computed: what a root given nothing, and a node
// not yet styled, read. A test checks it against the defaults.
static const muiTextRecord s_root = {
    .computed = {.color = {0.0f, 0.0f, 0.0f, 1.0f},
                 .size = 16.0f,
                 .weight = 400.0f,
                 .slant = mui_slantNormal,
                 .align = mui_textAlignStart,
                 .wrap = mui_textWrap,
                 .automaticLineHeight = true},
    .lineHeight = {0.0f, 0.0f, mui_dimensionAuto},
    .letterSpacing = {0.0f, 0.0f, mui_dimensionValue},
};

muiTextRecord muiRootTextRecord(void)
{
    return s_root;
}

static bool SameDimension(muiDimension a, muiDimension b)
{
    return a.kind == b.kind && a.scale == b.scale && a.offset == b.offset;
}

// Whether what lays text out differs: everything but color and
// alignment.
static bool SizesDiffer(const muiComputedTextStyle* a, const muiComputedTextStyle* b)
{
    return a->font != b->font || a->size != b->size || a->lineHeight != b->lineHeight ||
           a->letterSpacing != b->letterSpacing || a->weight != b->weight || a->slant != b->slant ||
           a->wrap != b->wrap || a->automaticLineHeight != b->automaticLineHeight;
}

static bool PaintDiffers(const muiComputedTextStyle* a, const muiComputedTextStyle* b)
{
    return a->color.r != b->color.r || a->color.g != b->color.g || a->color.b != b->color.b ||
           a->color.a != b->color.a || a->align != b->align;
}

// Computes one node's record; true when what its children inherit
// changed.
static bool Update(const muiTextNodes* nodes, uint32_t node, const muiTextRecord* root)
{
    uint32_t parent = muiTreeAt(nodes->tree, node)->links.parent;
    muiTextRecord* record = &nodes->records[node - 1];
    muiTextRecord next = Compute(&nodes->own[node - 1], record->given,
                                 parent != 0 ? &nodes->records[parent - 1] : root);
    next.parent = parent != 0 ? muiTreeIdOf(nodes->tree, parent) : (muiNodeId){0, 0};
    bool sizes = SizesDiffer(&record->computed, &next.computed);
    bool paint = PaintDiffers(&record->computed, &next.computed);
    bool written = !SameDimension(record->lineHeight, next.lineHeight) ||
                   !SameDimension(record->letterSpacing, next.letterSpacing);
    *record = next;
    if (nodes->layout[node - 1].style.content == mui_contentHost)
    {
        if (sizes)
        {
            muiTreeMarkLayout(nodes->tree, node);
        }
        else if (paint)
        {
            muiTreeMark(nodes->tree, node, mui_stagePaint);
        }
    }
    return sizes || paint || written;
}

bool muiIsTextRecordStale(const muiTextNodes* nodes, uint32_t node, muiPropertyMask given)
{
    const muiTextRecord* record = &nodes->records[node - 1];
    uint32_t parent = muiTreeAt(nodes->tree, node)->links.parent;
    muiNodeId parentId = parent != 0 ? muiTreeIdOf(nodes->tree, parent) : (muiNodeId){0, 0};
    return record->given != given || record->parent.index1 != parentId.index1 ||
           record->parent.generation != parentId.generation;
}

void muiInheritText(const muiTextNodes* nodes, uint32_t node)
{
    const muiTextRecord root = muiRootTextRecord();
    if (!Update(nodes, node, &root))
    {
        return;
    }
    const muiTree* tree = nodes->tree;
    for (uint32_t at = muiTreeAt(tree, node)->links.firstChild; at != 0;)
    {
        if (Update(nodes, at, &root) && muiTreeAt(tree, at)->links.firstChild != 0)
        {
            at = muiTreeAt(tree, at)->links.firstChild;
            continue;
        }
        while (at != node && muiTreeAt(tree, at)->links.next == 0)
        {
            at = muiTreeAt(tree, at)->links.parent;
        }
        at = at == node ? 0 : muiTreeAt(tree, at)->links.next;
    }
}
