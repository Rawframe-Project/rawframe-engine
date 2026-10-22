// An editor's clipboard (D999): the text it writes put on the platform's
// clipboard through its window, and the text it asks for read there and
// answered by its ticket once the read completes.

#include "rawframe/window_host/window_host.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

namespace rawframe::window_host {

void WindowHost::followClipboard(window::Windows& windows) {
    if (std::optional<std::string> written = clipboard_.written(); written.has_value()) {
        static_cast<void>(windows.requestClipboardWrite(window_, *written));
    }
    for (const std::uint64_t kTicket : clipboard_.reads()) {
        if (auto asked = windows.requestClipboardRead(window_); asked.has_value()) {
            clipboardReads_.emplace_back(*asked, kTicket);
        } else {
            clipboard_.answered(view::Clipboard::Read{.ticket = kTicket, .unsupported = true});
        }
    }
}

bool WindowHost::clipboardRead(const window::Windows& windows, const window::Event& event) {
    if (event.kind != window::EventKind::RequestCompleted ||
        event.completion.kind != window::RequestKind::ClipboardRead) {
        return false;
    }
    const auto kAsked = std::ranges::find(
        clipboardReads_, event.completion.request, &std::pair<window::RequestId, std::uint64_t>::first);
    if (kAsked == clipboardReads_.end()) {
        return true;
    }
    view::Clipboard::Read read{.ticket = kAsked->second};
    if (event.completion.outcome == window::Outcome::Done) {
        if (auto text = windows.clipboardText(event.completion.request); text.has_value()) {
            read.text = std::move(*text);
        } else {
            read.unsupported = true;
        }
    } else {
        read.unsupported = true;
    }
    clipboard_.answered(std::move(read));
    clipboardReads_.erase(kAsked);
    return true;
}

} // namespace rawframe::window_host
