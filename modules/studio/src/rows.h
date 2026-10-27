#pragma once

// What Studio's rows and buttons ask of the session: an edited
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
        /// The path of a new scene.
        NewScene,
        /// What the entity column shows of the scene.
        Find,
        /// A part of a style class's look: `component` its variant, `field`
        /// the part; with no variant, the class's transitions.
        Part,
        /// A field of a UI document's node.
        NodeField,
        /// A material instance's override of its base's input or switch.
        Override,
        /// A material graph's node's param, `field` its name.
        GraphParam,
        /// A material graph's node's input, `field` its name.
        GraphInput,
        /// The command palette's search.
        Palette,
        /// A pose's turn about an axis in degrees, `field` x, y, or z, its
        /// quaternion set whole.
        Turn,
        /// The start screen's new game's name, and a game to open.
        NewGame,
        OpenGame,
        /// A command's keys, `field` its name, and the editor's command, in
        /// the Preferences dialog.
        Shortcut,
        Editor,
        /// A chosen source's import setting, `field` its name.
        ImportSetting,
        /// A new folder's name in Assets, and the name the target of Assets
        /// is renamed to.
        NewFolder,
        RenameFile,
        /// A sequence key's time, track, animator, or event name, `field`
        /// its place among the keys, `kind` which.
        SequenceKey,
        /// A data document's value, `field` its path, `kind` "string" for
        /// text or "json".
        DataValue,
        /// A line of the game's description, `field` its place, `kind` the
        /// word it begins with.
        SettingLine,
        /// A line to put in the game's description, `field` the place it
        /// goes before.
        NewSetting,
        /// What follows a word of the chosen blockout solid's line, `field`
        /// the word, or `line` for the whole line.
        SolidWord,
        /// A solid's line to put in a blockout, `field` its place.
        NewSolid,
        /// The terrain brush's tool, radius, or amount, `field` which.
        BrushSetting,
        /// The tile a tilemap's block pressed paints.
        TileSetting,
        /// The message a commit of the game's changes is made with.
        CommitMessage,
        /// The Export dialog's output, title, or address, `field` which.
        ExportSetting,
        /// The Assets search, what the grid's names hold.
        AssetSearch,
        /// The Console's filter, what its lines shown hold.
        ConsoleSearch,
        /// The start screen's search of the games opened last, or a path
        /// to open, and the new game's folder.
        GameSearch,
        GameFolder
    };
    ui::Node value{};
    Role role = Role::Field;
    std::string component;
    std::string field;
    std::string kind;
    /// The catalog's field, its hints with it.
    Catalog::Field type{};
    /// The words a field shows while empty, read as nothing typed.
    std::string hint;
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
    /// A node of `component` made, not an operation: a UI node, or
    /// a material graph's node of that type.
    bool makesNode = false;
    /// An output of the chosen graph node, `field` its name: the input
    /// field pressed next, on this node or one chosen first, takes a
    /// connection from it.
    bool connects = false;
    /// The game's document `opens` opened in its place: a material
    /// instance's base.
    std::string opens;
};

/// A named action: its menu's row, its palette
/// entry, and its chords do it as its button's press does.
struct Command {
    std::string name;
    std::string menu;
    /// Its chords as `view::Typing` names them, the first shown beside it.
    std::vector<std::string> keys;
    ui::Node button{};
};

} // namespace rawframe::studio
