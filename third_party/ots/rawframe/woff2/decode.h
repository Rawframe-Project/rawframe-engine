// The WOFF 2.0 decoder OTS calls, refusing (D385): ADR-0049 rejects WOFF 2.0,
// so such a font decodes to nothing and fails.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace woff2 {

class WOFF2Out {
public:
    virtual ~WOFF2Out() = default;
};

class WOFF2StringOut : public WOFF2Out {
public:
    explicit WOFF2StringOut(std::string* /*buffer*/) {
    }
    void SetMaxSize(std::size_t /*size*/) {
    }
    [[nodiscard]] std::size_t Size() const {
        return 0;
    }
};

inline std::size_t ComputeWOFF2FinalSize(const std::uint8_t* /*data*/, std::size_t /*length*/) {
    return 0;
}

inline bool ConvertWOFF2ToTTF(const std::uint8_t* /*data*/, std::size_t /*length*/, WOFF2Out* /*out*/) {
    return false;
}

} // namespace woff2
