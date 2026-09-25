# Localization (ADR-0050, SPEC-0033): locales and their fallback, plural
# categories, numbers, MessageFormat 2.0 messages, and the string table
# documents with their catalogs, from CLDR data compiled ahead of
# time. Presentation only: a
# dedicated server never links it, holding keys and arguments as plain data.
set(rawframe_localization_development "")
set(rawframe_localization_development_tests "")
if(NOT RAWFRAME_CONFIGURATION STREQUAL "shipping")
    # Pseudo-localization never ships (SPEC-0033).
    set(rawframe_localization_development src/pseudo.cpp)
    set(rawframe_localization_development_tests tests/pseudo_test.cpp)
endif()

rawframe_module(NAME localization
    SOURCES ${rawframe_localization_development} src/catalog.cpp src/generated/locales.cpp src/generated/numbers.cpp
            src/generated/plurals.cpp src/locale.cpp src/message_format.cpp src/message_parse.cpp src/number.cpp
            src/plural.cpp src/table.cpp
    DEPS document result base
)

rawframe_module_tests(NAME localization
    SOURCES ${rawframe_localization_development_tests} tests/catalog_test.cpp tests/generated/plural_samples.cpp
            tests/locale_test.cpp tests/message_test.cpp tests/number_test.cpp tests/plural_test.cpp tests/table_test.cpp
)
