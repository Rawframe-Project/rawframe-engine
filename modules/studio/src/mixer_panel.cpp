#include "shell.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace rawframe::studio {

namespace {

/// A fader's range in decibels, and its track's height in pixels.
constexpr double kLeastDecibels = -60;
constexpr double kMostDecibels = 6;
constexpr float kFaderTall = 96;
constexpr float kStripWide = 104;

/// A bus's name as its strip heads it: its first letter raised.
std::string stripName(std::string name) {
    std::ranges::replace(name, '_', ' ');
    if (!name.empty()) {
        name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    }
    return name;
}

/// What a bus's effects do, as its strip says it under its name.
std::string effectsOf(const Value& bus) {
    const Value* kEffects = bus.find("effects");
    if (kEffects == nullptr || kEffects->items().empty()) {
        return "no effects";
    }
    std::string made;
    for (const Value& kEffect : kEffects->items()) {
        const Value* kNamed = kEffect.find("processor");
        if (kNamed == nullptr || kNamed->text() == nullptr) {
            kNamed = kEffect.find("type");
        }
        std::string said = kNamed != nullptr && kNamed->text() != nullptr ? *kNamed->text() : "effect";
        if (const std::size_t kSlash = said.find('/'); kSlash != std::string::npos) {
            said = said.substr(kSlash + 1, said.find('@') - kSlash - 1);
        }
        made += (made.empty() ? "" : ", ") + said;
    }
    return made;
}

} // namespace

result::Status ShellParticipant::makeMixer() {
    RAWFRAME_TRY_ASSIGN(held_.mixerColumn, pane("Mixer"));
    return {};
}

