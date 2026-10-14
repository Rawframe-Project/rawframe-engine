#include "rawframe/base/files.h"
#include "shell.h"

namespace rawframe::studio {

namespace {

/// A splitter's thickness: the gap it stands in.
constexpr float kThickness = 6;

} // namespace

result::Status ShellParticipant::splitter(ui::Node parent, Edge edge) {
    const bool kAcross = edge == Edge::Dock;
    RAWFRAME_TRY_ASSIGN(const ui::Node kSplitter,
                        box(parent,
                            ui::Layout{.width = kAcross ? ui::Dimension{} : ui::pixels(kThickness),
                                       .height = kAcross ? ui::pixels(kThickness) : ui::Dimension{},
                                       .shrink = 0},
                            kBackground));
    // Named, so assistive technology and a test find it by its dock.
    RAWFRAME_TRY(tree_->setName(kSplitter,
                                edge == Edge::Hierarchy   ? "Hierarchy edge"
                                : edge == Edge::Inspector ? "Inspector edge"
                                                          : "Dock edge"));
    splitters_[static_cast<std::size_t>(edge)] = kSplitter;
    return {};
}

void ShellParticipant::followSplitter() {
    if (released_) {
        released_ = false;
        releaseSplitter();
        return;
    }
    if (!resizing_.has_value() || pointing_ == nullptr) {
        return;
    }
    const std::optional<std::array<float, 2>> kPointer = pointing_->pointer();
    if (!kPointer.has_value()) {
        return;
    }
    const float kMoved = (resizing_->edge == Edge::Dock ? (*kPointer)[1] : (*kPointer)[0]) - resizing_->from;
    // The Hierarchy grows rightward; the Inspector and the dock grow away
    // from the edge they are dragged by.
    switch (resizing_->edge) {
    case Edge::Hierarchy:
        preferences_.hierarchyWidth = resizing_->size + kMoved;
        break;
    case Edge::Inspector:
        preferences_.inspectorWidth = resizing_->size - kMoved;
        break;
    case Edge::Dock:
        preferences_.dockHeight = std::max(1.0F, resizing_->size - kMoved);
        break;
    }
    preferences_ = held(preferences_);
    static_cast<void>(tree_->setLayout(entitiesColumn_, columnLayout(preferences_.hierarchyWidth)));
    static_cast<void>(tree_->setLayout(componentsColumn_, columnLayout(preferences_.inspectorWidth)));
    static_cast<void>(tree_->setLayout(bottomDock_, dockLayout(preferences_.dockHeight)));
}

void ShellParticipant::releaseSplitter() {
    if (!resizing_.has_value()) {
        return;
    }
    resizing_.reset();
    ++resized_;
    keepPreferences();
}

void ShellParticipant::keepPreferences() {
    if (preferencesFile_.empty()) {
        return;
    }
    const std::string kWritten = writtenPreferences(preferences_);
    if (!base::replaceFile(preferencesFile_, std::as_bytes(std::span{kWritten.data(), kWritten.size()}))) {
        say("the layout could not be kept in " + preferencesFile_.string());
    }
}

} // namespace rawframe::studio
