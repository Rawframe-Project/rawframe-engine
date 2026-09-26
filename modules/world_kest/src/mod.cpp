#include "rawframe/world_kest/mod.h"

#include "rawframe/content/product.h"
#include "rawframe/world_kest/errors.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <optional>

namespace rawframe::world_kest {

namespace {

std::unexpected<result::Error> badLine(std::size_t line, std::string_view why) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, kWorldKestDomain, code(WorldKestError::BadGameLine), why)
            .error()
            .withContext("line", std::to_string(line))};
}

std::vector<std::string_view> words(std::string_view line) {
    std::vector<std::string_view> found;
    std::size_t at = 0;
    while (at < line.size()) {
        const std::size_t kStart = line.find_first_not_of(" \t", at);
        if (kStart == std::string_view::npos) {
            break;
        }
        const std::size_t kEnd = std::min(line.find_first_of(" \t", kStart), line.size());
        found.push_back(line.substr(kStart, kEnd - kStart));
        at = kEnd;
    }
    return found;
}

/// One bound as written: a relation, then a version from 1.
std::optional<ModApiBound> boundOf(std::string_view text) {
    using Relation = ModApiBound::Relation;
    constexpr std::pair<std::string_view, Relation> kRelations[] = {{">=", Relation::AtLeast},
                                                                    {"<=", Relation::AtMost},
                                                                    {"=", Relation::Exactly},
                                                                    {"<", Relation::Below},
                                                                    {">", Relation::Above}};
    ModApiBound bound;
    for (const auto& [kSpelling, kRelation] : kRelations) {
        if (text.starts_with(kSpelling)) {
            bound.relation = kRelation;
            text.remove_prefix(kSpelling.size());
            break;
        }
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), bound.version);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size() || bound.version == 0 ||
        bound.version > 0x7FFF'FFFFU) {
        return std::nullopt;
    }
    return bound;
}

} // namespace

bool accepts(const std::vector<ModApiBound>& range, std::uint32_t version) noexcept {
    using Relation = ModApiBound::Relation;
    return std::ranges::all_of(range, [version](const ModApiBound& bound) {
        switch (bound.relation) {
        case Relation::AtLeast:
            return version >= bound.version;
        case Relation::AtMost:
            return version <= bound.version;
        case Relation::Exactly:
            return version == bound.version;
        case Relation::Below:
            return version < bound.version;
        case Relation::Above:
            return version > bound.version;
        }
        return false;
    });
}

result::Result<ModDescription> parseMod(std::string_view text) {
    ModDescription mod;
    std::size_t number = 0;
    std::size_t targetLine = 0;
    std::size_t modApiLine = 0;
    std::size_t programLine = 0;
    if (text.size() > kMaximumModDescriptorBytes) {
        return badLine(1, "a mod description is past its size limit");
    }
    while (!text.empty()) {
        const std::size_t kEnd = text.find('\n');
        std::string_view line = text.substr(0, kEnd);
        text = kEnd == std::string_view::npos ? std::string_view{} : text.substr(kEnd + 1);
        if (++number > kMaximumModLines) {
            return badLine(number, "a mod description has too many lines");
        }
        if (const std::size_t kComment = line.find('#'); kComment != std::string_view::npos) {
            line = line.substr(0, kComment);
        }
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        const std::vector<std::string_view> kWords = words(line);
        if (kWords.empty()) {
            continue;
        }
        if (kWords[0] == "target") {
            if (targetLine != 0 || kWords.size() != 2 || !content::validSubject(kWords[1])) {
                return badLine(number, "a mod targets one game, `target <publisher/name>`");
            }
            mod.target = kWords[1];
            targetLine = number;
        } else if (kWords[0] == "modapi") {
            if (modApiLine != 0 || kWords.size() < 2 || kWords.size() > 3) {
                return badLine(number, "a mod names one Mod API range, `modapi <constraint>...`, at most two");
            }
            for (std::size_t index = 1; index < kWords.size(); ++index) {
                const auto kBound = boundOf(kWords[index]);
                if (!kBound) {
                    return badLine(number, "a Mod API bound is a relation and a version from 1");
                }
                mod.modApi.push_back(*kBound);
            }
            // A range nothing satisfies is a mistake, not a mod for no game.
            bool any = false;
            for (const ModApiBound& bound : mod.modApi) {
                for (const std::uint32_t kTry : {bound.version - 1, bound.version, bound.version + 1}) {
                    any = any || (kTry != 0 && accepts(mod.modApi, kTry));
                }
            }
            if (!any) {
                return badLine(number, "no Mod API version satisfies the range");
            }
            modApiLine = number;
        } else if (kWords[0] == "contribute") {
            if (kWords.size() != 3 || std::ranges::any_of(mod.contributions, [&kWords](const ModContribution& each) {
                    return each.point == kWords[1] && each.scene == kWords[2];
                })) {
                return badLine(number, "a mod contributes each scene to a point once, `contribute <point> <scene>`");
            }
            mod.contributions.push_back(
                ModContribution{.point = std::string{kWords[1]}, .scene = std::string{kWords[2]}});
        } else if (kWords[0] == "program") {
            if (programLine != 0 || kWords.size() != 2) {
                return badLine(number, "a mod names one program, `program <file>`");
            }
            mod.program = kWords[1];
            programLine = number;
        } else if (kWords[0] == "handle") {
            if (kWords.size() != 3 || std::ranges::any_of(mod.handlers, [&kWords](const ModHandler& each) {
                    return each.point == kWords[1] && each.function == kWords[2];
                })) {
                return badLine(number, "a mod handles an event with each function once, `handle <point> <function>`");
            }
            mod.handlers.push_back(ModHandler{.point = std::string{kWords[1]}, .function = std::string{kWords[2]}});
        } else {
            return badLine(number, "a mod description line is target, modapi, contribute, program, or handle");
        }
        if (mod.contributions.size() + mod.handlers.size() > kMaximumModContributions) {
            return badLine(number, "a mod contributes and handles more than the limit");
        }
    }
    if (targetLine == 0 || modApiLine == 0) {
        return badLine(number, "a mod names its target and its Mod API range");
    }
    if (mod.handlers.empty() != mod.program.empty()) {
        return badLine(programLine != 0 ? programLine : number,
                       "a mod with handlers names their program, and only then");
    }
    return mod;
}

} // namespace rawframe::world_kest
