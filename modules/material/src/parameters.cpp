#include "parameters.h"

#include <algorithm>

namespace rawframe::material {

const std::array<Parameter, 10> kParameters = {{
    {"ambient_occlusion",
     1,
     0,
     1,
     1,
     [](Surface& s) {
         return &s.ambientOcclusion;
     }},
    {"base_color",
     3,
     0,
     1,
     0.8,
     [](Surface& s) {
         return s.baseColor.data();
     }},
    {"base_metalness",
     1,
     0,
     1,
     0,
     [](Surface& s) {
         return &s.baseMetalness;
     }},
    {"emission_color",
     3,
     0,
     1,
     1,
     [](Surface& s) {
         return s.emissionColor.data();
     }},
    {"emission_luminance",
     1,
     0,
     1e9,
     0,
     [](Surface& s) {
         return &s.emissionLuminance;
     }},
    {"geometry_opacity",
     1,
     0,
     1,
     1,
     [](Surface& s) {
         return &s.geometryOpacity;
     }},
    {"specular_color",
     3,
     0,
     1,
     1,
     [](Surface& s) {
         return s.specularColor.data();
     }},
    {"specular_ior",
     1,
     1,
     3,
     1.5,
     [](Surface& s) {
         return &s.specularIor;
     }},
    {"specular_roughness",
     1,
     0,
     1,
     0.3,
     [](Surface& s) {
         return &s.specularRoughness;
     }},
    {"specular_weight",
     1,
     0,
     1,
     1,
     [](Surface& s) {
         return &s.specularWeight;
     }},
}};

const std::array<Parameter, 10> kContractOrder = {kParameters[1],
                                                  kParameters[2],
                                                  kParameters[9],
                                                  kParameters[6],
                                                  kParameters[8],
                                                  kParameters[7],
                                                  kParameters[3],
                                                  kParameters[4],
                                                  kParameters[5],
                                                  kParameters[0]};

const Parameter* parameterNamed(std::string_view name) {
    const auto kFound = std::ranges::find(kParameters, name, &Parameter::name);
    return kFound == kParameters.end() ? nullptr : &*kFound;
}

} // namespace rawframe::material
