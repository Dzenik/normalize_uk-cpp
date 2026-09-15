#include "uktextnorm/uktextnorm.hpp"

#include "generated/uktextnorm_lexicons.hpp"
#include "numeric_internal.hpp"

namespace uktextnorm::detail {

const std::string& signed_number_pattern()
{
    static const std::string pattern = R"((?:\+|-|−|–|—)?(?:\d+(?:[.,]\d+)?|[.,]\d+))";
    return pattern;
}

const std::string& range_separator_pattern()
{
    static const std::string pattern = R"((?:-|−|‐|‑|‒|–|—|…))";
    return pattern;
}

const std::string& range_prefix_pattern()
{
    static const std::string pattern = R"((^|[\s(\[{:;,.!?=]|(?:[-–—]\s+)))";
    return pattern;
}

std::string preceding_word(std::string_view prefix)
{
    auto text = lower_text(prefix);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.pop_back();
    }
    const auto boundary = text.find_last_of(" \t\n\r([{,;:");
    return text.substr(boundary == std::string::npos ? 0 : boundary + 1);
}

std::string take_spoken_sign(std::string_view& token)
{
    if (token.starts_with("-")) {
        token.remove_prefix(1);
        return "мінус ";
    }
    if (token.starts_with("−")) {
        token.remove_prefix(std::string_view("−").size());
        return "мінус ";
    }
    if (token.starts_with("–")) {
        token.remove_prefix(std::string_view("–").size());
        return "мінус ";
    }
    if (token.starts_with("—")) {
        token.remove_prefix(std::string_view("—").size());
        return "мінус ";
    }
    if (token.starts_with("+")) {
        token.remove_prefix(1);
        return "плюс ";
    }
    return {};
}

std::vector<std::string> number_words_for_case(unsigned long long value, std::string_view grammatical_case, char gender)
{
    auto words = split_words(number_to_words(value));
    if (gender == 'f') {
        feminine_last(words);
    } else if (gender == 'n') {
        neuter_last(words);
    }
    if (grammatical_case == "gen") {
        for (auto& word : words) {
            if (const auto it = case_forms().find(word); it != case_forms().end()) {
                word = it->second[0];
            }
        }
    }
    return words;
}

std::optional<std::string>
signed_number_words(std::string_view token, std::string_view grammatical_case, char gender)
{
    const auto sign = take_spoken_sign(token);
    const auto pos = token.find_first_of(".,");
    if (pos == std::string_view::npos) {
        const auto value = try_parse_ull(token);
        if (!value) {
            return std::nullopt;
        }
        return sign + join(number_words_for_case(*value, grammatical_case, gender));
    }

    const auto integer =
        token.substr(0, pos).empty() ? std::optional<unsigned long long>{0} : try_parse_ull(token.substr(0, pos));
    const auto fraction = try_parse_ull(token.substr(pos + 1));
    static const std::unordered_map<std::size_t, std::array<std::string_view, 2>> places = {
        {1, {"десятої", "десятих"}},
        {2, {"сотої", "сотих"}},
        {3, {"тисячної", "тисячних"}},
        {4, {"десятитисячної", "десятитисячних"}},
        {5, {"стотисячної", "стотисячних"}},
        {6, {"мільйонної", "мільйонних"}}};
    const auto place = places.find(token.size() - pos - 1);
    if (!integer || !fraction || place == places.end()) {
        return std::nullopt;
    }
    if (grammatical_case != "gen") {
        const auto words = decimal_to_words(token.substr(0, pos), token.substr(pos + 1));
        return words.empty() ? std::nullopt : std::optional<std::string>(sign + words);
    }

    const auto integer_words = join(number_words_for_case(*integer, "gen", 'f'));
    const auto fraction_words = join(number_words_for_case(*fraction, "gen", 'f'));
    const auto whole = *integer % 10 == 1 && *integer % 100 != 11 ? " цілої і " : " цілих і ";
    const bool singular_fraction = *fraction % 10 == 1 && *fraction % 100 != 11;
    return sign + integer_words + whole + fraction_words + " " + std::string(place->second[singular_fraction ? 0 : 1]);
}

std::string compact_lower(std::string_view text)
{
    auto lowered = lower_text(text);
    std::erase_if(lowered, [](unsigned char ch) { return std::isspace(ch); });
    return lowered;
}

