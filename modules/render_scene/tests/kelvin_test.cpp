// `model.kelvin`, ADR-0051's color-temperature input: 6,500 K is white,
// lower is warmer and higher bluer along the black body's colors, the
// brightest channel full, and the ends held; `model.channel` encodes sRGB.

#include "rawframe/kest/machine.h"
#include "rawframe/test/test.h"
#include "rawframe/world_kest/game_files.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace rawframe;

namespace {

/// A machine running a program that asks the library's two functions.
std::unique_ptr<kest::Machine> machine() {
    std::vector<std::pair<std::string, std::string>> held;
    held.emplace_back("lit.kest",
                      "module lit\n\nimport rawframe.model\n\n"
                      "fn warm(temperature: f32) -> u32 {\n    return model.kelvin(temperature)\n}\n\n"
                      "fn byte(linear: f32) -> u32 {\n    return model.channel(linear)\n}\n");
    held.emplace_back("lit.game", "program lit.kest\n");
    const auto kFiles = world_kest::GameFiles::fromHeld("lit.game", std::move(held));
    RAWFRAME_EXPECT(kFiles.has_value());
    std::string report;
    const auto kProgram = kFiles->compile("lit.kest", {}, &report);
    if (!kProgram.has_value()) {
        std::fprintf(stderr, "%s\n", report.c_str());
        return nullptr;
    }
    auto made = kest::Machine::start(
        *kProgram, kest::DoorTable{}, kest::Trust::Untrusted, {.heapBytes = 4096, .fuelPerCall = 100000});
    RAWFRAME_EXPECT(made.has_value());
    return made.has_value() ? std::move(*made) : nullptr;
}

std::uint32_t asked(kest::Machine& machine, const char* name, double argument) {
    const auto kEntry = machine.entry(name);
    RAWFRAME_EXPECT(kEntry.has_value());
    std::array<kest::Value, 1> frame{};
    // An f32 argument must be one.
    frame[0].real = static_cast<float>(argument);
    RAWFRAME_EXPECT(kEntry.has_value() && machine.call(*kEntry, frame).hasValue());
    return static_cast<std::uint32_t>(frame[0].integer);
}

} // namespace

RAWFRAME_TEST(KelvinGivesABlackBodysColor) {
    const std::unique_ptr<kest::Machine> kMachine = machine();
    RAWFRAME_EXPECT(kMachine != nullptr);
    if (kMachine == nullptr) {
        return;
    }
    const auto kAt = [&kMachine](double temperature) {
        return asked(*kMachine, "warm", temperature);
    };
    std::printf("1000 %08X, 2700 %08X, 4000 %08X, 6500 %08X, 10000 %08X, 30000 %08X\n",
                kAt(1000),
                kAt(2700),
                kAt(4000),
                kAt(6500),
                kAt(10000),
                kAt(30000));
    RAWFRAME_EXPECT(kAt(6500) == 0xFFFFFFFFU);
    // A household bulb is orange-white, its red full, blue lowest.
    const std::uint32_t kBulb = kAt(2700);
    RAWFRAME_EXPECT((kBulb >> 24U) == 0xFF && ((kBulb >> 16U) & 0xFFU) > 0xA0 && ((kBulb >> 16U) & 0xFFU) < 0xC0 &&
                    ((kBulb >> 8U) & 0xFFU) > 0x40 && ((kBulb >> 8U) & 0xFFU) < 0x70 && (kBulb & 0xFFU) == 0xFF);
    // A clear north sky is blue, its blue full, red lowest.
    const std::uint32_t kSky = kAt(10000);
    RAWFRAME_EXPECT(((kSky >> 8U) & 0xFFU) == 0xFF && (kSky >> 24U) < ((kSky >> 16U) & 0xFFU));
    // Warmer is redder: green falls as the temperature does.
    std::uint32_t before = 0x100;
    for (const double kTemperature : {6000.0, 5000.0, 4000.0, 3000.0, 2000.0}) {
        const std::uint32_t kGreen = (kAt(kTemperature) >> 16U) & 0xFFU;
        RAWFRAME_EXPECT(kGreen < before);
        before = kGreen;
    }
    // The ends are held.
    RAWFRAME_EXPECT(kAt(100) == kAt(1667) && kAt(1.0e6) == kAt(25000));
    // sRGB's curve: its linear toe, its middle, and its ends.
    for (const auto& [kLinear, kByte] : std::array{std::pair{-1.0, 0U},
                                                   std::pair{0.002, 7U},
                                                   std::pair{0.01, 25U},
                                                   std::pair{0.216, 128U},
                                                   std::pair{0.5, 188U},
                                                   std::pair{1.0, 255U},
                                                   std::pair{3.0, 255U}}) {
        RAWFRAME_EXPECT(asked(*kMachine, "byte", kLinear) == kByte);
    }
}
