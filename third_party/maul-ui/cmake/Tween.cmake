# Maul Tween, the family's easing curves and springs: fetched at its
# release tag and built into maul-ui, the core included, as Maul Unicode
# is for the text component.
#
# Included before the family modules: Maul Tween brings its own copies
# of them, and ours, included after, then serve this project.
# FETCHCONTENT_SOURCE_DIR_MAUL-TWEEN builds from a local copy instead,
# offline.

include(FetchContent)

set(MAUL_UI_TWEEN_VERSION 0.12.0)

set(MAUL_TWEEN_BUILD_SHARED OFF)
set(MAUL_TWEEN_INSTALL OFF)
if(MAUL_UI_BUILD_SHARED)
    # Its static library goes into ours.
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()
FetchContent_Declare(maul-tween
    GIT_REPOSITORY https://github.com/Rawframe-Project/maul-tween.git
    GIT_TAG v${MAUL_UI_TWEEN_VERSION}
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(maul-tween)
