#include "rawframe/world_kest/commands.h"

#include "rawframe/world_kest/errors.h"
#include "rawframe/world_kest/layouts.h"

namespace rawframe::world_kest {

result::Result<std::vector<CommandKind>>
commandKindsOf(const GameDescription& game, const kest::Program& program, bool everyOne) {
    std::vector<CommandKind> kinds;
    for (std::size_t index = 0; index < game.commands.size(); ++index) {
        const GameCommand& command = game.commands[index];
        const auto kLayout = program.layout(command.kestType);
        if (!kLayout.has_value() && !everyOne) {
            continue;
        }
        if (!kLayout.has_value() || holdsEntity(*kLayout) || kLayout->size > kMaximumCommandRecord ||
            kLayout->size == 0) {
            return std::unexpected<result::Error>{
                result::fail(result::ErrorClass::InvalidArgument,
                             kWorldKestDomain,
                             code(WorldKestError::BadGameLine),
                             "a command's type is one the program lays out, of 1 to 1,024 bytes, holding no entity")
                    .error()
                    .withContext("command", command.name)};
        }
        kinds.push_back(CommandKind{.kind = static_cast<std::uint32_t>(index),
                                    .name = command.name,
                                    .kestType = command.kestType,
                                    .size = kLayout->size});
    }
    return kinds;
}

} // namespace rawframe::world_kest
