#include "rawframe/physics3d/physics.h"

#include "characters.h"
#include "rawframe/physics/filters.h"
#include "rawframe/physics3d/components.h"
#include "rawframe/physics3d/errors.h"
#include "rawframe/world/query.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <map>
#include <maul3d/maul3d.h>
#include <mutex>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>

namespace rawframe::physics3d {

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, Physics3DError error, std::string_view why) {
    return result::fail(errorClass, kPhysics3DDomain, code(error), why);
}

/// Maul3D keeps its worlds in one process-wide table that the caller must
/// serialize.
std::mutex& worldTableLock() noexcept {
    static std::mutex lock;
    return lock;
}

/// Bit for bit, so a value written back is told from any other, NaNs and
/// signed noughts included. Only for the components without padding:
/// Pose3D, Velocity3D, and Impulse3D.
template <typename T> [[nodiscard]] bool same(const T& left, const T& right) noexcept {
    return std::memcmp(&left, &right, sizeof(T)) == 0;
}

/// Field by field: a Body3D has padding, which holds whatever it holds.
[[nodiscard]] bool same(const Body3D& left, const Body3D& right) noexcept {
    const auto kBits = [](float value) {
        return std::bit_cast<std::uint32_t>(value);
    };
    return left.motion == right.motion && left.shape == right.shape && left.fixedRotation == right.fixedRotation &&
           left.bullet == right.bullet && left.sensor == right.sensor && left.collisionClass == right.collisionClass &&
           kBits(left.width) == kBits(right.width) && kBits(left.height) == kBits(right.height) &&
           kBits(left.depth) == kBits(right.depth) && kBits(left.density) == kBits(right.density) &&
           kBits(left.friction) == kBits(right.friction) && kBits(left.restitution) == kBits(right.restitution) &&
           kBits(left.linearDamping) == kBits(right.linearDamping) &&
           kBits(left.angularDamping) == kBits(right.angularDamping);
}

[[nodiscard]] bool finite(float value) noexcept {
    return std::isfinite(value);
}

/// A sensor twin's density: Maul3D wants one, and a twin must not weigh.
constexpr float kTwinDensity = 1e-6F;

/// Sides of a cylinder's prism.
constexpr std::int32_t kCylinderSides = 16;

/// Whether a body can be made of these: sizes positive, the rest finite and
/// in range.
[[nodiscard]] bool makeable(const Body3D& body, const Pose3D& pose, const Velocity3D& velocity) noexcept {
    bool shaped = false;
    switch (static_cast<Shape>(body.shape)) {
    case Shape::Sphere:
        shaped = body.width > 0;
        break;
    case Shape::Box:
        shaped = body.width > 0 && body.height > 0 && body.depth > 0;
        break;
    case Shape::Capsule:
        // A capsule of no length is a sphere, which Maul3D wants made as one.
        shaped = body.width > 0 && body.height > 0;
        break;
    case Shape::Cylinder:
        shaped = body.width > 0 && body.height > 0;
        break;
    case Shape::Mesh:
        // Triangles have no mass to move with, nor an inside to sense.
        shaped = body.motion == static_cast<std::uint8_t>(physics::Motion::Static) && !body.sensor;
        break;
    }
    return body.motion <= static_cast<std::uint8_t>(physics::Motion::Dynamic) && shaped && finite(body.width) &&
           finite(body.height) && finite(body.depth) && body.density >= 0 && finite(body.density) &&
           body.friction >= 0 && finite(body.friction) && body.restitution >= 0 && body.restitution <= 1 &&
           body.linearDamping >= 0 && finite(body.linearDamping) && body.angularDamping >= 0 &&
           finite(body.angularDamping) && std::isfinite(pose.x) && std::isfinite(pose.y) && std::isfinite(pose.z) &&
           finite(pose.qx) && finite(pose.qy) && finite(pose.qz) && finite(pose.qw) && finite(velocity.x) &&
           finite(velocity.y) && finite(velocity.z) && finite(velocity.angularX) && finite(velocity.angularY) &&
           finite(velocity.angularZ);
}

/// A pose's rotation made unit, all noughts (or nothing to make unit) as
/// none: what a body is made with, and what presentation shows of a blended
/// pose.
[[nodiscard]] m3Quat rotationOf(const Pose3D& pose) noexcept {
    const double kLength = std::sqrt((double{pose.qx} * pose.qx) + (double{pose.qy} * pose.qy) +
                                     (double{pose.qz} * pose.qz) + (double{pose.qw} * pose.qw));
    if (!(kLength > 0)) {
        return m3Quat{0, 0, 0, 1};
    }
    return m3Quat{static_cast<float>(pose.qx / kLength),
                  static_cast<float>(pose.qy / kLength),
                  static_cast<float>(pose.qz / kLength),
                  static_cast<float>(pose.qw / kLength)};
}

[[nodiscard]] m3Transform transformOf(const Pose3D& pose) noexcept {
    return m3Transform{.p = {pose.x, pose.y, pose.z}, .q = rotationOf(pose)};
}

/// A vector turned by a unit quaternion, or by its inverse; in doubles.
struct Turned {
    double x = 0;
    double y = 0;
    double z = 0;
};
[[nodiscard]] Turned turn(const m3Quat& rotation, Turned vector, bool inverse) noexcept {
    const double kW = rotation.w;
    const double kX = inverse ? -double{rotation.x} : double{rotation.x};
    const double kY = inverse ? -double{rotation.y} : double{rotation.y};
    const double kZ = inverse ? -double{rotation.z} : double{rotation.z};
    // v + 2w (q x v) + 2 q x (q x v)
    const double kCx = (kY * vector.z) - (kZ * vector.y);
    const double kCy = (kZ * vector.x) - (kX * vector.z);
    const double kCz = (kX * vector.y) - (kY * vector.x);
    return Turned{vector.x + (2 * kW * kCx) + (2 * ((kY * kCz) - (kZ * kCy))),
                  vector.y + (2 * kW * kCy) + (2 * ((kZ * kCx) - (kX * kCz))),
                  vector.z + (2 * kW * kCz) + (2 * ((kX * kCy) - (kY * kCx)))};
}

/// Where a pose's frame puts a world point or direction, and back.
[[nodiscard]] Turned toLocal(const m3Transform& frame, Turned point) noexcept {
    return turn(frame.q, Turned{point.x - frame.p.x, point.y - frame.p.y, point.z - frame.p.z}, true);
}
[[nodiscard]] Turned toWorld(const m3Transform& frame, Turned local) noexcept {
    const Turned kTurned = turn(frame.q, local, false);
    return Turned{frame.p.x + kTurned.x, frame.p.y + kTurned.y, frame.p.z + kTurned.z};
}

/// How far from its origin any of a body's shape reaches.
[[nodiscard]] double reachOf(const Body3D& body) noexcept {
    const double kWidth = body.width;
    const double kHeight = body.height;
    const double kDepth = body.depth;
    switch (static_cast<Shape>(body.shape)) {
    case Shape::Sphere:
        return kWidth;
    case Shape::Box:
        return std::sqrt((kWidth * kWidth) + (kHeight * kHeight) + (kDepth * kDepth));
    case Shape::Capsule:
        return kWidth + kHeight;
    case Shape::Cylinder:
        return std::sqrt((kWidth * kWidth) + (kHeight * kHeight));
    case Shape::Mesh:
        break;
    }
    return 0;
}

