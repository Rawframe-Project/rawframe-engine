#include "shell.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::studio {

namespace {

/// `path` with `from` at its start put as `to`, where it is `from` or under
/// it; none where it is not, and empty where `to` is, the file gone.
std::optional<std::string> movedPath(const std::string& path, const std::string& from, const std::string& to) {
    if (path == from) {
        return to;
    }
    if (path.size() > from.size() && path.starts_with(from) && path[from.size()] == '/') {
        return to.empty() ? std::string{} : to + path.substr(from.size());
    }
    return std::nullopt;
}

/// The file name at the end of `path`.
std::string leafOf(std::string_view path) {
    const std::size_t kSlash = path.rfind('/');
    return std::string{kSlash == std::string_view::npos ? path : path.substr(kSlash + 1)};
}

/// The folder `path` is in, with its slash; empty at the game's root.
std::string folderOf(std::string_view path) {
    const std::size_t kSlash = path.rfind('/');
    return kSlash == std::string_view::npos ? std::string{} : std::string{path.substr(0, kSlash + 1)};
}

/// The files a move's or a removal's answer says still name the old path.
std::vector<std::string> namedIn(std::string_view reply) {
    std::vector<std::string> made;
    const auto kParsed = document::parse(reply);
    const Value* kAnswer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
    const Value* kNamed = kAnswer != nullptr ? kAnswer->find("named") : nullptr;
    for (const Value& kEach : kNamed != nullptr ? kNamed->items() : std::span<const Value>{}) {
        if (kEach.kind() == Value::Kind::String) {
            made.push_back(*kEach.text());
        }
    }
    return made;
}

} // namespace

std::vector<std::string> foldersOf(std::string_view reply) {
    std::vector<std::string> made;
    const auto kParsed = document::parse(reply);
    const Value* kAnswer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
    const Value* kFolders = kAnswer != nullptr ? kAnswer->find("folders") : nullptr;
    for (const Value& kEach : kFolders != nullptr ? kFolders->items() : std::span<const Value>{}) {
        if (kEach.kind() == Value::Kind::String) {
            made.push_back(*kEach.text());
        }
    }
    return made;
}

std::string ShellParticipant::renamedFrom() const {
    // A file's name without its extension, which a rename keeps; a
    // folder's whole.
    const std::string kLeaf = leafOf(browser_.target);
    if (std::ranges::contains(browser_.folderList, browser_.target)) {
        return kLeaf;
    }
    const std::filesystem::path kPath{kLeaf};
    return kPath.has_extension() ? kPath.stem().string() : kLeaf;
}

void ShellParticipant::beginFolder() {
    browser_.typing = AssetBrowser::Typing::Folder;
    browser_.removing.clear();
    static_cast<void>(showScenes());
    beginEdit(FieldRow{
        .value = browser_.typed, .role = FieldRow::Role::NewFolder, .field = "new folder", .hint = "Folder name"});
}

void ShellParticipant::beginRename() {
    if (browser_.target.empty()) {
        say("choose a file or a folder in Assets to rename it");
        return;
    }
    browser_.typing = AssetBrowser::Typing::Rename;
    browser_.removing.clear();
    static_cast<void>(showScenes());
    beginEdit(FieldRow{.value = browser_.typed, .role = FieldRow::Role::RenameFile, .field = "rename"});
}

void ShellParticipant::madeFolder(const std::string& typed) {
    browser_.typing = AssetBrowser::Typing::None;
    if (typed.empty()) {
        static_cast<void>(showScenes());
        return;
    }
    // In the folder shown, as an editor's browser makes one.
    const std::string kFolder = (browser_.folder.has_value() ? *browser_.folder + "/" : std::string{}) + typed;
    const Answered kMade = answeredOf(ask(makeFolderRecord(next(), kFolder)));
    if (!kMade.done) {
        ++refused_;
        say(kMade.message);
        static_cast<void>(showScenes());
        return;
    }
    ++applied_;
    ++browser_.foldersMade;
    refreshFiles(std::string{}, std::string{});
    browser_.target = kFolder;
    say(kFolder + "/ made");
    logFiles("folder", kFolder, {}, 0);
}

