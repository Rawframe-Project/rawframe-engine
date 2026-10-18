#include "shell.h"

#include <algorithm>
#include <charconv>

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

void ShellParticipant::switchExtensionMode(const std::string& mode) {
    if (held_.extensionMode.has_value() && held_.extensionMode->second == mode) {
        held_.extensionMode.reset();
        say(mode + " mode off");
        return;
    }
    const auto kOffering = std::ranges::find_if(extensions_, [&](const ListedExtension& extension) {
        return std::ranges::contains(extension.modes, mode);
    });
    if (kOffering == extensions_.end()) {
        return;
    }
    held_.extensionMode = std::pair{kOffering->path, mode};
    say(mode + " mode: click in the played scene");
}

void ShellParticipant::runExtensionMode(const std::array<double, 3>& point, const std::optional<std::string>& picked) {
    // The mode as `authoring.extension` runs it headless with a point:
    // what it staged one step of the scene's history (D877).
    const auto& [kPath, kMode] = *held_.extensionMode;
    std::vector<std::string> chosen;
    if (picked.has_value()) {
        chosen.push_back(*picked);
    }
    const Outcome kOutcome = outcomeOf(ask(extensionRecord(next(), scene_, kPath, kMode, chosen, point)));
    told(kOutcome);
    if (kOutcome.done) {
        ++applied_;
        say(kMode + " done");
        recookPlayed();
    } else {
        ++refused_;
        say(kMode + ": " + kOutcome.message);
    }
    refresh(entity_.empty() ? std::nullopt : std::optional<std::string>{entity_});
}

void ShellParticipant::showSections(const std::optional<Value>& read) {
    const Value* kComponents = read.has_value() ? read->find("components") : nullptr;
    if (kComponents == nullptr || kComponents->kind() != Value::Kind::Array) {
        return;
    }
    const auto kHas = [&](const std::string& component) {
        return std::ranges::any_of(kComponents->items(), [&](const Value& each) {
            const Value* kName = each.find("name");
            return kName != nullptr && kName->kind() == Value::Kind::String && *kName->text() == component;
        });
    };
    // Each section of a component the entity has, filled by its extension
    // through the session as `authoring.extension_section` fills it
    // headless: its heading, then a row for each label and value.
    for (const ListedExtension& kExtension : extensions_) {
        for (const std::string& kComponent : kExtension.sections) {
            if (!kHas(kComponent)) {
                continue;
            }
            const auto kRows =
                sectionRowsOf(ask(extensionSectionRecord(next(), scene_, kExtension.path, kComponent, entity_)));
            const std::string kFile = kExtension.path.substr(kExtension.path.find_last_of('/') + 1);
            if (auto heading = row(componentsColumn_, kComponent + " by " + kFile, kQuiet, kPanel);
                heading.has_value()) {
                componentRows_.push_back(*heading);
            }
            for (const auto& [kLabel, kValue] : kRows) {
                std::array<char, 32> digits{};
                const auto kWritten =
                    std::to_chars(digits.data(), digits.data() + digits.size(), kValue, std::chars_format::general, 6);
                const std::string kRowText = kLabel + "  " + std::string{digits.data(), kWritten.ptr};
                if (auto line = row(componentsColumn_, kRowText, kText, kPanel); line.has_value()) {
                    componentRows_.push_back(*line);
                }
            }
            emitter_.log(diagnostics::Severity::Info,
                         kSectionShown,
                         "an extension's Inspector section shown",
                         {diagnostics::field("extension", kExtension.path),
                          diagnostics::field("component", kComponent),
                          diagnostics::field("rows", kRows.size())});
        }
    }
}

} // namespace rawframe::studio
