// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Resolution in the fixed layers of record mui-0004: the defaults, every
// class's base values in order, the state variants (states weakest
// first, classes in order within each), the conditions that hold, then
// the direct writes, which the node's resolved values already hold.

#include "restyle.h"

#include "animation.h"
#include "condition.h"
#include "layer.h"
#include "layout_node.h"
#include "notify.h"
#include "pool.h"
#include "property.h"
#include "style_store.h"
#include "theme_store.h"
#include "token_store.h"
#include "tree.h"

#include "maul-ui/layout.h"

// The live classes of a node, its type's then its own, as class slots.
typedef struct Classes
{
    uint32_t slots[2 * MUI_MAX_CLASSES];
    uint32_t count;
} Classes;

static void AddLive(const muiStyleStore* store, const muiClassList* list, Classes* classes)
{
    for (uint32_t i = 0; i < list->count; i++)
    {
        muiStyleId id = list->classes[i];
        uint32_t slot = muiPoolResolve(&store->classPool, id.index1, id.generation);
        if (slot != 0)
        {
            classes->slots[classes->count++] = slot;
        }
    }
}

static Classes ClassesOf(const muiStyleStore* store, const muiNodeStyle* node)
{
    Classes classes = {.count = 0};
    uint32_t type = muiPoolResolve(&store->typePool, node->type.index1, node->type.generation);
    if (type != 0)
    {
        AddLive(store, &store->types[type - 1], &classes);
    }
    AddLive(store, &node->classes, &classes);
    return classes;
}

// What resolution gives a node: its values, and for each property the
// spec its change takes, from the same layers.
typedef struct Resolution
{
    muiStyleValues values;
    // Read only for the properties named holds, so it is never cleared.
    muiTransitionId transitions[MUI_PROPERTY_LIMIT];
    // The properties a layer named a spec for, and those a layer gave a
    // value.
    muiPropertyBits named;
    muiPropertyBits given;
    // The themes the node reads tokens through.
    muiThemeScope scope;
} Resolution;

// Writes the values of the tokens a set names for free properties; a
// token that gives no value, or one the property does not allow, leaves
// the layer silent for the property.
static void ApplyTokens(const muiContext* context, const muiPropertySet* set, muiPropertyBits free,
                        Resolution* resolution)
{
    const muiStyleStore* store = &context->style;
    for (uint32_t at = set->firstTokenName; at != 0; at = store->names[at - 1].next)
    {
        const muiTokenName* name = &store->names[at - 1];
        if (!muiHasProperty(free, name->property))
        {
            continue;
        }
        const muiTokenValue* value =
            muiResolveTokenIn(&context->tokens, &context->themes, &resolution->scope, name->token);
        if (value != nullptr &&
            muiApplyTokenValue(muiRefOf(&resolution->values), name->property, value))
        {
            resolution->given = muiUnion(resolution->given, muiPropertyOf(name->property));
        }
    }
}

// Applies a set's values, tokens and transitions to the properties free
// names.
static void ApplySet(const muiContext* context, const muiPropertySet* set, muiPropertyBits free,
                     Resolution* resolution)
{
    const muiPropertyBits given = muiIntersection(set->properties, free);
    muiApplyProperties(muiRefOf(&resolution->values), muiConstRefOf(&set->values), given);
    resolution->given = muiUnion(resolution->given, given);
    if (muiIntersects(set->tokens, free))
    {
        ApplyTokens(context, set, free, resolution);
    }
    for (uint32_t i = 0; i < set->bindingCount; i++)
    {
        muiPropertyBits bound = muiIntersection(set->bindings[i].properties, free);
        resolution->named = muiUnion(resolution->named, bound);
        while (muiAnyProperty(bound))
        {
            resolution->transitions[muiTakeProperty(&bound)] = set->bindings[i].transition;
        }
    }
}

// The properties a set gives values or transitions to.
static muiPropertyBits Reach(const muiPropertySet* set)
{
    muiPropertyBits reach = muiUnion(set->properties, set->tokens);
    for (uint32_t i = 0; i < set->bindingCount; i++)
    {
        reach = muiUnion(reach, set->bindings[i].properties);
    }
    return reach;
}

