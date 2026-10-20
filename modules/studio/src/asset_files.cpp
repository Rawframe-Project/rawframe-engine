#include "shell.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <utility>

namespace rawframe::studio {

namespace {

/// How far a card's press moves before it is a drag rather than a click.
constexpr float kDragAfter = 6;

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
    if (!kHeld.has_value() || pointing_ == nullptr) {
        return;
    }
    const std::optional<std::array<float, 2>> kPointer = releasedAt_.has_value() ? releasedAt_ : pointing_->pointer();
    if (!kPointer.has_value() ||
        std::hypot((*kPointer)[0] - pressedAt_[0], (*kPointer)[1] - pressedAt_[1]) < kDragAfter) {
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
