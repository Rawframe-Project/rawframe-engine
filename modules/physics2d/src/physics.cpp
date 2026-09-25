#include "rawframe/physics2d/physics.h"

#include "attachments.h"
#include "characters.h"
#include "rawframe/physics/filters.h"
#include "rawframe/physics2d/components.h"
#include "rawframe/physics2d/errors.h"
#include "rawframe/world/query.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <map>
#include <maul2d/maul2d.h>
#include <mutex>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>

namespace rawframe::physics2d {

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, Physics2DError error, std::string_view why) {
    return result::fail(errorClass, kPhysics2DDomain, code(error), why);
}

/// Maul2D keeps its worlds in one process-wide table that the caller must
/// serialize.
std::mutex& worldTableLock() noexcept {
    static std::mutex lock;
    return lock;
}

/// Bit for bit, so a value written back is told from any other, NaNs and
/// signed noughts included. Only for the components without padding:
/// Pose2D, Velocity2D, and Impulse2D.
template <typename T> [[nodiscard]] bool same(const T& left, const T& right) noexcept {
    return std::memcmp(&left, &right, sizeof(T)) == 0;
}

/// Field by field: a Body2D has padding, which holds whatever it holds.
[[nodiscard]] bool same(const Body2D& left, const Body2D& right) noexcept {
    const auto kBits = [](float value) {
        return std::bit_cast<std::uint32_t>(value);
    };
    return left.motion == right.motion && left.shape == right.shape && left.fixedRotation == right.fixedRotation &&
           left.bullet == right.bullet && left.sensor == right.sensor && left.collisionClass == right.collisionClass &&
           kBits(left.width) == kBits(right.width) && kBits(left.height) == kBits(right.height) &&
           kBits(left.density) == kBits(right.density) && kBits(left.friction) == kBits(right.friction) &&
           kBits(left.restitution) == kBits(right.restitution) &&
           kBits(left.linearDamping) == kBits(right.linearDamping) &&
           kBits(left.angularDamping) == kBits(right.angularDamping);
}

[[nodiscard]] bool finite(float value) noexcept {
    return std::isfinite(value);
}

/// Whether a body can be made of these: sizes positive, the rest finite and
/// in range.
[[nodiscard]] bool makeable(const Body2D& body, const Pose2D& pose, const Velocity2D& velocity) noexcept {
    const bool kShape = body.shape == static_cast<std::uint8_t>(Shape::Circle) ||
                        body.shape == static_cast<std::uint8_t>(Shape::Box) ||
                        body.shape == static_cast<std::uint8_t>(Shape::Capsule);
    const bool kHeight = body.shape == static_cast<std::uint8_t>(Shape::Circle) ||
                         (body.shape == static_cast<std::uint8_t>(Shape::Box) && body.height > 0) ||
                         (body.shape == static_cast<std::uint8_t>(Shape::Capsule) && body.height >= 0);
    return body.motion <= static_cast<std::uint8_t>(physics::Motion::Dynamic) && kShape && body.width > 0 && kHeight &&
           finite(body.width) && finite(body.height) && body.density >= 0 && finite(body.density) &&
           body.friction >= 0 && finite(body.friction) && body.restitution >= 0 && body.restitution <= 1 &&
           body.linearDamping >= 0 && finite(body.linearDamping) && body.angularDamping >= 0 &&
           finite(body.angularDamping) && std::isfinite(pose.x) && std::isfinite(pose.y) && finite(pose.c) &&
           finite(pose.s) && finite(velocity.x) && finite(velocity.y) && finite(velocity.angular);
}

[[nodiscard]] m2Rot rotationOf(const Pose2D& pose) noexcept {
    return pose.c == 0 && pose.s == 0 ? m2Rot{1, 0} : m2Rot{pose.c, pose.s};
}

/// One entity's body, and what the last step wrote, to tell gameplay's
/// writes from its own.
struct Mapped {
    m2BodyId body{};
    m2ShapeId shape{};
    /// The sensor twin of a body whose class triggers with another.
    m2ShapeId trigger{};
    /// Its pose after each of the last steps, by tick modulo the history's
    /// length, and the first tick of its unbroken trail.
    std::vector<Pose2D> history;
    std::optional<std::uint64_t> since;
    bool refused = false;
    /// Made as a character's, which character queries do not see.
    bool character = false;
    Body2D made;
    Pose2D pose;
    Velocity2D velocity;
};

struct Row {
    world::EntityHandle entity;
    const Body2D* body = nullptr;
    Pose2D* pose = nullptr;
    Velocity2D* velocity = nullptr;
    Character2D* character = nullptr;
};

[[nodiscard]] bool sameBody(m2BodyId left, m2BodyId right) noexcept {
    return left.index1 == right.index1 && left.generation == right.generation && left.world == right.world;
}

/// One entity's joint: the joint made, what it was made of, and between
/// which bodies, to tell when either was made again.
struct MappedJoint {
    m2JointId joint{};
    Joint2D made;
    m2BodyId a{};
    m2BodyId b{};
    bool refused = false;
};

/// Field by field, for the padding a Joint2D has.
[[nodiscard]] bool same(const Joint2D& left, const Joint2D& right) noexcept {
    constexpr std::array kReals = {&Joint2D::anchorAX,
                                   &Joint2D::anchorAY,
                                   &Joint2D::anchorBX,
                                   &Joint2D::anchorBY,
                                   &Joint2D::axisX,
                                   &Joint2D::axisY,
                                   &Joint2D::linearLowerX,
                                   &Joint2D::linearLowerY,
                                   &Joint2D::linearUpperX,
                                   &Joint2D::linearUpperY,
                                   &Joint2D::angularLower,
                                   &Joint2D::angularUpper,
                                   &Joint2D::motorSpeed,
                                   &Joint2D::motorEffort,
                                   &Joint2D::breakForce,
                                   &Joint2D::breakTorque};
    constexpr std::array kBytes = {&Joint2D::linearX, &Joint2D::linearY, &Joint2D::angular, &Joint2D::motor};
    return left.a == right.a && left.b == right.b && left.collideConnected == right.collideConnected &&
           left.broken == right.broken &&
           std::ranges::all_of(kReals,
                               [&](float Joint2D::* field) {
                                   return std::bit_cast<std::uint32_t>(left.*field) ==
                                          std::bit_cast<std::uint32_t>(right.*field);
                               }) &&
           std::ranges::all_of(kBytes, [&](std::uint8_t Joint2D::* field) {
               return left.*field == right.*field;
           });
}

