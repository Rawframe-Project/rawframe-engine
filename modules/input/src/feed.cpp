#include "rawframe/input/feed.h"

#include <algorithm>
#include <vector>

namespace rawframe::input {

Feed::Feed(std::size_t capacity) : capacity_(std::max<std::size_t>(capacity, 2)) {
    records_.reserve(capacity_);
}

void Feed::connect(DeviceId device, DeviceClass deviceClass) {
    add(Record{.kind = Kind::Connect, .deviceClass = deviceClass, .event = {.device = device}});
}

void Feed::disconnect(DeviceId device) {
    add(Record{.kind = Kind::Disconnect, .event = {.device = device}});
}

void Feed::submit(const ControlEvent& event) {
    add(Record{.kind = Kind::Control, .event = event});
}

void Feed::releaseAll() {
    add(Record{.kind = Kind::ReleaseAll});
}

void Feed::add(const Record& record) {
    if (records_.size() >= capacity_) {
        dropped_ += static_cast<std::uint64_t>(std::ranges::count(records_, Kind::Control, &Record::kind));
        std::erase_if(records_, [](const Record& each) {
            return each.kind == Kind::Control || each.kind == Kind::ReleaseAll;
        });
        if (records_.size() < capacity_) {
            records_.push_back(Record{.kind = Kind::ReleaseAll});
        }
        if (records_.size() >= capacity_) {
            // Nothing but devices coming and going: this record has no room.
            dropped_ += record.kind == Kind::Control ? 1 : 0;
            return;
        }
    }
    records_.push_back(record);
}

void Feed::deliver(Mapper& mapper, PlayerSlot player) {
    for (const Record& record : records_) {
        switch (record.kind) {
        case Kind::Connect:
            // Refused past the mapper's device limit: its events then count
            // as unpaired there, which is what the limit means.
            static_cast<void>(mapper.pair(record.event.device, record.deviceClass, player));
            break;
        case Kind::Disconnect:
            mapper.unpair(record.event.device);
            break;
        case Kind::Control:
            mapper.submit(record.event);
            break;
        case Kind::ReleaseAll:
            mapper.releaseAll();
            break;
        }
    }
    records_.clear();
}

void Feed::route(Pairing& pairing, std::span<Feed* const> players) {
    const auto kFeedOf = [&](DeviceId device) -> Feed* {
        const auto kPlayer = pairing.playerOf(device);
        return kPlayer.has_value() && kPlayer->value < players.size() ? players[kPlayer->value] : nullptr;
    };
    for (const Record& record : records_) {
        switch (record.kind) {
        case Kind::Connect:
            if (const auto kPlayer = pairing.connect(record.event.device, record.deviceClass);
                kPlayer.has_value() && kPlayer->value < players.size() && players[kPlayer->value] != nullptr) {
                players[kPlayer->value]->add(record);
            }
            break;
        case Kind::Disconnect:
            if (Feed* feed = kFeedOf(record.event.device); feed != nullptr) {
                feed->add(record);
            }
            pairing.disconnect(record.event.device);
            break;
        case Kind::Control:
            if (Feed* feed = kFeedOf(record.event.device); feed != nullptr) {
                feed->add(record);
            } else {
                ++unrouted_;
            }
            break;
        case Kind::ReleaseAll:
            for (Feed* feed : players) {
                if (feed != nullptr) {
                    feed->add(record);
                }
            }
            break;
        }
    }
    records_.clear();
}

void Feed::feel(const HapticCommand& command) {
    const auto kWaiting = std::ranges::find(felt_, command.device, &HapticCommand::device);
    if (kWaiting != felt_.end()) {
        kWaiting->haptic = command.haptic;
    } else if (felt_.size() < kMostFelt) {
        felt_.push_back(command);
    } else {
        ++feltDropped_;
    }
}

void Feed::takeFelt(std::vector<HapticCommand>& into) {
    into.clear();
    into.swap(felt_);
}

} // namespace rawframe::input
