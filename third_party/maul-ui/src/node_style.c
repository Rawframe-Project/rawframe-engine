// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The public functions over a node's style: its type, classes and
// states, which the next layout resolves, and its direct writes, which
// take effect at once because nothing can win over them.

#include "animation.h"
#include "context.h"
#include "focus.h"
#include "fpenv.h"
#include "inherit.h"
#include "layer.h"
#include "layout_node.h"
#include "pool.h"
#include "property.h"
#include "scroll_store.h"
#include "style_store.h"
#include "tree.h"

#include "maul-ui/style.h"
#include "maul-ui/text_style.h"
#include "maul-ui/visual.h"

#include <string.h>

static muiResult NodeSetType(muiContext* context, muiNodeId nodeId, muiNodeTypeId typeId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    if (slot == 0)
    {
        return status;
    }
    if (typeId.index1 != 0 &&
        muiPoolResolve(&context->style.typePool, typeId.index1, typeId.generation) == 0)
    {
        return mui_errorStale;
    }
    context->style.nodes[slot - 1].type = typeId;
    context->style.nodes[slot - 1].edited = true;
    muiTreeMark(&context->tree, slot, mui_stageStyle);
    return mui_success;
}

muiResult muiNode_SetType(muiContext* context, muiNodeId nodeId, muiNodeTypeId typeId)
{
    muiResult status = muiEnter(context);
    if (status == mui_success)
    {
        status = NodeSetType(context, nodeId, typeId);
        muiLeave(context);
    }
    return status;
}

static muiResult NodeSetClasses(muiContext* context, muiNodeId nodeId, const muiStyleId* classes,
                                uint32_t count)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult listStatus = muiClassListStatus(classes, count);
    if (listStatus != mui_success)
    {
        return muiRefuseAs(context, listStatus);
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    if (slot != 0)
    {
        muiSetClassList(&context->style.nodes[slot - 1].classes, classes, count);
        context->style.nodes[slot - 1].edited = true;
        muiTreeMark(&context->tree, slot, mui_stageStyle);
    }
    return status;
}

muiResult muiNode_SetClasses(muiContext* context, muiNodeId nodeId, const muiStyleId* classes,
                             uint32_t count)
{
    muiResult status = muiEnter(context);
    if (status == mui_success)
    {
        status = NodeSetClasses(context, nodeId, classes, count);
        muiLeave(context);
    }
    return status;
}

static muiResult NodeSetStates(muiContext* context, muiNodeId nodeId, muiState states)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if ((states & ~MUI_KNOWN_STATES) != 0)
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    // Exiting is the library's (src/exit.c): kept as it is.
    states = slot != 0 ? (muiState)((states & ~mui_stateExiting) |
                                    (context->style.nodes[slot - 1].states & mui_stateExiting))
                       : states;
    if (slot != 0 && context->style.nodes[slot - 1].states != states)
    {
        context->style.nodes[slot - 1].states = states;
        context->style.nodes[slot - 1].edited = true;
        muiTreeMark(&context->tree, slot, mui_stageStyle);
    }
    return status;
}

muiResult muiNode_SetStates(muiContext* context, muiNodeId nodeId, muiState states)
{
    muiResult status = muiEnter(context);
    if (status == mui_success)
    {
        status = NodeSetStates(context, nodeId, states);
        muiLeave(context);
    }
    return status;
}

muiState muiNode_GetStates(const muiContext* context, muiNodeId nodeId)
{
    uint32_t slot = context != nullptr ? muiTreeResolve(&context->tree, nodeId) : 0;
    return slot != 0 ? muiStatesOf(&context->style.nodes[slot - 1]) : 0;
}