/// The Maul2D joint a Joint2D is, between two bodies, by the axes it lets
/// move: a weld, a revolute, a prismatic along one frame axis, or a filter;
/// the null joint for any other shape of axes or values out of range.
/// Maul2D refuses the rest (a limit's order, a body pair it cannot join).
[[nodiscard]] m2JointId makeJoint(m2WorldId physics, const Joint2D& joint, m2BodyId a, m2BodyId b) noexcept {
    constexpr auto kLocked = static_cast<std::uint8_t>(physics::JointAxis::Locked);
    constexpr auto kFree = static_cast<std::uint8_t>(physics::JointAxis::Free);
    constexpr auto kLimited = static_cast<std::uint8_t>(physics::JointAxis::Limited);
    const double kLength = std::sqrt((double{joint.axisX} * joint.axisX) + (double{joint.axisY} * joint.axisY));
    if (!std::isfinite(kLength) || joint.linearX > kLimited || joint.linearY > kLimited || joint.angular > kLimited ||
        joint.motor > 3 || !(joint.motorEffort >= 0) || !(joint.breakForce >= 0) || !std::isfinite(joint.breakForce) ||
        !(joint.breakTorque >= 0) || !std::isfinite(joint.breakTorque)) {
        return m2JointId{};
    }
    const m2Vec2 kAxis =
        kLength == 0 ? m2Vec2{1, 0}
                     : m2Vec2{static_cast<float>(joint.axisX / kLength), static_cast<float>(joint.axisY / kLength)};
    const m2Vec2 kAnchorA{joint.anchorAX, joint.anchorAY};
    const m2Vec2 kAnchorB{joint.anchorBX, joint.anchorBY};
    const bool kLinearLocked = joint.linearX == kLocked && joint.linearY == kLocked;
    if (kLinearLocked && joint.angular == kLocked && joint.motor == 0) {
        m2WeldJointDef definition = m2DefaultWeldJointDef();
        definition.bodyIdA = a;
        definition.bodyIdB = b;
        definition.localAnchorA = kAnchorA;
        definition.localAnchorB = kAnchorB;
        definition.collideConnected = joint.collideConnected;
        return m2CreateWeldJoint(physics, &definition);
    }
    if (kLinearLocked && joint.angular != kLocked && (joint.motor == 0 || joint.motor == 3)) {
        m2RevoluteJointDef definition = m2DefaultRevoluteJointDef();
        definition.bodyIdA = a;
        definition.bodyIdB = b;
        definition.localAnchorA = kAnchorA;
        definition.localAnchorB = kAnchorB;
        definition.enableLimit = joint.angular == kLimited;
        definition.lowerAngle = joint.angularLower;
        definition.upperAngle = joint.angularUpper;
        definition.enableMotor = joint.motor == 3;
        definition.motorSpeed = joint.motorSpeed;
        definition.maxMotorTorque = joint.motorEffort;
        definition.collideConnected = joint.collideConnected;
        return m2CreateRevoluteJoint(physics, &definition);
    }
    const bool kAlongX = joint.linearX != kLocked && joint.linearY == kLocked;
    const bool kAlongY = joint.linearY != kLocked && joint.linearX == kLocked;
    if ((kAlongX || kAlongY) && joint.angular == kLocked && (joint.motor == 0 || joint.motor == (kAlongX ? 1 : 2))) {
        m2PrismaticJointDef definition = m2DefaultPrismaticJointDef();
        definition.bodyIdA = a;
        definition.bodyIdB = b;
        definition.localAnchorA = kAnchorA;
        definition.localAnchorB = kAnchorB;
        definition.localAxisA = kAlongX ? kAxis : m2Vec2{-kAxis.y, kAxis.x};
        definition.enableLimit = (kAlongX ? joint.linearX : joint.linearY) == kLimited;
        definition.lowerTranslation = kAlongX ? joint.linearLowerX : joint.linearLowerY;
        definition.upperTranslation = kAlongX ? joint.linearUpperX : joint.linearUpperY;
        definition.enableMotor = joint.motor != 0;
        definition.motorSpeed = joint.motorSpeed;
        definition.maxMotorForce = joint.motorEffort;
        definition.collideConnected = joint.collideConnected;
        return m2CreatePrismaticJoint(physics, &definition);
    }
    if (joint.linearX == kFree && joint.linearY == kFree && joint.angular == kFree && joint.motor == 0 &&
        !joint.collideConnected) {
        // Nothing held: the pair only stops colliding.
        m2FilterJointDef definition = m2DefaultFilterJointDef();
        definition.bodyIdA = a;
        definition.bodyIdB = b;
        return m2CreateFilterJoint(physics, &definition);
    }
    return m2JointId{};
}

} // namespace

struct Physics2D::State {
    Physics2DSettings settings;
    /// The collision document as Maul2D's filter bits.
    physics::CollisionFilters filters;
    /// Entity pairs whose overlap was told this step, so a pair meeting
    /// through two sensors is told once.
    std::vector<std::pair<world::EntityHandle, world::EntityHandle>> toldEntered;
    std::vector<std::pair<world::EntityHandle, world::EntityHandle>> toldExited;
    std::vector<std::pair<world::EntityHandle, world::EntityHandle>> toldInside;
    /// The last tick stepped.
    std::uint64_t lastTick = 0;
    bool stepped = false;
    mutable std::uint64_t raysRewound = 0;
    mutable std::uint64_t rewindsClamped = 0;
    m2WorldId physics{};
    Physics2DStatistics statistics;
    std::map<world::EntityHandle, Mapped> mapped;
    std::optional<world::Query<world::Read<Body2D>, world::Write<Pose2D>, world::Write<Velocity2D>>> bodies;
    std::optional<schema::ComponentRuntimeId> impulse;
    std::optional<schema::ComponentRuntimeId> contact;
    std::optional<schema::ComponentRuntimeId> character;
    std::optional<world::Query<world::Write<Joint2D>>> jointQuery;
    std::optional<Attachments> attachments;
    std::map<world::EntityHandle, MappedJoint> joints;
    std::vector<std::pair<world::EntityHandle, Joint2D*>> jointRows;
    /// Whose each live shape is, by its index; the generation tells a
    /// reused index from the shape an event names.
    std::map<std::int32_t, std::pair<std::uint16_t, world::EntityHandle>> owners;
    /// Live sensor shapes, in entity order.
    std::vector<m2ShapeId> sensors;
    std::vector<m2ShapeId> overlaps;
    /// Scratch for overlap queries, which are const.
    mutable std::vector<m2ShapeId> overlapShapes;
    std::vector<m2ContactData> touching;
    std::vector<schema::ComponentRuntimeId> reads;
    std::vector<schema::ComponentRuntimeId> writes;
    std::unique_ptr<world::System> system;
    std::vector<Row> rows;

