// Messages (SPEC-0033): the MessageFormat 2.0 subset read and checked
// whole, selection by exact value, then plural category, then `*`, and
// placeholders written in the locale's numbers.

#include "rawframe/localization/errors.h"
#include "rawframe/localization/message.h"
#include "rawframe/test/test.h"

#include <cmath>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

using namespace rawframe;
using namespace rawframe::localization;

namespace {

bool refusedWith(const auto& outcome, LocalizationError error) {
    return !outcome.has_value() && outcome.error().domain() == kLocalizationDomain &&
           outcome.error().code() == code(error);
}

/// The message formatted in a locale, or the refusal's code as `error N`.
std::string said(std::string_view text, std::string_view tag, std::initializer_list<Argument> arguments = {}) {
    const auto kMessage = parseMessage(text);
    if (!kMessage.has_value()) {
        return "unread";
    }
    const std::vector<Argument> kArguments{arguments};
    const auto kMade = format(*kMessage, *parseLocale(tag), kArguments);
    return kMade.has_value() ? *kMade : "error " + std::to_string(kMade.error().code().value);
}

constexpr std::string_view kFiles = ".input {$count :integer}\n"
                                    ".match $count\n"
                                    "0   {{No files}}\n"
                                    "one {{One file}}\n"
                                    "*   {{{$count} files}}\n";

} // namespace

RAWFRAME_TEST(ASimpleMessageIsTextAndPlaceholders) {
    const auto kMessage = parseMessage("Hello, {$name}! \\{not\\} \\\\");
    RAWFRAME_EXPECT(kMessage.has_value());
    RAWFRAME_EXPECT(kMessage->pattern.size() == 3 && kMessage->pattern[0].text == "Hello, " &&
                    kMessage->pattern[1].placeholder->operand.text == "name" &&
                    kMessage->pattern[2].text == "! {not} \\");
    RAWFRAME_EXPECT(argumentsOf(*kMessage) == std::vector<std::string>{"name"});
    RAWFRAME_EXPECT(said("Hello, {$name}!", "en", {{"name", std::string{"Ada"}}}) == "Hello, Ada!");
    RAWFRAME_EXPECT(parseMessage("").has_value() && said("", "en").empty());
}

RAWFRAME_TEST(MessagesOutOfTheSubsetAreRefused) {
    for (const std::string_view kBad : {
             " Hello",                                         // leading whitespace in a simple message (D143)
             ".hello",                                         // a simple message beginning with `.`
             "{$x :datetime}",                                 // an unknown function
             "{$x :ns:number}",                                // a namespaced function
             "{#b}bold{/b}",                                   // markup
             "{$x @attribute}",                                // an attribute
             "{$x :string style=loud}",                        // an option :string does not take
             "{$x :integer minimumFractionDigits=1}",          // an option :integer does not take
             "{$x :number minimumFractionDigits=16}",          // past the ceiling
             "{$x :number minimumFractionDigits=4}",           // above the default maximum
             "{$x :number select=$y}",                         // a variable option
             "{$x :number select=exact select=ordinal}",       // an option twice
             "{|text|}",                                       // a placeholder without a variable
             "{42}",                                           // an unquoted literal
             "{$X}",                                           // a variable out of the name form
             "{$x:number}",                                    // parts not apart
             "a } b",                                          // an unescaped brace
             "\\n",                                            // an escape the subset lacks
             "{$x",                                            // an expression unclosed
             ".input {$n :integer} {{a}} b",                   // text past the quoted pattern
             ".input {$n :integer} .match $n one {{a}}",       // no all-`*` variant
             ".input {$n :integer} .match $n * {{a}} * {{b}}", // two
             ".input {$n :integer} .match $n one {{a}} one {{b}} * {{c}}",    // the same keys twice
             ".input {$n :integer} .match $n |one| {{a}} * {{b}}",            // a quoted key for a number
             ".input {$n :integer} .match $n lots {{a}} * {{b}}",             // a key neither category nor integer
             ".input {$n :integer select=exact} .match $n one {{a}} * {{b}}", // a category with exact selection
             ".input {$s :string} .match $s one {{a}} * {{b}}",               // an unquoted key for text
             ".input {$n :integer} .match $n one * {{a}} * {{b}}",            // more keys than selectors
             ".input {$n} .match $n * {{a}}",                                 // a selector no function annotates
             ".match $n * {{a}}",                                             // a selector not declared
             ".local $a = {$b} .input {$b} {{x}}",                            // declaring a variable already read
             ".input {$a} .input {$a} {{x}}",                                 // declaring one twice
             ".local $a = {$a :number} {{x}}",                                // a declaration reading itself
             ".local $a = {|x| :number} {{x}}",                               // a number function on text
             ".local $a = {|1.5| :integer} {{x}}",                            // an integer function on a decimal
             ".input {$n :number minimumFractionDigits=2} .local $m = {$n :number maximumFractionDigits=1} "
             "{{{$m}}}", // inherited options out of order
         }) {
        RAWFRAME_EXPECT(refusedWith(parseMessage(kBad), LocalizationError::MessageInvalid));
    }
}