/// The most vertices and triangles Maul3D takes in one mesh shape.
constexpr std::size_t kPieceMost = 65'535;

/// A mesh as Maul3D takes it: pieces of at most kPieceMost vertices and
/// triangles, and how far from its origin any of it reaches.
struct PreparedMesh {
    struct Piece {
        std::vector<m3Vec3> vertices;
        std::vector<std::uint16_t> indices;
    };
    std::vector<Piece> pieces;
    double reach = 0;
};

/// Triangles in order, a piece closed when the next might not fit. A
/// triangle of no area touches nothing and is left out, since Maul3D's
/// contacts need a face's normal.
[[nodiscard]] PreparedMesh prepare(const mesh::Mesh& source) {
    PreparedMesh out;
    std::vector<std::int32_t> local(source.positions.size(), -1);
    std::vector<std::uint32_t> used;
    PreparedMesh::Piece piece;
    const auto kClose = [&] {
        if (!piece.indices.empty()) {
            out.pieces.push_back(std::move(piece));
        }
        piece = {};
        for (const std::uint32_t kGlobal : used) {
            local[kGlobal] = -1;
        }
        used.clear();
    };
    for (std::size_t first = 0; first < source.indices.size(); first += 3) {
        const std::array<std::uint32_t, 3> kCorners = {
            source.indices[first], source.indices[first + 1], source.indices[first + 2]};
        const mesh::Vector3& kA = source.positions[kCorners[0]];
        const mesh::Vector3& kB = source.positions[kCorners[1]];
        const mesh::Vector3& kC = source.positions[kCorners[2]];
        const Turned kAb{double{kB[0]} - kA[0], double{kB[1]} - kA[1], double{kB[2]} - kA[2]};
        const Turned kAc{double{kC[0]} - kA[0], double{kC[1]} - kA[1], double{kC[2]} - kA[2]};
        const Turned kCross{
            (kAb.y * kAc.z) - (kAb.z * kAc.y), (kAb.z * kAc.x) - (kAb.x * kAc.z), (kAb.x * kAc.y) - (kAb.y * kAc.x)};
        if (kCross.x == 0 && kCross.y == 0 && kCross.z == 0) {
            continue;
        }
        // Three new vertices at most: closing a little early is harmless.
        if (piece.vertices.size() + 3 > kPieceMost || piece.indices.size() / 3 == kPieceMost) {
            kClose();
        }
        for (const std::uint32_t kGlobal : kCorners) {
            if (local[kGlobal] < 0) {
                local[kGlobal] = static_cast<std::int32_t>(piece.vertices.size());
                used.push_back(kGlobal);
                const mesh::Vector3& kPoint = source.positions[kGlobal];
                piece.vertices.push_back(m3Vec3{kPoint[0], kPoint[1], kPoint[2]});
            }
            piece.indices.push_back(static_cast<std::uint16_t>(local[kGlobal]));
        }
    }
    kClose();
    for (const mesh::Vector3& kPoint : source.positions) {
        const double kX = kPoint[0];
        const double kY = kPoint[1];
        const double kZ = kPoint[2];
        out.reach = std::max(out.reach, std::sqrt((kX * kX) + (kY * kY) + (kZ * kZ)));
    }
    return out;
}

/// Whether the segment from `origin` along `toward` passes within `reach` of
/// `center`.
[[nodiscard]] bool passesNear(Turned origin, Turned toward, Turned center, double reach) noexcept {
    const Turned kTo{center.x - origin.x, center.y - origin.y, center.z - origin.z};
    const double kLength = (toward.x * toward.x) + (toward.y * toward.y) + (toward.z * toward.z);
    const double kAlong =
        kLength > 0 ? std::clamp(((kTo.x * toward.x) + (kTo.y * toward.y) + (kTo.z * toward.z)) / kLength, 0.0, 1.0)
                    : 0.0;
    const Turned kOff{kTo.x - (toward.x * kAlong), kTo.y - (toward.y * kAlong), kTo.z - (toward.z * kAlong)};
    // A margin for the float geometry the shape is tested with.
    const double kWithin = reach + 0.01;
    return (kOff.x * kOff.x) + (kOff.y * kOff.y) + (kOff.z * kOff.z) <= kWithin * kWithin;
}

/// One entity's body, and what the last step wrote, to tell gameplay's
/// writes from its own.
struct Mapped {
    m3BodyId body{};
    m3ShapeId shape{};
    /// A mesh's shapes past its first.
    std::vector<m3ShapeId> pieces;
    /// The mesh it was made of, and how far from its origin it reaches.
    std::uint64_t mesh = 0;
    double reach = 0;
    /// The sensor twin of a body whose class triggers with another.
    m3ShapeId trigger{};
    /// Its pose after each of the last steps, by tick modulo the history's
    /// length, and the first tick of its unbroken trail.
    std::vector<Pose3D> history;
    std::optional<std::uint64_t> since;
    bool refused = false;
    /// Made as a character's, which character queries do not see.
    bool character = false;
    Body3D made;
    Pose3D pose;
    Velocity3D velocity;
};

struct Row {
    world::EntityHandle entity;
    const Body3D* body = nullptr;
    Pose3D* pose = nullptr;
    Velocity3D* velocity = nullptr;
    Character3D* character = nullptr;
    const Mesh3D* mesh = nullptr;
};

/// The mesh a row's body is made of: its Mesh3D's for a mesh body, else
/// none.
[[nodiscard]] std::uint64_t meshOf(const Row& row) noexcept {
    return row.body->shape == static_cast<std::uint8_t>(Shape::Mesh) && row.mesh != nullptr ? row.mesh->mesh : 0;
}

[[nodiscard]] bool sameShape(m3ShapeId left, m3ShapeId right) noexcept {
    return left.index1 == right.index1 && left.generation == right.generation && left.world == right.world;
}

[[nodiscard]] bool sameBody(m3BodyId left, m3BodyId right) noexcept {
    return left.index1 == right.index1 && left.generation == right.generation && left.world == right.world;
}

/// One entity's joint: the joint made, what it was made of, and between
/// which bodies, to tell when either was made again.
struct MappedJoint {
    m3JointId joint{};
    Joint3D made;
    m3BodyId a{};
    m3BodyId b{};
    bool refused = false;
};