    ~State() {
        if (m2World_IsValid(physics)) {
            const std::scoped_lock kLock{worldTableLock()};
            m2DestroyWorld(physics);
        }
    }

    [[nodiscard]] bool make(const Row& row, Mapped& into) {
        const Body2D& body = *row.body;
        into.made = body;
        into.pose = *row.pose;
        into.velocity = *row.velocity;
        into.body = {};
        into.shape = {};
        into.trigger = {};
        into.since.reset();
        into.character = row.character != nullptr;
        const auto kClass = filters.classIndex(body.collisionClass);
        into.refused = !makeable(body, *row.pose, *row.velocity) || !kClass.has_value();
        if (into.refused) {
            ++statistics.bodiesRefused;
            return false;
        }
        m2BodyDef definition = m2DefaultBodyDef();
        definition.type = static_cast<m2BodyType>(body.motion);
        definition.position = m2Pos2{row.pose->x, row.pose->y};
        definition.rotation = rotationOf(*row.pose);
        definition.linearVelocity = m2Vec2{row.velocity->x, row.velocity->y};
        definition.angularVelocity = row.velocity->angular;
        definition.linearDamping = body.linearDamping;
        definition.angularDamping = body.angularDamping;
        definition.fixedRotation = body.fixedRotation;
        definition.isBullet = body.bullet;
        definition.userData = (std::uint64_t{row.entity.slot} << 32U) | row.entity.generation;
        const m2BodyId kBody = m2CreateBody(physics, &definition);
        if (kBody.index1 == 0) {
            into.refused = true;
            ++statistics.bodiesRefused;
            return false;
        }
        m2ShapeDef shape = m2DefaultShapeDef();
        shape.density = body.density;
        shape.friction = body.friction;
        shape.restitution = body.restitution;
        shape.isSensor = body.sensor;
        const physics::ClassFilter& filter = filters.filter(*kClass);
        shape.categoryBits =
            body.sensor ? physics::CollisionFilters::sensorBit(*kClass) : physics::CollisionFilters::solidBit(*kClass);
        shape.maskBits = body.sensor ? filter.sensorMask : filter.solidMask;
        if (into.character) {
            shape.maskBits &= ~physics::kCharacterQuery;
        }
        const auto kShape = [&](const m2ShapeDef& definitionOf) {
            if (body.shape == static_cast<std::uint8_t>(Shape::Circle)) {
                const m2Circle kCircle{.center = {0, 0}, .radius = body.width};
                return m2CreateCircleShape(kBody, &definitionOf, &kCircle);
            }
            if (body.shape == static_cast<std::uint8_t>(Shape::Box)) {
                const m2Polygon kBox = m2MakeBox(body.width, body.height);
                return m2CreatePolygonShape(kBody, &definitionOf, &kBox);
            }
            const m2Capsule kCapsule{.point1 = {0, -body.height}, .point2 = {0, body.height}, .radius = body.width};
            return m2CreateCapsuleShape(kBody, &definitionOf, &kCapsule);
        };
        const m2ShapeId kMade = kShape(shape);
        m2ShapeId twin{};
        if (kMade.index1 != 0 && !body.sensor && filter.triggerMask != 0) {
            m2ShapeDef sensor = shape;
            sensor.isSensor = true;
            sensor.density = 0;
            sensor.categoryBits = physics::CollisionFilters::sensorBit(*kClass);
            sensor.maskBits = filter.triggerMask;
            twin = kShape(sensor);
        }
        if (kMade.index1 == 0 || (!body.sensor && filter.triggerMask != 0 && twin.index1 == 0)) {
            m2DestroyBody(kBody);
            into.refused = true;
            ++statistics.bodiesRefused;
            return false;
        }
        into.body = kBody;
        into.shape = kMade;
        into.trigger = twin;
        owners[kMade.index1] = {kMade.generation, row.entity};
        if (twin.index1 != 0) {
            owners[twin.index1] = {twin.generation, row.entity};
        }
        ++statistics.bodiesMade;
        return true;
    }

    /// Whether this step has not told this pair of entities yet, and now has.
    [[nodiscard]] static bool firstTelling(std::vector<std::pair<world::EntityHandle, world::EntityHandle>>& told,
                                           world::EntityHandle one,
                                           world::EntityHandle other) {
        const auto kPair = one < other ? std::pair{one, other} : std::pair{other, one};
        const auto kAt = std::lower_bound(told.begin(), told.end(), kPair);
        if (kAt != told.end() && *kAt == kPair) {
            return false;
        }
        told.insert(kAt, kPair);
        return true;
    }

    /// The entity whose body a shape is, or the null entity for a shape
    /// already gone.
    [[nodiscard]] world::EntityHandle ownerOf(m2ShapeId shape) const {
        const auto kOwner = owners.find(shape.index1);
        return kOwner != owners.end() && kOwner->second.first == shape.generation ? kOwner->second.second
                                                                                  : world::EntityHandle{};
    }