std::optional<TemperatureScale> temperature_scale(std::string_view scale)
{
    static constexpr TemperatureScale celsius{
        TemperatureScaleKind::Celsius, "Цельсія", {"градус", "градуси", "градусів"}, "градуса"};
    static constexpr TemperatureScale fahrenheit{
        TemperatureScaleKind::Fahrenheit, "Фаренгейта", {"градус", "градуси", "градусів"}, "градуса"};
    static constexpr TemperatureScale kelvin{
        TemperatureScaleKind::Kelvin, {}, {"кельвін", "кельвіни", "кельвінів"}, "кельвіна"};
    static constexpr TemperatureScale rankine{
        TemperatureScaleKind::Rankine, "Ранкіна", {"градус", "градуси", "градусів"}, "градуса"};
    static constexpr TemperatureScale reaumur{
        TemperatureScaleKind::Reaumur, "Реомюра", {"градус", "градуси", "градусів"}, "градуса"};
    static constexpr TemperatureScale delisle{
        TemperatureScaleKind::Delisle, "Деліля", {"градус", "градуси", "градусів"}, "градуса"};
    static constexpr TemperatureScale newton{
        TemperatureScaleKind::Newton, "Ньютона", {"градус", "градуси", "градусів"}, "градуса"};
    static constexpr TemperatureScale romer{
        TemperatureScaleKind::Romer, "Ремера", {"градус", "градуси", "градусів"}, "градуса"};

    const auto lowered = lower_text(scale);
    const auto compact = compact_lower(scale);
    const bool named_degrees = lowered.contains("град");
    if (lowered.contains("кельв") || compact == "k" || compact == "к" || compact == "K" || compact == "°k" ||
        compact == "°к" || (named_degrees && (compact.ends_with("k") || compact.ends_with("к")))) {
        return kelvin;
    }
    if (lowered.contains("реомюр") || compact == "°re" || compact == "°ré" || compact == "°rÉ" ||
        (named_degrees && (compact.ends_with("re") || compact.ends_with("ré") || compact.ends_with("rÉ")))) {
        return reaumur;
    }
    if (lowered.contains("деліл") || compact == "°de" || (named_degrees && compact.ends_with("de"))) {
        return delisle;
    }
    if (lowered.contains("ньютон") || compact == "°n") {
        return newton;
    }
    if (lowered.contains("ремер") || compact == "°rø" || compact == "°rØ" || compact == "°rō" || compact == "°rŌ" ||
        (named_degrees &&
         (compact.ends_with("rø") || compact.ends_with("rØ") || compact.ends_with("rō") || compact.ends_with("rŌ")))) {
        return romer;
    }
    if (lowered.contains("ранкін") || compact == "°r" || compact == "°ra" ||
        (named_degrees && (compact.ends_with("r") || compact.ends_with("ra")))) {
        return rankine;
    }
    if (lowered.contains("фаренгейт") || compact == "f" || compact == "°f" || compact == "℉" ||
        (named_degrees && compact.ends_with("f"))) {
        return fahrenheit;
    }
    if (lowered.contains("цельс") || lowered.contains("celsius") || compact == "c" || compact == "с" ||
        compact == "°c" || compact == "°с" || compact == "℃" ||
        (named_degrees && (compact.ends_with("c") || compact.ends_with("с")))) {
        return celsius;
    }
    return std::nullopt;
}

