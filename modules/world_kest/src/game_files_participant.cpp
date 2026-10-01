#include "game_files_participant.h"

#include "rawframe/composition/composition.h"
#include "rawframe/game_content/game_content.h"
#include "rawframe/world_kest/errors.h"
#include "rawframe/world_kest/game_files.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace rawframe::world_kest {

namespace {

constexpr std::string_view kProvides[] = {kGameFiles.name};
constexpr std::string_view kMaybe[] = {game_content::kGameContent.name};

class GameFilesParticipant final : public composition::Participant {
public:
    composition::CapabilityObject provide(std::string_view capability) noexcept override {
        if (capability == kGameFiles.name) {
            return composition::provideAs<GameFiles>(files);
        }
        return {};
    }

    GameFiles files;
};

result::Result<composition::ParticipantOwner> makeGameFiles(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<GameFilesParticipant>();
    const auto kPath = context.configuration().path("kest.game");
    const auto kResource = context.configuration().text("kest.game_resource");
    if (kPath && kResource) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kWorldKestDomain,
                            code(WorldKestError::UnknownName),
                            "a game is named by kest.game or kest.game_resource, not both");
    }
    if (kPath) {
        game_content::GameContent* content = nullptr;
        if (context.has(game_content::kGameContent.name)) {
            RAWFRAME_TRY_ASSIGN(content, context.capability(game_content::kGameContent));
        }
        // From the files the host holds when it holds some (D167): the
        // description's directory's files.
        if (const composition::HeldFiles* held = context.heldFiles()) {
            const std::string_view kHeld = *kPath;
            const std::size_t kSlash = kHeld.rfind('/');
            const std::string_view kDirectory = kSlash == std::string_view::npos ? "" : kHeld.substr(0, kSlash);
            std::vector<std::pair<std::string, std::string>> files;
            for (auto& [path, bytes] : held->under(kDirectory)) {
                files.emplace_back(std::move(path),
                                   std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()});
            }
            const std::string_view kName = kSlash == std::string_view::npos ? kHeld : kHeld.substr(kSlash + 1);
            RAWFRAME_TRY_ASSIGN(participant->files, GameFiles::fromHeld(kName, std::move(files), content));
        } else {
#if RAWFRAME_FILE_SYSTEM
            RAWFRAME_TRY_ASSIGN(participant->files, GameFiles::fromDirectory(std::string{*kPath}, content));
#else
            return result::fail(result::ErrorClass::FailedPrecondition,
                                kWorldKestDomain,
                                code(WorldKestError::UnreadableFile),
                                "kest.game names a file, and there are none here; name kest.game_resource or hold it");
#endif
        }
    } else if (kResource) {
        const base::Bits128Parse kId = base::parseBits128Hex(*kResource);
        if (!kId.parsed || kId.value == base::Bits128{} || !context.has(game_content::kGameContent.name)) {
            return result::fail(result::ErrorClass::InvalidArgument,
                                kWorldKestDomain,
                                code(WorldKestError::UnknownName),
                                "kest.game_resource is a resource identity of the Runtime's content");
        }
        RAWFRAME_TRY_ASSIGN(game_content::GameContent * content, context.capability(game_content::kGameContent));
        RAWFRAME_TRY_ASSIGN(participant->files, GameFiles::fromContent(*content, content::ResourceId{kId.value}));
    }
    return composition::ParticipantOwner{participant.release()};
}

} // namespace

void registerGameFiles(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.world_kest.game_files",
        .factory = &makeGameFiles,
        .scope = composition::LifetimeScope::Runtime,
        .providedCapabilities = kProvides,
        .optionalCapabilities = kMaybe,
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(10)},
        .observabilityIdentity = "world_kest.game_files",
        .budgetOwner = "world",
    });
}

} // namespace rawframe::world_kest