void ShellParticipant::renamed(const std::string& typed) {
    browser_.typing = AssetBrowser::Typing::None;
    if (typed.empty() || typed == leafOf(browser_.target) || browser_.target.empty()) {
        static_cast<void>(showScenes());
        return;
    }
    // Renamed where it is, a file keeping its kind's extension as an
    // editor's browser does; a name with a slash in it moves it under the
    // game's directory, as typed.
    std::string name = typed;
    const std::string kExtension = std::filesystem::path{browser_.target}.extension().string();
    if (!std::ranges::contains(browser_.folderList, browser_.target) && !kExtension.empty() &&
        !name.ends_with(kExtension)) {
        name += kExtension;
    }
    moveFile(browser_.target, name.find('/') == std::string::npos ? folderOf(browser_.target) + name : name);
}

void ShellParticipant::moveFile(const std::string& asked, const std::string& onto) {
    // Copies: the target followed in the lists after is what `asked` may
    // name.
    const std::string from = asked;
    const std::string to = onto;
    const std::string kReply = ask(moveFileRecord(next(), from, to));
    const Answered kMoved = answeredOf(kReply);
    if (!kMoved.done) {
        ++refused_;
        say(kMoved.message);
        static_cast<void>(showScenes());
        return;
    }
    ++applied_;
    ++browser_.moved;
    const std::vector<std::string> kNamed = namedIn(kReply);
    refreshFiles(from, to);
    say((folderOf(from) == folderOf(to) ? "renamed " : "moved ") + from + " to " + to +
        (kNamed.empty() ? std::string{}
                        : "; still named by its old path in " + std::to_string(kNamed.size()) +
                              (kNamed.size() == 1 ? " file: " : " files, first ") + kNamed.front()));
    logFiles("move", from, to, kNamed.size());
}

void ShellParticipant::removeAsked(bool sure) {
    if (!sure) {
        browser_.removing.clear();
        static_cast<void>(showScenes());
        say("kept");
        return;
    }
    const std::string kPath = std::exchange(browser_.removing, std::string{});
    const std::string kReply = ask(removeFileRecord(next(), kPath));
    const Answered kRemoved = answeredOf(kReply);
    if (!kRemoved.done) {
        ++refused_;
        say(kRemoved.message);
        static_cast<void>(showScenes());
        return;
    }
    ++applied_;
    ++browser_.removed;
    const std::vector<std::string> kNamed = namedIn(kReply);
    refreshFiles(kPath, std::string{});
    say("deleted " + kPath +
        (kNamed.empty() ? std::string{}
                        : "; still named in " + std::to_string(kNamed.size()) +
                              (kNamed.size() == 1 ? " file: " : " files, first ") + kNamed.front()));
    logFiles("remove", kPath, {}, kNamed.size());
}

void ShellParticipant::refreshFiles(const std::string& from, const std::string& to) {
    // The open document followed where it went, its preview let go first:
    // the session reads it again from its new path when next named.
    std::optional<std::string> open = scene_.empty() || from.empty() ? std::nullopt : movedPath(scene_, from, to);
    if (open.has_value() && previewing_) {
        static_cast<void>(ask(previewRecord(next(), previewedName(), nullptr)));
        previewing_ = false;
    }
    const std::string kAssets = ask(assetsRecord(next()));
    assets_ = assetsOf(kAssets);
    browser_.sources = sourcesOf(kAssets);
    browser_.folderList = foldersOf(kAssets);
    listDocuments();
    const auto kFollow = [&](std::string& path) {
        if (const auto kNow = from.empty() ? std::nullopt : movedPath(path, from, to); kNow.has_value()) {
            path = *kNow;
        }
    };
    kFollow(browser_.chosen);
    kFollow(browser_.target);
    if (browser_.folder.has_value()) {
        kFollow(*browser_.folder);
        if (browser_.folder->empty() || !std::ranges::contains(browser_.folderList, *browser_.folder)) {
            browser_.folder.reset();
        }
    }
    if (open.has_value()) {
        if (const auto kAt = std::ranges::find(scenes_, *open); !open->empty() && kAt != scenes_.end()) {
            scene_ = *open;
            showScene(static_cast<std::size_t>(kAt - scenes_.begin()));
        } else {
            // Gone: the first scene open instead, or none.
            scene_.clear();
            sceneAt_ = 0;
            clear(entityRows_);
            clear(componentRows_);
            entities_.clear();
            entity_.clear();
            if (const auto kFirst = std::ranges::find(listed_, Listed::Scene); kFirst != listed_.end()) {
                showScene(static_cast<std::size_t>(kFirst - listed_.begin()));
            }
        }
    } else if (!scene_.empty()) {
        if (const auto kAt = std::ranges::find(scenes_, scene_); kAt != scenes_.end()) {
            sceneAt_ = static_cast<std::size_t>(kAt - scenes_.begin());
        }
    }
    showTitle();
    static_cast<void>(showScenes());
}