// Writes the properties of a group mask names, within allowed, directly:
// at once, stopping their transitions; a layout one lays the node out
// again, a visual one paints it again.
static muiResult SetDirect(muiContext* context, muiNodeId nodeId, muiConstValuesRef values,
                           muiPropertyGroup group, muiPropertyMask mask, muiPropertyMask allowed)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    const muiPropertyBits properties = muiPropertiesOf(group, mask);
    if ((mask & ~allowed) != 0)
    {
        return muiRefuse(context);
    }
    muiResult valuesStatus = muiPropertiesStatus(values, properties);
    if (valuesStatus != mui_success)
    {
        return muiRefuseAs(context, valuesStatus);
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    if (slot == 0)
    {
        return status;
    }
    const muiMotion motion = {&context->animations, context->layout,     context->visual,
                              context->style.nodes, &context->tree,      context->text,
                              context->textRecords, context->interaction};
    // Most nodes move nothing.
    for (muiPropertyBits left = properties;
         context->style.nodes[slot - 1].firstAnimation != 0 && muiAnyProperty(left);)
    {
        muiStopAnimation(&motion, slot, muiTakeProperty(&left));
    }
    muiLayoutNode* layout = &context->layout[slot - 1];
    muiLayerKind layer = context->interaction[slot - 1].layer;
    muiApplyProperties((muiValuesRef){&layout->style, &context->visual[slot - 1],
                                      &context->text[slot - 1], &context->interaction[slot - 1]},
                       values, properties);
    muiSyncLayoutNode(layout);
    muiSyncScroll(&context->scrolls[slot - 1], layout->style.scrollAxes);
    muiNodeStyle* node = &context->style.nodes[slot - 1];
    node->direct = muiUnion(node->direct, properties);
    node->edited = true;
    if (group == mui_groupLayout && mask != 0)
    {
        muiTreeMarkLayout(&context->tree, slot);
    }
    if (group == mui_groupVisual && mask != 0)
    {
        muiTreeMark(&context->tree, slot, mui_stagePaint);
    }
    muiNoteLayer(context, slot, layer);
    muiNoteFocus(context, slot);
    // The node's text, and its inheriting children's, take the write at
    // once; the style pass then weighs it with the node's classes.
    if (group == mui_groupText && mask != 0)
    {
        context->textGiven = true;
        muiTextRecord* record = &context->textRecords[slot - 1];
        record->given |= mask;
        const muiTextNodes text = {&context->tree, context->layout, context->text,
                                   context->textRecords};
        muiInheritText(&text, slot);
        muiTreeMark(&context->tree, slot, mui_stageStyle);
    }
    return status;
}

static muiResult NodeSetLayoutValues(muiContext* context, muiNodeId nodeId,
                                     const muiLayoutStyle* values, muiPropertyMask mask)
{
    if (values == nullptr)
    {
        return context != nullptr ? muiRefuse(context) : mui_errorInvalid;
    }
    return SetDirect(context, nodeId, (muiConstValuesRef){values, nullptr, nullptr, nullptr},
                     mui_groupLayout, mask, MUI_LAYOUT_PROPERTIES);
}

static MUI_NOINLINE muiResult NodeSetLayoutValuesWorker(muiContext* context, muiNodeId nodeId,
                                                        const muiLayoutStyle* values,
                                                        muiPropertyMask mask)
{
    muiResult status = muiEnter(context);
    if (status == mui_success)
    {
        status = NodeSetLayoutValues(context, nodeId, values, mask);
        muiLeave(context);
    }
    return status;
}

muiResult muiNode_SetLayoutValues(muiContext* context, muiNodeId nodeId,
                                  const muiLayoutStyle* values, muiPropertyMask mask)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = NodeSetLayoutValuesWorker(context, nodeId, values, mask);
    muiFloatEnvLeave(floatEnv);
    return result;
}

static muiResult NodeSetVisualValues(muiContext* context, muiNodeId nodeId,
                                     const muiVisualStyle* values, muiPropertyMask mask)
{
    if (values == nullptr)
    {
        return context != nullptr ? muiRefuse(context) : mui_errorInvalid;
    }
    return SetDirect(context, nodeId, (muiConstValuesRef){nullptr, values, nullptr, nullptr},
                     mui_groupVisual, mask, MUI_VISUAL_PROPERTIES);
}

static MUI_NOINLINE muiResult NodeSetVisualValuesWorker(muiContext* context, muiNodeId nodeId,
                                                        const muiVisualStyle* values,
                                                        muiPropertyMask mask)
{
    muiResult status = muiEnter(context);
    if (status == mui_success)
    {
        status = NodeSetVisualValues(context, nodeId, values, mask);
        muiLeave(context);
    }
    return status;
}

muiResult muiNode_SetVisualValues(muiContext* context, muiNodeId nodeId,
                                  const muiVisualStyle* values, muiPropertyMask mask)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = NodeSetVisualValuesWorker(context, nodeId, values, mask);
    muiFloatEnvLeave(floatEnv);
    return result;
}

static muiResult NodeSetTextValues(muiContext* context, muiNodeId nodeId,
                                   const muiTextStyle* values, muiPropertyMask mask)
{
    if (values == nullptr)
    {
        return context != nullptr ? muiRefuse(context) : mui_errorInvalid;
    }
    return SetDirect(context, nodeId, (muiConstValuesRef){nullptr, nullptr, values, nullptr},
                     mui_groupText, mask, MUI_TEXT_PROPERTIES);
}

static MUI_NOINLINE muiResult NodeSetTextValuesWorker(muiContext* context, muiNodeId nodeId,
                                                      const muiTextStyle* values,
                                                      muiPropertyMask mask)
{
    muiResult status = muiEnter(context);
    if (status == mui_success)
    {
        status = NodeSetTextValues(context, nodeId, values, mask);
        muiLeave(context);
    }
    return status;
}