RAWFRAME_TEST(MessagesPastTheirLimitsAreRefused) {
    RAWFRAME_EXPECT(refusedWith(parseMessage(std::string(4097, 'a')), LocalizationError::OverLimit));
    std::string placeholders;
    for (int at = 0; at < 65; ++at) {
        placeholders += "{$a}";
    }
    RAWFRAME_EXPECT(refusedWith(parseMessage(placeholders), LocalizationError::OverLimit));
    RAWFRAME_EXPECT(
        refusedWith(parseMessage(".input {$a :integer} .input {$b :integer} {{x}}", {.maximumDeclarations = 1}),
                    LocalizationError::OverLimit));
    const auto kMessage = parseMessage("x");
    const std::vector<Argument> kArguments(33, Argument{"a", std::int64_t{1}});
    RAWFRAME_EXPECT(refusedWith(format(*kMessage, *parseLocale("en"), kArguments), LocalizationError::OverLimit));
}

RAWFRAME_TEST(ExactBeatsCategoryBeatsAny) {
    RAWFRAME_EXPECT(said(kFiles, "en", {{"count", std::int64_t{0}}}) == "No files");
    RAWFRAME_EXPECT(said(kFiles, "en", {{"count", std::int64_t{1}}}) == "One file");
    RAWFRAME_EXPECT(said(kFiles, "en", {{"count", std::int64_t{1234}}}) == "1,234 files");
    RAWFRAME_EXPECT(said(kFiles, "de", {{"count", std::int64_t{1234}}}) == "1.234 files");
    // English says "1 day" but "1.0 days": the category is the number as
    // written.
    constexpr std::string_view kDays = ".input {$n :number minimumFractionDigits=1}\n"
                                       ".match $n\none {{{$n} day}}\n* {{{$n} days}}";
    RAWFRAME_EXPECT(said(kDays, "en", {{"n", std::int64_t{1}}}) == "1.0 days");
    RAWFRAME_EXPECT(said(kDays, "en", {{"n", 2.5}}) == "2.5 days");
    // An exact key over a category that also matches, whatever the order.
    constexpr std::string_view kOrder = ".input {$n :integer}\n.match $n\none {{category}}\n1 {{exact}}\n* {{any}}";
    RAWFRAME_EXPECT(said(kOrder, "en", {{"n", std::int64_t{1}}}) == "exact");
    RAWFRAME_EXPECT(said(kOrder, "en", {{"n", std::int64_t{21}}}) == "any");
}

RAWFRAME_TEST(CategoriesAreTheLocalesOwn) {
    constexpr std::string_view kPolish = ".input {$n :integer}\n.match $n\none {{plik}}\nfew {{pliki}}\n"
                                         "many {{plików}}\n* {{pliku}}";
    RAWFRAME_EXPECT(said(kPolish, "pl", {{"n", std::int64_t{1}}}) == "plik");
    RAWFRAME_EXPECT(said(kPolish, "pl", {{"n", std::int64_t{3}}}) == "pliki");
    RAWFRAME_EXPECT(said(kPolish, "pl", {{"n", std::int64_t{5}}}) == "plików");
    RAWFRAME_EXPECT(said(kPolish, "pl", {{"n", std::int64_t{22}}}) == "pliki");
    RAWFRAME_EXPECT(said(kPolish, "pl", {{"n", 1.5}}) == "error 6");
    constexpr std::string_view kPlace = ".input {$n :integer select=ordinal}\n.match $n\none {{{$n}st}}\n"
                                        "two {{{$n}nd}}\nfew {{{$n}rd}}\n* {{{$n}th}}";
    RAWFRAME_EXPECT(said(kPlace, "en", {{"n", std::int64_t{1}}}) == "1st");
    RAWFRAME_EXPECT(said(kPlace, "en", {{"n", std::int64_t{22}}}) == "22nd");
    RAWFRAME_EXPECT(said(kPlace, "en", {{"n", std::int64_t{103}}}) == "103rd");
    RAWFRAME_EXPECT(said(kPlace, "en", {{"n", std::int64_t{11}}}) == "11th");
    // Exact selection reads integer keys only.
    constexpr std::string_view kExact = ".input {$n :integer select=exact}\n.match $n\n1 {{one}}\n* {{other}}";
    RAWFRAME_EXPECT(said(kExact, "en", {{"n", std::int64_t{1}}}) == "one");
}

RAWFRAME_TEST(TheFirstSelectorIsTheMostSignificant) {
    constexpr std::string_view kTwo = ".input {$who :string}\n.input {$n :integer}\n.match $who $n\n"
                                      "|host| one {{host one}}\n|host| * {{host many}}\n* one {{guest one}}\n"
                                      "* * {{anyone}}";
    RAWFRAME_EXPECT(said(kTwo, "en", {{"who", std::string{"host"}}, {"n", std::int64_t{1}}}) == "host one");
    RAWFRAME_EXPECT(said(kTwo, "en", {{"who", std::string{"host"}}, {"n", std::int64_t{2}}}) == "host many");
    RAWFRAME_EXPECT(said(kTwo, "en", {{"who", std::string{"ada"}}, {"n", std::int64_t{1}}}) == "guest one");
    RAWFRAME_EXPECT(said(kTwo, "en", {{"who", std::string{"ada"}}, {"n", std::int64_t{2}}}) == "anyone");
}

