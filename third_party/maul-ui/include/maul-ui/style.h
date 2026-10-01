// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Style: classes of typed property values, the node types and node
// states that pick them, and a node's direct writes (record mui-0004).
// A node's values resolve in fixed layers, each later one winning: the
// defaults, every class's base values in order, the state variants (the
// states in the order of muiState, weakest first, and the classes in
// order within each), the conditions that hold (classes in order, then
// each class's conditions in order), and the node's direct writes. A
// node's classes are its type's, then its own.

#ifndef MAUL_UI_STYLE_H
#define MAUL_UI_STYLE_H

#include "maul-ui/base.h"
#include "maul-ui/context.h"
#include "maul-ui/layout.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // A style class, in the shape of every id (family record 0016).
    typedef struct muiStyleId
    {
        uint32_t index1;
        uint32_t generation;
    } muiStyleId;

    // A node type: an ordered list of classes that every node of the type
    // takes before its own.
    typedef struct muiNodeTypeId
    {
        uint32_t index1;
        uint32_t generation;
    } muiNodeTypeId;

    // One value a style can set, named after the muiLayoutStyle,
    // muiVisualStyle or muiTextStyle field it sets. Ids come in groups of
    // 64, one group per values struct: layout from 0, visual from 64,
    // text from 128, and interaction (from 192) to come.
    typedef uint8_t muiProperty;

    enum
    {
        // Dimensions.
        mui_propertyWidth = 0,
        mui_propertyHeight = 1,
        mui_propertyMinWidth = 2,
        mui_propertyMinHeight = 3,
        mui_propertyMaxWidth = 4,
        mui_propertyMaxHeight = 5,
        // A number of 0 or more.
        mui_propertyAspectRatio = 6,
        // Enumerators of the layout types.
        mui_propertyFlexDirection = 7,
        mui_propertyFlexWrap = 8,
        mui_propertyJustify = 9,
        mui_propertyAlignItems = 10,
        mui_propertyAlignContent = 11,
        // Numbers of 0 or more.
        mui_propertyRowGap = 12,
        mui_propertyColumnGap = 13,
        mui_propertyGrow = 14,
        mui_propertyShrink = 15,
        // A dimension.
        mui_propertyBasis = 16,
        // An enumerator.
        mui_propertyAlignSelf = 17,
        // Finite numbers.
        mui_propertyMarginStart = 18,
        mui_propertyMarginEnd = 19,
        mui_propertyMarginTop = 20,
        mui_propertyMarginBottom = 21,
        // An enumerator: muiEdgeMask bits.
        mui_propertyMarginAuto = 22,
        // Numbers of 0 or more.
        mui_propertyBorderStart = 23,
        mui_propertyBorderEnd = 24,
        mui_propertyBorderTop = 25,
        mui_propertyBorderBottom = 26,
        mui_propertyPaddingStart = 27,
        mui_propertyPaddingEnd = 28,
        mui_propertyPaddingTop = 29,
        mui_propertyPaddingBottom = 30,
        // An enumerator.
        mui_propertyPosition = 31,
        // Dimensions.
        mui_propertyInsetStart = 32,
        mui_propertyInsetEnd = 33,
        mui_propertyInsetTop = 34,
        mui_propertyInsetBottom = 35,
        // Numbers from 0 to 1.
        mui_propertyAnchorX = 36,
        mui_propertyAnchorY = 37,
        // Enumerators.
        mui_propertyTextDirection = 38,
        mui_propertyContent = 39,
        // Visual properties, named after the muiVisualStyle field they set
        // (maul-ui/visual.h). Colors.
        mui_propertyBackground = 64,
        mui_propertyGradient = 65,
        // Dimensions.
        mui_propertyRadiusTopStart = 66,
        mui_propertyRadiusTopEnd = 67,
        mui_propertyRadiusBottomEnd = 68,
        mui_propertyRadiusBottomStart = 69,
        // Colors.
        mui_propertyBorderColorStart = 70,
        mui_propertyBorderColorEnd = 71,
        mui_propertyBorderColorTop = 72,
        mui_propertyBorderColorBottom = 73,
        // Shadows.
        mui_propertyOuterShadow = 74,
        mui_propertyInnerShadow = 75,
        // A host key, insets and a color.
        mui_propertyImage = 76,
        mui_propertyImageSlice = 77,
        mui_propertyImageTint = 78,
        // A number from 0 to 1, and a flag.
        mui_propertyOpacity = 79,
        mui_propertyClip = 80,
        // Text properties, named after the muiTextStyle field they set
        // (maul-ui/text_style.h), inherited. A color and a font key.
        mui_propertyTextColor = 128,
        mui_propertyFont = 129,
        // Dimensions: the size, the line height and the letter spacing.
        mui_propertyFontSize = 130,
        mui_propertyLineHeight = 131,
        mui_propertyLetterSpacing = 132,
        // A number from 1 to 1000, then enumerators.
        mui_propertyFontWeight = 133,
        mui_propertyFontSlant = 134,
        mui_propertyTextAlign = 135,
        mui_propertyTextWrap = 136,
    };

    // A group of properties: those of one values struct.
    typedef uint8_t muiPropertyGroup;

    enum
    {
        mui_groupLayout = 0,
        mui_groupVisual = 1,
        mui_groupText = 2,
    };

    // A set of properties of one group, one bit each.
    typedef uint64_t muiPropertyMask;