// Applies one variant of every class, in class order.
static void ApplyVariant(const muiContext* context, const Classes* classes, muiVariant variant,
                         muiPropertyBits free, Resolution* resolution)
{
    const muiStyleStore* store = &context->style;
    for (uint32_t i = 0; i < classes->count; i++)
    {
        uint32_t set = store->classes[classes->slots[i] - 1].sets[variant];
        if (set != 0)
        {
            ApplySet(context, &store->sets[set - 1], free, resolution);
        }
    }
}

// Applies the conditional values that hold, in class order and then
// condition order, sampling the node's last layout; records what was
// read and returns the run: which held, as one bit per condition with
// values in that order (folded past 64), and the size read.
static muiConditionRun ApplyConditions(muiContext* context, uint32_t slot, const Classes* classes,
                                       muiPropertyBits free, Resolution* resolution)
{
    const muiStyleStore* store = &context->style;
    muiLayoutNode* layout = &context->layout[slot - 1];
    const muiConditionSample sample = {
        .width = layout->rect.width,
        .height = layout->rect.height,
        .rtl = layout->rtl,
        .environment = &context->environment,
    };
    muiConditionRun run = {.width = sample.width, .height = sample.height};
    muiConditionReads reads = 0;
    uint32_t position = 0;
    for (uint32_t i = 0; i < classes->count; i++)
    {
        const muiStyleClass* class = &store->classes[classes->slots[i] - 1];
        for (uint32_t k = 0; k < class->conditionCount; k++)
        {
            uint32_t set = class->sets[mui_variantCondition0 + k];
            // A condition whose values and transitions reach no free
            // property changes nothing, so it reads nothing.
            if (set == 0 || !muiIntersects(Reach(&store->sets[set - 1]), free))
            {
                continue;
            }
            reads |= muiConditionReadsOf(&class->conditions[k]);
            if (muiConditionHolds(&class->conditions[k], &sample))
            {
                ApplySet(context, &store->sets[set - 1], free, resolution);
                run.outcome |= (uint64_t)1 << (position % 64);
            }
            position++;
        }
    }
    layout->conditionReads = reads;
    layout->conditionSize = (muiSize){sample.width, sample.height};
    layout->conditionRtl = sample.rtl;
    return run;
}

static bool IsSameSize(const muiConditionRun* a, const muiConditionRun* b)
{
    return a->width == b->width && a->height == b->height;
}

// Adds a styling to the node's history and reports an oscillation: the
// last four stylings, none the host's but perhaps the first, read two
// sizes in turn and their outcomes flipped with them. Its conditions
// are then held at this outcome (record mui-0004).
static void Watch(muiContext* context, uint32_t slot, const muiConditionRun* run)
{
    muiNodeStyle* node = &context->style.nodes[slot - 1];
    muiConditionRun* history = node->history;
    // A styling the layout asked for reads a size the one before did not,
    // so two in a row never read the same.
    if (node->historyCount == MUI_CONDITION_HISTORY && IsSameSize(run, &history[1]) &&
        IsSameSize(&history[0], &history[2]) && run->outcome != history[0].outcome)
    {
        muiLayoutNode* layout = &context->layout[slot - 1];
        layout->held = true;
        layout->heldSizes[0] = (muiSize){run->width, run->height};
        layout->heldSizes[1] = (muiSize){history[0].width, history[0].height};
        const muiNotification record = {
            .kind = mui_notificationOscillation,
            .nodeId = muiTreeIdOf(&context->tree, slot),
        };
        muiNotifyPost(&context->notifications, &record);
        return;
    }
    for (uint32_t i = MUI_CONDITION_HISTORY - 1; i > 0; i--)
    {
        history[i] = history[i - 1];
    }
    history[0] = *run;
    if (node->historyCount < MUI_CONDITION_HISTORY)
    {
        node->historyCount++;
    }
}