muiResult muiNode_SetTextValues(muiContext* context, muiNodeId nodeId, const muiTextStyle* values,
                                muiPropertyMask mask)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = NodeSetTextValuesWorker(context, nodeId, values, mask);
    muiFloatEnvLeave(floatEnv);
    return result;
}

static muiResult NodeSetInteractionValues(muiContext* context, muiNodeId nodeId,
                                          const muiInteractionStyle* values, muiPropertyMask mask)
{
    if (values == nullptr)
    {
        return context != nullptr ? muiRefuse(context) : mui_errorInvalid;
    }
    return SetDirect(context, nodeId, (muiConstValuesRef){nullptr, nullptr, nullptr, values},
                     mui_groupInteraction, mask, MUI_INTERACTION_PROPERTIES);
}

static MUI_NOINLINE muiResult NodeSetInteractionValuesWorker(muiContext* context, muiNodeId nodeId,
                                                             const muiInteractionStyle* values,
                                                             muiPropertyMask mask)
{
    muiResult status = muiEnter(context);
    if (status == mui_success)
    {
        status = NodeSetInteractionValues(context, nodeId, values, mask);
        muiLeave(context);
    }
    return status;
}

muiResult muiNode_SetInteractionValues(muiContext* context, muiNodeId nodeId,
                                       const muiInteractionStyle* values, muiPropertyMask mask)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = NodeSetInteractionValuesWorker(context, nodeId, values, mask);
    muiFloatEnvLeave(floatEnv);
    return result;
}

muiResult muiNode_GetInteractionStyle(const muiContext* context, muiNodeId nodeId,
                                      muiInteractionStyle* valuesOut)
{
    if (valuesOut != nullptr)
    {
        memset(valuesOut, 0, sizeof *valuesOut);
    }
    if (context == nullptr || valuesOut == nullptr || nodeId.index1 == 0)
    {
        return mui_errorInvalid;
    }
    uint32_t slot = muiTreeResolve(&context->tree, nodeId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    *valuesOut = context->interaction[slot - 1];
    return mui_success;
}

muiResult muiNode_GetTextStyle(const muiContext* context, muiNodeId nodeId,
                               muiComputedTextStyle* styleOut)
{
    if (styleOut != nullptr)
    {
        memset(styleOut, 0, sizeof *styleOut);
    }
    if (context == nullptr || styleOut == nullptr || nodeId.index1 == 0)
    {
        return mui_errorInvalid;
    }
    uint32_t slot = muiTreeResolve(&context->tree, nodeId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    *styleOut = context->textRecords[slot - 1].computed;
    return mui_success;
}

muiResult muiNode_GetVisualStyle(const muiContext* context, muiNodeId nodeId,
                                 muiVisualStyle* valuesOut)
{
    if (valuesOut != nullptr)
    {
        memset(valuesOut, 0, sizeof *valuesOut);
    }
    if (context == nullptr || valuesOut == nullptr || nodeId.index1 == 0)
    {
        return mui_errorInvalid;
    }
    uint32_t slot = muiTreeResolve(&context->tree, nodeId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    *valuesOut = context->visual[slot - 1];
    return mui_success;
}

static muiResult NodeResetProperties(muiContext* context, muiNodeId nodeId, muiPropertyGroup group,
                                     muiPropertyMask mask)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (!muiIsGroupMaskKnown(group, mask))
    {
        return muiRefuse(context);
    }
    const muiPropertyBits properties = muiPropertiesOf(group, mask);
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    if (slot != 0)
    {
        muiNodeStyle* node = &context->style.nodes[slot - 1];
        node->direct = muiWithout(node->direct, properties);
        node->edited = true;
        // Its classes may name none of them: the defaults they go back to.
        context->style.reach = muiUnion(context->style.reach, properties);
        muiTreeMark(&context->tree, slot, mui_stageStyle);
    }
    return status;
}

muiResult muiNode_ResetProperties(muiContext* context, muiNodeId nodeId, muiPropertyGroup group,
                                  muiPropertyMask mask)
{
    muiResult status = muiEnter(context);
    if (status == mui_success)
    {
        status = NodeResetProperties(context, nodeId, group, mask);
        muiLeave(context);
    }
    return status;
}

muiPropertyMask muiNode_GetDirectProperties(const muiContext* context, muiNodeId nodeId,
                                            muiPropertyGroup group)
{
    uint32_t slot = context != nullptr ? muiTreeResolve(&context->tree, nodeId) : 0;
    return slot != 0 && group < MUI_PROPERTY_GROUPS
               ? context->style.nodes[slot - 1].direct.words[group]
               : 0;
}
