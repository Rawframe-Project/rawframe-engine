# The iOS backend: RemoteIO units in C, the
# audio session in Objective-C with manual retain and release.
# AudioToolbox for the units, AVFAudio and Foundation for the session.

enable_language(OBJC)
target_sources(maul-audio PRIVATE src/apple_objects.c src/backend_ios.c src/ios_session.m
    src/ios_stream.c)
target_compile_definitions(maul-audio PRIVATE MAUD_HAVE_IOS=1)
set_target_properties(maul-audio PROPERTIES
    OBJC_STANDARD 23
    OBJC_STANDARD_REQUIRED ON
    OBJC_EXTENSIONS OFF)
target_compile_options(maul-audio PRIVATE
    $<$<COMPILE_LANGUAGE:OBJC>:-fno-objc-arc -Wmissing-prototypes>)
target_link_libraries(maul-audio PRIVATE "-framework AudioToolbox" "-framework AVFAudio"
    "-framework Foundation")
