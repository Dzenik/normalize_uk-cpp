#include "uktextnorm/uktextnorm.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    if (size > 64 * 1024) {
        return 0;
    }
    const auto input = std::string_view(reinterpret_cast<const char*>(data), size);
    for (const auto preset : {uktextnorm::NormalizePreset::Default,
                              uktextnorm::NormalizePreset::TtsFriendly,
                              uktextnorm::NormalizePreset::Conservative,
                              uktextnorm::NormalizePreset::SearchIndexing}) {
        (void)uktextnorm::normalize_ukrainian(input, preset);
    }
    (void)uktextnorm::flag_uncertain(input);
    return 0;
}
