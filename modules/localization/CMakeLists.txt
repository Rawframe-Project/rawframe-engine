# Localization (ADR-0050, SPEC-0033): locales and their fallback, from CLDR
# data compiled ahead of time. Presentation only: a dedicated server never
# links it, holding keys and arguments as plain data.
rawframe_module(NAME localization
    SOURCES src/generated/locales.cpp src/locale.cpp
    DEPS result base
)

rawframe_module_tests(NAME localization SOURCES tests/locale_test.cpp)