RAWFRAME_TEST(NumbersAreWrittenAsTheirFunctionsSay) {
    RAWFRAME_EXPECT(said("{$x}", "en", {{"x", 1234.5678}}) == "1,234.568");
    RAWFRAME_EXPECT(said("{$x :number maximumFractionDigits=1}", "de", {{"x", 1234.56}}) == "1.234,6");
    RAWFRAME_EXPECT(said("{$x :number minimumFractionDigits=2}", "fr", {{"x", std::int64_t{1234}}}) == "1\u202F234,00");
    RAWFRAME_EXPECT(said("{$x :integer useGrouping=never}", "en", {{"x", std::int64_t{-1234}}}) == "-1234");
    RAWFRAME_EXPECT(said("{$x :string}", "en", {{"x", true}}) == "true");
    // A local keeps the options it inherits and sets its own.
    RAWFRAME_EXPECT(said(".input {$x :number useGrouping=never}\n.local $y = {$x :number maximumFractionDigits=0}\n"
                         "{{{$x} {$y}}}",
                         "en",
                         {{"x", 1234.5}}) == "1234.5 1234");
    RAWFRAME_EXPECT(said(".local $pi = {|3.14159| :number maximumFractionDigits=2} {{{$pi}}}", "tr") == "3,14");
    RAWFRAME_EXPECT(said(".local $name = {|Ada|} {{{$name}}}", "en") == "Ada");
}

RAWFRAME_TEST(ArgumentsAreGivenAndOfTheirTypes) {
    RAWFRAME_EXPECT(said(kFiles, "en") == "error 5");
    RAWFRAME_EXPECT(said(kFiles, "en", {{"count", 1.0}}) == "error 6");
    RAWFRAME_EXPECT(said(kFiles, "en", {{"count", std::string{"1"}}}) == "error 6");
    RAWFRAME_EXPECT(said("{$x :string}", "en", {{"x", std::int64_t{1}}}) == "error 6");
    RAWFRAME_EXPECT(said("{$x}", "en", {{"x", std::numeric_limits<double>::infinity()}}) == "error 6");
    RAWFRAME_EXPECT(said("{$x}", "en", {{"x", std::nan("")}}) == "error 6");
    // The first argument of a name is the one read; others are ignored.
    RAWFRAME_EXPECT(said("{$x}", "en", {{"x", std::string{"a"}}, {"x", std::string{"b"}}, {"y", false}}) == "a");
    const auto kMessage = parseMessage(".input {$a :integer}\n.local $b = {$c :number}\n{{{$b} {$a} {$d}}}");
    RAWFRAME_EXPECT(argumentsOf(*kMessage) == (std::vector<std::string>{"a", "c", "d"}));
}

RAWFRAME_TEST(HostileMessagesAreReadOrRefusedWhole) {
    // Every prefix of a message, and the message with each byte made each
    // of the grammar's own characters, is refused or reads to a message
    // that formats given integers.
    const std::vector<Argument> kArguments{{"count", std::int64_t{2}}, {"n", std::int64_t{3}}, {"x", std::int64_t{4}}};
    const Locale kLocale = *parseLocale("pl");
    std::size_t read = 0;
    const auto kTry = [&](std::string_view text) {
        const auto kMessage = parseMessage(text);
        if (!kMessage.has_value()) {
            RAWFRAME_EXPECT(refusedWith(kMessage, LocalizationError::MessageInvalid));
            return;
        }
        ++read;
        const auto kMade = format(*kMessage, kLocale, kArguments);
        RAWFRAME_EXPECT(kMade.has_value() || refusedWith(kMade, LocalizationError::ArgumentMissing) ||
                        refusedWith(kMade, LocalizationError::ArgumentMistyped));
    };
    for (const std::string_view kSeed :
         {kFiles,
          std::string_view{".input {$x :number minimumFractionDigits=1 useGrouping=never select=ordinal}\n"
                           ".local $y = {$x :integer}\n.match $y $x\n1 one {{\\{a\\}}}\none * {{{$y}}}\n* * {{b}}"}}) {
        RAWFRAME_EXPECT(parseMessage(kSeed).has_value());
        std::string text{kSeed};
        for (std::size_t at = 0; at <= text.size(); ++at) {
            kTry(text.substr(0, at));
        }
        for (std::size_t at = 0; at < text.size(); ++at) {
            for (const char kEach : std::string_view{"{}|$:.=*\\ @#/0a-"}) {
                const char kWas = text[at];
                text[at] = kEach;
                kTry(text);
                text[at] = kWas;
            }
        }
    }
    RAWFRAME_EXPECT(read > 0);
}
