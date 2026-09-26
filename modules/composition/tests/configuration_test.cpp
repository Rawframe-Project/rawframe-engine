// The configuration snapshot: parsing, typed reads, and refusals.

#include "rawframe/composition/configuration.h"
#include "rawframe/composition/errors.h"
#include "rawframe/test/test.h"

#include <string>
#include <string_view>
#include <vector>

using namespace rawframe::composition;

RAWFRAME_TEST(ConfigurationParsesKeyValueLines) {
    const auto kParsed = Configuration::parse("# a comment\n\nworld.tick_rate = 60\r\n  world.name=  arena one  \n");
    RAWFRAME_EXPECT(kParsed.has_value());
    if (!kParsed.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(kParsed->size() == 2);
    RAWFRAME_EXPECT(kParsed->text("world.name") == "arena one");
    RAWFRAME_EXPECT(*kParsed->unsignedInteger("world.tick_rate", 1) == 60);
    RAWFRAME_EXPECT(*kParsed->unsignedInteger("world.missing", 7) == 7);
    RAWFRAME_EXPECT(!kParsed->unsignedInteger("world.name", 0).has_value());
    RAWFRAME_EXPECT(!kParsed->text("world.missing").has_value());
}

RAWFRAME_TEST(MalformedConfigurationIsRefused) {
    for (const std::string_view kText :
         {"no_equals_here", "Upper.key = 1", "a = 1\na = 2", "= value", "key with space = 1", "9starts.digit = 1"}) {
        const auto kParsed = Configuration::parse(kText);
        RAWFRAME_EXPECT(!kParsed.has_value() && kParsed.error().code() == code(CompositionError::BadConfiguration));
    }
    std::string tooMany;
    for (int index = 0; index <= static_cast<int>(kMaximumConfigurationEntries); ++index) {
        tooMany += "key" + std::to_string(index) + " = 1\n";
    }
    RAWFRAME_EXPECT(!Configuration::parse(tooMany).has_value());
    RAWFRAME_EXPECT(
        !Configuration::parse("key = " + std::string(kMaximumConfigurationValueBytes + 1, 'v')).has_value());
    RAWFRAME_EXPECT(Configuration::parse("").has_value());
}

RAWFRAME_TEST(PathsAreUnderTheConfigurationsOwnDirectory) {
    // Where a process starts from never changes what its configuration
    // names (SPEC-0012, D188): relative paths are under the file's directory.
    const auto kRead = Configuration::parse(
        "game = games/arena.game\nroot = /srv/content\ndrive = C:/content\nshare = \\\\host\\content\nempty =\n",
        "/etc/rawframe/");
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(kRead->path("game") == "/etc/rawframe/games/arena.game");
    RAWFRAME_EXPECT(kRead->path("root") == "/srv/content" && kRead->path("drive") == "C:/content" &&
                    kRead->path("share") == "\\\\host\\content" && kRead->path("empty") == "");
    RAWFRAME_EXPECT(!kRead->path("absent").has_value());
    // Without a base, as given; and a path read is a key read.
    const auto kGiven = Configuration::parse("game = games/arena.game\n");
    RAWFRAME_EXPECT(kGiven.has_value() && kGiven->path("game") == "games/arena.game" && kGiven->unread().empty());
}

RAWFRAME_TEST(AKeyNothingAskedForIsUnread) {
    const auto kRead = Configuration::parse("a.b = 1\nc.d = 2\ne.f = x\n");
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    RAWFRAME_EXPECT((kRead->unread() == std::vector<std::string_view>{"a.b", "c.d", "e.f"}));
    static_cast<void>(kRead->unsignedInteger("c.d", 0));
    static_cast<void>(kRead->text("e.f"));
    static_cast<void>(kRead->text("g.h"));
    RAWFRAME_EXPECT((kRead->unread() == std::vector<std::string_view>{"a.b"}));
}