// Starts the node's history again when the host caused this styling,
// rather than its own layout: it edited the node, or a class, a node type
// or the environment. That also releases a hold.
static void NoteHostEdit(muiContext* context, uint32_t slot)
{
    muiNodeStyle* node = &context->style.nodes[slot - 1];
    bool edited = node->edited || node->editsSeen != context->styleEdits;
    node->edited = false;
    node->editsSeen = context->styleEdits;
    if (edited)
    {
        node->historyCount = 0;
        context->layout[slot - 1].held = false;
    }
}

static muiMotion MotionOf(muiContext* context)
{
    return (muiMotion){&context->animations, context->layout,     context->visual,
                       context->style.nodes, &context->tree,      context->text,
                       context->textRecords, context->interaction};
}

// A node's resolved values, layout's, visual's, text's and interaction's.
static muiValuesRef NodeValues(muiContext* context, uint32_t slot)
{
    return (muiValuesRef){&context->layout[slot - 1].style, &context->visual[slot - 1],
                          &context->text[slot - 1], &context->interaction[slot - 1]};
}

// Whether a node's property is to change: its new value differs from
// where it is going, the target of its running transition or its value.
static bool IsChange(const muiMotion* motion, uint32_t slot, const muiStyleValues* values,
                     muiProperty property)
{
    uint32_t record = muiFindAnimation(motion, slot, property);
    if (record == 0)
    {
        const muiConstValuesRef now = {&motion->nodes[slot - 1].style, &motion->visuals[slot - 1],
                                       &motion->texts[slot - 1], &motion->interactions[slot - 1]};
        return muiDoPropertiesDiffer(muiConstRefOf(values), now, muiPropertyOf(property));
    }
    float target[MUI_MAX_CHANNELS] = {0};
    uint32_t channels = muiPropertyChannels(muiConstRefOf(values), property, target);
    const muiAnimation* animation = &motion->store->records[record - 1];
    if (channels != animation->channels)
    {
        return true;
    }
    for (uint32_t i = 0; i < channels; i++)
    {
        if (target[i] != animation->to[i])
        {
            return true;
        }
    }
    return false;
}

// Moves a changed property with its transition when it has one that can
// move it; false when the change is to apply at once.
static bool Transition(muiContext* context, uint32_t slot, const Resolution* resolution,
                       muiProperty property, uint64_t nowNs)
{
    if (!muiHasProperty(resolution->named, property) || context->environment.reducedMotion ||
        !context->style.nodes[slot - 1].styled)
    {
        return false;
    }
    const muiTransitionSpec* spec =
        muiFindSpec(&context->animations, resolution->transitions[property]);
    if (spec == nullptr)
    {
        return false;
    }
    float current[MUI_MAX_CHANNELS] = {0};
    float target[MUI_MAX_CHANNELS] = {0};
    const muiConstValuesRef values = muiConstRefOf(&resolution->values);
    uint32_t from = muiPropertyChannels(muiConstRef(NodeValues(context, slot)), property, current);
    uint32_t to = muiPropertyChannels(values, property, target);
    muiMotion motion = MotionOf(context);
    return from != 0 && from == to &&
           muiStartAnimation(&motion, slot, property, values, spec, nowNs);
}

