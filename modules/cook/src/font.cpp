#include "rawframe/cook/font.h"

#include "rawframe/cook/errors.h"
#include "rawframe/font_import/sanitize.h"
#include "rawframe/ui/font.h"
#include "rawframe/ui/tree.h"

namespace rawframe::cook {

namespace {

std::unexpected<result::Error> refuse(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kCookDomain, code(CookError::BadSidecar), why);
}

/// Takes no settings.
result::Result<std::string> normalize(const document::Value* settings) {
    if (settings != nullptr) {
        return refuse("a font takes no settings");
    }
    return std::string{};
}

result::Result<Artifact> cookFont(std::span<const std::byte> source, std::string_view, Reads&) {
    RAWFRAME_TRY_ASSIGN(std::vector<std::byte> rebuilt, font_import::sanitize(source));
    // What the runtime reads it with must take it: FreeType and HarfBuzz
    // behind a tree, its first face.
    RAWFRAME_TRY_ASSIGN(const std::unique_ptr<ui::Tree> kTree, ui::Tree::create(1, 1));
    RAWFRAME_TRY(kTree->addFont(rebuilt));
    return Artifact{.type = content::ResourceTypeId{ui::kFontType},
                    .representation = *content::RepresentationId::parse(ui::kFontRepresentation),
                    .bytes = std::move(rebuilt)};
}

} // namespace

Importer fontImporter() noexcept {
    return Importer{.identity = "rawframe.font", .normalize = &normalize, .cook = &cookFont};
}

} // namespace rawframe::cook
