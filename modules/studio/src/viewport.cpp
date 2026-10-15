#include "rawframe/render/capture.h"
#include "shell.h"

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

} // namespace rawframe::studio