// Gives a node its resolved values: at once, or through the transitions
// the changes take. A layout change lays the node out again; a visual one
// only paints it again; whether a text one happened is returned.
static bool Commit(muiContext* context, uint32_t slot, const Resolution* resolution,
                   muiPropertyBits free, uint64_t nowNs)
{
    muiLayerKind layer = context->interaction[slot - 1].layer;
    muiLayoutNode* layout = &context->layout[slot - 1];
    const muiConstValuesRef values = muiConstRefOf(&resolution->values);
    muiPropertyBits changed = {0};
    if (!muiAnyProperty(resolution->named) && context->style.nodes[slot - 1].firstAnimation == 0)
    {
        const muiConstValuesRef now = muiConstRef(NodeValues(context, slot));
        const muiPropertyBits layoutFree =
            muiPropertiesOf(mui_groupLayout, free.words[mui_groupLayout]);
        const muiPropertyBits visualFree =
            muiPropertiesOf(mui_groupVisual, free.words[mui_groupVisual]);
        const muiPropertyBits textFree = muiPropertiesOf(mui_groupText, free.words[mui_groupText]);
        if (layoutFree.words[mui_groupLayout] != 0 &&
            muiDoPropertiesDiffer(values, now, layoutFree))
        {
            layout->style = resolution->values.layout;
            changed = muiUnion(changed, layoutFree);
        }
        if (visualFree.words[mui_groupVisual] != 0 &&
            muiDoPropertiesDiffer(values, now, visualFree))
        {
            context->visual[slot - 1] = resolution->values.visual;
            changed = muiUnion(changed, visualFree);
        }
        // Inheritance, after, finds what text changes reach.
        if (textFree.words[mui_groupText] != 0 && muiDoPropertiesDiffer(values, now, textFree))
        {
            context->text[slot - 1] = resolution->values.text;
            changed = muiUnion(changed, textFree);
        }
        const muiPropertyBits interactionFree =
            muiPropertiesOf(mui_groupInteraction, free.words[mui_groupInteraction]);
        if (interactionFree.words[mui_groupInteraction] != 0 &&
            muiDoPropertiesDiffer(values, now, interactionFree))
        {
            context->interaction[slot - 1] = resolution->values.interaction;
        }
    }
    else
    {
        // Each property reads and writes only its own field, so changes
        // apply in place.
        muiMotion motion = MotionOf(context);
        for (muiPropertyBits left = free; muiAnyProperty(left);)
        {
            muiProperty property = muiTakeProperty(&left);
            if (!IsChange(&motion, slot, &resolution->values, property) ||
                Transition(context, slot, resolution, property, nowNs))
            {
                continue;
            }
            muiStopAnimation(&motion, slot, property);
            muiApplyProperties(NodeValues(context, slot), values, muiPropertyOf(property));
            changed = muiUnion(changed, muiPropertyOf(property));
        }
    }
    if (changed.words[mui_groupLayout] != 0)
    {
        muiSyncLayoutNode(layout);
        muiTreeMarkLayout(&context->tree, slot);
    }
    if (changed.words[mui_groupVisual] != 0)
    {
        muiTreeMark(&context->tree, slot, mui_stagePaint);
    }
    muiNoteLayer(context, slot, layer);
    return changed.words[mui_groupText] != 0;
}

// Resolves the properties a node does not write directly; the direct
// ones already hold their values, which are carried over.
muiThemeScope muiScopeOf(const muiContext* context, uint32_t slot)
{
    muiThemeScope scope = {.count = 0};
    const muiNodeStyle* nodes = context->style.nodes;
    for (uint32_t at = nodes[slot - 1].scope; at != 0 && scope.count < MUI_MAX_THEME_DEPTH;)
    {
        muiThemeId theme = nodes[at - 1].theme;
        uint32_t found = muiPoolResolve(&context->themes.pool, theme.index1, theme.generation);
        if (found != 0)
        {
            scope.themes[scope.count++] = found;
        }
        uint32_t parent = muiTreeAt(&context->tree, at)->links.parent;
        at = parent != 0 ? nodes[parent - 1].scope : 0;
    }
    return scope;
}

// Finds the nearest node, itself or above, whose theme lives; a change
// reaches its children, which the style pass visits after it.
static void UpdateScope(muiContext* context, uint32_t slot)
{
    muiNodeStyle* nodes = context->style.nodes;
    muiThemeId theme = nodes[slot - 1].theme;
    uint32_t parent = muiTreeAt(&context->tree, slot)->links.parent;
    uint32_t scope = 0;
    if (muiPoolResolve(&context->themes.pool, theme.index1, theme.generation) != 0)
    {
        scope = slot;
    }
    else if (parent != 0)
    {
        scope = nodes[parent - 1].scope;
    }
    if (scope == nodes[slot - 1].scope)
    {
        return;
    }
    nodes[slot - 1].scope = scope;
    for (uint32_t child = muiTreeAt(&context->tree, slot)->links.firstChild; child != 0;
         child = muiTreeAt(&context->tree, child)->links.next)
    {
        muiTreeMark(&context->tree, child, mui_stageStyle);
    }
}