    /// The query filter that sees the shapes of the class `among`, its
    /// solids and sensors by its two bits, or all.
    [[nodiscard]] std::optional<m2QueryFilter> amongFilter(std::uint64_t among) const noexcept {
        m2QueryFilter filter{~std::uint64_t{0}, ~std::uint64_t{0}};
        if (among == physics::kEveryClass) {
            return filter;
        }
        const auto kClass = filters.classIndex(among);
        if (!kClass.has_value()) {
            return std::nullopt;
        }
        filter.maskBits = physics::CollisionFilters::solidBit(*kClass) | physics::CollisionFilters::sensorBit(*kClass);
        return filter;
    }

    /// A cast's closest hit as the answer a script reads.
    [[nodiscard]] RayHit2D hitOf(const m2RayCastResult& result) const {
        if (!result.hit) {
            return RayHit2D{};
        }
        return RayHit2D{.hit = true,
                        .inside = result.normal.x == 0 && result.normal.y == 0,
                        .entity = ownerOf(result.shapeId),
                        .x = result.point.x,
                        .y = result.point.y,
                        .normalX = result.normal.x,
                        .normalY = result.normal.y,
                        .fraction = result.fraction};
    }

    [[nodiscard]] Contact2D* contactOf(world::World& world, world::EntityHandle entity) const {
        return entity.isNull() ? nullptr : static_cast<Contact2D*>(world.getErased(entity, *contact));
    }

    /// Every Contact2D of a body, from this step's event streams and what
    /// touches and overlaps now, all in Maul2D's canonical order.
    void report(world::World& world) {
        sensors.clear();
        toldEntered.clear();
        toldExited.clear();
        toldInside.clear();
        for (const Row& row : rows) {
            Contact2D* const kContact = contactOf(world, row.entity);
            if (kContact != nullptr) {
                *kContact = Contact2D{};
            }
            const Mapped& entry = mapped.find(row.entity)->second;
            if (!entry.refused && entry.made.sensor) {
                sensors.push_back(entry.shape);
            }
            if (!entry.refused && entry.trigger.index1 != 0) {
                sensors.push_back(entry.trigger);
            }
        }
        const m2ContactEvents kContacts = m2World_GetContactEvents(physics);
        for (std::int32_t index = 0; index < kContacts.beginCount; ++index) {
            const m2ContactBeginEvent& event = kContacts.beginEvents[index];
            const world::EntityHandle kA = ownerOf(event.shapeIdA);
            const world::EntityHandle kB = ownerOf(event.shapeIdB);
            ++statistics.contactsBegun;
            for (const auto& [kSelf, kOther, kSign] : {std::tuple{kA, kB, 1.0F}, std::tuple{kB, kA, -1.0F}}) {
                Contact2D* const kContact = contactOf(world, kSelf);
                if (kContact == nullptr) {
                    continue;
                }
                ++kContact->began;
                if (kContact->hit.isNull() || event.approachSpeed > kContact->hitSpeed) {
                    kContact->hit = kOther;
                    kContact->hitSpeed = event.approachSpeed;
                    kContact->hitNormalX = event.normal.x * kSign;
                    kContact->hitNormalY = event.normal.y * kSign;
                }
            }
        }
        for (std::int32_t index = 0; index < kContacts.endCount; ++index) {
            for (const m2ShapeId kShape : {kContacts.endEvents[index].shapeIdA, kContacts.endEvents[index].shapeIdB}) {
                if (Contact2D* const kContact = contactOf(world, ownerOf(kShape))) {
                    ++kContact->ended;
                }
            }
        }
        const m2SensorEvents kOverlaps = m2World_GetSensorEvents(physics);
        for (std::int32_t index = 0; index < kOverlaps.beginCount; ++index) {
            const world::EntityHandle kA = ownerOf(kOverlaps.beginEvents[index].shapeIdA);
            const world::EntityHandle kB = ownerOf(kOverlaps.beginEvents[index].shapeIdB);
            if (!firstTelling(toldEntered, kA, kB)) {
                continue;
            }
            ++statistics.overlapsBegun;
            for (const auto& [kSelf, kOther] : {std::pair{kA, kB}, std::pair{kB, kA}}) {
                if (Contact2D* const kContact = contactOf(world, kSelf)) {
                    ++kContact->entered;
                    kContact->visitor = kContact->visitor.isNull() ? kOther : kContact->visitor;
                }
            }
        }
        for (std::int32_t index = 0; index < kOverlaps.endCount; ++index) {
            if (!firstTelling(toldExited,
                              ownerOf(kOverlaps.endEvents[index].shapeIdA),
                              ownerOf(kOverlaps.endEvents[index].shapeIdB))) {
                continue;
            }
            for (const m2ShapeId kShape : {kOverlaps.endEvents[index].shapeIdA, kOverlaps.endEvents[index].shapeIdB}) {
                if (Contact2D* const kContact = contactOf(world, ownerOf(kShape))) {
                    ++kContact->exited;
                }
            }
        }
        // What touches and overlaps now.
        touching.resize(std::max<std::size_t>(touching.size(), 64));
        std::int32_t total =
            m2World_GetContactData(physics, touching.data(), static_cast<std::int32_t>(touching.size()));
        if (static_cast<std::size_t>(total) > touching.size()) {
            touching.resize(static_cast<std::size_t>(total));
            total = m2World_GetContactData(physics, touching.data(), total);
        }
        for (std::int32_t index = 0; index < total; ++index) {
            for (const m2ShapeId kShape : {touching[static_cast<std::size_t>(index)].shapeIdA,
                                           touching[static_cast<std::size_t>(index)].shapeIdB}) {
                if (Contact2D* const kContact = contactOf(world, ownerOf(kShape))) {
                    ++kContact->touching;
                }
            }
        }
        for (const m2ShapeId kSensor : sensors) {
            overlaps.resize(std::max<std::size_t>(overlaps.size(), 16));
            std::int32_t inside =
                m2Shape_GetSensorOverlaps(kSensor, overlaps.data(), static_cast<std::int32_t>(overlaps.size()));
            if (static_cast<std::size_t>(inside) > overlaps.size()) {
                overlaps.resize(static_cast<std::size_t>(inside));
                inside = m2Shape_GetSensorOverlaps(kSensor, overlaps.data(), inside);
            }
            const world::EntityHandle kSensorEntity = ownerOf(kSensor);
            Contact2D* const kSensorContact = contactOf(world, kSensorEntity);
            for (std::int32_t index = 0; index < inside; ++index) {
                const world::EntityHandle kInside = ownerOf(overlaps[static_cast<std::size_t>(index)]);
                if (!firstTelling(toldInside, kSensorEntity, kInside)) {
                    continue;
                }
                if (kSensorContact != nullptr) {
                    ++kSensorContact->overlapping;
                }
                if (Contact2D* const kContact = contactOf(world, kInside)) {
                    ++kContact->overlapping;
                }
            }
        }
    }

