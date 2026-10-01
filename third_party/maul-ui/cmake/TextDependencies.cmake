# The text component's dependencies (record mui-0006): FreeType and
# HarfBuzz at pinned releases, fetched by hash and built here in reduced
# configurations, or the installed ones with
# MAUL_UI_TEXT_SYSTEM_LIBRARIES; and Maul Unicode at its tag, its
# HarfBuzz functions built against the HarfBuzz chosen here.
#
# Included before the family modules: Maul Unicode brings its own
# copies of them, and ours, included after, then serve this project.
# FETCHCONTENT_SOURCE_DIR_MAUL_UI_FREETYPE, ..._MAUL_UI_HARFBUZZ and
# ..._MAUL-UNICODE build from local copies instead, offline.

include(FetchContent)

set(MAUL_UI_FREETYPE_VERSION 2.14.3)
set(MAUL_UI_HARFBUZZ_VERSION 14.5.1)
set(MAUL_UI_UNICODE_VERSION 0.2.0)

if(MAUL_UI_TEXT_SYSTEM_LIBRARIES)
    find_package(Freetype REQUIRED)
    find_package(harfbuzz CONFIG QUIET)
    if(NOT TARGET harfbuzz::harfbuzz)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(MUI_HARFBUZZ REQUIRED IMPORTED_TARGET GLOBAL harfbuzz)
        add_library(harfbuzz::harfbuzz ALIAS PkgConfig::MUI_HARFBUZZ)
    endif()
    add_library(maul_ui_freetype INTERFACE)
    target_link_libraries(maul_ui_freetype INTERFACE Freetype::Freetype)
