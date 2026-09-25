#include "rawframe/collision/filters.h"

#include "rawframe/collision/errors.h"

#include <algorithm>
#include <string_view>

namespace rawframe::collision {

namespace {

std::unexpected<result::Error> refuse(std::string_view why) {
    return result::fail(
        result::ErrorClass::InvalidArgument, kCollisionDomain, code(CollisionError::InvalidDocument), why);
}

} // namespace

result::Result<CollisionFilters> CollisionFilters::make(const CollisionDocument& document) {
    if (document.classes.size() > kMaximumCollisionClasses) {
        return refuse("a collision document declares more classes than it may");
    }
    CollisionFilters made;
    std::vector<std::string_view> names;
    for (std::size_t index = 0; index < document.classes.size(); ++index) {
        const CollisionClass& declared = document.classes[index];
        if (declared.id == 0 || declared.name.empty()) {
            return refuse("a collision class has identity nought or no name");
        }
        made.classes_.emplace_back(declared.id, index + 1);
        names.push_back(declared.name);
    }
    std::ranges::sort(made.classes_);
    std::ranges::sort(names);
    if (std::ranges::adjacent_find(made.classes_, {}, &std::pair<std::uint64_t, std::size_t>::first) !=
            made.classes_.end() ||
        std::ranges::adjacent_find(names) != names.end()) {
        return refuse("a collision class's identity or name is declared twice");
    }
    const std::size_t kCount = document.classes.size() + 1;
    std::vector<std::optional<CollisionRule>> rules(kCount * kCount);
    for (const CollisionPair& pair : document.rules) {
        const auto kFirst = made.classIndex(pair.first);
        const auto kSecond = made.classIndex(pair.second);
        if (pair.first == 0 || pair.second == 0 || !kFirst.has_value() || !kSecond.has_value()) {
            return refuse("a collision rule names a class the document does not declare");
        }
        auto& forward = rules[(*kFirst * kCount) + *kSecond];
        if (forward.has_value()) {
            return refuse("a pair of collision classes is ruled twice");
        }
        forward = pair.rule;
        rules[(*kSecond * kCount) + *kFirst] = pair.rule;
    }
    made.filters_.assign(kCount, ClassFilter{.solidMask = kCharacterQuery});
    for (std::size_t one = 0; one < kCount; ++one) {
        for (std::size_t other = 0; other < kCount; ++other) {
            const CollisionRule kRule = rules[(one * kCount) + other].value_or(document.fallback);
            ClassFilter& filter = made.filters_[one];
            if (kRule == CollisionRule::Collide) {
                filter.solidMask |= solidBit(other);
            }
            if (kRule != CollisionRule::Ignore) {
                filter.solidMask |= sensorBit(other);
                filter.sensorMask |= solidBit(other);
            }
            if (kRule == CollisionRule::Trigger) {
                filter.triggerMask |= solidBit(other);
            }
        }
    }
    return made;
}

std::optional<std::size_t> CollisionFilters::classIndex(std::uint64_t id) const noexcept {
    if (id == 0) {
        return 0;
    }
    const auto kFound =
        std::lower_bound(classes_.begin(), classes_.end(), id, [](const auto& entry, std::uint64_t key) {
            return entry.first < key;
        });
    return kFound != classes_.end() && kFound->first == id ? std::optional{kFound->second} : std::nullopt;
}

} // namespace rawframe::collision