// Recomputes a node's text with what its layers and direct writes give
// it, which reaches its inheriting children, when its own text values,
// what is given or its parent changed; a change above reaches it from
// there.
static void Inherit(muiContext* context, uint32_t slot, muiPropertyMask given, bool changed)
{
    if (!context->textGiven)
    {
        return;
    }
    const muiTextNodes text = {&context->tree, context->layout, context->text,
                               context->textRecords};
    if (!changed && !muiIsTextRecordStale(&text, slot, given))
    {
        return;
    }
    context->textRecords[slot - 1].given = given;
    muiInheritText(&text, slot);
}

static void Resolve(muiContext* context, uint32_t slot, uint64_t nowNs)
{
    NoteHostEdit(context, slot);
    UpdateScope(context, slot);
    const muiStyleStore* store = &context->style;
    const muiNodeStyle* node = &store->nodes[slot - 1];
    muiPropertyBits free = muiWithout(store->reach, node->direct);
    muiLayoutNode* layout = &context->layout[slot - 1];
    layout->conditionReads = 0;
    if (!muiAnyProperty(free))
    {
        context->style.nodes[slot - 1].styled = true;
        Inherit(context, slot, node->direct.words[mui_groupText], false);
        return;
    }
    Resolution resolution;
    // Commit copies a whole struct whose free properties changed, so each
    // one that has any carries the node's direct writes; visual and text
    // values, and interaction ones, are filled only when some are free.
    muiPropertyBits carried = muiPropertiesOf(mui_groupLayout, MUI_LAYOUT_PROPERTIES);
    resolution.values.layout = *muiLayoutDefaults();
    if (free.words[mui_groupVisual] != 0)
    {
        resolution.values.visual = *muiVisualDefaults();
        carried.words[mui_groupVisual] = MUI_VISUAL_PROPERTIES;
    }
    if (free.words[mui_groupText] != 0)
    {
        resolution.values.text = *muiTextDefaults();
        carried.words[mui_groupText] = MUI_TEXT_PROPERTIES;
    }
    if (free.words[mui_groupInteraction] != 0)
    {
        resolution.values.interaction = *muiInteractionDefaults();
        carried.words[mui_groupInteraction] = MUI_INTERACTION_PROPERTIES;
    }
    resolution.named = (muiPropertyBits){0};
    resolution.given = (muiPropertyBits){0};
    resolution.scope = muiScopeOf(context, slot);
    muiApplyProperties(muiRefOf(&resolution.values), muiConstRef(NodeValues(context, slot)),
                       muiIntersection(node->direct, carried));
    Classes classes = ClassesOf(store, node);
    ApplyVariant(context, &classes, mui_variantBase, free, &resolution);
    for (uint32_t v = mui_variantChecked; v < mui_variantCondition0; v++)
    {
        // Variant v belongs to state bit v - 1.
        if ((node->states & (1u << (v - 1))) != 0)
        {
            ApplyVariant(context, &classes, (muiVariant)v, free, &resolution);
        }
    }
    muiConditionRun run = ApplyConditions(context, slot, &classes, free, &resolution);
    if (layout->conditionReads != 0)
    {
        Watch(context, slot, &run);
    }
    bool textChanged = Commit(context, slot, &resolution, free, nowNs);
    context->style.nodes[slot - 1].styled = true;
    Inherit(context, slot,
            resolution.given.words[mui_groupText] | node->direct.words[mui_groupText], textChanged);
}

void muiRestyle(muiContext* context, uint32_t root, uint64_t nowNs)
{
    muiTree* tree = &context->tree;
    for (uint32_t at = muiTreeNextOwing(tree, root, 0, mui_stageStyle); at != 0;
         at = muiTreeNextOwing(tree, root, at, mui_stageStyle))
    {
        if ((muiTreeAt(tree, at)->dirty.request & mui_stageStyle) != 0)
        {
            Resolve(context, at, nowNs);
        }
    }
    muiTreeSweep(tree, root, mui_stageStyle);
}