else()
    # HarfBuzz is C++; nothing of Maul UI's is.
    enable_language(CXX)
    set(ft_version ${MAUL_UI_FREETYPE_VERSION})
    set(hb_version ${MAUL_UI_HARFBUZZ_VERSION})
    # Both are only populated (their SOURCE_SUBDIR has no CMakeLists.txt):
    # they are built below, not by their own build.
    FetchContent_Declare(maul_ui_freetype
        URL https://downloads.sourceforge.net/project/freetype/freetype2/${ft_version}/freetype-${ft_version}.tar.xz
            https://download.savannah.gnu.org/releases/freetype/freetype-${ft_version}.tar.xz
        URL_HASH SHA256=36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR maul-ui-builds-it)
    FetchContent_Declare(maul_ui_harfbuzz
        URL https://github.com/harfbuzz/harfbuzz/releases/download/${hb_version}/harfbuzz-${hb_version}.tar.xz
        URL_HASH SHA256=7e2fa4e8c7c98e8d8140671f5772542afaaa6acccfbd746506886b6d85f7f8d6
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR maul-ui-builds-it)
    FetchContent_MakeAvailable(maul_ui_freetype maul_ui_harfbuzz)

    # FreeType: the modules maul-ui-ftmodule.h lists, with the options of
    # maul-ui-ftoption.h, both named by cmake/text/ft2build.h, which is
    # found before FreeType's own by FreeType and the text component.
    set(ft ${maul_ui_freetype_SOURCE_DIR})
    add_library(maul_ui_freetype STATIC
        ${ft}/src/base/ftbase.c
        ${ft}/src/base/ftbbox.c
        ${ft}/src/base/ftbitmap.c
        ${ft}/src/base/ftdebug.c
        ${ft}/src/base/ftglyph.c
        ${ft}/src/base/ftinit.c
        ${ft}/src/base/ftmm.c
        ${ft}/src/base/ftsynth.c
        ${ft}/src/base/ftsystem.c
        ${ft}/src/cff/cff.c
        ${ft}/src/psaux/psaux.c
        ${ft}/src/pshinter/pshinter.c
        ${ft}/src/psnames/psnames.c
        ${ft}/src/sfnt/sfnt.c
        ${ft}/src/smooth/smooth.c
        ${ft}/src/truetype/truetype.c)
    target_include_directories(maul_ui_freetype SYSTEM
        PUBLIC ${PROJECT_SOURCE_DIR}/cmake/text ${ft}/include)
    target_compile_definitions(maul_ui_freetype PRIVATE FT2_BUILD_LIBRARY)

    # HarfBuzz: its one-unit build, lean with variable fonts, without its
    # Unicode tables or default Unicode functions (Maul Unicode supplies
    # them to every buffer), exceptions or RTTI.
    set(hb ${maul_ui_harfbuzz_SOURCE_DIR})
    add_library(maul_ui_harfbuzz STATIC ${hb}/src/harfbuzz.cc)
    target_include_directories(maul_ui_harfbuzz SYSTEM PUBLIC ${hb}/src)
    target_include_directories(maul_ui_harfbuzz PRIVATE ${PROJECT_SOURCE_DIR}/cmake/text/harfbuzz)
    target_compile_definitions(maul_ui_harfbuzz PRIVATE HB_LEAN HB_MINI HB_NO_UCD HB_NO_UNICODE_FUNCS HAVE_CONFIG_OVERRIDE_H)
    if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "WASI")
        target_compile_definitions(maul_ui_harfbuzz PRIVATE HB_NO_MT)
    elseif(NOT WIN32)
        target_compile_definitions(maul_ui_harfbuzz PRIVATE HAVE_PTHREAD)
    endif()
    set_target_properties(maul_ui_harfbuzz PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)
    if(MSVC)
        target_compile_options(maul_ui_harfbuzz PRIVATE /GR- /EHs-c-)
        target_compile_definitions(maul_ui_harfbuzz PRIVATE _HAS_EXCEPTIONS=0)
    else()
        target_compile_options(maul_ui_harfbuzz PRIVATE -fno-exceptions -fno-rtti -fno-threadsafe-statics)
    endif()
    # What Maul Unicode's HarfBuzz functions compile against: the
    # headers alone. Linking the C++ library would make that C library
    # link as C++, and Visual Studio projects then drop its C standard;
    # the objects go into maul-ui (cmake/Text.cmake).
    add_library(maul_ui_harfbuzz_headers INTERFACE)
    target_include_directories(maul_ui_harfbuzz_headers SYSTEM INTERFACE ${hb}/src)
    add_library(harfbuzz::harfbuzz ALIAS maul_ui_harfbuzz_headers)
    # Built only as objects for maul-ui (cmake/Text.cmake).
    set_target_properties(maul_ui_freetype maul_ui_harfbuzz PROPERTIES EXCLUDE_FROM_ALL ON)

    # Not Maul UI's code: their warnings are theirs, their symbols stay
    # inside a shared build, and they match its code model.
    foreach(dependency maul_ui_freetype maul_ui_harfbuzz)
        set_target_properties(${dependency} PROPERTIES
            C_VISIBILITY_PRESET hidden
            POSITION_INDEPENDENT_CODE ${MAUL_UI_BUILD_SHARED})
        if(MSVC)
            target_compile_options(${dependency} PRIVATE /w)
        else()
            target_compile_options(${dependency} PRIVATE -w)
        endif()
    endforeach()
endif()

# Maul Unicode, with its HarfBuzz functions built against the HarfBuzz
# above rather than one it would look for.
set(CMAKE_DISABLE_FIND_PACKAGE_harfbuzz ON)
set(MAUL_UNICODE_HARFBUZZ ON)
set(MAUL_UNICODE_NORMALIZATION ON)
set(MAUL_UNICODE_SECURITY OFF)
set(MAUL_UNICODE_INSTALL OFF)
if(MAUL_UI_BUILD_SHARED)
    # Its static libraries go into ours.
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()
FetchContent_Declare(maul-unicode
    GIT_REPOSITORY https://github.com/Rawframe-Project/maul-unicode.git
    GIT_TAG v${MAUL_UI_UNICODE_VERSION}
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(maul-unicode)
if(NOT TARGET maul-unicode::maul-unicode-harfbuzz)
    message(FATAL_ERROR "The text component needs Maul Unicode configured with MAUL_UNICODE_HARFBUZZ ON")
endif()
