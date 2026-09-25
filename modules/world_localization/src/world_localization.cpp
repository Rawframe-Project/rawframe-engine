#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/content/store.h"
#include "rawframe/game_content/game_content.h"
#include "rawframe/localization/errors.h"
#include "rawframe/localization/table.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_localization/registrar.h"
#include "rawframe/world_localization/text.h"

#if !RAWFRAME_SHIPPING
#include "rawframe/localization/pseudo.h"
#endif

#include <algorithm>
#include <array>
#include <memory>

namespace rawframe::world_localization {

GameText::GameText(localization::Catalog catalog,
                   std::vector<std::pair<std::string, base::Bits128>> tables,
                   localization::Locale requested,
                   localization::Locale projectDefault) noexcept
    : catalog_(std::move(catalog)), tables_(std::move(tables)), requested_(std::move(requested)),
      projectDefault_(std::move(projectDefault)) {
}

std::optional<base::Bits128> GameText::table(std::string_view path) const noexcept {
    const auto kFound = std::ranges::find(tables_, path, &std::pair<std::string, base::Bits128>::first);
    return kFound != tables_.end() ? std::optional{kFound->second} : std::nullopt;
}

result::Result<std::string>
GameText::format(std::string_view path, std::string_view key, std::span<const localization::Argument> arguments) const {
    const std::optional<base::Bits128> kTable = table(path);
    if (!kTable.has_value()) {
        return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                           localization::kLocalizationDomain,
                                                           code(localization::LocalizationError::KeyUnknown),
                                                           "no text line of the game names that table")
                                                  .error()
                                                  .withContext("path", path)};
    }
    return catalog_.format(*kTable, key, requested_, projectDefault_, arguments);
}

namespace {

using diagnostics::EventIdentity;

constexpr EventIdentity kCatalogEvent{"localization", "text_catalog"};
constexpr EventIdentity kStaleEvent{"localization", "text_stale"};
constexpr EventIdentity kUnavailableEvent{"localization", "text_unavailable"};
constexpr EventIdentity kLocaleRefusedEvent{"localization", "locale_refused"};
constexpr std::string_view kProvides[] = {kGameText.name};
constexpr std::string_view kMaybe[] = {world_kest::kGameFiles.name, game_content::kGameContent.name};

std::string hexOf(base::Bits128 value) {
    std::array<char, base::kBits128HexDigits> digits{};
    base::formatBits128Hex(value, digits);
    return std::string{digits.data(), digits.size()};
}

result::Result<std::string> readResource(content::ContentStore& store, const content::ResourceRef& reference) {
    RAWFRAME_TRY_ASSIGN(execution::AsyncHandle<content::VerifiedContent> read, store.read(reference));
    // Cancelled only when the Runtime is stopping as it composes.
    RAWFRAME_TRY_ASSIGN(const content::VerifiedContent kRead,
                        execution::toResult(read.wait(),
                                            execution::CancellationMapping{
                                                .errorClass = result::ErrorClass::Unavailable,
                                                .domain = localization::kLocalizationDomain,
                                                .code = code(localization::LocalizationError::DocumentInvalid),
                                                .description = "a text document's read was cancelled"}));
    const std::span<const std::byte> kBytes = kRead.bytes();
    return std::string{reinterpret_cast<const char*>(kBytes.data()), kBytes.size()};
}

class TextParticipant final : public composition::Participant {
public:
    composition::CapabilityObject provide(std::string_view capability) noexcept override {
        if (capability == kGameText.name && text_ != nullptr) {
            return composition::provideAs<GameText>(*text_);
        }
        return {};
    }

    result::Status load(composition::ParticipantContext& context) {
        if (!context.has(world_kest::kGameFiles.name)) {
            return {};
        }
        RAWFRAME_TRY_ASSIGN(const world_kest::GameFiles* files, context.capability(world_kest::kGameFiles));
        if (!files->named() || files->texts().empty()) {
            return {};
        }
        game_content::GameContent* content = nullptr;
        if (context.has(game_content::kGameContent.name)) {
            RAWFRAME_TRY_ASSIGN(content, context.capability(game_content::kGameContent));
        }
        if (content == nullptr || !content->held()) {
            unavailable_ = true;
            return {};
        }
        const content::ResourceTypeId kStrings{localization::kStringsType};
        const content::ResourceTypeId kTranslations{localization::kTranslationsType};
        const std::array<content::AdmittedRepresentation, 2> kAdmitted = {
            content::AdmittedRepresentation{
                .type = kStrings,
                .representation = *content::RepresentationId::parse(localization::kStringsRepresentation)},
            content::AdmittedRepresentation{
                .type = kTranslations,
                .representation = *content::RepresentationId::parse(localization::kTranslationsRepresentation)}};
        RAWFRAME_TRY(content->admit(kAdmitted));

        // Each document by the type its bytes say; the store checks it is
        // the type the catalog holds it as.
        std::vector<localization::TableDocument> tables;
        std::vector<localization::Translations> translations;
        std::vector<std::pair<std::string, base::Bits128>> named;
        for (const world_kest::GameText& text : files->texts()) {
            const content::ResourceId kId{text.document};
            auto table = readResource(content->store(), content::ResourceRef{.id = kId, .type = kStrings});
            if (table.has_value()) {
                RAWFRAME_TRY_ASSIGN(localization::StringTable read, localization::readStrings(*table));
                tables.push_back(localization::TableDocument{.id = text.document, .table = std::move(read)});
                named.emplace_back(text.path, text.document);
                continue;
            }
            auto translation = readResource(content->store(), content::ResourceRef{.id = kId, .type = kTranslations});
            if (!translation.has_value()) {
                return std::unexpected<result::Error>{std::move(translation).error().withContext("path", text.path)};
            }
            RAWFRAME_TRY_ASSIGN(localization::Translations read, localization::readTranslations(*translation));
            translations.push_back(std::move(read));
        }
        if (tables.empty()) {
            return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                               localization::kLocalizationDomain,
                                                               code(localization::LocalizationError::CatalogInvalid),
                                                               "a game's text lines name a string table")
                                                      .error()};
        }

