#pragma once

// What Studio's rows and buttons ask of the session (D435): an edited
// field and what it is, a button's operation, and a named command.

#include "rawframe/ui/tree.h"
#include "records.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rawframe::studio {

/// A field Studio edits and what the session needs to act on it: a
/// component's field (its kind the read value's key), the chosen entity's
/// name, the name of a component to add, or a part of the scene's view.
struct FieldRow {
    enum class Role : std::uint8_t {
        Field,
        Name,
        Add,
        View,
        /// The scene an instance placed in the chosen scene is of.
        Instance,
        /// The path of a new scene (D449).
        NewScene,
        /// What the entity column shows of the scene (D473).
        Find,
        /// A part of a style class's look: `component` its variant, `field`
        /// the part (D637); with no variant, the class's transitions
        /// (D639).
        Part,
        /// A field of a UI document's node (D643).
        NodeField,
        /// A material instance's override of its base's input or switch
        /// (D722).
        Override,
        /// A material graph's node's param, `field` its name (D730).
        GraphParam,
        /// A material graph's node's input, `field` its name (D730).
        GraphInput,
        /// The command palette's search (D777).
        Palette,
        /// The start screen's new game's name, and a game to open (D781).
        NewGame,
        OpenGame,
        /// A command's keys, `field` its name, and the editor's command, in
        /// the Preferences dialog (D782).
        Shortcut,
        Editor,
        /// A chosen source's import setting, `field` its name (D800).
        ImportSetting,
        /// A data document's value, `field` its path, `kind` "string" for
        /// text or "json" (D812).
        DataValue,
        /// A line of the game's description, `field` its place, `kind` the
        /// word it begins with (D818).
        SettingLine,
        /// A line to put in the game's description, `field` the place it
        /// goes before (D818).
        NewSetting,
        /// What follows a word of the chosen blockout solid's line, `field`
        /// the word, or `line` for the whole line (D850).
        SolidWord,
        /// A solid's line to put in a blockout, `field` its place (D850).
        NewSolid
    };
    ui::Node value{};
    Role role = Role::Field;
    std::string component;
    std::string field;
    std::string kind;
    /// The catalog's field, its hints with it (D784).
    Catalog::Field type{};
};

/// A button on a component or a field and the operation it asks: Remove,
/// or an instance's Revert of a component or a field.
struct ActionButton {
    ui::Node node{};
    std::string operation;
    std::string component;
    /// The field, for a field's.
    std::string field;
    /// What the header says once it is done.
    std::string done;
    /// The scene's operation, naming no entity: a component's remark.
    bool whole = false;
    /// A node of `component` made, not an operation: a UI node (D672), or
    /// a material graph's node of that type (D731).
    bool makesNode = false;
    /// An output of the chosen graph node, `field` its name: the input
    /// field pressed next, on this node or one chosen first, takes a
    /// connection from it (D733).
    bool connects = false;
};

/// A named action (SPEC-0051 Part 1, D777): its menu's row, its palette
/// entry, and its chords do it as its button's press does.
struct Command {
    std::string name;
    std::string menu;
    /// Its chords as `view::Typing` names them, the first shown beside it.
    std::vector<std::string> keys;
    ui::Node button{};
};

} // namespace rawframe::studio