    /// A character's move this tick, as the velocity its kinematic body
    /// steps with; one that is not a made kinematic capsule stays Airborne
    /// and moves as its body would.
    void moveCharacter(const Row& row, const Mapped& entry, float seconds) {
        Character2D& controlled = *row.character;
        const bool kWasGrounded = controlled.ground == static_cast<std::uint8_t>(physics::Ground::Grounded);
        controlled.ground = static_cast<std::uint8_t>(physics::Ground::Airborne);
        controlled.groundNormalX = 0;
        controlled.groundNormalY = 0;
        const Body2D& body = entry.made;
        if (entry.refused || body.motion != static_cast<std::uint8_t>(physics::Motion::Kinematic) ||
            body.shape != static_cast<std::uint8_t>(Shape::Capsule) || body.sensor ||
            !finite(controlled.groundNormal) || !finite(controlled.snap) || !(seconds > 0)) {
            return;
        }
        const m2Vec2 kWish = m2Body_GetLinearVelocity(entry.body);
        const m2QueryFilter kFilter{.categoryBits = physics::kCharacterQuery,
                                    .maskBits = filters.filter(*filters.classIndex(body.collisionClass)).solidMask &
                                                physics::kSolids};
        const CharacterMove kMove = physics2d::moveCharacter(physics,
                                                             body.width,
                                                             body.height,
                                                             m2Body_GetTransform(entry.body),
                                                             m2Vec2{kWish.x * seconds, kWish.y * seconds},
                                                             controlled,
                                                             kWasGrounded,
                                                             kFilter);
        m2Body_SetLinearVelocity(entry.body, m2Vec2{kMove.translation.x / seconds, kMove.translation.y / seconds});
        m2Body_SetAngularVelocity(entry.body, 0);
        controlled.ground = static_cast<std::uint8_t>(kMove.ground);
        controlled.groundNormalX = kMove.normal.x;
        controlled.groundNormalY = kMove.normal.y;
        ++statistics.characterMoves;
    }

    void remove(Mapped& entry) {
        if (!entry.refused) {
            owners.erase(entry.shape.index1);
            owners.erase(entry.trigger.index1);
            m2DestroyBody(entry.body);
            ++statistics.bodiesRemoved;
        }
    }

    /// The body an entity has now, or the null body.
    [[nodiscard]] m2BodyId bodyOf(world::EntityHandle entity) const {
        const auto kFound = entity.isNull() ? mapped.end() : mapped.find(entity);
        return kFound == mapped.end() || kFound->second.refused ? m2BodyId{} : kFound->second.body;
    }

    /// 3. Joints follow their entities: what is gone first, then what is
    /// new, changed, or between a body made again.
    void followJoints(world::World& world) {
        jointRows.clear();
        jointQuery->forEach(world, [this](world::EntityHandle entity, Joint2D& joint) {
            jointRows.emplace_back(entity, &joint);
        });
        std::ranges::sort(jointRows, {}, &std::pair<world::EntityHandle, Joint2D*>::first);
        for (auto entry = joints.begin(); entry != joints.end();) {
            if (std::ranges::binary_search(
                    jointRows, entry->first, {}, &std::pair<world::EntityHandle, Joint2D*>::first)) {
                ++entry;
                continue;
            }
            if (m2Joint_IsValid(entry->second.joint)) {
                m2DestroyJoint(entry->second.joint);
            }
            ++statistics.jointsRemoved;
            entry = joints.erase(entry);
        }
        for (const auto& [kEntity, kJoint] : jointRows) {
            const auto [kEntry, kNew] = joints.try_emplace(kEntity);
            MappedJoint& entry = kEntry->second;
            const m2BodyId kA = bodyOf(kJoint->a);
            const m2BodyId kB = bodyOf(kJoint->b);
            // A joint whose body was made again went with the body.
            if (!kNew && same(entry.made, *kJoint) && sameBody(entry.a, kA) && sameBody(entry.b, kB) &&
                (entry.refused || m2Joint_IsValid(entry.joint))) {
                continue;
            }
            if (!entry.refused && m2Joint_IsValid(entry.joint)) {
                m2DestroyJoint(entry.joint);
            }
            entry = MappedJoint{.joint = {}, .made = *kJoint, .a = kA, .b = kB, .refused = true};
            // Broken, it waits for gameplay to mend it.
            if (kJoint->broken) {
                continue;
            }
            if (kA.index1 != 0 && kB.index1 != 0 && !sameBody(kA, kB)) {
                entry.joint = makeJoint(physics, *kJoint, kA, kB);
                entry.refused = entry.joint.index1 == 0;
            }
            if (!entry.refused && (kJoint->breakForce > 0 || kJoint->breakTorque > 0)) {
                m2Joint_SetBreakLimits(entry.joint, kJoint->breakForce, kJoint->breakTorque);
            }
            ++(entry.refused ? statistics.jointsRefused : statistics.jointsMade);
        }
    }

    /// The joints the step broke: each written `broken`, and kept unmade.
    void breakJoints() {
        const m2JointEvents kEvents = m2World_GetJointEvents(physics);
        for (std::int32_t index = 0; index < kEvents.breakCount; ++index) {
            const m2JointId kBroken = kEvents.breakEvents[index].jointId;
            for (auto& [kEntity, entry] : joints) {
                if (entry.refused || entry.joint.index1 != kBroken.index1 ||
                    entry.joint.generation != kBroken.generation) {
                    continue;
                }
                const auto kRow =
                    std::ranges::lower_bound(jointRows, kEntity, {}, &std::pair<world::EntityHandle, Joint2D*>::first);
                kRow->second->broken = true;
                entry.made.broken = true;
                entry.joint = {};
                entry.refused = true;
                ++statistics.jointsBroken;
                break;
            }
        }
    }