        // The project's default, and the locale the player asks for.
        localization::Locale projectDefault = tables.front().table.sourceLocale;
        if (!files->description().locale.empty()) {
            RAWFRAME_TRY_ASSIGN(projectDefault, localization::parseLocale(files->description().locale));
        }
        localization::Locale requested = projectDefault;
        if (const auto kAsked = context.configuration().text("localization.locale")) {
            auto taken = localization::intake(*kAsked);
            if (taken.has_value()) {
                requested = std::move(*taken);
            } else {
                refusedLocale_ = std::string{*kAsked};
            }
        }

        std::vector<localization::Translations> pseudo;
#if !RAWFRAME_SHIPPING
        if (context.configuration().text("localization.pseudo") == "true") {
            const localization::Locale kPseudo = *localization::parseLocale("en-XA");
            for (const localization::TableDocument& table : tables) {
                RAWFRAME_TRY_ASSIGN(localization::Translations made,
                                    localization::pseudoTranslations(table.id, table.table, kPseudo));
                pseudo.push_back(std::move(made));
            }
        }
        RAWFRAME_TRY_ASSIGN(localization::Catalog catalog,
                            localization::Catalog::buildWithPseudo(tables, translations, pseudo));
#else
        RAWFRAME_TRY_ASSIGN(localization::Catalog catalog, localization::Catalog::build(tables, translations));
#endif

        summary_.tables = tables.size();
        summary_.translations = translations.size() + pseudo.size();
        for (const localization::TableDocument& table : tables) {
            summary_.keys += table.table.entries.size();
        }
        text_ = std::make_unique<GameText>(std::move(catalog), std::move(named), std::move(requested), projectDefault);
        return {};
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        diagnostics::Emitter emitter = context.emitter();
        if (unavailable_) {
            emitter.log(diagnostics::Severity::Warning,
                        kUnavailableEvent,
                        "no cooked content: the game's text is not read",
                        {});
        }
        if (refusedLocale_.has_value()) {
            emitter.log(diagnostics::Severity::Warning,
                        kLocaleRefusedEvent,
                        "the asked locale is not one CLDR knows: the default serves",
                        {diagnostics::field("asked", *refusedLocale_)});
        }
        if (text_ == nullptr) {
            return {};
        }
        for (const localization::StaleEntry& stale : text_->catalog().stale()) {
            emitter.log(diagnostics::Severity::Warning,
                        kStaleEvent,
                        "a translation's source changed since it was translated",
                        {diagnostics::field("table", hexOf(stale.table)),
                         diagnostics::field("locale", stale.locale.text()),
                         diagnostics::field("key", stale.key)});
        }
        emitter.log(diagnostics::Severity::Info,
                    kCatalogEvent,
                    "the game's text",
                    {diagnostics::field("tables", static_cast<std::uint64_t>(summary_.tables)),
                     diagnostics::field("translations", static_cast<std::uint64_t>(summary_.translations)),
                     diagnostics::field("keys", static_cast<std::uint64_t>(summary_.keys)),
                     diagnostics::field("stale", static_cast<std::uint64_t>(text_->catalog().stale().size())),
                     diagnostics::field("locale", text_->requested().text()),
                     diagnostics::field("default", text_->projectDefault().text())});
        return {};
    }

private:
    struct Summary {
        std::size_t tables = 0;
        std::size_t translations = 0;
        std::size_t keys = 0;
    };

    std::unique_ptr<GameText> text_;
    Summary summary_;
    bool unavailable_ = false;
    std::optional<std::string> refusedLocale_;
};

result::Result<composition::ParticipantOwner> make(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<TextParticipant>();
    RAWFRAME_TRY(participant->load(context));
    return composition::ParticipantOwner{participant.release()};
}

} // namespace

void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.world_localization.text",
        .factory = &make,
        .scope = composition::LifetimeScope::Runtime,
        .providedCapabilities = kProvides,
        .optionalCapabilities = kMaybe,
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(10)},
        .observabilityIdentity = "world_localization.text",
        .budgetOwner = "presentation",
    });
}

} // namespace rawframe::world_localization