// A property's group, and its bit in its group's mask.
#define MUI_PROPERTY_GROUP(property) ((muiPropertyGroup)((property) >> 6))
#define MUI_PROPERTY_BIT(property)   ((muiPropertyMask)1 << ((property) & 63))
// Every layout, visual and text property, in their groups' masks.
#define MUI_LAYOUT_PROPERTIES ((muiPropertyMask)0xFFFFFFFFFFull)
#define MUI_VISUAL_PROPERTIES ((muiPropertyMask)0x1FFFFull)
#define MUI_TEXT_PROPERTIES   ((muiPropertyMask)0x1FFull)

    // The states a node can be in, as bits, weakest first: a later
    // state's variant wins over an earlier one's.
    typedef uint8_t muiState;

    enum
    {
        mui_stateChecked = 1,
        mui_stateSelected = 2,
        mui_stateFocused = 4,
        mui_stateHovered = 8,
        mui_statePressed = 16,
        mui_stateDisabled = 32,
        mui_stateExiting = 64,
    };

    // Which values of a class a call reads or writes: its base values, the
    // variant one state brings, or the values of one of its conditions.
    typedef uint8_t muiVariant;

    enum
    {
        mui_variantBase = 0,
        mui_variantChecked = 1,
        mui_variantSelected = 2,
        mui_variantFocused = 3,
        mui_variantHovered = 4,
        mui_variantPressed = 5,
        mui_variantDisabled = 6,
        mui_variantExiting = 7,
        // The values of the class's first condition; condition i has
        // mui_variantCondition0 + i.
        mui_variantCondition0 = 8,
    };

    enum
    {
        // The classes a node or a node type lists.
        MUI_MAX_CLASSES = 8,
        // The conditions of one class.
        MUI_MAX_CONDITIONS = 8,
    };

    // The size of the screen or window a UI is shown on, as the host
    // classes it, as bits.
    typedef uint8_t muiViewportClass;

    enum
    {
        // Most phones and tablets.
        mui_viewportSmall = 1,
        // Most laptops and monitors.
        mui_viewportMedium = 2,
        // Most televisions and larger.
        mui_viewportLarge = 4,
    };

    // How the user is controlling the UI, as bits.
    typedef uint8_t muiInputModality;

    enum
    {
        // A mouse, touchpad or pen with a keyboard.
        mui_inputPointer = 1,
        mui_inputTouch = 2,
        mui_inputGamepad = 4,
    };

    // What a condition asks of the reduced-motion setting.
    typedef uint8_t muiMotionMatch;

    enum
    {
        mui_motionAny = 0,
        // Holds when motion is not reduced.
        mui_motionFull = 1,
        mui_motionReduced = 2,
    };

    // What a condition asks of a node's resolved text direction.
    typedef uint8_t muiDirectionMatch;

    enum
    {
        mui_directionAny = 0,
        mui_directionLeftToRight = 1,
        mui_directionRightToLeft = 2,
    };

    // Values from min up to, but not including, max; max may be infinite.
    typedef struct muiRange
    {
        float min;
        float max;
    } muiRange;

    // When a class's conditional values apply: when every clause holds. A
    // clause at its default (the range [0, inf), every bit, any) holds
    // always and reads nothing. Build it with muiDefaultCondition.
    typedef struct muiCondition
    {
        // The node's border box from its last layout. Aspect is width over
        // height: infinite for a zero height, 0 for an empty box.
        muiRange width;
        muiRange height;
        muiRange aspect;
        // The environment's text scale.
        muiRange textScale;
        // The viewport classes and input modalities it holds for.
        muiViewportClass viewports;
        muiInputModality inputs;
        muiMotionMatch motion;
        // The node's resolved direction from its last layout.
        muiDirectionMatch direction;
    } muiCondition;

    // What the host tells the context about where its UI is shown.
    typedef struct muiEnvironment
    {
        // One viewport class bit.
        muiViewportClass viewport;
        // One input modality bit.
        muiInputModality input;
        // The user's text size over the default, more than 0.
        float textScale;
        bool reducedMotion;
    } muiEnvironment;

    /// Returns the condition that always holds.
    ///
    /// @return The condition.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiCondition muiDefaultCondition(void);

    /// Returns the environment of a new context: a medium viewport, a
    /// pointer, a text scale of 1 and full motion.
    ///
    /// @return The environment.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiEnvironment muiDefaultEnvironment(void);

    /// Sets the environment conditions read. Every node is styled again at
    /// the next muiComputeLayout when it changes.
    ///
    /// @param context      The context.
    /// @param environment  One viewport class bit, one input modality bit
    ///                     and a finite text scale above 0.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, a
    ///         value outside the above or a call from a measure or paint function.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiSetContextEnvironment(muiContext* context,
                                                             const muiEnvironment* environment);

    /// Returns the environment conditions read.
    ///
    /// @param context  The context.
    /// @return The environment; muiDefaultEnvironment's for a NULL context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_API muiEnvironment muiGetContextEnvironment(const muiContext* context);

    /// Creates a style class with no values set.
    ///
    /// @param context     The context.
    /// @param styleIdOut  Receives the class; set to the null id on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or a
    ///         call from a measure or paint function; `mui_errorCapacity` when the
    ///         context's style limit is reached.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiCreateStyle(muiContext* context, muiStyleId* styleIdOut);

    /// Destroys a style class. Nodes and node types that list it skip it,
    /// and every node is styled again at the next muiComputeLayout.
    ///
    /// @param context  The context.
    /// @param styleId  The class.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null id or a call from a measure or paint function; `mui_errorStale`
    ///         for an id whose class is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiDestroyStyle(muiContext* context, muiStyleId styleId);

    /// Sets layout properties in one variant of a class from the fields of
    /// values. Every node is styled again at the next muiComputeLayout.
    ///
    /// @param context  The context.
    /// @param styleId  The class.
    /// @param variant  The variant.
    /// @param values   The values; only the fields mask names are read, and
    ///                 each must be one muiNode_SetLayoutStyle allows.
    /// @param mask     The properties, within MUI_LAYOUT_PROPERTIES.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id, an unknown variant or property bit, a value outside
    ///         the above, a property the variant's condition reads (its
    ///         axis's sizes and limits, the aspect ratio, the text
    ///         direction) or a call from a measure or paint function, which changes
    ///         nothing; `mui_errorStale` for an id whose class is gone;
    ///         `mui_errorCapacity` when the variant had no values and the
    ///         context's limit of property sets is reached.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiStyle_SetLayoutValues(muiContext* context,
                                                             muiStyleId styleId, muiVariant variant,
                                                             const muiLayoutStyle* values,
                                                             muiPropertyMask mask);

    /// Unsets properties in one variant of a class: their values and the
    /// tokens named for them. Every node is styled again at the next
    /// muiComputeLayout.
    ///
    /// @param context  The context.
    /// @param styleId  The class.
    /// @param variant  The variant.
    /// @param group    The properties' group.
    /// @param mask     The properties, within the group's.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null id, an unknown variant, group or property bit or a call
    ///         from a measure or paint function; `mui_errorStale` for an id whose
    ///         class is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiStyle_ResetProperties(muiContext* context,
                                                             muiStyleId styleId, muiVariant variant,
                                                             muiPropertyGroup group,
                                                             muiPropertyMask mask);

    /// Reads the values one variant of a class sets.
    ///
    /// @param context    The context.
    /// @param styleId    The class.
    /// @param variant    The variant.
    /// @param valuesOut  Receives the set values, and muiDefaultLayoutStyle's
    ///                   for the rest.
    /// @param maskOut    Receives which properties are set.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id or an unknown variant; `mui_errorStale` for an id
    ///         whose class is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiStyle_GetLayoutValues(const muiContext* context,
                                                             muiStyleId styleId, muiVariant variant,
                                                             muiLayoutStyle* valuesOut,
                                                             muiPropertyMask* maskOut);

    /// Adds a condition to a class, after its others. Its values are then
    /// set through the variant it gives. Every node is styled again at the
    /// next muiComputeLayout.
    ///
    /// @param context     The context.
    /// @param styleId     The class.
    /// @param condition   Ranges with a finite minimum of 0 or more and a
    ///                    maximum not below it, known bits and choices.
    /// @param variantOut  Receives the condition's variant; set to
    ///                    mui_variantBase on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id, a condition outside the above or a call from a
    ///         measure or paint function; `mui_errorStale` for an id whose class is
    ///         gone; `mui_errorCapacity` when the class has
    ///         MUI_MAX_CONDITIONS.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiStyle_AddCondition(muiContext* context, muiStyleId styleId,
                                                          const muiCondition* condition,
                                                          muiVariant* variantOut);

    /// Replaces one of a class's conditions, keeping its values. Every node
    /// is styled again at the next muiComputeLayout.
    ///
    /// @param context    The context.
    /// @param styleId    The class.
    /// @param variant    The condition's variant.
    /// @param condition  As muiStyle_AddCondition takes it, reading nothing
    ///                   the condition's values set.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id, a variant that is not one of the class's
    ///         conditions, a condition outside the above or a call from a
    ///         measure or paint function; `mui_errorStale` for an id whose class is
    ///         gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiStyle_SetCondition(muiContext* context, muiStyleId styleId,
                                                          muiVariant variant,
                                                          const muiCondition* condition);

    /// Reads one of a class's conditions.
    ///
    /// @param context       The context.
    /// @param styleId       The class.
    /// @param variant       The condition's variant.
    /// @param conditionOut  Receives the condition.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id or a variant that is not one of the class's
    ///         conditions; `mui_errorStale` for an id whose class is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiStyle_GetCondition(const muiContext* context,
                                                          muiStyleId styleId, muiVariant variant,
                                                          muiCondition* conditionOut);

    /// Removes every condition of a class and its values. Every node is
    /// styled again at the next muiComputeLayout.
    ///
    /// @param context  The context.
    /// @param styleId  The class.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null id or a call from a measure or paint function; `mui_errorStale`
    ///         for an id whose class is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiStyle_ClearConditions(muiContext* context,
                                                             muiStyleId styleId);

    /// Creates a node type with an ordered list of classes.
    ///
    /// @param context    The context.
    /// @param classes    count classes, kept as given; a class destroyed
    ///                   later is skipped. NULL when count is 0.
    /// @param count      At most MUI_MAX_CLASSES.
    /// @param typeIdOut  Receives the type; set to the null id on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, a count
    ///         over the limit or a call from a measure or paint function;
    ///         `mui_errorCapacity` when the context's node type limit is
    ///         reached.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiCreateNodeType(muiContext* context,
                                                      const muiStyleId* classes, uint32_t count,
                                                      muiNodeTypeId* typeIdOut);

    /// Destroys a node type. Its nodes are left with no type, and every
    /// node is styled again at the next muiComputeLayout.
    ///
    /// @param context  The context.
    /// @param typeId   The type.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null id or a call from a measure or paint function; `mui_errorStale`
    ///         for an id whose type is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiDestroyNodeType(muiContext* context, muiNodeTypeId typeId);

    /// Replaces a node type's classes. Every node is styled again at the
    /// next muiComputeLayout.
    ///
    /// @param context  The context.
    /// @param typeId   The type.
    /// @param classes  count classes, as muiCreateNodeType takes them.
    /// @param count    At most MUI_MAX_CLASSES.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null id, NULL classes with a count, a count over the limit or
    ///         a call from a measure or paint function; `mui_errorStale` for an id
    ///         whose type is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNodeType_SetClasses(muiContext* context,
                                                           muiNodeTypeId typeId,
                                                           const muiStyleId* classes,
                                                           uint32_t count);

    /// Sets a node's type. The node is styled again at the next
    /// muiComputeLayout.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param typeId   The type; the null id for none.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null node id or a call from a measure or paint function;
    ///         `mui_errorStale` for a node or a type that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetType(muiContext* context, muiNodeId nodeId,
                                                    muiNodeTypeId typeId);

    /// Replaces a node's own classes, which follow its type's. The node is
    /// styled again at the next muiComputeLayout.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param classes  count classes, kept as given; a class destroyed later
    ///                 is skipped. NULL when count is 0.
    /// @param count    At most MUI_MAX_CLASSES.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null id, NULL classes with a count, a count over the limit or
    ///         a call from a measure or paint function; `mui_errorStale` for a node
    ///         that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetClasses(muiContext* context, muiNodeId nodeId,
                                                       const muiStyleId* classes, uint32_t count);

    /// Sets the states a node is in. The node is styled again at the next
    /// muiComputeLayout when they change.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param states   muiState bits.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null id, unknown bits or a call from a measure or paint function;
    ///         `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetStates(muiContext* context, muiNodeId nodeId,
                                                      muiState states);

    /// Returns the states a node is in.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @return Its muiState bits; 0 for a stale id or a NULL context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_API muiState muiNode_GetStates(const muiContext* context, muiNodeId nodeId);

    /// Writes layout properties of a node directly from the fields of
    /// values: they win over every class until reset, at once. The node and
    /// its parent are laid out again at the next muiComputeLayout.
    /// muiNode_SetLayoutStyle writes every layout property.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param values   The values; only the fields mask names are read, and
    ///                 each must be one muiNode_SetLayoutStyle allows.
    /// @param mask     The properties, within MUI_LAYOUT_PROPERTIES.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id, an unknown property bit, a value outside the above
    ///         or a call from a measure or paint function, which changes nothing;
    ///         `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetLayoutValues(muiContext* context, muiNodeId nodeId,
                                                            const muiLayoutStyle* values,
                                                            muiPropertyMask mask);

    /// Ends direct writes of a node's properties: they take their classes'
    /// values again at the next muiComputeLayout.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param group    The properties' group.
    /// @param mask     The properties, within the group's.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null id, an unknown group or property bit or a call from a
    ///         measure or paint function; `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_ResetProperties(muiContext* context, muiNodeId nodeId,
                                                            muiPropertyGroup group,
                                                            muiPropertyMask mask);

    /// Returns which properties of a group a node writes directly.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param group    The group.
    /// @return The properties; 0 for a stale id, a NULL context or an
    ///         unknown group.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_API muiPropertyMask muiNode_GetDirectProperties(const muiContext* context, muiNodeId nodeId,
                                                        muiPropertyGroup group);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_STYLE_H