/// Field by field, for the padding a Joint3D may have.
[[nodiscard]] bool same(const Joint3D& left, const Joint3D& right) noexcept {
    constexpr std::array kReals = {
        &Joint3D::anchorAX,      &Joint3D::anchorAY,      &Joint3D::anchorAZ,      &Joint3D::anchorBX,
        &Joint3D::anchorBY,      &Joint3D::anchorBZ,      &Joint3D::axisAX,        &Joint3D::axisAY,
        &Joint3D::axisAZ,        &Joint3D::axisBX,        &Joint3D::axisBY,        &Joint3D::axisBZ,
        &Joint3D::linearLowerX,  &Joint3D::linearLowerY,  &Joint3D::linearLowerZ,  &Joint3D::linearUpperX,
        &Joint3D::linearUpperY,  &Joint3D::linearUpperZ,  &Joint3D::angularLowerX, &Joint3D::angularLowerY,
        &Joint3D::angularLowerZ, &Joint3D::angularUpperX, &Joint3D::angularUpperY, &Joint3D::angularUpperZ,
        &Joint3D::motorSpeed,    &Joint3D::motorEffort};
    constexpr std::array kBytes = {&Joint3D::linearX,
                                   &Joint3D::linearY,
                                   &Joint3D::linearZ,
                                   &Joint3D::angularX,
                                   &Joint3D::angularY,
                                   &Joint3D::angularZ,
                                   &Joint3D::motor};
    return left.a == right.a && left.b == right.b && left.collideConnected == right.collideConnected &&
           std::ranges::all_of(kReals,
                               [&](float Joint3D::* field) {
                                   return std::bit_cast<std::uint32_t>(left.*field) ==
                                          std::bit_cast<std::uint32_t>(right.*field);
                               }) &&
           std::ranges::all_of(kBytes, [&](std::uint8_t Joint3D::* field) {
               return left.*field == right.*field;
           });
}

/// A joint's axis in a body's frame: unit, all noughts as +z; none for one
/// that is not finite.
[[nodiscard]] std::optional<m3Vec3> jointAxis(float x, float y, float z) noexcept {
    const double kLength = std::sqrt((double{x} * x) + (double{y} * y) + (double{z} * z));
    if (!std::isfinite(kLength)) {
        return std::nullopt;
    }
    if (kLength == 0) {
        return m3Vec3{0, 0, 1};
    }
    return m3Vec3{static_cast<float>(x / kLength), static_cast<float>(y / kLength), static_cast<float>(z / kLength)};
}

/// The Maul3D generic joint a Joint3D is, between two bodies; none for
/// values out of range. Maul3D refuses the rest (a limit's order, the
/// angular contract, a motor on a locked axis) when the joint is made.
[[nodiscard]] std::optional<m3JointDef> jointDef(const Joint3D& joint, m3BodyId a, m3BodyId b) noexcept {
    const auto kAxisA = jointAxis(joint.axisAX, joint.axisAY, joint.axisAZ);
    const auto kAxisB = jointAxis(joint.axisBX, joint.axisBY, joint.axisBZ);
    const std::array<std::uint8_t, 6> kModes = {
        joint.linearX, joint.linearY, joint.linearZ, joint.angularX, joint.angularY, joint.angularZ};
    if (!kAxisA || !kAxisB || joint.motor > 6 || std::ranges::any_of(kModes, [](std::uint8_t mode) {
            return mode > static_cast<std::uint8_t>(JointAxis::Limited);
        })) {
        return std::nullopt;
    }
    m3JointDef definition = m3DefaultJointDef();
    definition.type = m3_genericJoint;
    definition.bodyIdA = a;
    definition.bodyIdB = b;
    definition.localAnchorA = m3Vec3{joint.anchorAX, joint.anchorAY, joint.anchorAZ};
    definition.localAnchorB = m3Vec3{joint.anchorBX, joint.anchorBY, joint.anchorBZ};
    definition.localAxisA = *kAxisA;
    definition.localAxisB = *kAxisB;
    definition.collideConnected = joint.collideConnected;
    // The modes are Maul3D's own numbers: locked, free, limited.
    for (std::size_t axis = 0; axis < 3; ++axis) {
        definition.genericLinear[axis] = kModes[axis];
        definition.genericAngular[axis] = kModes[3 + axis];
    }
    definition.genericLinearLower[0] = joint.linearLowerX;
    definition.genericLinearLower[1] = joint.linearLowerY;
    definition.genericLinearLower[2] = joint.linearLowerZ;
    definition.genericLinearUpper[0] = joint.linearUpperX;
    definition.genericLinearUpper[1] = joint.linearUpperY;
    definition.genericLinearUpper[2] = joint.linearUpperZ;
    definition.genericAngularLower[0] = joint.angularLowerX;
    definition.genericAngularLower[1] = joint.angularLowerY;
    definition.genericAngularLower[2] = joint.angularLowerZ;
    definition.genericAngularUpper[0] = joint.angularUpperX;
    definition.genericAngularUpper[1] = joint.angularUpperY;
    definition.genericAngularUpper[2] = joint.angularUpperZ;
    definition.genericMotorAxis = joint.motor == 0 ? std::uint8_t{255} : static_cast<std::uint8_t>(joint.motor - 1);
    definition.motorSpeed = joint.motorSpeed;
    definition.maxMotorEffort = joint.motorEffort;
    return definition;
}

} // namespace

struct Physics3D::State {
    Physics3DSettings settings;
    /// The collision document as Maul3D's filter bits.
    physics::CollisionFilters filters;
    /// Entity pairs overlapping now, lower entity first, and through how
    /// many pairs of shapes: an overlap begins and ends for the entities when
    /// the count leaves and returns to nought.
    std::map<std::pair<world::EntityHandle, world::EntityHandle>, std::uint32_t> overlapping;
    /// The last tick stepped.
    std::uint64_t lastTick = 0;
    bool stepped = false;
    mutable std::uint64_t raysRewound = 0;
    mutable std::uint64_t rewindsClamped = 0;
    mutable std::vector<m3RayHit> rayHits;
    mutable std::vector<m3ShapeId> overlapShapes;
    m3WorldId physics{};
    Physics3DStatistics statistics;
    std::map<world::EntityHandle, Mapped> mapped;
    std::optional<world::Query<world::Read<Body3D>, world::Write<Pose3D>, world::Write<Velocity3D>>> bodies;
    std::optional<schema::ComponentRuntimeId> impulse;
    std::optional<schema::ComponentRuntimeId> contact;
    std::optional<schema::ComponentRuntimeId> character;
    std::optional<schema::ComponentRuntimeId> meshShape;
    std::map<std::uint64_t, PreparedMesh> meshes;
    std::optional<world::Query<world::Read<Joint3D>>> jointQuery;
    std::map<world::EntityHandle, MappedJoint> joints;
    std::vector<std::pair<world::EntityHandle, const Joint3D*>> jointRows;
    /// Whose each live shape is, by its index; the generation tells a
    /// reused index from the shape an event names.
    std::map<std::int32_t, std::pair<std::uint16_t, world::EntityHandle>> owners;
    std::vector<m3ContactData> touching;
    std::vector<schema::ComponentRuntimeId> reads;
    std::vector<schema::ComponentRuntimeId> writes;
    std::unique_ptr<world::System> system;
    std::vector<Row> rows;

    ~State() {
        if (m3World_IsValid(physics)) {
            const std::scoped_lock kLock{worldTableLock()};
            m3DestroyWorld(physics);
        }
    }

