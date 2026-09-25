#include "rawframe/test/files.h"

#include <algorithm>
#include <cstdio>

#if defined(__wasi__)
#include <dirent.h>
#else
#include <filesystem>
#endif

namespace rawframe::test {

std::string readFile(const std::string& path) {
    std::string text;
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return text;
    }
    char buffer[4096];
    for (std::size_t got = 0; (got = std::fread(buffer, 1, sizeof(buffer), file)) != 0;) {
        text.append(buffer, got);
    }
    std::fclose(file);
    return text;
}

namespace {

#if defined(__wasi__)
void collect(const std::string& root,
             const std::string& relative,
             std::string_view suffix,
             std::vector<std::string>& into) {
    DIR* directory = ::opendir((root + relative).c_str());
    if (directory == nullptr) {
        return;
    }
    while (const dirent* entry = ::readdir(directory)) {
        const std::string kName = entry->d_name;
        if (kName == "." || kName == "..") {
            continue;
        }
        if (entry->d_type == DT_DIR) {
            collect(root, relative + kName + "/", suffix, into);
        } else if (entry->d_type == DT_REG && kName.ends_with(suffix)) {
            into.push_back(relative + kName);
        }
    }
    ::closedir(directory);
}
#endif

} // namespace

std::vector<std::string> filesUnder(const std::string& directory, std::string_view suffix) {
    std::vector<std::string> made;
#if defined(__wasi__)
    collect(directory.ends_with('/') ? directory : directory + "/", "", suffix, made);
#else
    std::error_code error;
    for (auto entry = std::filesystem::recursive_directory_iterator{directory, error};
         !error && entry != std::filesystem::recursive_directory_iterator{};
         entry.increment(error)) {
        if (entry->is_regular_file() && entry->path().filename().string().ends_with(suffix)) {
            made.push_back(entry->path().lexically_relative(directory).generic_string());
        }
    }
#endif
    std::ranges::sort(made);
    return made;
}

} // namespace rawframe::test