void ShellParticipant::dropAsset() {
    const std::optional<std::string> kHeld = std::exchange(browser_.held, std::nullopt);
    const std::optional<std::size_t> kOpening = std::exchange(browser_.opening, std::nullopt);
    const bool kChoosing = std::exchange(browser_.choosing, false);
    const std::optional<std::array<float, 2>> kPointer = releasedAt_.has_value() ? releasedAt_ : pointerLaidOut();
    if (!kHeld.has_value() || pointing_ == nullptr || !kPointer.has_value() ||
        std::hypot((*kPointer)[0] - pressedAt_[0], (*kPointer)[1] - pressedAt_[1]) < kDragAfter) {
        // A scene's row clicked, not dragged, opens it, and a source's
        // card chooses it.
        if (kOpening.has_value() && *kOpening < scenes_.size()) {
            showScene(*kOpening);
        } else if (kChoosing && kHeld.has_value()) {
            chooseSource(*kHeld);
        }
        return;
    }
    if (placeDropped(*kHeld, *kPointer)) {
        return;
    }
    const auto kHit = tree_->hit(root_, (*kPointer)[0], (*kPointer)[1]);
    if (!kHit.has_value() || !kHit->node.has_value()) {
        return;
    }
    // Onto a folder, it goes in it; onto the game's own row, to its root.
    std::optional<std::string> into;
    if (*kHit->node == browser_.rootRow && browser_.rootRow != ui::Node{}) {
        into = std::string{};
    } else if (const auto kFolder =
                   std::ranges::find(browser_.folderRows, *kHit->node, &std::pair<ui::Node, std::string>::first);
               kFolder != browser_.folderRows.end() && !kFolder->second.empty()) {
        into = kFolder->second + "/";
    }
    if (!into.has_value()) {
        return;
    }
    const std::string kTo = *into + leafOf(*kHeld);
    if (kTo == *kHeld || folderOf(*kHeld) == *into) {
        return;
    }
    moveFile(*kHeld, kTo);
}

bool ShellParticipant::placeDropped(const std::string& held, const std::array<float, 2>& at) {
    const auto kInside = [this, &at](ui::Node node) {
        if (node == ui::Node{} || !tree_->contains(node)) {
            return false;
        }
        const ui::Rect kRect = tree_->placedRectOf(node);
        return at[0] >= kRect.x && at[1] >= kRect.y && at[0] < kRect.x + kRect.width && at[1] < kRect.y + kRect.height;
    };
    const auto kPlaced = [this, &held](std::string_view onto) {
        ++browser_.placed;
        emitter_.log(diagnostics::Severity::Info,
                     kAssetPlaced,
                     "an asset dragged from Assets was placed",
                     {diagnostics::field("asset", std::string_view{held}),
                      diagnostics::field("onto", onto),
                      diagnostics::field("placed", browser_.placed)});
    };
    // Onto a field that holds an asset of its kind (D786), that asset, as
    // typing its name there gives it (D975).
    for (const FieldRow& kTaking : fields_) {
        if (kTaking.role != FieldRow::Role::Field || kTaking.type.asset.empty() || !kInside(kTaking.value)) {
            continue;
        }
        const auto kAsset = std::ranges::find_if(assets_, [&](const Asset& each) {
            return each.name == held && each.kind == kTaking.type.asset;
        });
        if (kAsset == assets_.end()) {
            ++refused_;
            say(kTaking.field + " holds a " + kTaking.type.asset + ", not " + leafOf(held));
            return true;
        }
        const FieldRow kOnto = kTaking;
        apply(kOnto, assetShown(*kAsset));
        kPlaced("field");
        return true;
    }
    // Onto the Hierarchy or the viewport, a scene is placed in the scene
    // shown as an instance of it (D975), as the instance well does.
    if (!scene_.empty() && (kInside(entitiesColumn_) || kInside(centerColumn_))) {
        // A mesh, an entity of the game's component that holds one (D981).
        if (const auto kMesh = std::ranges::find_if(assets_,
                                                    [&held](const Asset& each) {
                                                        return each.name == held && each.kind == "mesh";
                                                    });
            kMesh != assets_.end() && !held.ends_with(".scene")) {
            const Asset kDropped = *kMesh;
            placeMesh(kDropped);
            kPlaced("mesh");
            return true;
        }
        if (!held.ends_with(".scene")) {
            ++refused_;
            say(leafOf(held) + " is placed through a field that takes it");
            return true;
        }
        if (held == scene_) {
            ++refused_;
            say("a scene is not placed in itself");
            return true;
        }
        place(held);
        kPlaced("scene");
        return true;
    }
    return false;
}

