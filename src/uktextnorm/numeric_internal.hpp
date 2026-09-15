#pragma once

#include "internal.hpp"

namespace uktextnorm::detail {

enum class TemperatureScaleKind {
    Celsius,
    Fahrenheit,
    Kelvin,
    Rankine,
    Reaumur,
    Delisle,
    Newton,
    Romer
};

struct TemperatureScale {
    TemperatureScaleKind kind;
    std::string_view genitive_name;
    std::array<std::string_view, 3> unit_forms;
    std::string_view decimal_unit;
};

struct RangeCurrency {
    std::string_view many;
    char gender = 'm';
};

const std::string& signed_number_pattern();
const std::string& range_separator_pattern();
const std::string& range_prefix_pattern();
std::string preceding_word(std::string_view prefix);
std::string take_spoken_sign(std::string_view& token);
std::vector<std::string>
number_words_for_case(unsigned long long value, std::string_view grammatical_case, char gender);
std::optional<std::string>
signed_number_words(std::string_view token, std::string_view grammatical_case, char gender = 'm');
std::string compact_lower(std::string_view text);
std::optional<TemperatureScale> temperature_scale(std::string_view scale);
const std::string& temperature_unit_pattern();
std::optional<std::string> temperature_quantity_words(std::string_view token,
                                                      const TemperatureScale& scale,
                                                      std::string_view grammatical_case = "nom");
std::string temperature_range_unit(std::string_view upper, const TemperatureScale& scale);
std::optional<RangeCurrency> range_currency(std::string_view token);
std::string
range_connector(RangeStyle style, const std::string& low, const std::string& high, bool explicitly_from_to = false);
std::string clock_time_words(std::string_view hour_text,
                             std::string_view minute_text,
                             const std::ssub_match& second,
                             RangeStyle style);

} // namespace uktextnorm::detail