const std::string& temperature_unit_pattern()
{
    static const std::string degree_symbol =
        R"((?:°\s*(?:Celsius|celsius|Fahrenheit|fahrenheit|C|c|С|с|F|f|N|n|D(?:e|E)|d[eE]|R(?:a|A|e|E|é|É|ø|Ø|ō|Ō)?|r(?:a|e|é|ø|ō)?)|℃|℉))";
    static const std::string kelvin_symbol = R"((?:K|К|K|°\s*(?:K|k|К|к)))";
    static const std::string degrees =
        R"((?:град\.?|Град\.?|ГРАД\.?|градус(?:а|и|ів)?|Градус(?:а|и|ів)?|ГРАДУС(?:А|И|ІВ)?))";
    static const std::string degree_scale =
        R"((?:C|c|С|с|F|f|R|r|Ra|ra|Re|re|Ré|ré|De|de|Rø|rø|Rō|rō|Цельсія|цельсія|ЦЕЛЬСІЯ|Фаренгейта|фаренгейта|ФАРЕНГЕЙТА|Ранкіна|ранкіна|РАНКІНА|Реомюра|реомюра|РЕОМЮРА|Деліля|деліля|ДЕЛІЛЯ|Ньютона|ньютона|НЬЮТОНА|Ремера|ремера|РЕМЕРА|за\s+(?:Цельсієм|цельсієм|ЦЕЛЬСІЄМ|Фаренгейтом|фаренгейтом|ФАРЕНГЕЙТОМ|Ранкіном|ранкіном|РАНКІНОМ|Реомюром|реомюром|РЕОМЮРОМ|Делілем|делілем|ДЕЛІЛЕМ|Ньютоном|ньютоном|НЬЮТОНОМ|Ремером|ремером|РЕМЕРОМ)))";
    static const std::string kelvin_name = R"((?:кельвін(?:а|и|ів)?|Кельвін(?:а|и|ів)?|КЕЛЬВІН(?:А|И|ІВ)?))";
    static const std::string kelvin_degree_name =
        R"((?:K|k|К|к|K|Кельвіна|кельвіна|КЕЛЬВІНА|за\s+(?:Кельвіном|кельвіном|КЕЛЬВІНОМ)))";
    static const std::string pattern = "(?:" + degree_symbol + "|(?:C|c|С|с|F|f)(?!\\.)|" + kelvin_name + "|" +
                                       kelvin_symbol + "|" + degrees + "\\s+(?:" + degree_scale + "|" +
                                       kelvin_degree_name + "))";
    return pattern;
}

std::optional<std::string> temperature_quantity_words(std::string_view token,
                                                      const TemperatureScale& scale,
                                                      std::string_view grammatical_case)
{
    const auto words = signed_number_words(token, grammatical_case);
    if (!words) {
        return std::nullopt;
    }
    if (token.find_first_of(".,") != std::string_view::npos) {
        return *words + " " + std::string(scale.decimal_unit) +
               (scale.genitive_name.empty() ? "" : " " + std::string(scale.genitive_name));
    }
    auto unsigned_token = token;
    take_spoken_sign(unsigned_token);
    const auto value = try_parse_ull(unsigned_token);
    if (!value) {
        return std::nullopt;
    }
    const std::string unit =
        grammatical_case == "gen" ? std::string(scale.unit_forms[2]) : plural(*value, scale.unit_forms);
    return *words + " " + unit + (scale.genitive_name.empty() ? "" : " " + std::string(scale.genitive_name));
}

std::string temperature_range_unit(std::string_view upper, const TemperatureScale& scale)
{
    if (upper.find_first_of(".,") != std::string_view::npos) {
        return std::string(scale.decimal_unit);
    }
    take_spoken_sign(upper);
    const auto value = try_parse_ull(upper);
    if (value && *value % 10 == 1 && *value % 100 != 11) {
        return std::string(scale.decimal_unit);
    }
    return std::string(scale.unit_forms[2]);
}

std::optional<RangeCurrency> range_currency(std::string_view token)
{
    if (lower_text(token) == "грн") {
        return RangeCurrency{"гривень", 'f'};
    }
    for (const auto& entry : lexicon::kCurrencies) {
        if (lower_text(token) == lower_text(entry.code) || (!entry.symbol.empty() && token == entry.symbol)) {
            return RangeCurrency{entry.main_many, entry.main_fem ? 'f' : 'm'};
        }
    }
    return std::nullopt;
}

std::string
range_connector(RangeStyle style, const std::string& low, const std::string& high, bool explicitly_from_to)
{
    return style == RangeStyle::FromTo || explicitly_from_to ? "від " + low + " до " + high : low + " " + high;
}

std::string clock_time_words(std::string_view hour_text,
                             std::string_view minute_text,
                             const std::ssub_match& second,
                             RangeStyle style)
{
    const auto hour = parse_ull(hour_text);
    const auto minute = parse_ull(minute_text);
    if (style == RangeStyle::FromTo) {
        std::string out = number_to_ordinal_words(hour, "gen_f") + " години";
        if (minute) {
            out += " " + join(number_words_for_case(minute, "gen", 'f')) + " хвилин";
        }
        if (second.matched && parse_ull(second.str())) {
            out += " " + join(number_words_for_case(parse_ull(second.str()), "gen", 'f')) + " секунд";
        }
        return out;
    }
    std::string out = hours_words(static_cast<int>(hour));
    if (minute) {
        out += " " + minutes_words(static_cast<int>(minute));
    }
    if (second.matched && parse_ull(second.str())) {
        out += " " + minutes_words(static_cast<int>(parse_ull(second.str())), {"секунда", "секунди", "секунд"});
    }
    return out;
}

} // namespace uktextnorm::detail