void ShellParticipant::showMixer() {
    clear(held_.mixerRows);
    held_.mixerStrips.clear();
    // The game's mixer: its first `.mixer` document, the one Play mixes
    // through.
    held_.mixerDocument.clear();
    for (const std::string& kDocument : scenes_) {
        if (std::filesystem::path{kDocument}.extension() == ".mixer") {
            held_.mixerDocument = kDocument;
            break;
        }
    }
    const std::optional<Value> kRead =
        held_.mixerDocument.empty() ? std::nullopt : read(held_.mixerDocument, "data.read");
    const Value* kData = kRead.has_value() ? kRead->find("data") : nullptr;
    const Value* kMaster = kData != nullptr ? kData->find("master") : nullptr;
    if (kMaster == nullptr || kMaster->kind() != Value::Kind::Object) {
        if (auto none = box(held_.mixerColumn, ui::Layout{.shrink = 0, .padding = {14, 14, 12, 12}}, 0);
            none.has_value()) {
            held_.mixerRows.push_back(*none);
            static_cast<void>(wrappedWords(*none,
                                           held_.mixerDocument.empty()
                                               ? "This game has no mixer. A .mixer document's buses show here as "
                                                 "channel strips, its master first."
                                               : held_.mixerDocument + " does not read as a mixer.",
                                           look::kFaintInk,
                                           look::kBodySize));
        }
        return;
    }
    auto strips = box(held_.mixerColumn,
                      ui::Layout{.gap = 8, .shrink = 0, .padding = {12, 12, 10, 10}, .scroll = ui::Scroll::Horizontal},
                      0);
    if (!strips.has_value()) {
        return;
    }
    held_.mixerRows.push_back(*strips);
    static_cast<void>(tree_->setName(*strips, "Mixer strips"));
    // The master, then each bus under it, depth first, as the document
    // nests them; each by its path as the data editor's rows are.
    std::vector<std::pair<const Value*, std::string>> buses;
    const auto kWalk = [&buses](const auto& self, const Value& bus, const std::string& path) -> void {
        buses.emplace_back(&bus, path);
        const Value* kChildren = bus.find("children");
        for (const Value& kChild : kChildren != nullptr ? kChildren->items() : std::span<const Value>{}) {
            const Value* kId = kChild.find("busId");
            if (kId != nullptr && kId->text() != nullptr) {
                self(self, kChild, path + ".children[busId=" + *kId->text() + "]");
            }
        }
    };
    kWalk(kWalk, *kMaster, "master");
    for (const auto& [kBus, kPath] : buses) {
        const Value* kName = kBus->find("name");
        const Value* kVolume = kBus->find("volume");
        const double kDecibels = kVolume != nullptr ? kVolume->real().value_or(0) : 0;
        auto strip = box(*strips,
                         ui::Layout{.width = ui::pixels(kStripWide),
                                    .direction = ui::Direction::Column,
                                    .alignItems = ui::Align::Center,
                                    .gap = 6,
                                    .shrink = 0,
                                    .padding = {8, 8, 10, 10},
                                    .border = {1, 1, 1, 1}},
                         0);
        if (!strip.has_value()) {
            return;
        }
        static_cast<void>(
            tree_->setLook(*strip, ui::Look{.fill = 0x17181CFFU, .borderColor = look::kEdge, .radius = 6}));
        static_cast<void>(tree_->setRole(*strip, ui::Role::Button));
        const std::string kHeading = stripName(kName != nullptr && kName->text() != nullptr ? *kName->text() : "bus");
        static_cast<void>(tree_->setName(*strip, kHeading + " strip"));
        held_.mixerStrips.emplace_back(*strip, kPath);
        const auto kInert = [this](ui::Node parent) {
            static_cast<void>(tree_->setInteraction(parent, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
        };
        if (auto named = box(*strip, ui::Layout{.shrink = 0}, 0); named.has_value()) {
            static_cast<void>(strongWords(*named, kHeading, look::kInk));
            kInert(*named);
        }
        if (auto effects = box(
                *strip,
                ui::Layout{
                    .height = ui::pixels(20), .alignItems = ui::Align::Center, .shrink = 0, .padding = {6, 6, 0, 0}},
                0);
            effects.has_value()) {
            static_cast<void>(tree_->setLook(*effects, ui::Look{.fill = look::kWellFill, .radius = 4, .clip = true}));
            static_cast<void>(words(*effects, effectsOf(*kBus), look::kLabelInk, look::kSmallSize));
            kInert(*effects);
        }
        // Its fader: the track, filled up to the volume, and its knob there.
        const double kAt = std::clamp((kDecibels - kLeastDecibels) / (kMostDecibels - kLeastDecibels), 0.0, 1.0);
        if (auto track = box(*strip,
                             ui::Layout{.width = ui::pixels(28),
                                        .height = ui::pixels(kFaderTall),
                                        .direction = ui::Direction::Column,
                                        .justify = ui::Justify::End,
                                        .alignItems = ui::Align::Center,
                                        .shrink = 0},
                             0);
            track.has_value()) {
            kInert(*track);
            if (auto rail = box(*track,
                                ui::Layout{.width = ui::pixels(6),
                                           .height = ui::pixels(kFaderTall),
                                           .direction = ui::Direction::Column,
                                           .justify = ui::Justify::End,
                                           .shrink = 0},
                                0);
                rail.has_value()) {
                static_cast<void>(tree_->setLook(*rail, ui::Look{.fill = look::kWellFill, .radius = 3}));
                kInert(*rail);
                if (auto filled = box(*rail,
                                      ui::Layout{.width = ui::pixels(6),
                                                 .height = ui::pixels(static_cast<float>(kAt) * kFaderTall),
                                                 .shrink = 0},
                                      0);
                    filled.has_value()) {
                    static_cast<void>(tree_->setLook(*filled, ui::Look{.fill = look::kReady, .radius = 3}));
                }
            }
            if (auto knob =
                    box(*track,
                        ui::Layout{.width = ui::pixels(24),
                                   .height = ui::pixels(10),
                                   .shrink = 0,
                                   .placement = {.absolute = true,
                                                 .x = ui::pixels(2),
                                                 .y = ui::pixels(static_cast<float>(1 - kAt) * (kFaderTall - 10))}},
                        0);
                knob.has_value()) {
                static_cast<void>(tree_->setLook(*knob, ui::Look{.fill = look::kInk, .radius = 2}));
            }
        }
        std::array<char, 24> said{};
        std::snprintf(said.data(), said.size(), "%.1f dB", kDecibels);
        if (auto level = box(*strip, ui::Layout{.shrink = 0}, 0); level.has_value()) {
            static_cast<void>(monoWords(*level, said.data(), look::kSoftInk, look::kSmallSize));
            kInert(*level);
        }
        const Value* kRole = kBus->find("role");
        if (auto role = box(*strip, ui::Layout{.shrink = 0}, 0); role.has_value()) {
            static_cast<void>(monoWords(*role,
                                        kRole != nullptr && kRole->text() != nullptr ? *kRole->text() : "",
                                        look::kFaintInk,
                                        look::kSmallSize));
            kInert(*role);
        }
    }
    if (auto note = box(held_.mixerColumn, ui::Layout{.shrink = 0, .padding = {14, 14, 0, 8}}, 0); note.has_value()) {
        held_.mixerRows.push_back(*note);
        static_cast<void>(words(*note,
                                held_.mixerDocument + ": press a strip to set its bus in the Inspector",
                                look::kFaintInk,
                                look::kLabelSize));
    }
}

bool ShellParticipant::pressMixer(ui::Node node) {
    const auto kStrip = std::ranges::find(held_.mixerStrips, node, &std::pair<ui::Node, std::string>::first);
    if (kStrip == held_.mixerStrips.end()) {
        return false;
    }
    const std::string kPath = kStrip->second;
    const auto kDocument = std::ranges::find(scenes_, held_.mixerDocument);
    if (kDocument == scenes_.end()) {
        return true;
    }
    if (scene_ != held_.mixerDocument) {
        showScene(static_cast<std::size_t>(kDocument - scenes_.begin()));
    }
    if (const auto kBusRow = std::ranges::find(entities_, kPath); kBusRow != entities_.end()) {
        showDataEntry(static_cast<std::size_t>(kBusRow - entities_.begin()));
    }
    say("the " + kPath + " bus");
    return true;
}

} // namespace rawframe::studio
