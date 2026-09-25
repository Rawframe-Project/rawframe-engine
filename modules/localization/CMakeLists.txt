# Localization (ADR-0050, SPEC-0033): locales and their fallback, and plural
# categories, from CLDR data compiled ahead of time. Presentation only: a
# dedicated server never links it, holding keys and arguments as plain data.
rawframe_module(NAME localization
    SOURCES src/generated/locales.cpp src/generated/plurals.cpp src/locale.cpp src/plural.cpp
    DEPS result base
)

rawframe_module_tests(NAME localization SOURCES tests/generated/plural_samples.cpp tests/locale_test.cpp tests/plural_test.cpp)
