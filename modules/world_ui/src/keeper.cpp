#include "keeper.h"

#include "rawframe/base/files.h"
#include "rawframe/document/json.h"
#include "rawframe/document/record.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <span>
#include <vector>

#if RAWFRAME_FILE_SYSTEM
#include <chrono>
#include <system_error>
#include <thread>
#endif

namespace rawframe::world_ui {

namespace {

using document::Record;
using document::Value;

constexpr std::array<std::string_view, 3> kKeptFields = {"kind", "formatVersion", "values"};
/// A component's name is at most this long (ui::DocumentLimits).
constexpr std::size_t kLongestName = 128;

} // namespace

result::Result<KeptValues> readKept(std::string_view text) {
    RAWFRAME_TRY_ASSIGN(const Value kRoot, document::parse(text, {.maximumBytes = kMostKeptBytes, .maximumDepth = 4}));
    // The version first: a document of another version is not read further.
    const Value* version = kRoot.find("formatVersion");
    if (version == nullptr || version->integer() != 1) {
        return document::invalid("formatVersion", "the format version is 1");
    }
    RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(kRoot, kKeptFields, "$"));
    RAWFRAME_TRY_ASSIGN(const std::string_view kKind, kRecord.text("kind"));
    if (kKind != "ui.kept") {
        return document::invalid(kRecord.pathOf("kind"), "the kind is ui.kept");
    }
    RAWFRAME_TRY_ASSIGN(const Value* values, kRecord.required("values", Value::Kind::Object));
    if (values->items().size() > kMostKeptValues) {
        return document::invalid(kRecord.pathOf("values"), "more values than are kept");
    }
    KeptValues kept;
    for (std::size_t index = 0; index < values->items().size(); ++index) {
        const std::string& kName = values->names()[index];
        const std::optional<double> kValue = values->items()[index].real();
        const std::string kPath = kRecord.pathOf("values") + "." + kName;
        if (kName.empty() || kName.size() > kLongestName) {
            return document::invalid(kPath, "a value is kept under its component's name");
        }
        if (!kValue.has_value() || !std::isfinite(static_cast<float>(*kValue))) {
            return document::invalid(kPath, "a kept value is a finite number");
        }
        kept.emplace(kName, static_cast<float>(*kValue));
    }
    return kept;
}

std::string writeKept(const KeptValues& values) {
    Value each = Value::object();
    for (const auto& [kName, kValue] : values) {
        each.add(kName, Value::real(kValue));
    }
    Value root = Value::object();
    root.add("kind", Value::string("ui.kept"));
    root.add("formatVersion", Value::integer(1));
    root.add("values", std::move(each));
    return document::write(root);
}

#if RAWFRAME_FILE_SYSTEM

Keeper::~Keeper() {
    finish(nullptr);
}

result::Result<KeptValues> Keeper::read() const {
    std::FILE* file = std::fopen(path_.string().c_str(), "rb");
    if (file == nullptr) {
        return KeptValues{};
    }
    std::string text;
    std::array<char, 4096> chunk{};
    std::size_t got = 0;
    while ((got = std::fread(chunk.data(), 1, chunk.size(), file)) != 0 && text.size() <= kMostKeptBytes) {
        text.append(chunk.data(), got);
    }
    const bool kFailed = std::ferror(file) != 0;
    std::fclose(file);
    if (kFailed || text.size() > kMostKeptBytes) {
        return document::invalid("$", "the player's settings cannot be read whole");
    }
    return readKept(text);
}

void Keeper::keep(const KeptValues& values, execution::Executor* io, execution::OwnerId owner) noexcept {
    if (writing_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    const auto kSubmitted = io != nullptr ? io->submit(owner,
                                                       execution::Priority::Background,
                                                       [this, text = writeKept(values)]() noexcept {
                                                           write(text);
                                                           writing_.store(false, std::memory_order_release);
                                                       })
                                          : result::Status{};
    if (io == nullptr || !kSubmitted.has_value()) {
        write(writeKept(values));
        writing_.store(false, std::memory_order_release);
    }
}

void Keeper::finish(execution::Executor* io) noexcept {
    // Helping: the write runs here if no worker has taken it.
    while (writing_.load(std::memory_order_acquire)) {
        if (io != nullptr && io->runOne()) {
            continue;
        }
#if RAWFRAME_THREADS
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
#endif
    }
}

void Keeper::write(const std::string& text) noexcept {
    std::error_code error;
    if (path_.has_parent_path()) {
        std::filesystem::create_directories(path_.parent_path(), error);
    }
    const bool kWritten = base::replaceFile(path_, std::as_bytes(std::span{text.data(), text.size()}));
    (kWritten ? written_ : failed_).fetch_add(1, std::memory_order_acq_rel);
}

#endif

} // namespace rawframe::world_ui