void ShellParticipant::placeMesh(const Asset& mesh) {
    // The game's own component kept in scenes with a field holding a mesh,
    // as the starter's prop; the engine's (a body's triangles) are not
    // drawn alone.
    const Catalog::Component* holder = nullptr;
    std::string held;
    for (const Catalog::Component& kComponent : catalog_.components) {
        const auto kHolding = std::ranges::find(kComponent.fields, std::string_view{"mesh"}, &Catalog::Field::asset);
        if (!kComponent.presented && !kComponent.name.starts_with("rawframe.") && kHolding != kComponent.fields.end()) {
            holder = &kComponent;
            held = kHolding->name;
            break;
        }
    }
    const auto kPose = std::ranges::find(catalog_.components, "rawframe.physics3d.pose", &Catalog::Component::name);
    if (holder == nullptr || kPose == catalog_.components.end()) {
        ++refused_;
        say("the game keeps no mesh in its scenes: give a component a field of `asset mesh`");
        return;
    }
    const std::string kIdentity = mintedIdentity();
    const auto kOperation = [&kIdentity](std::string_view kind) {
        Value made = Value::object();
        made.add("operation", Value::string(std::string{kind}));
        made.add("entity", Value::string(kIdentity));
        return made;
    };
    const auto kSet = [&kOperation](const Catalog::Component& component, std::string_view name, Value typed) {
        Value set = kOperation("scene.set_field");
        set.add("component", Value::string(component.id));
        set.add("field", Value::string(std::string{name}));
        set.add("value", std::move(typed));
        return set;
    };
    std::string named = std::filesystem::path{mesh.name}.stem().string();
    std::vector<Value> operations;
    Value created = kOperation("scene.create_entity");
    created.add("name", Value::string(named));
    operations.push_back(std::move(created));
    for (const Catalog::Component* kAdded : std::array<const Catalog::Component*, 2>{&*kPose, holder}) {
        Value added = kOperation("scene.add_component");
        added.add("component", Value::string(kAdded->id));
        operations.push_back(std::move(added));
    }
    // Where the view looks, its origin on the ground.
    std::array<double, 3> at = viewTarget(view_.has_value() ? &*view_ : nullptr);
    at[1] = 0;
    constexpr std::array<std::string_view, 3> kAxes = {"x", "y", "z"};
    for (std::size_t each = 0; each < 3; ++each) {
        if (at[each] != 0) {
            Value real = Value::object();
            real.add("real", Value::real(at[each]));
            operations.push_back(kSet(*kPose, kAxes[each], std::move(real)));
        }
    }
    Value identity = Value::object();
    identity.add("unsigned", Value::string(std::to_string(mesh.id)));
    operations.push_back(kSet(*holder, held, std::move(identity)));
    commitAll(std::move(operations), named + " placed", kIdentity);
}

void ShellParticipant::logFiles(std::string_view verb, std::string_view from, std::string_view to, std::size_t named) {
    emitter_.log(diagnostics::Severity::Info,
                 kFilesChanged,
                 "a file or folder of the game's moved, removed, or made in Assets",
                 {diagnostics::field("verb", verb),
                  diagnostics::field("from", from),
                  diagnostics::field("to", to),
                  diagnostics::field("named", named),
                  diagnostics::field("moved", browser_.moved),
                  diagnostics::field("removed", browser_.removed),
                  diagnostics::field("folders", browser_.foldersMade)});
}

} // namespace rawframe::studio
