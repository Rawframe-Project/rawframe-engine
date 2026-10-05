// The contact report: each body's Contact3D filled from a step's event
// streams and what touches it now (moved out of physics.cpp, STD-0001).
#include "contacts.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace rawframe::physics3d {

namespace {

world::EntityHandle ownerOf(const ContactReport& report, m3ShapeId shape) {
    return shapeOwner(*report.owners, shape);
}

Contact3D* contactOf(world::World& world, const ContactReport& report, world::EntityHandle entity) {
    return entity.isNull() ? nullptr : static_cast<Contact3D*>(world.getErased(entity, report.contact));
}

} // namespace

void reportContacts(world::World& world, const ContactReport& report) {
    for (const Row& row : *report.rows) {
        if (Contact3D* const kContact = contactOf(world, report, row.entity)) {
            *kContact = Contact3D{};
        }
    }
    const m3ContactEvents kContacts = m3World_GetContactEvents(report.physics);
    for (std::int32_t index = 0; index < kContacts.beginCount; ++index) {
        ++report.statistics->contactsBegun;
        for (const m3ShapeId kShape : {kContacts.beginEvents[index].shapeIdA, kContacts.beginEvents[index].shapeIdB}) {
            if (Contact3D* const kContact = contactOf(world, report, ownerOf(report, kShape))) {
                ++kContact->began;
            }
        }
    }
    for (std::int32_t index = 0; index < kContacts.endCount; ++index) {
        for (const m3ShapeId kShape : {kContacts.endEvents[index].shapeIdA, kContacts.endEvents[index].shapeIdB}) {
            if (Contact3D* const kContact = contactOf(world, report, ownerOf(report, kShape))) {
                ++kContact->ended;
            }
        }
    }
    for (std::int32_t index = 0; index < kContacts.hitCount; ++index) {
        const m3ContactHitEvent& event = kContacts.hitEvents[index];
        const world::EntityHandle kA = ownerOf(report, event.shapeIdA);
        const world::EntityHandle kB = ownerOf(report, event.shapeIdB);
        for (const auto& [kSelf, kOther, kSign] : {std::tuple{kA, kB, 1.0F}, std::tuple{kB, kA, -1.0F}}) {
            Contact3D* const kContact = contactOf(world, report, kSelf);
            if (kContact == nullptr || (!kContact->hit.isNull() && event.approachSpeed <= kContact->hitSpeed)) {
                continue;
            }
            kContact->hit = kOther;
            kContact->hitSpeed = event.approachSpeed;
            kContact->hitNormalX = event.normal.x * kSign;
            kContact->hitNormalY = event.normal.y * kSign;
            kContact->hitNormalZ = event.normal.z * kSign;
        }
    }
    const m3SensorEvents kOverlaps = m3World_GetSensorEvents(report.physics);
    const auto kPair = [&report](m3ShapeId one, m3ShapeId other) {
        const world::EntityHandle kOne = ownerOf(report, one);
        const world::EntityHandle kOther = ownerOf(report, other);
        return kOne < kOther ? std::pair{kOne, kOther} : std::pair{kOther, kOne};
    };
    for (std::int32_t index = 0; index < kOverlaps.beginCount; ++index) {
        const auto kBetween = kPair(kOverlaps.beginEvents[index].shapeIdA, kOverlaps.beginEvents[index].shapeIdB);
        if (kBetween.first.isNull() || kBetween.first == kBetween.second || ++(*report.overlapping)[kBetween] != 1) {
            continue;
        }
        ++report.statistics->overlapsBegun;
        for (const auto& [kSelf, kOther] :
             {std::pair{kBetween.first, kBetween.second}, std::pair{kBetween.second, kBetween.first}}) {
            if (Contact3D* const kContact = contactOf(world, report, kSelf)) {
                ++kContact->entered;
                kContact->visitor = kContact->visitor.isNull() ? kOther : kContact->visitor;
            }
        }
    }
    for (std::int32_t index = 0; index < kOverlaps.endCount; ++index) {
        const auto kFound =
            (*report.overlapping).find(kPair(kOverlaps.endEvents[index].shapeIdA, kOverlaps.endEvents[index].shapeIdB));
        if (kFound == (*report.overlapping).end() || --kFound->second != 0) {
            continue;
        }
        for (const world::EntityHandle kSide : {kFound->first.first, kFound->first.second}) {
            if (Contact3D* const kContact = contactOf(world, report, kSide)) {
                ++kContact->exited;
            }
        }
        (*report.overlapping).erase(kFound);
    }
    // What touches and overlaps now.
    for (const auto& [kBetween, kShapes] : (*report.overlapping)) {
        for (const world::EntityHandle kSide : {kBetween.first, kBetween.second}) {
            if (Contact3D* const kContact = contactOf(world, report, kSide)) {
                ++kContact->overlapping;
            }
        }
    }
    for (const Row& row : *report.rows) {
        Contact3D* const kContact = contactOf(world, report, row.entity);
        const Mapped& entry = report.mapped->find(row.entity)->second;
        if (kContact == nullptr || entry.refused) {
            continue;
        }
        report.touching->resize(std::max<std::size_t>(report.touching->size(), 16));
        std::int32_t total = m3Body_GetContactData(
            entry.body, report.touching->data(), static_cast<std::int32_t>(report.touching->size()));
        if (static_cast<std::size_t>(total) > report.touching->size()) {
            report.touching->resize(static_cast<std::size_t>(total));
            total = m3Body_GetContactData(entry.body, report.touching->data(), total);
        }
        for (std::int32_t index = 0; index < total; ++index) {
            kContact->touching += (*report.touching)[static_cast<std::size_t>(index)].pointCount > 0 ? 1U : 0U;
        }
    }
}

} // namespace rawframe::physics3d
