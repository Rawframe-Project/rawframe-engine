#pragma once

// A session's JSON documents of one kind (D636, D641): style sheets
// (`.styles`, AuthoredStyles), UI documents (`.ui`, AuthoredUi), or
// material instances (`.rfmaterial`, AuthoredMaterial, D721), under
// its root, opened as records name them and kept open with their histories
// as its scenes are, reopened when their file changes under it, and
// written through base::replaceFile when a change is kept. A record names
// one where it would name a scene: apply runs its kind's operations, read
// its query, undo and redo step its history, and history lists it (D638).
// A played game follows the files (D634).

#include "rawframe/authoring/authored_lines.h"
#include "rawframe/authoring/json_document.h"
#include "rawframe/authoring/session.h"
#include "rawframe/base/bits128.h"
#include "rawframe/document/json.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace rawframe::authoring_session {

/// Adds to a history entry's summary who made it and what it is (D879):
/// `author`, the name its record gave, where it gave one, of `authors` by
/// the entry's author, and `kind`, `change`, `undo`, or `redo`.
void addAuthorship(document::Value& entry,
                   const authoring::Authorship& authorship,
                   std::span<const std::string> authors);

/// A step refused for another author's later change, that author named as
/// their records name them (`author`) beside the number the document knows
/// them by (D879).
[[nodiscard]] result::Result<authoring::Committed> namingAuthor(result::Result<authoring::Committed> step,
                                                                std::span<const std::string> authors);

template <class Authored> class Documents {
public:
    /// Documents named by `extension` under `root`, `called` what they are
    /// in what is said of them (`style sheet`).
    Documents(std::filesystem::path root, std::string_view extension, std::string_view called)
        : root_(std::move(root)), extension_(extension), called_(called) {
    }
    /// Data documents (D811), read by `reads`; line documents of a kind
    /// whose own operations `rewrites` makes lines (D855).
    Documents(std::filesystem::path root,
              std::string_view extension,
              std::string_view called,
              authoring::DocumentReader reads,
              authoring::LineRewriter rewrites = nullptr)
        : root_(std::move(root)), extension_(extension), called_(called), reads_(reads), rewrites_(rewrites) {
    }
    // Moved, never copied: an open document is its own. Said outright, so
    // a vector of these grows by moving them where a library would copy
    // (MSVC's does, and an open document cannot be copied).
    Documents(Documents&&) = default;
    Documents& operator=(Documents&&) = default;
    Documents(const Documents&) = delete;
    Documents& operator=(const Documents&) = delete;
    ~Documents() = default;

    /// Whether a record's document is one of these.
    [[nodiscard]] bool names(std::string_view document) const noexcept {
        return std::filesystem::path{document}.extension() == extension_;
    }

    /// The outcome of an apply, a step, or a read; `failed` set where an
    /// operation in it failed, as the session's own are.
    [[nodiscard]] result::Result<document::Value> applied(const authoring::SessionRecord& record, bool& failed);
    [[nodiscard]] result::Result<document::Value>
    stepped(const authoring::SessionRecord& record, bool undo, bool& failed, std::span<const std::string> authors);
    [[nodiscard]] result::Result<document::Value> answered(const authoring::SessionRecord& record, bool& failed);
    /// The document's history, each entry summed up, as a scene's is, its
    /// authors named as `authors` names them.
    [[nodiscard]] result::Result<document::Value> history(const authoring::SessionRecord& record,
                                                          std::span<const std::string> authors);
    /// The document `name` names, open, and a copy of it as it stands
    /// whose changes are the current author's, to learn what a change or
    /// a step would touch (D886).
    [[nodiscard]] result::Result<const Authored*> held(const std::string& name);
    [[nodiscard]] result::Result<std::unique_ptr<Authored>> copied(const std::string& name);
    /// Who makes the changes and steps of the records that follow, by the
    /// session's number for them (ADR-0085, D879).
    void setAuthor(std::uint32_t author) noexcept {
        author_ = author;
    }

private:
    struct Open {
        std::unique_ptr<Authored> document;
        /// The digest of the bytes on disk as last read or written.
        std::string onDisk;
    };

    [[nodiscard]] result::Result<Open*> open(const std::string& name, bool& reopened);
    [[nodiscard]] result::Result<bool> save(const std::string& name, Open& open);
    /// A document of these opened from `text`.
    [[nodiscard]] result::Result<std::unique_ptr<Authored>> made(rawframe::base::Bits128 identity,
                                                                 std::string_view text) const;

    std::filesystem::path root_;
    std::string extension_;
    std::string called_;
    authoring::DocumentReader reads_ = nullptr;
    authoring::LineRewriter rewrites_ = nullptr;
    std::map<std::string, Open> opened_;
    std::uint32_t author_ = 0;
};

} // namespace rawframe::authoring_session
