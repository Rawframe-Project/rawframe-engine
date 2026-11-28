#include "rawframe/world/lifecycle.h"

#include "rawframe/world/errors.h"

#include <algorithm>
#include <utility>

namespace rawframe::world {

namespace {

result::Status refuse(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kWorldDomain, code(WorldError::InvalidLifecycle), why);
}

} // namespace

result::Status Lifecycle::watch(std::string_view reader, LifecycleWatch watch) {
    if (watch.kind == LifecycleKind::Destroyed) {
        watch.component.reset();
    } else if (!watch.component.has_value()) {
        return refuse("an addition or a removal is watched for a component");
    }
    auto found = std::ranges::find(readers_, reader, &LifecycleReader::identity);
    if (found == readers_.end()) {
        if (readers_.size() >= kMaximumLifecycleReaders || reader.empty() ||
            reader.size() > kMaximumLifecycleIdentityBytes) {
            return refuse("a World has at most 64 lifecycle readers, each named in at most 256 bytes");
        }
        readers_.push_back(LifecycleReader{.identity = std::string{reader}, .cursor = nextSequence_, .watches = {}});
        found = readers_.end() - 1;
    }
    if (std::ranges::contains(found->watches, watch)) {
        return {};
    }
    if (found->watches.size() >= kMaximumLifecycleWatches) {
        return refuse("a lifecycle reader watches at most 16 kinds of change");
    }
    found->watches.push_back(watch);
    if (!std::ranges::contains(watched_, watch)) {
        watched_.push_back(watch);
    }
    return {};
}

const LifecycleReader* Lifecycle::reader(std::string_view identity) const noexcept {
    const auto kFound = std::ranges::find(readers_, identity, &LifecycleReader::identity);
    return kFound == readers_.end() ? nullptr : &*kFound;
}

bool Lifecycle::watched(const LifecycleWatch& watch) const noexcept {
    return std::ranges::contains(watched_, watch);
}

std::span<const LifecycleChange> Lifecycle::unread(std::string_view reader) const noexcept {
    const LifecycleReader* const kReader = this->reader(reader);
    if (kReader == nullptr) {
        return {};
    }
    // Kept changes rise in sequence, so the cursor's place is found by
    // halving; a cursor before the oldest kept reads from it.
    const std::span<const LifecycleChange> kKept = changes();
    const auto kFrom = std::ranges::lower_bound(kKept, kReader->cursor, {}, &LifecycleChange::sequence);
    return kKept.subspan(static_cast<std::size_t>(kFrom - kKept.begin()));
}

void Lifecycle::markRead(std::string_view reader) noexcept {
    const auto kFound = std::ranges::find(readers_, reader, &LifecycleReader::identity);
    if (kFound == readers_.end()) {
        return;
    }
    kFound->cursor = nextSequence_;
    trim();
}

void Lifecycle::record(LifecycleKind kind,
                       std::optional<schema::ComponentRuntimeId> component,
                       EntityHandle entity,
                       std::span<const std::byte> value) {
    if (!watched(LifecycleWatch{.kind = kind, .component = component})) {
        return;
    }
    const std::size_t kKeep = std::min(value.size(), kMaximumLifecycleValueBytes);
    // The oldest go first: past the count, or past the bytes with this one.
    while (first_ < changes_.size() && (changes_.size() - first_ >= kMaximumLifecycleChanges ||
                                        valueBytes_ + kKeep > kMaximumLifecycleValueBytes)) {
        valueBytes_ -= changes_[first_].value.size();
        changes_[first_].value = {};
        ++first_;
        ++dropped_;
    }
    changes_.push_back(LifecycleChange{.sequence = nextSequence_++,
                                       .kind = kind,
                                       .component = component,
                                       .entity = entity,
                                       .value = {value.begin(), value.begin() + static_cast<std::ptrdiff_t>(kKeep)}});
    valueBytes_ += kKeep;
    // With no reader of anything left behind, nothing is held.
    trim();
}

void Lifecycle::trim() noexcept {
    std::uint64_t least = nextSequence_;
    for (const LifecycleReader& each : readers_) {
        least = std::min(least, each.cursor);
    }
    while (first_ < changes_.size() && changes_[first_].sequence < least) {
        valueBytes_ -= changes_[first_].value.size();
        changes_[first_].value = {};
        ++first_;
    }
    // Compacted once the let-go are half of what is held, so each change
    // is moved a bounded number of times.
    if (first_ != 0 && first_ * 2 >= changes_.size()) {
        changes_.erase(changes_.begin(), changes_.begin() + static_cast<std::ptrdiff_t>(first_));
        first_ = 0;
    }
}

result::Status Lifecycle::restore(std::vector<LifecycleReader> readers,
                                  std::vector<LifecycleChange> changes,
                                  std::uint64_t nextSequence,
                                  std::uint64_t dropped) {
    if (readers.size() > kMaximumLifecycleReaders || changes.size() > kMaximumLifecycleChanges) {
        return refuse("a lifecycle holds more than its bounds");
    }
    std::vector<LifecycleWatch> watched;
    for (std::size_t index = 0; index < readers.size(); ++index) {
        const LifecycleReader& kReader = readers[index];
        if (kReader.cursor > nextSequence || kReader.watches.size() > kMaximumLifecycleWatches ||
            kReader.identity.empty() || kReader.identity.size() > kMaximumLifecycleIdentityBytes ||
            std::ranges::contains(std::span{readers}.first(index), kReader.identity, &LifecycleReader::identity)) {
            return refuse("a lifecycle reader is past the last change, watches too much, or is named twice");
        }
        for (const LifecycleWatch& kWatch : kReader.watches) {
            if ((kWatch.kind == LifecycleKind::Destroyed) == kWatch.component.has_value()) {
                return refuse("a destruction is watched for no component, anything else for one");
            }
            if (!std::ranges::contains(watched, kWatch)) {
                watched.push_back(kWatch);
            }
        }
    }
    std::size_t bytes = 0;
    for (std::size_t index = 0; index < changes.size(); ++index) {
        const LifecycleChange& kChange = changes[index];
        const bool kInOrder = index == 0 || kChange.sequence > changes[index - 1].sequence;
        if (!kInOrder || kChange.sequence >= nextSequence ||
            (kChange.kind == LifecycleKind::Destroyed) == kChange.component.has_value()) {
            return refuse("lifecycle changes do not rise below the next sequence, or name the wrong component");
        }
        bytes += kChange.value.size();
    }
    if (bytes > kMaximumLifecycleValueBytes) {
        return refuse("a lifecycle holds more than its bounds");
    }
    readers_ = std::move(readers);
    changes_ = std::move(changes);
    watched_ = std::move(watched);
    first_ = 0;
    valueBytes_ = bytes;
    nextSequence_ = nextSequence;
    dropped_ = dropped;
    return {};
}

} // namespace rawframe::world