    [[nodiscard]] bool make(const Row& row, Mapped& into) {
        const Body3D& body = *row.body;
        into.made = body;
        into.pose = *row.pose;
        into.velocity = *row.velocity;
        into.body = {};
        into.shape = {};
        into.trigger = {};
        into.pieces.clear();
        into.mesh = meshOf(row);
        into.reach = reachOf(body);
        into.since.reset();
        into.character = row.character != nullptr;
        const auto kClass = filters.classIndex(body.collisionClass);
        const PreparedMesh* kMesh = nullptr;
        if (body.shape == static_cast<std::uint8_t>(Shape::Mesh)) {
            const auto kFound = meshes.find(into.mesh);
            kMesh = kFound != meshes.end() && !kFound->second.pieces.empty() ? &kFound->second : nullptr;
            into.reach = kMesh != nullptr ? kMesh->reach : 0;
        }
        into.refused = !makeable(body, *row.pose, *row.velocity) || !kClass.has_value() ||
                       (body.shape == static_cast<std::uint8_t>(Shape::Mesh) && kMesh == nullptr);
        if (into.refused) {
            ++statistics.bodiesRefused;
            return false;
        }
        m3BodyDef definition = m3DefaultBodyDef();
        definition.type = body.motion;
        definition.position = m3Pos3{row.pose->x, row.pose->y, row.pose->z};
        definition.rotation = rotationOf(*row.pose);
        definition.linearVelocity = m3Vec3{row.velocity->x, row.velocity->y, row.velocity->z};
        definition.angularVelocity = m3Vec3{row.velocity->angularX, row.velocity->angularY, row.velocity->angularZ};
        definition.linearDamping = body.linearDamping;
        definition.angularDamping = body.angularDamping;
        definition.isBullet = body.bullet;
        if (body.fixedRotation) {
            definition.motionLocks.angularX = true;
            definition.motionLocks.angularY = true;
            definition.motionLocks.angularZ = true;
        }
        definition.userData = (std::uint64_t{row.entity.slot} << 32U) | row.entity.generation;
        const m3BodyId kBody = m3CreateBody(physics, &definition);
        if (kBody.index1 == 0) {
            into.refused = true;
            ++statistics.bodiesRefused;
            return false;
        }
        m3ShapeDef shape = m3DefaultShapeDef();
        // Maul3D wants every shape to have a density: nought is taken as
        // a kilogram a cubic meter, which a body that is not dynamic never
        // feels.
        shape.density = body.density > 0 ? body.density : 1;
        shape.friction = body.friction;
        shape.restitution = body.restitution;
        shape.isSensor = body.sensor;
        shape.enableHitEvents = !body.sensor;
        const physics::ClassFilter& filter = filters.filter(*kClass);
        shape.categoryBits =
            body.sensor ? physics::CollisionFilters::sensorBit(*kClass) : physics::CollisionFilters::solidBit(*kClass);
        shape.maskBits = body.sensor ? filter.sensorMask : filter.solidMask;
        if (into.character) {
            shape.maskBits &= ~physics::kCharacterQuery;
        }
        const auto kShape = [&](const m3ShapeDef& definitionOf) {
            switch (static_cast<Shape>(body.shape)) {
            case Shape::Sphere: {
                const m3Sphere kSphere{.center = {0, 0, 0}, .radius = body.width};
                return m3CreateSphereShape(kBody, &definitionOf, &kSphere);
            }
            case Shape::Box:
                return m3CreateBoxShape(kBody, &definitionOf, m3Vec3{body.width, body.height, body.depth});
            case Shape::Capsule: {
                const m3Capsule kCapsule{
                    .point1 = {0, -body.height, 0}, .point2 = {0, body.height, 0}, .radius = body.width};
                return m3CreateCapsuleShape(kBody, &definitionOf, &kCapsule);
            }
            case Shape::Cylinder: {
                const m3Cylinder kCylinder{
                    .point1 = {0, -body.height, 0}, .point2 = {0, body.height, 0}, .radius = body.width};
                return m3CreateCylinderShape(kBody, &definitionOf, &kCylinder, kCylinderSides);
            }
            case Shape::Mesh:
                break;
            }
            return m3ShapeId{};
        };
        if (kMesh != nullptr) {
            return makeMesh(row.entity, *kMesh, kBody, shape, into);
        }
        const m3ShapeId kMade = kShape(shape);
        m3ShapeId twin{};
        if (kMade.index1 != 0 && !body.sensor && filter.triggerMask != 0) {
            m3ShapeDef sensor = shape;
            sensor.isSensor = true;
            sensor.enableHitEvents = false;
            // Sensors weigh: the least density adds next to nothing.
            sensor.density = kTwinDensity;
            sensor.categoryBits = physics::CollisionFilters::sensorBit(*kClass);
            sensor.maskBits = filter.triggerMask;
            twin = kShape(sensor);
        }
        if (kMade.index1 == 0 || (!body.sensor && filter.triggerMask != 0 && twin.index1 == 0)) {
            m3DestroyBody(kBody);
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

    /// A mesh body's shapes, one a piece. A mesh never triggers: it has no
    /// twin.
    [[nodiscard]] bool makeMesh(world::EntityHandle entity,
                                const PreparedMesh& prepared,
                                m3BodyId body,
                                const m3ShapeDef& shape,
                                Mapped& into) {
        std::vector<m3ShapeId> made;
        for (const PreparedMesh::Piece& kPiece : prepared.pieces) {
            const m3ShapeId kMade = m3CreateMeshShape(body,
                                                      &shape,
                                                      kPiece.vertices.data(),
                                                      static_cast<std::int32_t>(kPiece.vertices.size()),
                                                      kPiece.indices.data(),
                                                      static_cast<std::int32_t>(kPiece.indices.size() / 3));
            if (kMade.index1 == 0) {
                m3DestroyBody(body);
                into.refused = true;
                ++statistics.bodiesRefused;
                return false;
            }
            made.push_back(kMade);
        }
        into.body = body;
        into.shape = made.front();
        into.pieces.assign(made.begin() + 1, made.end());
        for (const m3ShapeId kMade : made) {
            owners[kMade.index1] = {kMade.generation, entity};
        }
        ++statistics.bodiesMade;
        return true;
    }

    /// The entity whose body a shape is, or the null entity for a shape
    /// already gone.
    [[nodiscard]] world::EntityHandle ownerOf(m3ShapeId shape) const {
        const auto kOwner = owners.find(shape.index1);
        return kOwner != owners.end() && kOwner->second.first == shape.generation ? kOwner->second.second
                                                                                  : world::EntityHandle{};
    }

    [[nodiscard]] Contact3D* contactOf(world::World& world, world::EntityHandle entity) const {
        return entity.isNull() ? nullptr : static_cast<Contact3D*>(world.getErased(entity, *contact));
    }

    /// Every Contact3D of a body, from this step's event streams and what
    /// touches and overlaps now, all in Maul3D's canonical order.
    void report(world::World& world) {
        for (const Row& row : rows) {
            if (Contact3D* const kContact = contactOf(world, row.entity)) {
                *kContact = Contact3D{};
            }
        }
        const m3ContactEvents kContacts = m3World_GetContactEvents(physics);
        for (std::int32_t index = 0; index < kContacts.beginCount; ++index) {
            ++statistics.contactsBegun;
            for (const m3ShapeId kShape :
                 {kContacts.beginEvents[index].shapeIdA, kContacts.beginEvents[index].shapeIdB}) {
                if (Contact3D* const kContact = contactOf(world, ownerOf(kShape))) {
                    ++kContact->began;
                }
            }
        }
        for (std::int32_t index = 0; index < kContacts.endCount; ++index) {
            for (const m3ShapeId kShape : {kContacts.endEvents[index].shapeIdA, kContacts.endEvents[index].shapeIdB}) {
                if (Contact3D* const kContact = contactOf(world, ownerOf(kShape))) {
                    ++kContact->ended;
                }
            }
        }
        for (std::int32_t index = 0; index < kContacts.hitCount; ++index) {
            const m3ContactHitEvent& event = kContacts.hitEvents[index];
            const world::EntityHandle kA = ownerOf(event.shapeIdA);
            const world::EntityHandle kB = ownerOf(event.shapeIdB);
            for (const auto& [kSelf, kOther, kSign] : {std::tuple{kA, kB, 1.0F}, std::tuple{kB, kA, -1.0F}}) {
                Contact3D* const kContact = contactOf(world, kSelf);
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
        const m3SensorEvents kOverlaps = m3World_GetSensorEvents(physics);
        const auto kPair = [this](m3ShapeId one, m3ShapeId other) {
            const world::EntityHandle kOne = ownerOf(one);
            const world::EntityHandle kOther = ownerOf(other);
            return kOne < kOther ? std::pair{kOne, kOther} : std::pair{kOther, kOne};
        };
        for (std::int32_t index = 0; index < kOverlaps.beginCount; ++index) {
            const auto kBetween = kPair(kOverlaps.beginEvents[index].shapeIdA, kOverlaps.beginEvents[index].shapeIdB);
            if (kBetween.first.isNull() || kBetween.first == kBetween.second || ++overlapping[kBetween] != 1) {
                continue;
            }
            ++statistics.overlapsBegun;
            for (const auto& [kSelf, kOther] :
                 {std::pair{kBetween.first, kBetween.second}, std::pair{kBetween.second, kBetween.first}}) {
                if (Contact3D* const kContact = contactOf(world, kSelf)) {
                    ++kContact->entered;
                    kContact->visitor = kContact->visitor.isNull() ? kOther : kContact->visitor;
                }
            }
        }
        for (std::int32_t index = 0; index < kOverlaps.endCount; ++index) {
            const auto kFound =
                overlapping.find(kPair(kOverlaps.endEvents[index].shapeIdA, kOverlaps.endEvents[index].shapeIdB));
            if (kFound == overlapping.end() || --kFound->second != 0) {
                continue;
            }
            for (const world::EntityHandle kSide : {kFound->first.first, kFound->first.second}) {
                if (Contact3D* const kContact = contactOf(world, kSide)) {
                    ++kContact->exited;
                }
            }
            overlapping.erase(kFound);
        }
        // What touches and overlaps now.
        for (const auto& [kBetween, kShapes] : overlapping) {
            for (const world::EntityHandle kSide : {kBetween.first, kBetween.second}) {
                if (Contact3D* const kContact = contactOf(world, kSide)) {
                    ++kContact->overlapping;
                }
            }
        }
        for (const Row& row : rows) {
            Contact3D* const kContact = contactOf(world, row.entity);
            const Mapped& entry = mapped.find(row.entity)->second;
            if (kContact == nullptr || entry.refused) {
                continue;
            }
            touching.resize(std::max<std::size_t>(touching.size(), 16));
            std::int32_t total =
                m3Body_GetContactData(entry.body, touching.data(), static_cast<std::int32_t>(touching.size()));
            if (static_cast<std::size_t>(total) > touching.size()) {
                touching.resize(static_cast<std::size_t>(total));
                total = m3Body_GetContactData(entry.body, touching.data(), total);
            }
            for (std::int32_t index = 0; index < total; ++index) {
                kContact->touching += touching[static_cast<std::size_t>(index)].pointCount > 0 ? 1U : 0U;
            }
        }
    }

    /// A character's move this tick, as the velocity its kinematic body
    /// steps with; one that is not a made kinematic capsule stays Airborne
    /// and moves as its body would.
    void moveCharacter(const Row& row, const Mapped& entry, float seconds) {
        Character3D& controlled = *row.character;
        const bool kWasGrounded = controlled.ground == static_cast<std::uint8_t>(physics::Ground::Grounded);
        controlled.ground = static_cast<std::uint8_t>(physics::Ground::Airborne);
        controlled.groundNormalX = 0;
        controlled.groundNormalY = 0;
        controlled.groundNormalZ = 0;
        const Body3D& body = entry.made;
        if (entry.refused || body.motion != static_cast<std::uint8_t>(physics::Motion::Kinematic) ||
            body.shape != static_cast<std::uint8_t>(Shape::Capsule) || body.sensor ||
            !finite(controlled.groundNormal) || !finite(controlled.snap) || !(seconds > 0)) {
            return;
        }
        const m3Vec3 kWish = m3Body_GetLinearVelocity(entry.body);
        const m3QueryFilter kFilter{.categoryBits = physics::kCharacterQuery,
                                    .maskBits = filters.filter(*filters.classIndex(body.collisionClass)).solidMask &
                                                physics::kSolids};
        const CharacterMove kMove =
            physics3d::moveCharacter(physics,
                                     body.width,
                                     body.height,
                                     m3Body_GetPosition(entry.body),
                                     m3Vec3{kWish.x * seconds, kWish.y * seconds, kWish.z * seconds},
                                     controlled,
                                     kWasGrounded,
                                     kFilter);
        m3Body_SetLinearVelocity(
            entry.body,
            m3Vec3{kMove.translation.x / seconds, kMove.translation.y / seconds, kMove.translation.z / seconds});
        m3Body_SetAngularVelocity(entry.body, m3Vec3{0, 0, 0});
        controlled.ground = static_cast<std::uint8_t>(kMove.ground);
        controlled.groundNormalX = kMove.normal.x;
        controlled.groundNormalY = kMove.normal.y;
        controlled.groundNormalZ = kMove.normal.z;
        ++statistics.characterMoves;
    }

    void remove(world::EntityHandle entity, Mapped& entry) {
        if (!entry.refused) {
            owners.erase(entry.shape.index1);
            owners.erase(entry.trigger.index1);
            for (const m3ShapeId kPiece : entry.pieces) {
                owners.erase(kPiece.index1);
            }
            m3DestroyBody(entry.body);
            ++statistics.bodiesRemoved;
        }
        // Gone with its shapes: no end will be told for them.
        std::erase_if(overlapping, [entity](const auto& pair) {
            return pair.first.first == entity || pair.first.second == entity;
        });
    }

    /// The body an entity has now, or the null body.
    [[nodiscard]] m3BodyId bodyOf(world::EntityHandle entity) const {
        const auto kFound = entity.isNull() ? mapped.end() : mapped.find(entity);
        return kFound == mapped.end() || kFound->second.refused ? m3BodyId{} : kFound->second.body;
    }

    /// 3. Joints follow their entities: what is gone first, then what is
    /// new, changed, or between a body made again.
    void followJoints(world::World& world) {
        jointRows.clear();
        jointQuery->forEach(world, [this](world::EntityHandle entity, const Joint3D& joint) {
            jointRows.emplace_back(entity, &joint);
        });
        std::ranges::sort(jointRows, {}, &std::pair<world::EntityHandle, const Joint3D*>::first);
        for (auto entry = joints.begin(); entry != joints.end();) {
            if (std::ranges::binary_search(
                    jointRows, entry->first, {}, &std::pair<world::EntityHandle, const Joint3D*>::first)) {
                ++entry;
                continue;
            }
            if (m3Joint_IsValid(entry->second.joint)) {
                m3DestroyJoint(entry->second.joint);
            }
            ++statistics.jointsRemoved;
            entry = joints.erase(entry);
        }
        for (const auto& [kEntity, kJoint] : jointRows) {
            const auto [kEntry, kNew] = joints.try_emplace(kEntity);
            MappedJoint& entry = kEntry->second;
            const m3BodyId kA = bodyOf(kJoint->a);
            const m3BodyId kB = bodyOf(kJoint->b);
            // A joint whose body was made again went with the body.
            if (!kNew && same(entry.made, *kJoint) && sameBody(entry.a, kA) && sameBody(entry.b, kB) &&
                (entry.refused || m3Joint_IsValid(entry.joint))) {
                continue;
            }
            if (!entry.refused && m3Joint_IsValid(entry.joint)) {
                m3DestroyJoint(entry.joint);
            }
            entry = MappedJoint{.joint = {}, .made = *kJoint, .a = kA, .b = kB, .refused = true};
            const auto kDefinition =
                kA.index1 != 0 && kB.index1 != 0 && !sameBody(kA, kB) ? jointDef(*kJoint, kA, kB) : std::nullopt;
            if (kDefinition.has_value()) {
                entry.joint = m3CreateJoint(physics, &*kDefinition);
                entry.refused = entry.joint.index1 == 0;
            }
            ++(entry.refused ? statistics.jointsRefused : statistics.jointsMade);
        }
    }

    result::Status step(world::World& world, world::TickRate rate, world::TickIndex tick) {
        rows.clear();
        bodies->forEach(world,
                        [this](world::EntityHandle entity, const Body3D& body, Pose3D& pose, Velocity3D& velocity) {
                            rows.push_back(Row{.entity = entity, .body = &body, .pose = &pose, .velocity = &velocity});
                        });
        for (Row& row : rows) {
            row.character = static_cast<Character3D*>(world.getErased(row.entity, *character));
            row.mesh = static_cast<const Mesh3D*>(world.getErased(row.entity, *meshShape));
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
            remove(entry->first, entry->second);
            entry = mapped.erase(entry);
        }
        for (const Row& row : rows) {
            const auto [kEntry, kNew] = mapped.try_emplace(row.entity);
            Mapped& entry = kEntry->second;
            if (kNew || !same(entry.made, *row.body) || entry.character != (row.character != nullptr) ||
                entry.mesh != meshOf(row)) {
                if (!kNew) {
                    remove(row.entity, entry);
                }
                if (!make(row, entry)) {
                    continue;
                }
            } else if (entry.refused) {
                // Refused until its Body3D changes, or its pose or velocity
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
                    remove(row.entity, entry);
                    ++statistics.teleports;
                    if (!make(row, entry)) {
                        continue;
                    }
                }
                if (!same(entry.velocity, *row.velocity)) {
                    const Velocity3D& velocity = *row.velocity;
                    if (finite(velocity.x) && finite(velocity.y) && finite(velocity.z) && finite(velocity.angularX) &&
                        finite(velocity.angularY) && finite(velocity.angularZ)) {
                        m3Body_SetLinearVelocity(entry.body, m3Vec3{velocity.x, velocity.y, velocity.z});
                        m3Body_SetAngularVelocity(entry.body,
                                                  m3Vec3{velocity.angularX, velocity.angularY, velocity.angularZ});
                        ++statistics.velocitiesSet;
                    }
                }
            }
            if (impulse) {
                auto* const kImpulse = static_cast<Impulse3D*>(world.getErased(row.entity, *impulse));
                if (kImpulse != nullptr && !same(*kImpulse, Impulse3D{})) {
                    if (finite(kImpulse->x) && finite(kImpulse->y) && finite(kImpulse->z) &&
                        finite(kImpulse->angularX) && finite(kImpulse->angularY) && finite(kImpulse->angularZ)) {
                        m3Body_ApplyLinearImpulse(entry.body, m3Vec3{kImpulse->x, kImpulse->y, kImpulse->z});
                        m3Body_ApplyAngularImpulse(entry.body,
                                                   m3Vec3{kImpulse->angularX, kImpulse->angularY, kImpulse->angularZ});
                        ++statistics.impulses;
                    }
                    *kImpulse = Impulse3D{};
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
        m3World_Step(physics, kSeconds, static_cast<std::int32_t>(settings.substeps));
        ++statistics.steps;

        report(world);

        // 5. Every body's pose and velocity back into the World.
        for (const Row& row : rows) {
            Mapped& entry = mapped.find(row.entity)->second;
            if (entry.refused) {
                continue;
            }
            const m3Transform kTransform = m3Body_GetTransform(entry.body);
            const m3Vec3 kLinear = m3Body_GetLinearVelocity(entry.body);
            const m3Vec3 kAngular = m3Body_GetAngularVelocity(entry.body);
            *row.pose = Pose3D{.x = kTransform.p.x,
                               .y = kTransform.p.y,
                               .z = kTransform.p.z,
                               .qx = kTransform.q.x,
                               .qy = kTransform.q.y,
                               .qz = kTransform.q.z,
                               .qw = kTransform.q.w};
            *row.velocity = Velocity3D{.x = kLinear.x,
                                       .y = kLinear.y,
                                       .z = kLinear.z,
                                       .angularX = kAngular.x,
                                       .angularY = kAngular.y,
                                       .angularZ = kAngular.z};
            entry.pose = *row.pose;
            entry.velocity = *row.velocity;
            if (settings.historyTicks != 0) {
                entry.history.resize(settings.historyTicks);
                entry.history[tick.value % settings.historyTicks] = entry.pose;
                entry.since = entry.since.value_or(tick.value);
            }
        }
        lastTick = tick.value;
        stepped = true;
        return {};
    }

    /// A cast's closest hit as the answer a script reads.
    [[nodiscard]] RayHit3D hitOf(const m3RayCastResult& result) const {
        if (!result.hit) {
            return RayHit3D{};
        }
        return RayHit3D{.hit = true,
                        .inside = result.normal.x == 0 && result.normal.y == 0 && result.normal.z == 0,
                        .entity = ownerOf(result.shapeId),
                        .x = result.point.x,
                        .y = result.point.y,
                        .z = result.point.z,
                        .normalX = result.normal.x,
                        .normalY = result.normal.y,
                        .normalZ = result.normal.z,
                        .fraction = result.fraction};
    }

    /// The query filter that sees the shapes of the class `among`, or all.
    [[nodiscard]] std::optional<m3QueryFilter> amongFilter(std::uint64_t among) const noexcept {
        m3QueryFilter filter{~std::uint64_t{0}, ~std::uint64_t{0}};
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
};

namespace {

class Step final : public world::System {
public:
    explicit Step(Physics3D::State& state) noexcept : state_(&state) {
    }
    result::Status run(world::SystemContext& context) noexcept override {
        return state_->step(context.world, context.rate, context.tick);
    }

private:
    Physics3D::State* state_;
};

} // namespace

Physics3D::Physics3D(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Physics3D::~Physics3D() = default;

result::Result<std::unique_ptr<Physics3D>> Physics3D::create(const Physics3DSettings& settings) {
    constexpr std::uint32_t kMost = 1U << 20U;
    if (settings.historyTicks > 1024) {
        return refuse(result::ErrorClass::InvalidArgument,
                      Physics3DError::InvalidSettings,
                      "physics settings: at most 1024 ticks of history");
    }
    if (!std::isfinite(settings.gravityX) || !std::isfinite(settings.gravityY) || !std::isfinite(settings.gravityZ) ||
        settings.substeps == 0 || settings.substeps > 64 || settings.bodyCapacity == 0 ||
        settings.bodyCapacity > kMost || settings.shapeCapacity == 0 || settings.shapeCapacity > kMost ||
        settings.jointCapacity == 0 || settings.jointCapacity > kMost || settings.meshCapacity == 0 ||
        settings.meshCapacity > kMost) {
        return refuse(result::ErrorClass::InvalidArgument,
                      Physics3DError::InvalidSettings,
                      "physics settings: finite gravity, 1 to 64 substeps, and capacities of 1 to 2^20");
    }
    if (m3CpuSupportsBackend() == 0) {
        return refuse(result::ErrorClass::Unsupported,
                      Physics3DError::Unsupported,
                      "this processor cannot run the physics build's kernels");
    }
    auto state = std::make_unique<State>();
    state->settings = settings;
    RAWFRAME_TRY_ASSIGN(state->filters, physics::CollisionFilters::make(settings.collision));
    for (const BodyMesh& each : settings.meshes) {
        // Nought is a Mesh3D's name for none.
        if (each.id == 0 || each.mesh == nullptr || !mesh::validate(*each.mesh).has_value() ||
            state->meshes.contains(each.id)) {
            return refuse(result::ErrorClass::InvalidArgument,
                          Physics3DError::InvalidSettings,
                          "physics settings: every mesh valid, each identity once and not nought");
        }
        state->meshes.emplace(each.id, prepare(*each.mesh));
    }
    m3WorldDef definition = m3DefaultWorldDef();
    definition.gravity = m3Vec3{settings.gravityX, settings.gravityY, settings.gravityZ};
    definition.bodyCapacity = static_cast<std::int32_t>(settings.bodyCapacity);
    definition.shapeCapacity = static_cast<std::int32_t>(settings.shapeCapacity);
    definition.jointCapacity = static_cast<std::int32_t>(settings.jointCapacity);
    definition.meshCapacity = static_cast<std::int32_t>(settings.meshCapacity);
    definition.enableSleeping = settings.sleeping;
    // Every hit is told, however slow: Contact3D says how hard.
    definition.hitEventThreshold = 0;
    {
        const std::scoped_lock kLock{worldTableLock()};
        state->physics = m3CreateWorld(&definition);
    }
    if (state->physics.index1 == 0) {
        return refuse(result::ErrorClass::ResourceExhausted,
                      Physics3DError::Capacity,
                      "no room for another physics world in this process");
    }
    return std::make_unique<Physics3D>(std::move(state));
}

result::Status Physics3D::declareSystems(const schema::SchemaRegistry& registry,
                                         std::vector<world::SystemDeclaration>& systems) noexcept {
    State& state = *state_;
    for (const physics::ComponentLayout& layout : componentLayouts()) {
        const auto kId = registry.find(layout.id);
        if (!kId.has_value() || registry.descriptor(*kId).size != layout.size || !registry.descriptor(*kId).plainData) {
            return refuse(result::ErrorClass::InvalidArgument,
                          Physics3DError::InvalidSettings,
                          "the World does not hold the engine's physics components");
        }
    }
    RAWFRAME_TRY_ASSIGN(
        state.bodies,
        (world::Query<world::Read<Body3D>, world::Write<Pose3D>, world::Write<Velocity3D>>::resolve(registry)));
    RAWFRAME_TRY_ASSIGN(state.impulse, registry.find(Impulse3D::kComponentTypeId));
    RAWFRAME_TRY_ASSIGN(state.contact, registry.find(Contact3D::kComponentTypeId));
    RAWFRAME_TRY_ASSIGN(state.character, registry.find(Character3D::kComponentTypeId));
    RAWFRAME_TRY_ASSIGN(state.meshShape, registry.find(Mesh3D::kComponentTypeId));
    RAWFRAME_TRY_ASSIGN(state.jointQuery, (world::Query<world::Read<Joint3D>>::resolve(registry)));
    state.reads = state.bodies->reads();
    state.reads.push_back(*state.meshShape);
    const std::vector<schema::ComponentRuntimeId> kJointReads = state.jointQuery->reads();
    state.reads.insert(state.reads.end(), kJointReads.begin(), kJointReads.end());
    state.writes = state.bodies->writes();
    state.writes.push_back(*state.impulse);
    state.writes.push_back(*state.contact);
    state.writes.push_back(*state.character);
    state.system = std::make_unique<Step>(state);
    systems.push_back(world::SystemDeclaration{.identity = kStepSystem,
                                               .phase = world::Phase::Simulation,
                                               .reads = state.reads,
                                               .writes = state.writes,
                                               .system = state.system.get()});
    return {};
}

RayHit3D Physics3D::castRay(double originX,
                            double originY,
                            double originZ,
                            float towardX,
                            float towardY,
                            float towardZ,
                            std::uint64_t among) const noexcept {
    const auto kFilter = state_->amongFilter(among);
    if (!kFilter.has_value()) {
        return RayHit3D{};
    }
    // A ray never starts inside what it meets: Maul3D rays pass out of it.
    RayHit3D hit = state_->hitOf(m3World_CastRayClosest(
        state_->physics, m3Pos3{originX, originY, originZ}, m3Vec3{towardX, towardY, towardZ}, *kFilter));
    hit.inside = false;
    return hit;
}

RayHit3D Physics3D::castSphere(double originX,
                               double originY,
                               double originZ,
                               float radius,
                               float towardX,
                               float towardY,
                               float towardZ,
                               std::uint64_t among) const noexcept {
    const auto kFilter = state_->amongFilter(among);
    if (!kFilter.has_value() || !(radius > 0) || !std::isfinite(radius)) {
        return RayHit3D{};
    }
    return state_->hitOf(m3World_CastSphereClosest(
        state_->physics, m3Pos3{originX, originY, originZ}, radius, m3Vec3{towardX, towardY, towardZ}, *kFilter));
}

void Physics3D::overlapSphere(
    double x, double y, double z, float radius, std::uint64_t among, std::vector<world::EntityHandle>& into) const {
    into.clear();
    const auto kFilter = state_->amongFilter(among);
    if (!kFilter.has_value() || !(radius > 0) || !std::isfinite(radius)) {
        return;
    }
    // Maul3D says how many it wrote, not how many there are: ask again
    // with more room until some is left over.
    std::vector<m3ShapeId>& shapes = state_->overlapShapes;
    shapes.resize(std::max<std::size_t>(shapes.size(), 16));
    std::int32_t written = 0;
    while (true) {
        written = m3World_OverlapSphere(state_->physics,
                                        m3Pos3{x, y, z},
                                        radius,
                                        shapes.data(),
                                        static_cast<std::int32_t>(shapes.size()),
                                        *kFilter);
        if (static_cast<std::size_t>(written) < shapes.size()) {
            break;
        }
        shapes.resize(shapes.size() * 2);
    }
    for (std::int32_t index = 0; index < written; ++index) {
        const world::EntityHandle kOwner = state_->ownerOf(shapes[static_cast<std::size_t>(index)]);
        if (!kOwner.isNull()) {
            into.push_back(kOwner);
        }
    }
    // A body met through its sensor twin too is told once.
    std::ranges::sort(into);
    into.erase(std::unique(into.begin(), into.end()), into.end());
}

RayHit3D Physics3D::castRayAt(double originX,
                              double originY,
                              double originZ,
                              float towardX,
                              float towardY,
                              float towardZ,
                              const physics::Moment& moment,
                              std::uint64_t among) const noexcept {
    const State& state = *state_;
    const std::uint32_t kKept = state.settings.historyTicks;
    if (!state.stepped || kKept == 0) {
        return castRay(originX, originY, originZ, towardX, towardY, towardZ, among);
    }
    const auto kFilter = state.amongFilter(among);
    if (!kFilter.has_value()) {
        return RayHit3D{};
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
    const Turned kOrigin{originX, originY, originZ};
    const Turned kToward{towardX, towardY, towardZ};

    RayHit3D closest;
    for (const auto& [entity, entry] : state.mapped) {
        if (entry.refused || (among != physics::kEveryClass && entry.made.collisionClass != among)) {
            continue;
        }
        // The gate may hold it to a later tick, or keep it where it is.
        const std::optional<std::uint64_t> kSince =
            moment.gate == nullptr ? std::optional<std::uint64_t>{0} : moment.gate->since(entity);
        const bool kRewound = kSince.has_value() && *kSince < state.lastTick;
        const std::uint64_t kTick = kRewound ? std::max(kBase, *kSince) : kBase;
        const bool kTrail = entry.since.has_value() && *entry.since <= kTick;
        const m3Transform kNow = m3Body_GetTransform(entry.body);
        m3Transform then = kNow;
        if (kTrail && kRewound) {
            const Pose3D& from = entry.history[kTick % kKept];
            Pose3D at = from;
            if (kFraction != 0 && kTick == kBase) {
                // The client's blend of two states (interpolation.cpp),
                // field by field, made unit when shown.
                const Pose3D& to = entry.history[(kTick + 1) % kKept];
                const auto kBlend = [kAlong](float one, float other) {
                    return static_cast<float>(one + ((static_cast<double>(other) - one) * kAlong));
                };
                at.x = from.x + ((to.x - from.x) * kAlong);
                at.y = from.y + ((to.y - from.y) * kAlong);
                at.z = from.z + ((to.z - from.z) * kAlong);
                at.qx = kBlend(from.qx, to.qx);
                at.qy = kBlend(from.qy, to.qy);
                at.qz = kBlend(from.qz, to.qz);
                at.qw = kBlend(from.qw, to.qw);
            }
            then = transformOf(at);
        }
        if (!passesNear(kOrigin, kToward, Turned{then.p.x, then.p.y, then.p.z}, entry.reach)) {
            continue;
        }
        // The ray in the body's frame then is the ray in its frame now; the
        // world's ray is asked, and only this body's shape is looked for.
        const Turned kFrom = toWorld(kNow, toLocal(then, kOrigin));
        const Turned kAcross = turn(kNow.q, turn(then.q, kToward, true), false);
        const m3Pos3 kStart{kFrom.x, kFrom.y, kFrom.z};
        const m3Vec3 kDirection{
            static_cast<float>(kAcross.x), static_cast<float>(kAcross.y), static_cast<float>(kAcross.z)};
        state.rayHits.resize(std::max<std::size_t>(state.rayHits.size(), 16));
        std::int32_t total = m3World_CastRayAll(state.physics,
                                                kStart,
                                                kDirection,
                                                state.rayHits.data(),
                                                static_cast<std::int32_t>(state.rayHits.size()),
                                                *kFilter);
        if (static_cast<std::size_t>(total) > state.rayHits.size()) {
            state.rayHits.resize(static_cast<std::size_t>(total));
            total = m3World_CastRayAll(state.physics, kStart, kDirection, state.rayHits.data(), total, *kFilter);
        }
        const auto kEnd = state.rayHits.begin() + std::min<std::ptrdiff_t>(total, std::ssize(state.rayHits));
        const auto kHit = std::find_if(state.rayHits.begin(), kEnd, [&entry](const m3RayHit& hit) {
            return sameShape(hit.shapeId, entry.shape) ||
                   (entry.trigger.index1 != 0 && sameShape(hit.shapeId, entry.trigger)) ||
                   std::ranges::any_of(entry.pieces, [&hit](m3ShapeId piece) {
                       return sameShape(hit.shapeId, piece);
                   });
        });
        if (kHit == kEnd || (closest.hit && kHit->fraction >= closest.fraction)) {
            continue;
        }
        const Turned kPoint = toWorld(then, toLocal(kNow, Turned{kHit->point.x, kHit->point.y, kHit->point.z}));
        const Turned kNormal =
            turn(then.q, turn(kNow.q, Turned{kHit->normal.x, kHit->normal.y, kHit->normal.z}, true), false);
        closest = RayHit3D{.hit = true,
                           .discontinuous = kRewound && !kTrail,
                           .entity = entity,
                           .x = kPoint.x,
                           .y = kPoint.y,
                           .z = kPoint.z,
                           .normalX = static_cast<float>(kNormal.x),
                           .normalY = static_cast<float>(kNormal.y),
                           .normalZ = static_cast<float>(kNormal.z),
                           .fraction = kHit->fraction};
    }
    return closest;
}

Physics3DStatistics Physics3D::statistics() const noexcept {
    Physics3DStatistics statistics = state_->statistics;
    statistics.raysRewound = state_->raysRewound;
    statistics.rewindsClamped = state_->rewindsClamped;
    return statistics;
}

std::uint64_t Physics3D::digest() const noexcept {
    return m3World_Hash(state_->physics);
}

} // namespace rawframe::physics3d
