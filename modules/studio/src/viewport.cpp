#include "rawframe/render/capture.h"
#include "rawframe/world_tooling/window_input.h"
#include "shell.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace rawframe::studio {

void ShellParticipant::followPicture() {
    if (!playing_.has_value() || !play_.has_value() || !play_->viewport.has_value()) {
        return;
    }
    // The newest picture the client streamed, when the file changed: it
    // is put there whole, so what reads is one picture or none.
    const std::filesystem::path kPath = playing_->picture();
    std::error_code error;
    const auto kWritten = std::filesystem::last_write_time(kPath, error);
    if (error || (pictureWritten_.has_value() && *pictureWritten_ == kWritten)) {
        return;
    }
    pictureWritten_ = kWritten;
    std::ifstream file{kPath, std::ios::binary};
    std::vector<char> bytes{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    std::optional<render::StreamedPicture> read =
        render::streamedFrom(std::as_bytes(std::span{bytes.data(), bytes.size()}));
    if (!read.has_value() || read->sequence == pictureSequence_) {
        return;
    }
    pictureSequence_ = read->sequence;
    texture::Texture made{.format = texture::Format::Rgba8Srgb};
    made.levels.push_back(
        texture::Level{.width = read->width, .height = read->height, .bytes = std::move(read->pixels)});
    picture_ = std::make_shared<const texture::Texture>(std::move(made));
    ++picturesShown_;
}

void ShellParticipant::showViewport() {
    // Under the view's fields, the rest of the center: the played game's
    // pictures while it plays here (D789).
    auto made = box(centerColumn_,
                    ui::Layout{.height = ui::pixels(0), .minHeight = ui::pixels(240), .grow = 1, .shrink = 0},
                    kBackground);
    if (!made.has_value()) {
        return;
    }
    viewport_ = *made;
    viewRows_.push_back(viewport_);
    static_cast<void>(tree_->setLook(viewport_, ui::Look{.fill = kBackground, .image = kViewportImage}));
    static_cast<void>(tree_->setName(viewport_, "Viewport"));
}

void ShellParticipant::forwardInput() {
    if (forwarding_ == nullptr) {
        return;
    }
    // While the game plays in the viewport and shows there, its area is
    // the game's; else the window host acts on all of it.
    const bool kPlaysHere = playing_.has_value() && play_.has_value() && play_->viewport.has_value() &&
                            picture_ != nullptr && tree_->contains(viewport_) && !scene_.empty();
    if (!kPlaysHere) {
        forwarding_->forwardOver(std::nullopt);
        static_cast<void>(forwarding_->take());
        return;
    }
    const ui::Rect kAt = tree_->placedRectOf(viewport_);
    forwarding_->forwardOver(std::array<float, 4>{kAt.x, kAt.y, kAt.width, kAt.height});
    std::vector<world_tooling::WindowInput> events = forwarding_->take();
    // In records of at most the most one carries.
    for (std::size_t from = 0; from < events.size(); from += world_tooling::kMostWindowInputs) {
        const std::size_t kTo = std::min(events.size(), from + world_tooling::kMostWindowInputs);
        const std::vector<world_tooling::WindowInput> kPart{events.begin() + static_cast<std::ptrdiff_t>(from),
                                                            events.begin() + static_cast<std::ptrdiff_t>(kTo)};
        inputsForwarded_ += kPart.size();
        const std::string kReply = ask(previewInputRecord(next(), scene_, world_tooling::windowInputsValue(kPart)));
        if (kReply.find("\"taken\":true") != std::string::npos) {
            ++inputsTaken_;
        }
    }
}

} // namespace rawframe::studio
