# Writes a C++ source holding files as bytes, run at build time:
#
#   cmake -DOUTPUT=<file.cpp> -DROOTS=<root>=<prefix>|... -P embed.cmake
#
# Every `.kest` file under each root is written under its prefix, in path
# order, so the table is the same wherever and whenever it is made.
string(REPLACE "|" ";" ROOTS "${ROOTS}")
set(entries "")
set(arrays "")
set(index 0)
foreach(pair IN LISTS ROOTS)
    string(FIND "${pair}" "=" split)
    string(SUBSTRING "${pair}" 0 ${split} root)
    math(EXPR after "${split} + 1")
    string(SUBSTRING "${pair}" ${after} -1 prefix)
    file(GLOB_RECURSE files RELATIVE "${root}" "${root}/*.kest")
    list(SORT files)
    foreach(file IN LISTS files)
        file(READ "${root}/${file}" bytes HEX)
        string(LENGTH "${bytes}" digits)
        math(EXPR length "${digits} / 2")
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "'\\\\x\\1'," bytes "${bytes}")
        string(APPEND arrays "constexpr char kFile${index}[] = {${bytes}};\n")
        string(APPEND entries "    {\"${prefix}${file}\", {kFile${index}, ${length}}},\n")
        math(EXPR index "${index} + 1")
    endforeach()
endforeach()
file(WRITE "${OUTPUT}.part" "// Made by embed.cmake from the files it names; never edited.\n\n#include \"embedded.h\"\n\nnamespace rawframe::kest_library {\n\nnamespace {\n\n${arrays}\n} // namespace\n\nconst std::array<EmbeddedFile, ${index}> kEmbedded = {{\n${entries}}};\n\nstd::span<const EmbeddedFile> embedded() noexcept {\n    return kEmbedded;\n}\n\n} // namespace rawframe::kest_library\n")
file(COPY_FILE "${OUTPUT}.part" "${OUTPUT}" ONLY_IF_DIFFERENT)
