#include "shell.h"

#include <algorithm>

namespace rawframe::studio {

void ShellParticipant::runExtensionCommand(const std::string& command) {
    const auto kOffering = std::ranges::find_if(extensions_, [&](const ListedExtension& extension) {
        return std::ranges::contains(extension.commands, command);
    });
    if (kOffering == extensions_.end() || scene_.empty()) {
        say(command + " runs on an open scene");
        return;
    }
    // The command as `authoring.extension` runs it headless: on the
    // entities chosen, what it staged one step of the scene's history,
    // undone as any other (D874).
    std::vector<std::string> chosen = selected_;
    if (chosen.empty() && !entity_.empty()) {
        chosen.push_back(entity_);
    }
    const Outcome kOutcome = outcomeOf(ask(extensionRecord(next(), scene_, kOffering->path, command, chosen)));
    told(kOutcome);
    if (kOutcome.done) {
        ++applied_;
        say(command + " done");
        recookPlayed();
    } else {
        ++refused_;
        say(command + ": " + kOutcome.message);
    }
    refresh(entity_.empty() ? std::nullopt : std::optional<std::string>{entity_});
}

} // namespace rawframe::studio
