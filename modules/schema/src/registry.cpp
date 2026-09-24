#include "rawframe/schema/registry.h"

#include "rawframe/schema/errors.h"

#include <algorithm>

namespace rawframe::schema {

result::Result<ComponentRuntimeId> SchemaRegistry::find(ComponentTypeId id) const {
    const auto kFound = std::lower_bound(
        components_.begin(), components_.end(), id, [](const ComponentDescriptor& descriptor, ComponentTypeId wanted) {
            return descriptor.id < wanted;
        });
    if (kFound == components_.end() || kFound->id != id) {
        return result::fail(result::ErrorClass::NotFound,
                            kSchemaDomain,
                            code(SchemaError::UnknownComponent),
                            "the component type is not in this registry");
    }
    return ComponentRuntimeId{static_cast<std::uint32_t>(kFound - components_.begin())};
}

result::Result<std::shared_ptr<const SchemaRegistry>> RegistryBuilder::freeze() const {
    auto registry = std::make_shared<SchemaRegistry>();
    registry->components_ = descriptors_;
    std::vector<ComponentDescriptor>& components = registry->components_;
    for (const ComponentDescriptor& descriptor : components) {
        if (!descriptor.id.valid()) {
            return result::fail(result::ErrorClass::InvalidArgument,
                                kSchemaDomain,
                                code(SchemaError::InvalidComponentId),
                                "a component has the all-zero stable ID");
        }
        if (descriptor.name.empty() || descriptor.name.size() > kMaximumComponentNameBytes) {
            return result::fail(result::ErrorClass::InvalidArgument,
                                kSchemaDomain,
                                code(SchemaError::InvalidComponentName),
                                "a component name is empty or longer than kMaximumComponentNameBytes");
        }
        // Only plain data may move as bytes: anything else needs its own move
        // and destroy.
        const bool kMissing =
            descriptor.operations.moveConstruct == nullptr || descriptor.operations.destroy == nullptr;
        if (descriptor.size != 0 && kMissing && !descriptor.plainData) {
            return result::fail(result::ErrorClass::InvalidArgument,
                                kSchemaDomain,
                                code(SchemaError::MissingOperations),
                                "a component that is not plain data has no move or destroy");
        }
    }
    std::sort(
        components.begin(), components.end(), [](const ComponentDescriptor& left, const ComponentDescriptor& right) {
            return left.id < right.id;
        });
    for (std::size_t index = 1; index < components.size(); ++index) {
        if (components[index - 1].id == components[index].id) {
            return std::unexpected<result::Error>{result::fail(result::ErrorClass::AlreadyExists,
                                                               kSchemaDomain,
                                                               code(SchemaError::DuplicateComponentId),
                                                               "two components share a stable ID")
                                                      .error()
                                                      .withContext("name", components[index].name)};
        }
    }
    std::vector<std::string_view> names;
    for (const ComponentDescriptor& descriptor : components) {
        names.push_back(descriptor.name);
    }
    std::sort(names.begin(), names.end());
    if (const auto kDuplicate = std::adjacent_find(names.begin(), names.end()); kDuplicate != names.end()) {
        return std::unexpected<result::Error>{result::fail(result::ErrorClass::AlreadyExists,
                                                           kSchemaDomain,
                                                           code(SchemaError::DuplicateComponentName),
                                                           "two components share a canonical name")
                                                  .error()
                                                  .withContext("name", *kDuplicate)};
    }
    return std::shared_ptr<const SchemaRegistry>{std::move(registry)};
}

} // namespace rawframe::schema