    result::Status step(world::World& world, world::TickRate rate, world::TickIndex tick) {
        rows.clear();
        bodies->forEach(world,
                        [this](world::EntityHandle entity, const Body2D& body, Pose2D& pose, Velocity2D& velocity) {
                            rows.push_back(Row{.entity = entity, .body = &body, .pose = &pose, .velocity = &velocity});
                        });
        for (Row& row : rows) {
            row.character = static_cast<Character2D*>(world.getErased(row.entity, *character));
        }
        std::sort(rows.begin(), rows.end(), [](const Row& left, const Row& right) {
            return left.entity < right.entity;
        });

        // 1. Bodies follow their entities, in entity order: first what is
        // gone, then what is new or remade.
        auto next = rows.begin();
        for (auto entry = mapped.begin(); entry != mapped.end();) {
            next = std::lower_bound(next, rows.end(), entry->first, [](const Row& row, world::EntityHandle key) {
                return row.entity < key;
            });
            if (next != rows.end() && next->entity == entry->first) {
                ++entry;
                continue;
            }
            remove(entry->second);
            entry = mapped.erase(entry);
        }
        for (const Row& row : rows) {
            const auto [kEntry, kNew] = mapped.try_emplace(row.entity);
            Mapped& entry = kEntry->second;
            if (kNew || !same(entry.made, *row.body) || entry.character != (row.character != nullptr)) {
                if (!kNew) {
                    remove(entry);
                }
                if (!make(row, entry)) {
                    continue;
                }
            } else if (entry.refused) {
                // Refused until its Body2D changes, or its pose or velocity
                // does and so might now be makeable.
                if (same(entry.pose, *row.pose) && same(entry.velocity, *row.velocity)) {
                    continue;
                }
                if (!make(row, entry)) {
                    continue;
                }
            } else {
                // 2. What gameplay wrote since the last step.
                if (!same(entry.pose, *row.pose)) {
                    remove(entry);
                    ++statistics.teleports;
                    if (!make(row, entry)) {
                        continue;
                    }
                }
                if (!same(entry.velocity, *row.velocity)) {
                    if (finite(row.velocity->x) && finite(row.velocity->y) && finite(row.velocity->angular)) {
                        m2Body_SetLinearVelocity(entry.body, m2Vec2{row.velocity->x, row.velocity->y});
                        m2Body_SetAngularVelocity(entry.body, row.velocity->angular);
                        ++statistics.velocitiesSet;
                    }
                }
            }
            if (impulse) {
                auto* const kImpulse = static_cast<Impulse2D*>(world.getErased(row.entity, *impulse));
                if (kImpulse != nullptr && !same(*kImpulse, Impulse2D{})) {
                    if (finite(kImpulse->x) && finite(kImpulse->y) && finite(kImpulse->angular)) {
                        m2Body_ApplyLinearImpulse(entry.body, m2Vec2{kImpulse->x, kImpulse->y});
                        m2Body_ApplyAngularImpulse(entry.body, kImpulse->angular);
                        ++statistics.impulses;
                    }
                    *kImpulse = Impulse2D{};
                }
            }
        }

        followJoints(world);

        // 4. Characters find their moves, in entity order and each against
        // the world as the last step left it; then one step of the tick's
        // length.
        const auto kSeconds = static_cast<float>(static_cast<double>(rate.seconds) / static_cast<double>(rate.ticks));
        for (const Row& row : rows) {
            if (row.character != nullptr) {
                moveCharacter(row, mapped.find(row.entity)->second, kSeconds);
            }
        }
        m2World_Step(physics, kSeconds, static_cast<std::int32_t>(settings.substeps));
        ++statistics.steps;

        report(world);
        breakJoints();

        // 5. Every body's pose and velocity back into the World.
        for (const Row& row : rows) {
            Mapped& entry = mapped.find(row.entity)->second;
            if (entry.refused) {
                continue;
            }
            const m2Transform kTransform = m2Body_GetTransform(entry.body);
            const m2Vec2 kLinear = m2Body_GetLinearVelocity(entry.body);
            *row.pose = Pose2D{.x = kTransform.p.x, .y = kTransform.p.y, .c = kTransform.q.c, .s = kTransform.q.s};
            *row.velocity =
                Velocity2D{.x = kLinear.x, .y = kLinear.y, .angular = m2Body_GetAngularVelocity(entry.body)};
            entry.pose = *row.pose;
            entry.velocity = *row.velocity;
            if (settings.historyTicks != 0) {
                entry.history.resize(settings.historyTicks);
                entry.history[tick.value % settings.historyTicks] = entry.pose;
                entry.since = entry.since.value_or(tick.value);
            }
        }
        // 6. Attached entities follow their parents where they now are.
        attachments->follow(world, statistics.attachmentsRefused);
        lastTick = tick.value;
        stepped = true;
        return {};
    }
};

namespace {

class Step final : public world::System {
public:
    explicit Step(Physics2D::State& state) noexcept : state_(&state) {
    }
    result::Status run(world::SystemContext& context) noexcept override {
        return state_->step(context.world, context.rate, context.tick);
    }

private:
    Physics2D::State* state_;
};

} // namespace

