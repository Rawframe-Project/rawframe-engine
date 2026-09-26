#include "rawframe/install/plan.h"

#include "rawframe/document/json.h"
#include "rawframe/install/errors.h"

namespace rawframe::install {

std::uint64_t UpdatePlan::bytes() const noexcept {
    std::uint64_t total = 0;
    for (const PlanEntry& entry : entries) {
        total += entry.blobSize;
    }
    return total;
}

UpdatePlan planUpdate(std::span<const content::BuildManifest> targets, const Inventory& inventory) {
    UpdatePlan plan;
    Inventory planned;
    for (const content::BuildManifest& target : targets) {
        for (const std::vector<content::BuildChunk>& chunks : target.chunks) {
            for (const content::BuildChunk& chunk : chunks) {
                if (inventory.contains(chunk.blob.bytes) || !planned.insert(chunk.blob.bytes).second) {
                    continue;
                }
                plan.entries.push_back(PlanEntry{.blob = chunk.blob,
                                                 .blobSize = chunk.blobSize,
                                                 .content = chunk.content,
                                                 .manifest = target.descriptor});
            }
        }
    }
    return plan;
}

result::Result<std::string> writePlan(const UpdatePlan& plan) {
    using document::Value;
    Value entries = Value::array();
    for (const PlanEntry& entry : plan.entries) {
        Value each = Value::object();
        each.add("blob", Value::string(entry.blob.text()));
        each.add("blob_size", Value::integer(static_cast<std::int64_t>(entry.blobSize)));
        each.add("content", Value::string(entry.content.text()));
        each.add("manifest", Value::string(entry.manifest.text()));
        entries.push(std::move(each));
    }
    Value record = Value::object();
    record.add("schema", Value::integer(1));
    record.add("entries", std::move(entries));
    RAWFRAME_TRY_ASSIGN(std::string written, document::writeCanonicalRecord(record));
    if (written.size() > kMaximumPlanRecord) {
        return std::unexpected<result::Error>{result::fail(result::ErrorClass::ResourceExhausted,
                                                           kInstallDomain,
                                                           code(InstallError::OverLimit),
                                                           "an UpdatePlan's record is at most 64 MiB")
                                                  .error()};
    }
    return written;
}

} // namespace rawframe::install
