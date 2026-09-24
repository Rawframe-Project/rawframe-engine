// The configuration snapshot: parsing, typed reads, and refusals.

#include "rawframe/composition/configuration.h"
#include "rawframe/composition/errors.h"
#include "rawframe/test/test.h"

#include <string>

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
