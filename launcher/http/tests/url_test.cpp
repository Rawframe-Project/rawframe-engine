// URLs as the launcher's client reads them (D414).

#include "rawframe/http/url.h"
#include "rawframe/test/test.h"

#include <string>

using namespace rawframe;

RAWFRAME_TEST(AUrlGivesItsSchemeHostPortAndPath) {
    const auto kPlain = http::parseUrl("http://Mirror.Example.com/games/runners?x=1");
    RAWFRAME_EXPECT(kPlain.has_value() && !kPlain->secure && kPlain->host == "mirror.example.com" &&
                    kPlain->port == 80 && kPlain->target == "/games/runners?x=1");
    const auto kSecure = http::parseUrl("https://127.0.0.1:8443");
    RAWFRAME_EXPECT(kSecure.has_value() && kSecure->secure && kSecure->host == "127.0.0.1" && kSecure->port == 8443 &&
                    kSecure->target == "/" && kSecure->authority() == "127.0.0.1:8443");
    const auto kSix = http::parseUrl("https://[::1]:9/a%20b");
    RAWFRAME_EXPECT(kSix.has_value() && kSix->host == "::1" && kSix->port == 9 && kSix->authority() == "[::1]:9" &&
                    kSix->text() == "https://[::1]:9/a%20b");
    // The scheme's own port is left out of the Host header.
    RAWFRAME_EXPECT(http::parseUrl("https://a.example:443/")->authority() == "a.example");
}

RAWFRAME_TEST(AUrlOutOfFormIsRefused) {
    for (const char* kBad : {"ftp://a.example/",
                             "https://user:secret@a.example/",
                             "https://a.example/#part",
                             "https://a.example:0/",
                             "https://a.example:65536/",
                             "https://a.example:/",
                             "https://a.example:8x/",
                             "https:///path",
                             "https://a_b.example/",
                             "https://[::1/",
                             "https://[::1]x/",
                             "https://a.example/a b",
                             "https://a.example/%zz",
                             "https://a.example/%2"}) {
        RAWFRAME_EXPECT(!http::parseUrl(kBad).has_value());
    }
    RAWFRAME_EXPECT(!http::parseUrl("https://a.example/" + std::string(9000, 'a')).has_value());
}

RAWFRAME_TEST(ARedirectResolvesAgainstWhereItCameFrom) {
    const http::Url kBase = *http::parseUrl("https://a.example:8443/one/two");
    const auto kPath = http::resolveUrl(kBase, "/three");
    RAWFRAME_EXPECT(kPath.has_value() && kPath->text() == "https://a.example:8443/three");
    const auto kElsewhere = http::resolveUrl(kBase, "http://b.example/four");
    RAWFRAME_EXPECT(kElsewhere.has_value() && kElsewhere->host == "b.example" && !kElsewhere->secure);
    // Relative paths and scheme-relative references are not followed.
    RAWFRAME_EXPECT(!http::resolveUrl(kBase, "three").has_value());
    RAWFRAME_EXPECT(!http::resolveUrl(kBase, "//b.example/").has_value());
}