Physics2D::Physics2D(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Physics2D::~Physics2D() = default;

result::Result<std::unique_ptr<Physics2D>> Physics2D::create(const Physics2DSettings& settings) {
    constexpr std::uint32_t kMost = 1U << 20U;
    if (settings.historyTicks > 1024) {
        return refuse(result::ErrorClass::InvalidArgument,
                      Physics2DError::InvalidSettings,
                      "physics settings: at most 1024 ticks of history");
    }
    if (!std::isfinite(settings.gravityX) || !std::isfinite(settings.gravityY) || settings.substeps == 0 ||
        settings.substeps > 64 || settings.bodyCapacity == 0 || settings.bodyCapacity > kMost ||
        settings.shapeCapacity == 0 || settings.shapeCapacity > kMost || settings.jointCapacity == 0 ||
        settings.jointCapacity > kMost) {
        return refuse(result::ErrorClass::InvalidArgument,
                      Physics2DError::InvalidSettings,
                      "physics settings: finite gravity, 1 to 64 substeps, and capacities of 1 to 2^20");
    }
    if (m2CpuSupportsBackend() == 0) {
        return refuse(result::ErrorClass::Unsupported,
                      Physics2DError::Unsupported,
                      "this processor cannot run the physics build's kernels");
    }
    auto state = std::make_unique<State>();
    state->settings = settings;
    RAWFRAME_TRY_ASSIGN(state->filters, physics::CollisionFilters::make(settings.collision));
    m2WorldDef definition = m2DefaultWorldDef();
    definition.gravity = m2Vec2{settings.gravityX, settings.gravityY};
    definition.bodyCapacity = static_cast<std::int32_t>(settings.bodyCapacity);
    definition.shapeCapacity = static_cast<std::int32_t>(settings.shapeCapacity);
    definition.jointCapacity = static_cast<std::int32_t>(settings.jointCapacity);
    definition.enableSleeping = settings.sleeping;
    {
        const std::scoped_lock kLock{worldTableLock()};
        state->physics = m2CreateWorld(&definition);
    }
    if (state->physics.index1 == 0) {
        return refuse(result::ErrorClass::ResourceExhausted,
                      Physics2DError::Capacity,
                      "no room for another physics world in this process");
    }
    return std::make_unique<Physics2D>(std::move(state));
}

result::Status Physics2D::declareSystems(const schema::SchemaRegistry& registry,
                                         std::vector<world::SystemDeclaration>& systems) noexcept {
    State& state = *state_;
    for (const physics::ComponentLayout& layout : componentLayouts()) {
        const auto kId = registry.find(layout.id);
        if (!kId.has_value() || registry.descriptor(*kId).size != layout.size || !registry.descriptor(*kId).plainData) {
            return refuse(result::ErrorClass::InvalidArgument,
                          Physics2DError::InvalidSettings,
                          "the World does not hold the engine's physics components");
        }
    }
    RAWFRAME_TRY_ASSIGN(
        state.bodies,
        (world::Query<world::Read<Body2D>, world::Write<Pose2D>, world::Write<Velocity2D>>::resolve(registry)));
    RAWFRAME_TRY_ASSIGN(state.impulse, registry.find(Impulse2D::kComponentTypeId));
    RAWFRAME_TRY_ASSIGN(state.contact, registry.find(Contact2D::kComponentTypeId));
    RAWFRAME_TRY_ASSIGN(state.character, registry.find(Character2D::kComponentTypeId));
    RAWFRAME_TRY_ASSIGN(state.jointQuery, (world::Query<world::Write<Joint2D>>::resolve(registry)));
    RAWFRAME_TRY_ASSIGN(state.attachments, Attachments::resolve(registry));
    state.reads = state.bodies->reads();
    const std::vector<schema::ComponentRuntimeId> kAttachReads = state.attachments->reads();
    state.reads.insert(state.reads.end(), kAttachReads.begin(), kAttachReads.end());
    state.writes = state.bodies->writes();
    state.writes.push_back(*state.impulse);
    state.writes.push_back(*state.contact);
    state.writes.push_back(*state.character);
    const std::vector<schema::ComponentRuntimeId> kJointWrites = state.jointQuery->writes();
    state.writes.insert(state.writes.end(), kJointWrites.begin(), kJointWrites.end());
    state.system = std::make_unique<Step>(state);
    systems.push_back(world::SystemDeclaration{.identity = kStepSystem,
                                               .phase = world::Phase::Simulation,
                                               .reads = state.reads,
                                               .writes = state.writes,
                                               .system = state.system.get()});
    return {};
}

RayHit2D
Physics2D::castRay(double originX, double originY, float towardX, float towardY, std::uint64_t among) const noexcept {
    const auto kFilter = state_->amongFilter(among);
    if (!kFilter.has_value()) {
        return RayHit2D{};
    }
    return state_->hitOf(
        m2World_CastRayClosest(state_->physics, m2Pos2{originX, originY}, m2Vec2{towardX, towardY}, *kFilter));
}

RayHit2D Physics2D::castCircle(
    double originX, double originY, float radius, float towardX, float towardY, std::uint64_t among) const noexcept {
    const auto kFilter = state_->amongFilter(among);
    if (!kFilter.has_value() || !(radius > 0) || !std::isfinite(radius)) {
        return RayHit2D{};
    }
    const m2Circle kCircle{.center = {0, 0}, .radius = radius};
    return state_->hitOf(m2World_CastCircleClosest(state_->physics,
                                                   &kCircle,
                                                   m2Transform{.p = {originX, originY}, .q = {1, 0}},
                                                   m2Vec2{towardX, towardY},
                                                   *kFilter));
}

void Physics2D::overlapCircle(
    double x, double y, float radius, std::uint64_t among, std::vector<world::EntityHandle>& into) const {
    into.clear();
    const auto kFilter = state_->amongFilter(among);
    if (!kFilter.has_value() || !(radius > 0) || !std::isfinite(radius)) {
        return;
    }
    const m2Circle kCircle{.center = {0, 0}, .radius = radius};
    const m2Transform kAt{.p = {x, y}, .q = {1, 0}};
    std::vector<m2ShapeId>& shapes = state_->overlapShapes;
    shapes.resize(std::max<std::size_t>(shapes.size(), 16));
    std::int32_t total = m2World_OverlapCircle(
        state_->physics, &kCircle, kAt, shapes.data(), static_cast<std::int32_t>(shapes.size()), *kFilter);
    if (static_cast<std::size_t>(total) > shapes.size()) {
        shapes.resize(static_cast<std::size_t>(total));
        total = m2World_OverlapCircle(state_->physics, &kCircle, kAt, shapes.data(), total, *kFilter);
    }
    for (std::int32_t index = 0; index < total; ++index) {
        const world::EntityHandle kOwner = state_->ownerOf(shapes[static_cast<std::size_t>(index)]);
        if (!kOwner.isNull()) {
            into.push_back(kOwner);
        }
    }
    // A body met through its sensor twin too is told once.
    std::ranges::sort(into);
    into.erase(std::unique(into.begin(), into.end()), into.end());
}

namespace {

/// A pose as a rotation that is a unit complex number: what presentation
/// shows of a blended one.
[[nodiscard]] m2Transform transformOf(const Pose2D& pose) noexcept {
    const double kLength = std::sqrt((double{pose.c} * pose.c) + (double{pose.s} * pose.s));
    if (!(kLength > 0)) {
        return m2Transform{.p = {pose.x, pose.y}, .q = {1, 0}};
    }
    return m2Transform{.p = {pose.x, pose.y},
                       .q = {static_cast<float>(pose.c / kLength), static_cast<float>(pose.s / kLength)}};
}

/// Where a pose's frame puts a world point or direction, and back.
[[nodiscard]] m2Pos2 toLocal(const m2Transform& frame, m2Pos2 point) noexcept {
    const double kX = point.x - frame.p.x;
    const double kY = point.y - frame.p.y;
    return m2Pos2{(frame.q.c * kX) + (frame.q.s * kY), (-frame.q.s * kX) + (frame.q.c * kY)};
}
[[nodiscard]] m2Pos2 toWorld(const m2Transform& frame, m2Pos2 local) noexcept {
    return m2Pos2{frame.p.x + (frame.q.c * local.x) - (frame.q.s * local.y),
                  frame.p.y + (frame.q.s * local.x) + (frame.q.c * local.y)};
}
[[nodiscard]] m2Vec2 turnToLocal(const m2Transform& frame, m2Vec2 vector) noexcept {
    return m2Vec2{(frame.q.c * vector.x) + (frame.q.s * vector.y), (-frame.q.s * vector.x) + (frame.q.c * vector.y)};
}
[[nodiscard]] m2Vec2 turnToWorld(const m2Transform& frame, m2Vec2 vector) noexcept {
    return m2Vec2{(frame.q.c * vector.x) - (frame.q.s * vector.y), (frame.q.s * vector.x) + (frame.q.c * vector.y)};
}

} // namespace

RayHit2D Physics2D::castRayAt(double originX,
                              double originY,
                              float towardX,
                              float towardY,
                              const physics::Moment& moment,
                              std::uint64_t among) const noexcept {
    const State& state = *state_;
    const std::uint32_t kKept = state.settings.historyTicks;
    if (!state.stepped || kKept == 0) {
        return castRay(originX, originY, towardX, towardY, among);
    }
    if (among != physics::kEveryClass && !state.filters.classIndex(among).has_value()) {
        return RayHit2D{};
    }
    ++state.raysRewound;
    const std::uint64_t kOldest = state.lastTick + 1 >= kKept ? state.lastTick + 1 - kKept : 0;
    if (moment.base < kOldest || moment.base > state.lastTick ||
        (moment.base == state.lastTick && moment.fraction != 0)) {
        ++state.rewindsClamped;
    }
    const std::uint64_t kBase = std::clamp(moment.base, kOldest, state.lastTick);
    const std::uint16_t kFraction = kBase == state.lastTick ? std::uint16_t{0} : moment.fraction;
    const double kAlong = kFraction / 65536.0;

    RayHit2D closest;
    const m2Vec2 kToward{towardX, towardY};
    for (const auto& [entity, entry] : state.mapped) {
        if (entry.refused || (among != physics::kEveryClass && entry.made.collisionClass != among)) {
            continue;
        }
        const m2Transform kNow = m2Body_GetTransform(entry.body);
        // The gate may hold it to a later tick, or keep it where it is.
        const std::optional<std::uint64_t> kSince =
            moment.gate == nullptr ? std::optional<std::uint64_t>{0} : moment.gate->since(entity);
        const bool kRewound = kSince.has_value() && *kSince < state.lastTick;
        const std::uint64_t kTick = kRewound ? std::max(kBase, *kSince) : kBase;
        const bool kTrail = entry.since.has_value() && *entry.since <= kTick;
        m2Transform then = kNow;
        if (kTrail && kRewound) {
            const Pose2D& from = entry.history[kTick % kKept];
            Pose2D at = from;
            if (kFraction != 0 && kTick == kBase) {
                // The client's blend of two states (interpolation.cpp).
                const Pose2D& to = entry.history[(kTick + 1) % kKept];
                at.x = from.x + ((to.x - from.x) * kAlong);
                at.y = from.y + ((to.y - from.y) * kAlong);
                at.c = static_cast<float>(from.c + ((static_cast<double>(to.c) - from.c) * kAlong));
                at.s = static_cast<float>(from.s + ((static_cast<double>(to.s) - from.s) * kAlong));
            }
            then = transformOf(at);
        }
        // The ray in the body's frame then is the ray in its frame now.
        const m2Pos2 kOrigin = toWorld(kNow, toLocal(then, m2Pos2{originX, originY}));
        const m2Vec2 kDirection = turnToWorld(kNow, turnToLocal(then, kToward));
        const m2RayCastResult kResult = m2Shape_CastRay(entry.shape, kOrigin, kDirection);
        if (!kResult.hit || (closest.hit && kResult.fraction >= closest.fraction)) {
            continue;
        }
        const m2Pos2 kPoint = toWorld(then, toLocal(kNow, kResult.point));
        const m2Vec2 kNormal = turnToWorld(then, turnToLocal(kNow, kResult.normal));
        closest = RayHit2D{.hit = true,
                           .inside = kResult.normal.x == 0 && kResult.normal.y == 0,
                           .discontinuous = kRewound && !kTrail,
                           .entity = entity,
                           .x = kPoint.x,
                           .y = kPoint.y,
                           .normalX = kNormal.x,
                           .normalY = kNormal.y,
                           .fraction = kResult.fraction};
    }
    return closest;
}

Physics2DStatistics Physics2D::statistics() const noexcept {
    Physics2DStatistics statistics = state_->statistics;
    statistics.raysRewound = state_->raysRewound;
    statistics.rewindsClamped = state_->rewindsClamped;
    return statistics;
}

std::uint64_t Physics2D::digest() const noexcept {
    return m2World_Hash(state_->physics);
}

} // namespace rawframe::physics2d
