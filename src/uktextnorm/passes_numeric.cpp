#include "uktextnorm/uktextnorm.hpp"

#include "generated/uktextnorm_lexicons.hpp"
#include "internal.hpp"

namespace uktextnorm::detail {

namespace {

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
signed_number_words(std::string_view token, std::string_view grammatical_case, char gender = 'm')
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
                                                      std::string_view grammatical_case = "nom")
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

struct RangeCurrency {
    std::string_view many;
    char gender = 'm';
};

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
range_connector(RangeStyle style, const std::string& low, const std::string& high, bool explicitly_from_to = false)
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

} // namespace

std::string normalize_dates(
    std::string text, DateStyle style, bool validate, RangeStyle range_style, NumericDateOrder numeric_date_order)
{
    static const std::array<std::string_view, 12> months = {"січня",
                                                            "лютого",
                                                            "березня",
                                                            "квітня",
                                                            "травня",
                                                            "червня",
                                                            "липня",
                                                            "серпня",
                                                            "вересня",
                                                            "жовтня",
                                                            "листопада",
                                                            "грудня"};
    auto month_name = [](std::string token) {
        replace_all(token, ".", "");
        token = lower_text(token);
        static const std::unordered_map<std::string, std::string_view> names = {
            {"січ", "січня"},      {"січня", "січня"},         {"лют", "лютого"},  {"лютого", "лютого"},
            {"бер", "березня"},    {"березня", "березня"},     {"квіт", "квітня"}, {"квітня", "квітня"},
            {"трав", "травня"},    {"травня", "травня"},       {"черв", "червня"}, {"червня", "червня"},
            {"лип", "липня"},      {"липня", "липня"},         {"серп", "серпня"}, {"серпня", "серпня"},
            {"вер", "вересня"},    {"вересня", "вересня"},     {"жовт", "жовтня"}, {"жовтня", "жовтня"},
            {"лист", "листопада"}, {"листопада", "листопада"}, {"груд", "грудня"}, {"грудня", "грудня"}};
        if (const auto it = names.find(token); it != names.end()) {
            return std::string(it->second);
        }
        return token;
    };
    auto month_nominative = [](std::string token) {
        replace_all(token, ".", "");
        token = lower_text(token);
        static const std::unordered_map<std::string, std::string_view> names = {
            {"січ", "січень"},         {"січень", "січень"},     {"січня", "січень"},     {"лют", "лютий"},
            {"лютий", "лютий"},        {"лютого", "лютий"},      {"бер", "березень"},     {"березень", "березень"},
            {"березня", "березень"},   {"квіт", "квітень"},      {"квітень", "квітень"},  {"квітня", "квітень"},
            {"трав", "травень"},       {"травень", "травень"},   {"травня", "травень"},   {"черв", "червень"},
            {"червень", "червень"},    {"червня", "червень"},    {"лип", "липень"},       {"липень", "липень"},
            {"липня", "липень"},       {"серп", "серпень"},      {"серпень", "серпень"},  {"серпня", "серпень"},
            {"вер", "вересень"},       {"вересень", "вересень"}, {"вересня", "вересень"}, {"жовт", "жовтень"},
            {"жовтень", "жовтень"},    {"жовтня", "жовтень"},    {"лист", "листопад"},    {"листопад", "листопад"},
            {"листопада", "листопад"}, {"груд", "грудень"},      {"грудень", "грудень"},  {"грудня", "грудень"}};
        if (const auto it = names.find(token); it != names.end()) {
            return std::string(it->second);
        }
        return token;
    };
    auto day_words = [style](std::string_view day, std::string_view formal_form = "nom_n") {
        return number_to_ordinal_words(parse_ull(day), style == DateStyle::Spoken ? "gen" : formal_form);
    };
    auto range_day_words = [&](std::string_view day) {
        return range_style == RangeStyle::FromTo ? number_to_ordinal_words(parse_ull(day), "gen") : day_words(day);
    };
    auto date_range_connector = [&](const std::smatch& m, const std::string& low, const std::string& high) {
        if (range_style == RangeStyle::FromTo) {
            const auto word = preceding_word(m.prefix().str());
            if (word == "на" || word == "в" || word == "у") {
                return "період від " + low + " до " + high;
            }
            if (word == "до" || word == "від" || word == "близько") {
                return low + "–" + high;
            }
        }
        return range_connector(range_style, low, high);
    };
    auto expand_short_year = [](std::string_view year) {
        const auto value = parse_ull(year);
        return year.size() == 2 ? (value < 50 ? 2000ULL + value : 1900ULL + value) : value;
    };
    auto full_date_words = [&](std::string_view day,
                               std::string_view month,
                               std::string_view year,
                               std::string_view forced_day_form = {}) {
        const auto day_value = parse_int(day);
        const auto month_value = parse_int(month);
        const auto year_value = expand_short_year(year);
        if (month_value < 1 || month_value > 12 ||
            (validate && !is_valid_date(day_value, month_value, static_cast<int>(year_value)))) {
            return std::optional<std::string>{};
        }
        const auto spoken_day =
            forced_day_form.empty() ? day_words(day) : number_to_ordinal_words(parse_ull(day), forced_day_form);
        return std::optional<std::string>{spoken_day + " " + std::string(months[month_value - 1]) + " " +
                                          number_to_ordinal_words(year_value, "gen") + " року"};
    };
    auto ordered_date_words = [&](std::string_view first,
                                  std::string_view second,
                                  std::string_view year,
                                  std::string_view forced_day_form = {}) {
        if (numeric_date_order == NumericDateOrder::MonthDayYear) {
            return full_date_words(second, first, year, forced_day_form);
        }
        return full_date_words(first, second, year, forced_day_form);
    };
    auto slash_date_words = [&](std::string_view first,
                                std::string_view second,
                                std::string_view year,
                                std::string_view forced_day_form = {}) {
        // A slash date such as 04/29/02 cannot be DMY. Interpret the only
        // valid order without changing ambiguous dates such as 04/05/02.
        if (numeric_date_order != NumericDateOrder::MonthDayYear && parse_int(first) <= 12 && parse_int(second) > 12) {
            return full_date_words(second, first, year, forced_day_form);
        }
        return ordered_date_words(first, second, year, forced_day_form);
    };
    auto time_words = [](int hour, int minute, std::optional<int> second = std::nullopt) {
        std::string out = hours_words(hour);
        if (minute) {
            out += " " + minutes_words(minute);
        }
        if (second && *second) {
            out += " " + minutes_words(*second, {"секунда", "секунди", "секунд"});
        }
        return out;
    };

    static const std::regex iso_duration(
        R"(\bP(?:(\d+(?:[.,]\d+)?)Y)?(?:(\d+(?:[.,]\d+)?)M)?(?:(\d+(?:[.,]\d+)?)W)?(?:(\d+(?:[.,]\d+)?)D)?(?:T(?:(\d+(?:[.,]\d+)?)H)?(?:(\d+(?:[.,]\d+)?)M)?(?:(\d+(?:[.,]\d+)?)S)?)?\b)",
        std::regex::icase);
    text = regex_sub(text, iso_duration, [](const std::smatch& m) {
        static const std::array<Forms, 7> forms = {{{"рік", "роки", "років"},
                                                    {"місяць", "місяці", "місяців"},
                                                    {"тиждень", "тижні", "тижнів"},
                                                    {"день", "дні", "днів"},
                                                    {"година", "години", "годин"},
                                                    {"хвилина", "хвилини", "хвилин"},
                                                    {"секунда", "секунди", "секунд"}}};
        static constexpr std::array<std::string_view, 7> decimal_forms = {
            "року", "місяця", "тижня", "дня", "години", "хвилини", "секунди"};
        static constexpr std::array<char, 7> genders = {'m', 'm', 'm', 'm', 'f', 'f', 'f'};
        std::vector<std::string> parts;
        for (std::size_t i = 1; i <= forms.size(); ++i) {
            if (!m[i].matched) {
                continue;
            }
            const auto token = m[i].str();
            const auto decimal = token.find_first_of(".,");
            if (decimal != std::string::npos) {
                const auto words = decimal_to_words(std::string_view(token).substr(0, decimal),
                                                    std::string_view(token).substr(decimal + 1));
                parts.push_back(words + " " + std::string(decimal_forms[i - 1]));
            } else {
                const auto value = parse_ull(token);
                parts.push_back(number_words_for_gender(value, genders[i - 1]) + " " + plural(value, forms[i - 1]));
            }
        }
        return parts.empty() ? m.str() : join(parts);
    });

    static const std::regex iso_week(R"(\b(\d{4})-W(\d{2})(?:-(\d))?\b)", std::regex::icase);
    text = regex_sub(text, iso_week, [](const std::smatch& m) {
        const auto year = parse_ull(m[1].str());
        const auto week = parse_ull(m[2].str());
        const auto day = m[3].matched ? parse_ull(m[3].str()) : 0;
        if (!is_valid_iso_week(static_cast<int>(year), static_cast<int>(week)) || day > 7) {
            return m.str();
        }
        std::string out;
        if (day) {
            out = number_to_ordinal_words(day, "nom_m") + " день ";
        }
        return out + number_to_ordinal_words(week, "gen") + " тижня " + number_to_ordinal_words(year, "gen") + " року";
    });

    static const std::regex iso_ordinal_date(R"(\b(\d{4})-(\d{3})\b)");
    text = regex_sub(text, iso_ordinal_date, [](const std::smatch& m) {
        const auto year = parse_ull(m[1].str());
        const auto day = parse_ull(m[2].str());
        const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
        if (day < 1 || day > (leap ? 366 : 365)) {
            return m.str();
        }
        return number_to_ordinal_words(day, "nom_m") + " день " + number_to_ordinal_words(year, "gen") + " року";
    });

    static const std::regex iso_datetime(
        R"(\b(\d{4})-(\d{2})-(\d{2})[Tt ](\d{2}):([0-5]\d)(?::([0-5]\d))?(?:(Z)|(?:(UTC|GMT)\s*)?([+-])(\d{2})(?::?(\d{2}))?)?(?![A-Za-z0-9:+-]))",
        std::regex::icase);
    text = regex_sub(text, iso_datetime, [&](const std::smatch& m) {
        const auto date = full_date_words(m[3].str(), m[2].str(), m[1].str());
        const auto hour = parse_int(m[4].str());
        if (!date || hour > 23) {
            return m.str();
        }
        std::string out = *date + " о " +
                          time_words(hour,
                                     parse_int(m[5].str()),
                                     m[6].matched ? std::optional<int>(parse_int(m[6].str())) : std::nullopt);
        if (m[7].matched) {
            out += " за всесвітнім координованим часом";
        } else if (m[9].matched) {
            const auto offset_hour = parse_int(m[10].str());
            const auto offset_minute = m[11].matched ? parse_int(m[11].str()) : 0;
            if (offset_hour > 14 || offset_minute > 59 || (offset_hour == 14 && offset_minute != 0)) {
                return m.str();
            }
            out += " за часовим поясом " + std::string(m[9].str() == "+" ? "плюс " : "мінус ") +
                   number_to_words(static_cast<unsigned long long>(offset_hour)) + " " +
                   plural(static_cast<unsigned long long>(offset_hour), {"година", "години", "годин"});
            if (offset_minute) {
                out += " " + minutes_words(offset_minute);
            }
        }
        return out;
    });

    static const std::regex cross_month_range("\\b(\\d{1,2})\\s+(" + month_alt() +
                                              ")\\s*(?:-|−|–|—)\\s*(\\d{1,2})\\s+(" + month_alt() +
                                              R"()(?:\s+(\d{4}))?(?![\dА-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, cross_month_range, [&](const std::smatch& m) {
        const auto low = number_to_ordinal_words(parse_ull(m[1].str()), "gen") + " " + month_name(m[2].str());
        const auto high = number_to_ordinal_words(parse_ull(m[3].str()), "gen") + " " + month_name(m[4].str());
        auto out = date_range_connector(m, low, high);
        if (m[5].matched) {
            out += " " + number_to_ordinal_words(parse_ull(m[5].str()), "gen") + " року";
        }
        return out;
    });
    text = regex_sub(text, date_day_range_re(), [&](const std::smatch& m) {
        const auto low = number_to_ordinal_words(parse_ull(m[1].str()), "gen");
        const auto high = number_to_ordinal_words(parse_ull(m[2].str()), "gen");
        return date_range_connector(m, low, high) + " " + month_name(m[3].str()) + " " +
               number_to_ordinal_words(parse_ull(m[4].str()), "gen") + " року";
    });
    static const std::regex date_day_range_without_year("\\b(\\d{1,2})\\s*(?:-|−|–|—)\\s*(\\d{1,2})\\s+(" +
                                                        month_alt() + R"()(?!\s+\d{2,4})(?![А-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, date_day_range_without_year, [&](const std::smatch& m) {
        const auto first = parse_ull(m[1].str());
        const auto second = parse_ull(m[2].str());
        if (first < 1 || first > 31 || second < 1 || second > 31) {
            return m.str();
        }
        const auto form = range_style == RangeStyle::FromTo ? "gen" : "nom_n";
        const auto low = number_to_ordinal_words(first, form);
        const auto high = number_to_ordinal_words(second, form);
        return date_range_connector(m, low, high) + " " + month_name(m[3].str());
    });
    static const std::regex numeric_range(
        R"(\b(\d{1,2})\.(\d{1,2})\.(\d{4})\s*(?:-|−|–|—)\s*(\d{1,2})\.(\d{1,2})\.(\d{4})\b)");
    text = regex_sub(text, numeric_range, [&](const std::smatch& m) {
        const auto range_form = range_style == RangeStyle::FromTo ? std::string_view("gen") : std::string_view{};
        const auto first = ordered_date_words(m[1].str(), m[2].str(), m[3].str(), range_form);
        const auto second = ordered_date_words(m[4].str(), m[5].str(), m[6].str(), range_form);
        if (!first || !second) {
            return m.str();
        }
        return range_connector(range_style, *first, *second);
    });
    static const std::regex iso_range(R"(\b(\d{4})-(\d{2})-(\d{2})\s*(?:-|−|–|—)\s*(\d{4})-(\d{2})-(\d{2})\b)");
    text = regex_sub(text, iso_range, [&](const std::smatch& m) {
        const int m1 = parse_int(m[2].str());
        const int m2 = parse_int(m[5].str());
        if (m1 < 1 || m1 > 12 || m2 < 1 || m2 > 12) {
            return m.str();
        }
        if (validate && (!is_valid_date(parse_int(m[3].str()), m1, parse_int(m[1].str())) ||
                         !is_valid_date(parse_int(m[6].str()), m2, parse_int(m[4].str())))) {
            return m.str();
        }
        const auto low = range_day_words(m[3].str()) + " " + std::string(months[m1 - 1]) + " " +
                         number_to_ordinal_words(parse_ull(m[1].str()), "gen") + " року";
        const auto high = range_day_words(m[6].str()) + " " + std::string(months[m2 - 1]) + " " +
                          number_to_ordinal_words(parse_ull(m[4].str()), "gen") + " року";
        return range_connector(range_style, low, high);
    });
    static const std::regex dmy_dash(R"(\b(\d{1,2})-(\d{1,2})-(\d{2}|\d{4})\b)");
    static const std::regex governed_numeric_date(
        R"((^|[^А-Яа-яЄєІіЇїҐґ])(від|до|з|із|після|станом\s+на)\s+(\d{1,2})[./-](\d{1,2})[./-](\d{2}|\d{4})\b(?:\s+(?:року|р\.))?)",
        std::regex::icase);
    text = regex_sub(text, governed_numeric_date, [&](const std::smatch& m) {
        const auto out = m.str().contains('/') ? slash_date_words(m[3].str(), m[4].str(), m[5].str(), "gen")
                                               : ordered_date_words(m[3].str(), m[4].str(), m[5].str(), "gen");
        return out ? m[1].str() + m[2].str() + " " + *out : m.str();
    });
    text = regex_sub(text, dmy_dash, [&](const std::smatch& m) {
        const auto out = ordered_date_words(m[1].str(), m[2].str(), m[3].str());
        return out ? *out : m.str();
    });
    static const std::regex dmy_short_dot(R"(\b(\d{1,2})\.(\d{1,2})\.(\d{2})\b(?:\s+(?:року|р\.))?)");
    text = regex_sub(text, dmy_short_dot, [&](const std::smatch& m) {
        const auto out = ordered_date_words(m[1].str(), m[2].str(), m[3].str());
        return out ? *out : m.str();
    });
    static const std::regex dmy_short_slash(R"(\b(\d{1,2})/(\d{1,2})/(\d{2})\b(?!/\d)(?:\s+(?:року|р\.))?)");
    text = regex_sub(text, dmy_short_slash, [&](const std::smatch& m) {
        const auto out = slash_date_words(m[1].str(), m[2].str(), m[3].str());
        return out ? *out : m.str();
    });
    static const std::regex ymd_slash(R"(\b(\d{4})/(\d{1,2})/(\d{1,2})\b(?!/\d)(?:\s+(?:року|р\.))?)");
    text = regex_sub(text, ymd_slash, [&](const std::smatch& m) {
        const auto out = full_date_words(m[3].str(), m[2].str(), m[1].str());
        return out ? *out : m.str();
    });
    text = ctre_sub<R"(\b(\d{1,2})\.(\d{1,2})\.(\d{4})\b(?:\s+(?:року|р\.))?)">(text, [&](const auto& m) {
        const auto out = ordered_date_words(cap<1>(m), cap<2>(m), cap<3>(m));
        return out ? *out : whole_string(m);
    });
    text = ctre_sub<R"(\b(\d{1,2})/(\d{1,2})/(\d{4})\b(?!/\d)(?:\s+(?:року|р\.))?)">(text, [&](const auto& m) {
        const auto out = slash_date_words(cap<1>(m), cap<2>(m), cap<3>(m));
        return out ? *out : whole_string(m);
    });
    text = ctre_sub<R"(\b(\d{4})-(\d{2})-(\d{2})\b)">(text, [&](const auto& m) {
        const int month = parse_int(cap<2>(m));
        if (month < 1 || month > 12) {
            return whole_string(m);
        }
        if (validate && !is_valid_date(parse_int(cap<3>(m)), month, parse_int(cap<1>(m)))) {
            return whole_string(m);
        }
        return day_words(cap<3>(m)) + " " + std::string(months[month - 1]) + " " +
               number_to_ordinal_words(parse_ull(cap<1>(m)), "gen") + " року";
    });
    static const std::regex year_month(R"(\b(\d{4})-(0?[1-9]|1[0-2])(?!-?\d))");
    text = regex_sub(text, year_month, [&](const std::smatch& m) {
        const auto month = static_cast<std::size_t>(parse_int(m[2].str()));
        static const std::array<std::string_view, 12> nominative = {"січень",
                                                                    "лютий",
                                                                    "березень",
                                                                    "квітень",
                                                                    "травень",
                                                                    "червень",
                                                                    "липень",
                                                                    "серпень",
                                                                    "вересень",
                                                                    "жовтень",
                                                                    "листопад",
                                                                    "грудень"};
        return std::string(nominative[month - 1]) + " " + number_to_ordinal_words(parse_ull(m[1].str()), "gen") +
               " року";
    });
    text = regex_sub(text, date_spelled_re(), [](const std::smatch& m) {
        auto token = m[2].str();
        replace_all(token, ".", "");
        token = lower_text(token);
        static const std::unordered_map<std::string, std::string_view> names = {
            {"січ", "січня"},      {"січня", "січня"},         {"лют", "лютого"},  {"лютого", "лютого"},
            {"бер", "березня"},    {"березня", "березня"},     {"квіт", "квітня"}, {"квітня", "квітня"},
            {"трав", "травня"},    {"травня", "травня"},       {"черв", "червня"}, {"червня", "червня"},
            {"лип", "липня"},      {"липня", "липня"},         {"серп", "серпня"}, {"серпня", "серпня"},
            {"вер", "вересня"},    {"вересня", "вересня"},     {"жовт", "жовтня"}, {"жовтня", "жовтня"},
            {"лист", "листопада"}, {"листопада", "листопада"}, {"груд", "грудня"}, {"грудня", "грудня"}};
        const auto it = names.find(token);
        const auto month = it == names.end() ? token : std::string(it->second);
        return number_to_ordinal_words(parse_ull(m[1].str()), "gen") + " " + month + " " +
               number_to_ordinal_words(parse_ull(m[3].str()), "gen") + " року";
    });
    static const std::string month_any =
        R"((?:січ(?:ень|ня|ні)?|лют(?:ий|ого|ому)?|бер(?:езень|езня|езні)?|квіт(?:ень|ня|ні)?|трав(?:ень|ня|ні)?|черв(?:ень|ня|ні)?|лип(?:ень|ня|ні)?|серп(?:ень|ня|ні)?|вер(?:есень|есня|есні)?|жовт(?:ень|ня|ні)?|лист(?:опад|опада|опаді)?|груд(?:ень|ня|ні)?))";
    static const std::regex named_without_year(
        "\\b(\\d{1,2})\\s+(" + month_any + R"()(?!\s+\d{2,4})(?![А-Яа-яЄєІіЇїҐґ]))", std::regex::icase);
    text = regex_sub(text, named_without_year, [&](const std::smatch& m) {
        const auto day = parse_ull(m[1].str());
        if (day < 1 || day > 31) {
            return m.str();
        }
        return number_to_ordinal_words(day, "gen") + " " + month_name(m[2].str());
    });
    static const std::regex named_month_year("(^|[^А-Яа-яЄєІіЇїҐґ])(" + month_any +
                                                 R"()\s+(\d{4})(?:\s+(?:року|р\.)(?![А-Яа-яЄєІіЇїҐґ]))?(?!\d))",
                                             std::regex::icase);
    text = regex_sub(text, named_month_year, [&](const std::smatch& m) {
        static const std::unordered_set<std::string> genitive_months = {"січня",
                                                                        "лютого",
                                                                        "березня",
                                                                        "квітня",
                                                                        "травня",
                                                                        "червня",
                                                                        "липня",
                                                                        "серпня",
                                                                        "вересня",
                                                                        "жовтня",
                                                                        "листопада",
                                                                        "грудня"};
        static const std::unordered_set<std::string> locative_months = {"січні",
                                                                        "лютому",
                                                                        "березні",
                                                                        "квітні",
                                                                        "травні",
                                                                        "червні",
                                                                        "липні",
                                                                        "серпні",
                                                                        "вересні",
                                                                        "жовтні",
                                                                        "листопаді",
                                                                        "грудні"};
        const auto source_month = lower_text(m[2].str());
        const auto month = genitive_months.contains(source_month)   ? month_name(source_month)
                           : locative_months.contains(source_month) ? source_month
                                                                    : month_nominative(source_month);
        return m[1].str() + month + " " + number_to_ordinal_words(parse_ull(m[3].str()), "gen") + " року";
    });
    static const std::regex abbreviated_year_context(
        R"((^|[^А-Яа-яЄєІіЇїҐґ])(У|у|В|в|До|до|Від|від|Після|після)\s+(\d{3,4})\s*(?:р\.|рік)(?![а-яіїєґ]))");
    text = regex_sub(text, abbreviated_year_context, [](const std::smatch& m) {
        const auto preposition = lower_text(m[2].str());
        const bool locative = preposition == "у" || preposition == "в";
        return m[1].str() + m[2].str() + " " +
               number_to_ordinal_words(parse_ull(m[3].str()), locative ? "prep" : "gen") +
               (locative ? " році" : " року");
    });
    static const std::regex decade_without_suffix(
        R"((^|[^А-Яа-яЄєІіЇїҐґ])(У|у|В|в)\s+(\d{4})\s+роках(?![А-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, decade_without_suffix, [](const std::smatch& m) {
        return m[1].str() + m[2].str() + " " + number_to_ordinal_words(parse_ull(m[3].str()), "loc_pl") + " роках";
    });
    text = ctre_sub<R"((^|[^\d])(\d{3,4})\s+(році|року|роком|рік)(?![А-Яа-яЄєІіЇїҐґ]))">(text, [](const auto& m) {
        static const std::unordered_map<std::string, std::string_view> forms = {
            {"рік", "nom_m"}, {"року", "gen"}, {"році", "prep"}, {"роком", "ins"}};
        const auto form = cap_string<3>(m);
        return cap_string<1>(m) + number_to_ordinal_words(parse_ull(cap<2>(m)), forms.at(form)) + " " + form;
    });
    static const std::regex ordinal_year_suffix(R"((^|[^\d])(\d{3,4})[-–—](го|му|й|м)(?![А-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, ordinal_year_suffix, [](const std::smatch& m) {
        static const std::unordered_map<std::string, std::string_view> forms = {
            {"го", "gen"}, {"му", "dat"}, {"й", "nom_m"}, {"м", "prep"}};
        return m[1].str() + number_to_ordinal_words(parse_ull(m[2].str()), forms.at(m[3].str()));
    });
    return ctre_sub<R"(\b(\d{3,4})\s*р\.(?![а-яіїєґ]))">(
        text, [](const auto& m) { return number_to_ordinal_words(parse_ull(cap<1>(m)), "nom_m") + " рік"; });
}

std::string normalize_discourse_dates(std::string text)
{
    static const std::regex explicit_year_span(
        R"((^|[^А-Яа-яЄєІіЇїҐґ])((?:З|з|Із|із|Від|від))\s+(\d{4})\s+(?:по|до)\s+(\d{4})\s*(?:рр?\.?|роки)?(?![\dА-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, explicit_year_span, [](const std::smatch& m) {
        return m[1].str() + m[2].str() + " " + number_to_ordinal_words(parse_ull(m[3].str()), "gen") + " до " +
               number_to_ordinal_words(parse_ull(m[4].str()), "gen") + " року";
    });
    static const std::regex season_year(
        R"((^|[^А-Яа-яЄєІіЇїҐґ])((?:весна|літо|осінь|зима))\s+(\d{3,4})(?![\dА-Яа-яЄєІіЇїҐґ]))", std::regex::icase);
    text = regex_sub(text, season_year, [](const std::smatch& m) {
        return m[1].str() + m[2].str() + " " + number_to_ordinal_words(parse_ull(m[3].str()), "gen") + " року";
    });

    static const std::regex early_decade(
        R"((^|[^А-Яа-яЄєІіЇїҐґ])((?:на\s+початку|у\s+середині|в\s+середині|наприкінці|у\s+кінці|в\s+кінці))\s+(\d{4})-х(?![А-Яа-яЄєІіЇїҐґ]))",
        std::regex::icase);
    return regex_sub(text, early_decade, [](const std::smatch& m) {
        const auto year = parse_ull(m[3].str());
        if (year == 2000) {
            return m[1].str() + m[2].str() + " двотисячних";
        }
        return m[1].str() + m[2].str() + " " + number_to_ordinal_words(year, "pl");
    });
}
std::string normalize_biblical_references(std::string text)
{
    static const std::string books =
        R"((?:Ісая|Єзекіїл|Буття|Вихід|Левит|Числа|Повторення Закону|Псалми|Матвій|Марко|Лука|Іван))";
    static const std::regex reference_group("\\((" + books + R"()\s+([^)]{3,120})\))");
    static const std::regex chapter_verse(R"((^|[^\d])(\d{1,3}):(\d{1,3})(?!\d))");
    auto say_references = [&](std::string body) {
        return regex_sub(body, chapter_verse, [](const std::smatch& m) {
            return m[1].str() + "розділ " + number_to_words(parse_ull(m[2].str())) + ", вірш " +
                   number_to_words(parse_ull(m[3].str()));
        });
    };
    text = regex_sub(text, reference_group, [&](const std::smatch& m) {
        return "(" + m[1].str() + " " + say_references(m[2].str()) + ")";
    });
    static const std::regex labelled_reference("(^|[^А-Яа-яЄєІіЇїҐґ])(" + books + R"()\s+(\d{1,3}):(\d{1,3})(?!\d))");
    return regex_sub(text, labelled_reference, [](const std::smatch& m) {
        return m[1].str() + m[2].str() + " розділ " + number_to_words(parse_ull(m[3].str())) + ", вірш " +
               number_to_words(parse_ull(m[4].str()));
    });
}
std::string normalize_ordinals(std::string text)
{
    static const std::unordered_map<std::string, std::string_view> suffix_form = {{"й", "nom_m"},
                                                                                  {"ший", "nom_m"},
                                                                                  {"го", "gen"},
                                                                                  {"му", "dat"},
                                                                                  {"м", "prep"},
                                                                                  {"а", "nom_f"},
                                                                                  {"ша", "nom_f"},
                                                                                  {"га", "nom_f"},
                                                                                  {"тя", "nom_f"},
                                                                                  {"у", "acc_f"},
                                                                                  {"е", "nom_n"},
                                                                                  {"ше", "nom_n"},
                                                                                  {"ге", "nom_n"},
                                                                                  {"тє", "nom_n"},
                                                                                  {"х", "pl"},
                                                                                  {"им", "ins"},
                                                                                  {"ім", "ins"},
                                                                                  {"ою", "ins_f"},
                                                                                  {"ій", "loc_f"},
                                                                                  {"ими", "ins_pl"}};
    static const std::unordered_set<std::string> stop = {
        "CD", "DVD", "MD", "DC", "MC", "MI", "MM", "DI", "DIV", "DVI", "DL", "CLI", "MIX", "CIV", "LCD"};
    // Ukrainian Wikipedia commonly writes Roman centuries with Cyrillic
    // homoglyphs (ХХІ). Repair only in a century context, never in identifiers.
    auto cyrillic_roman_value = [](std::string token) -> std::optional<int> {
        if (token.find("Х") == std::string::npos && token.find("І") == std::string::npos) {
            return std::nullopt;
        }
        replace_all(token, "Х", "X");
        replace_all(token, "І", "I");
        return valid_roman(token) ? std::optional<int>{roman_to_int(token)} : std::nullopt;
    };
    static const std::regex cyrillic_century_range(
        R"((^|[^A-Za-zА-Яа-яЄєІіЇїҐґ])((?:Х|І|X|I|V|M|C|D|L){1,8})\s*(?:-|–|—)\s*((?:Х|І|X|I|V|M|C|D|L){1,8})\s*(ст\.|століття|столітті|сторіччя|сторіччі)(?![А-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, cyrillic_century_range, [&](const std::smatch& m) {
        const auto first = cyrillic_roman_value(m[2].str());
        const auto second = cyrillic_roman_value(m[3].str());
        if (!first || !second) {
            return m.str();
        }
        auto left = lower_text(m.prefix().str() + m[1].str());
        while (!left.empty() && std::isspace(static_cast<unsigned char>(left.back()))) {
            left.pop_back();
        }
        const auto boundary = left.find_last_of(" \t\n");
        const auto word = left.substr(boundary == std::string::npos ? 0 : boundary + 1);
        const auto noun = m[4].str().starts_with("сторіч") ? "сторіччя" : "століття";
        if (word == "у" || word == "в" || m[4].str() == "столітті" || m[4].str() == "сторіччі") {
            return m[1].str() + number_to_ordinal_words(*first, "prep") + "–" +
                   number_to_ordinal_words(*second, "prep") +
                   (std::string_view(noun) == "сторіччя" ? " сторіччях" : " століттях");
        }
        return m[1].str() + "від " + number_to_ordinal_words(*first, "gen") + " до " +
               number_to_ordinal_words(*second, "gen") + " " + noun;
    });
    static const std::regex cyrillic_century(
        R"((^|[^A-Za-zА-Яа-яЄєІіЇїҐґ])((?:Х|І|X|I|V|M|C|D|L){1,8})\s*(ст\.|століття|столітті|сторіччя|сторіччі)(?![А-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, cyrillic_century, [&](const std::smatch& m) {
        const auto value = cyrillic_roman_value(m[2].str());
        if (!value) {
            return m.str();
        }
        auto left = lower_text(m.prefix().str() + m[1].str());
        while (!left.empty() && std::isspace(static_cast<unsigned char>(left.back()))) {
            left.pop_back();
        }
        const auto boundary = left.find_last_of(" \t\n");
        const auto word = left.substr(boundary == std::string::npos ? 0 : boundary + 1);
        const bool locative = word == "у" || word == "в" || m[3].str() == "столітті" || m[3].str() == "сторіччі";
        const bool genitive = word == "початку" || word == "кінця" || word == "середини" || word == "половини";
        const auto form = locative ? "prep" : genitive ? "gen" : "nom_n";
        const auto noun = m[3].str().starts_with("сторіч") ? (locative ? " сторіччі" : " сторіччя")
                                                           : (locative ? " столітті" : " століття");
        return m[1].str() + number_to_ordinal_words(*value, form) + noun;
    });
    static const std::regex bare_cyrillic_century_before_start(
        R"((Протягом|протягом)\s+((?:Х|І|X|I|V|M|C|D|L){1,8})\s+та\s+початку)");
    text = regex_sub(text, bare_cyrillic_century_before_start, [&](const std::smatch& m) {
        const auto value = cyrillic_roman_value(m[2].str());
        return value ? m[1].str() + " " + number_to_ordinal_words(*value, "gen") + " століття та початку" : m.str();
    });
    // Keep these patterns in std::regex rather than CTRE. The CTRE expansion
    // for these UTF-8 lookahead/alternation expressions has high runtime stack
    // usage; on the default 1 MiB Windows executable stack even a short input
    // can terminate with STATUS_STACK_OVERFLOW.
    static const std::regex ordinal_suffix(R"((\d+)(?:-|–|—)(ший|ими|им|ім|ою|ій|ше|ша|ге|га|тє|тя|го|му|й|м|а|у|е|х)(?![А-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, ordinal_suffix, [&](const std::smatch& m) {
        return number_to_ordinal_words(parse_ull(m[1].str()), suffix_form.at(m[2].str()));
    });
    static const std::regex roman_century_range(
        R"((^|[^A-Za-z])([MDCLXVI]{1,6})\s*(?:-|–|—)\s*([MDCLXVI]{1,6})\s*(?:ст\.|століття)(?![А-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, roman_century_range, [](const std::smatch& m) {
        const auto start = m[2].str();
        const auto stop = m[3].str();
        if (!valid_roman(start) || !valid_roman(stop)) {
            return m.str();
        }
        return m[1].str() + number_to_ordinal_words(roman_to_int(start), "nom_n") + " " +
               number_to_ordinal_words(roman_to_int(stop), "nom_n") + " століття";
    });
    static const std::regex roman_section_range(
        R"((^|[^A-Za-z])([MDCLXVI]{1,6})\s*(?:-|–|—)\s*([MDCLXVI]{1,6})\s*(розд\.|розділ)(?![А-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, roman_section_range, [](const std::smatch& m) {
        const auto start = m[2].str();
        const auto stop = m[3].str();
        if (!valid_roman(start) || !valid_roman(stop)) {
            return m.str();
        }
        return m[1].str() + number_to_ordinal_words(roman_to_int(start), "nom_m") + " " +
               number_to_ordinal_words(roman_to_int(stop), "nom_m") + " розділ";
    });
    static const std::regex roman_century(R"((^|[^A-Za-z])([MDCLXVI]{1,6})\s*(?:ст\.|століття)(?![А-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, roman_century, [](const std::smatch& m) {
        const auto tok = m[2].str();
        if (!valid_roman(tok)) {
            return m.str();
        }
        return m[1].str() + number_to_ordinal_words(roman_to_int(tok), "nom_n") + " століття";
    });
    static const std::regex roman_group(R"((^|[^A-Za-z])([MDCLXVI]{1,6})\s+(група|групи)(?![А-Яа-яЄєІіЇїҐґ]))",
                                        std::regex::icase);
    text = regex_sub(text, roman_group, [](const std::smatch& m) {
        const auto token = m[2].str();
        if (!valid_roman(token)) {
            return m.str();
        }
        const auto form = lower_text(m[3].str()) == "групи" ? "gen_f" : "nom_f";
        return m[1].str() + number_to_ordinal_words(roman_to_int(token), form) + " " + m[3].str();
    });
    static const std::regex bare_roman(R"(\b[MDCLXVI]{2,}\b)");
    return regex_sub(text, bare_roman, [&](const std::smatch& m) {
        const auto tok = m.str();
        if (stop.contains(tok) || !valid_roman(tok)) {
            return tok;
        }
        return number_to_ordinal_words(roman_to_int(tok), "nom_m");
    });
}

std::string normalize_quarters(std::string text)
{
    static const std::regex roman_q(
        R"((^|[^A-Za-z])([MDCLXVI]{1,6})\s*(?:кв\.|квартал)(?:\s+(\d{3,4})(?:\s*р\.|\s+року)?)?(?![А-Яа-яЄєІіЇїҐґ]))");
    static const std::regex num_q(
        R"((^|[^\d])(\d{1,2})(?:[-–—]?(?:й|ій))?\s*(?:кв\.|квартал)(?:\s+(\d{3,4})(?:\s*р\.|\s+року)?)?(?![А-Яа-яЄєІіЇїҐґ]))");
    auto say = [](unsigned long long q, const std::ssub_match& year) {
        if (q < 1 || q > 4) {
            return std::string{};
        }
        std::string out = number_to_ordinal_words(q, "nom_m") + " квартал";
        if (year.matched) {
            out += " " + number_to_ordinal_words(parse_ull(year.str()), "gen") + " року";
        }
        return out;
    };
    text = regex_sub(text, roman_q, [&](const std::smatch& m) {
        const auto tok = m[2].str();
        if (!valid_roman(tok)) {
            return m.str();
        }
        const auto out = say(static_cast<unsigned long long>(roman_to_int(tok)), m[3]);
        return out.empty() ? m.str() : m[1].str() + out;
    });
    return regex_sub(text, num_q, [&](const std::smatch& m) {
        const auto out = say(parse_ull(m[2].str()), m[3]);
        return out.empty() ? m.str() : m[1].str() + out;
    });
}

std::string normalize_page_ranges(std::string text, RangeStyle style)
{
    static const std::regex bibliographic_volumes(R"((^|[\s,:;])(У|у|В|в)\s+(\d+)\s+(?:т|Т)(?:т|Т)?\.?([ \t]*)(?=/))");
    text = regex_sub(text, bibliographic_volumes, [](const std::smatch& m) {
        const auto count = parse_ull(m[3].str());
        const bool singular = count % 10 == 1 && count % 100 != 11;
        return m[1].str() + m[2].str() + " " + number_to_words_case(count, "prep") + (singular ? " томі" : " томах") +
               m[4].str();
    });
    static const std::regex page_count(R"((^|(?:-|—)\s+)(\d+)\s+(?:с|С)\.(?=\s*(?::|;|-|—|ISBN|$)))",
                                       std::regex::icase);
    text = regex_sub(text, page_count, [](const std::smatch& m) {
        const auto count = parse_ull(m[2].str());
        return m[1].str() + number_words_for_gender(count, 'f') + " " +
               plural(count, {"сторінка", "сторінки", "сторінок"}) + (m.suffix().str().empty() ? "." : "");
    });
    static const std::regex page_range(
        R"((^|[^А-Яа-яЄєІіЇїҐґA-Za-z])(?:стор\.|Стор\.|СТОР\.|с\.|С\.|pp?\.)\s*(\d+)\s*(?:-|−|–|—)\s*(\d+)(?!\d))",
        std::regex::icase);
    text = regex_sub(text, page_range, [&](const std::smatch& m) {
        const auto low = try_parse_ull(m[2].str());
        const auto high = try_parse_ull(m[3].str());
        if (!low || !high) {
            return m.str();
        }
        if (style == RangeStyle::FromTo) {
            return m[1].str() + "від " + number_to_ordinal_words(*low, "gen_f") + " до " +
                   number_to_ordinal_words(*high, "gen_f") + " сторінки";
        }
        return m[1].str() + "сторінки " + number_to_words(*low) + " " + number_to_words(*high);
    });
    static const std::regex single_page(R"((^|[^А-Яа-яЄєІіЇїҐґA-Za-z])(?:стор|Стор|СТОР|с|С|pp?)\.\s*(\d+)(?!\d))",
                                        std::regex::icase);
    return regex_sub(text, single_page, [](const std::smatch& m) {
        return m[1].str() + "сторінка " + number_to_words(parse_ull(m[2].str()));
    });
}

std::string normalize_section_ranges(std::string text, RangeStyle style)
{
    struct SectionRange {
        std::string_view compact;
        std::string_view genitive;
        std::string_view ordinal_form;
    };
    static const std::unordered_map<std::string, SectionRange> sections = {
        {"ст", {"статті", "статті", "gen_f"}},
        {"статті", {"статті", "статті", "gen_f"}},
        {"ч", {"частини", "частини", "gen_f"}},
        {"частини", {"частини", "частини", "gen_f"}},
        {"п", {"пункти", "пункту", "gen"}},
        {"пункти", {"пункти", "пункту", "gen"}},
        {"пп", {"підпункти", "підпункту", "gen"}},
        {"підпункти", {"підпункти", "підпункту", "gen"}},
        {"абз", {"абзаци", "абзацу", "gen"}},
        {"розд", {"розділи", "розділу", "gen"}},
        {"гл", {"глави", "глави", "gen_f"}},
        {"табл", {"таблиці", "таблиці", "gen_f"}},
        {"рис", {"рисунки", "рисунка", "gen"}},
    };
    static const std::regex dotted_range(
        R"((^|[^А-Яа-яЄєІіЇїҐґA-Za-z])(пп|підпункти)\.?\s*(\d+(?:\.\d+)+)\s*(?:-|−|–|—)\s*(\d+(?:\.\d+)+)(?![\d.]))",
        std::regex::icase);
    text = regex_sub(text, dotted_range, [&](const std::smatch& m) {
        if (style == RangeStyle::FromTo) {
            return m[1].str() + "від підпункту " + read_dotted(m[3].str()) + " до підпункту " + read_dotted(m[4].str());
        }
        return m[1].str() + "підпункти " + read_dotted(m[3].str()) + " " + read_dotted(m[4].str());
    });
    static const std::regex range(
        R"((^|[^А-Яа-яЄєІіЇїҐґA-Za-z])(ст|статті|ч|частини|пп|підпункти|п|пункти|абз|розд|гл|табл|рис)\.?\s*(\d+)\s*(?:-|−|–|—)\s*(\d+)(?!\d))",
        std::regex::icase);
    return regex_sub(text, range, [&](const std::smatch& m) {
        const auto low = try_parse_ull(m[3].str());
        const auto high = try_parse_ull(m[4].str());
        if (!low || !high) {
            return m.str();
        }
        const auto& section = sections.at(lower_text(m[2].str()));
        if (style == RangeStyle::FromTo) {
            return m[1].str() + "від " + number_to_ordinal_words(*low, section.ordinal_form) + " до " +
                   number_to_ordinal_words(*high, section.ordinal_form) + " " + std::string(section.genitive);
        }
        return m[1].str() + std::string(section.compact) + " " + number_to_words(*low) + " " + number_to_words(*high);
    });
}

std::string normalize_ranges(std::string text, RangeStyle style)
{
    const auto& number = signed_number_pattern();
    const auto& separator = range_separator_pattern();
    const auto& prefix = range_prefix_pattern();
    const auto& temperature_unit = temperature_unit_pattern();
    static const std::string number_boundary = R"((?![\d:/+\-−–—])(?![.,]\d))";
    static const std::string temperature_boundary = number_boundary + R"((?![A-Za-zА-Яа-яЄєІіЇїҐґ]))";

    enum class GovernedRange {
        None,
        From,
        To,
        Near,
        On,
        In
    };
    auto governed_range = [&](const std::smatch& m, bool explicitly_from_to) {
        if (style != RangeStyle::FromTo || explicitly_from_to) {
            return GovernedRange::None;
        }
        const auto word = preceding_word(m.prefix().str() + m[1].str());
        if (word == "від") {
            return GovernedRange::From;
        }
        if (word == "до") {
            return GovernedRange::To;
        }
        if (word == "близько") {
            return GovernedRange::Near;
        }
        if (word == "на") {
            return GovernedRange::On;
        }
        if (word == "в" || word == "у") {
            return GovernedRange::In;
        }
        return GovernedRange::None;
    };
    auto governed_connector =
        [&](const std::smatch& m, const std::string& low, const std::string& high, bool explicitly_from_to) {
            return governed_range(m, explicitly_from_to) == GovernedRange::None
                       ? range_connector(style, low, high, explicitly_from_to)
                       : low + "–" + high;
        };
    auto governed_case = [&](const std::smatch& m, bool explicitly_from_to) {
        const auto context = governed_range(m, explicitly_from_to);
        return style == RangeStyle::FromTo && context != GovernedRange::On && context != GovernedRange::In ? "gen"
               : explicitly_from_to                                                                        ? "gen"
                                                                                                           : "nom";
    };

    auto temperature_bound = [](std::string_view token, const TemperatureScale& scale, bool genitive) {
        if (!genitive) {
            return temperature_quantity_words(token, scale);
        }
        const auto words = signed_number_words(token, "gen");
        if (!words) {
            return std::optional<std::string>{};
        }
        return std::optional<std::string>{*words + " " + temperature_range_unit(token, scale) +
                                          (scale.genitive_name.empty() ? "" : " " + std::string(scale.genitive_name))};
    };

    auto say_temperature = [&](const std::smatch& m,
                               std::size_t low_index,
                               std::size_t high_index,
                               std::size_t scale_index,
                               bool explicitly_from_to) {
        const auto grammatical_case = governed_case(m, explicitly_from_to);
        const auto low = signed_number_words(m[low_index].str(), grammatical_case);
        const auto high = signed_number_words(m[high_index].str(), grammatical_case);
        if (!low || !high) {
            return m.str();
        }
        const auto scale = temperature_scale(m[scale_index].str());
        if (!scale) {
            return m.str();
        }
        return m[1].str() + governed_connector(m, *low, *high, explicitly_from_to) + " " +
               temperature_range_unit(m[high_index].str(), *scale) +
               (scale->genitive_name.empty() ? "" : " " + std::string(scale->genitive_name));
    };

    static const std::regex explicit_repeated_temperature(prefix + "від\\s+(" + number + ")\\s*(" + temperature_unit +
                                                          ")\\s+до\\s+(" + number + ")\\s*(" + temperature_unit + ")" +
                                                          temperature_boundary);
    text = regex_sub(text, explicit_repeated_temperature, [&](const std::smatch& m) {
        const auto low_scale = temperature_scale(m[3].str());
        const auto high_scale = temperature_scale(m[5].str());
        if (!low_scale || !high_scale) {
            return m.str();
        }
        if (low_scale->kind != high_scale->kind) {
            const auto low = temperature_bound(m[2].str(), *low_scale, true);
            const auto high = temperature_bound(m[4].str(), *high_scale, true);
            return low && high ? m[1].str() + "від " + *low + " до " + *high : m.str();
        }
        return say_temperature(m, 2, 4, 5, true);
    });
    static const std::regex repeated_temperature(prefix + "(" + number + ")\\s*(" + temperature_unit + ")\\s*" +
                                                 separator + "\\s*(" + number + ")\\s*(" + temperature_unit + ")" +
                                                 temperature_boundary);
    text = regex_sub(text, repeated_temperature, [&](const std::smatch& m) {
        const auto low_scale = temperature_scale(m[3].str());
        const auto high_scale = temperature_scale(m[5].str());
        if (!low_scale || !high_scale) {
            return m.str();
        }
        if (low_scale->kind != high_scale->kind) {
            const bool genitive = style == RangeStyle::FromTo;
            const auto low = temperature_bound(m[2].str(), *low_scale, genitive);
            const auto high = temperature_bound(m[4].str(), *high_scale, genitive);
            if (!low || !high) {
                return m.str();
            }
            return m[1].str() + governed_connector(m, *low, *high, false);
        }
        return say_temperature(m, 2, 4, 5, false);
    });
    static const std::regex explicit_temperature(prefix + "від\\s+(" + number + ")\\s+до\\s+(" + number + ")\\s*(" +
                                                 temperature_unit + ")" + temperature_boundary);
    text =
        regex_sub(text, explicit_temperature, [&](const std::smatch& m) { return say_temperature(m, 2, 3, 4, true); });
    static const std::regex temperature_range(prefix + "(" + number + ")\\s*" + separator + "\\s*(" + number +
                                              ")\\s*(" + temperature_unit + ")" + temperature_boundary);
    text = regex_sub(text, temperature_range, [&](const std::smatch& m) { return say_temperature(m, 2, 3, 4, false); });

    static const std::regex prepositional_year_range(
        R"((^|[^А-Яа-яЄєІіЇїҐґ])(У|у|В|в)\s+(\d{3,4})\s*(?:-|−|–|—)\s*(\d{3,4})\s*(?:рр\.?|роки|роках|року|років)(?![\dа-яіїєґ]))");
    const auto say_prepositional_year_range = [](const std::smatch& m) {
        return m[1].str() + m[2].str() + " період від " + number_to_ordinal_words(parse_ull(m[3].str()), "gen") + " до " +
               number_to_ordinal_words(parse_ull(m[4].str()), "gen") + " року";
    };
    text = regex_sub(text, prepositional_year_range, say_prepositional_year_range);
    // Without an explicit year word, only treat a four-digit span as years.
    static const std::regex bare_prepositional_year_range(
        R"((^|[^А-Яа-яЄєІіЇїҐґ])(У|у|В|в)\s+(\d{4})\s*(?:-|−|–|—)\s*(\d{4})(?![\dа-яіїєґ]))");
    text = regex_sub(text, bare_prepositional_year_range, say_prepositional_year_range);
    auto expanded_short_year = [](unsigned long long first, unsigned long long short_second) {
        auto second = (first / 100) * 100 + short_second;
        if (second < first) {
            second += 100;
        }
        return second;
    };
    static const std::regex abbreviated_decade_range(
        R"(\b((?:19|20)\d{2})\s*(?:-|–|—)\s*(\d{2})(?:-|–|—)?(х|их|і|ї)(?:\s+(роках|років|роки))?(?![\dА-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, abbreviated_decade_range, [&](const std::smatch& m) {
        const auto first = parse_ull(m[1].str());
        const auto second = expanded_short_year(first, parse_ull(m[2].str()));
        if (first / 100 != second / 100 || first % 10 != 0 || second % 10 != 0) {
            return m.str();
        }
        const auto form = m[3].str() == "і" || m[3].str() == "ї" ? "nom_pl" : "pl";
        const auto year_word = m[4].matched ? m[4].str() : (form == std::string_view("nom_pl") ? "роки" : "роках");
        return number_to_ordinal_words(first % 100, form) + "–" + number_to_ordinal_words(second % 100, form) + " " +
               year_word + " " + number_to_ordinal_words(first / 100 + 1, "gen") + " століття";
    });
    static const std::regex abbreviated_prepositional_year_range(
        R"((^|[^А-Яа-яЄєІіЇїҐґ])(У|у|В|в)\s+((?:19|20)\d{2})\s*(?:-|–|—)\s*(\d{2})\s*(?:рр?\.?|роки|роках|року|років)(?![\dа-яіїєґ]))");
    text = regex_sub(text, abbreviated_prepositional_year_range, [&](const std::smatch& m) {
        const auto first = parse_ull(m[3].str());
        const auto second = expanded_short_year(first, parse_ull(m[4].str()));
        return m[1].str() + m[2].str() + " період від " + number_to_ordinal_words(first, "gen") + " до " +
               number_to_ordinal_words(second, "gen") + " року";
    });
    static const std::regex abbreviated_year_range(
        R"(\b((?:19|20)\d{2})\s*(?:-|–|—)\s*(\d{2})\s*(?:рр?\.?|роки|роках|року|років)(?![\dа-яіїєґ]))");
    text = regex_sub(text, abbreviated_year_range, [&](const std::smatch& m) {
        const auto first = parse_ull(m[1].str());
        const auto second = expanded_short_year(first, parse_ull(m[2].str()));
        return style == RangeStyle::FromTo
                   ? "від " + number_to_ordinal_words(first, "gen") + " до " + number_to_ordinal_words(second, "gen") +
                         " року"
                   : number_to_ordinal_words(first, "nom_m") + "–" + number_to_ordinal_words(second, "nom_m") + " роки";
    });
    static const std::regex year_range(R"(\b(\d{3,4})\s*(?:-|−|–|—)\s*(\d{3,4})\s*(?:рр\.?|роки)(?![а-яіїєґ]))");
    text = regex_sub(text, year_range, [&](const std::smatch& m) {
        const auto low = parse_ull(m[1].str());
        const auto high = parse_ull(m[2].str());
        if (style == RangeStyle::FromTo) {
            const auto word = preceding_word(m.prefix().str());
            if (word == "на") {
                return "період від " + number_to_ordinal_words(low, "gen") + " до " +
                       number_to_ordinal_words(high, "gen") + " року";
            }
            if (word == "близько" || word == "до" || word == "від") {
                return number_to_ordinal_words(low, "gen") + "–" + number_to_ordinal_words(high, "gen") + " року";
            }
            return "від " + number_to_ordinal_words(low, "gen") + " до " + number_to_ordinal_words(high, "gen") +
                   " року";
        }
        return number_to_ordinal_words(low, "nom_m") + " " + number_to_ordinal_words(high, "nom_m") + " роки";
    });

    static const std::regex time_range(prefix + R"((\d{1,2}):([0-5]\d)(?::([0-5]\d))?\s*)" + separator +
                                       R"(\s*(\d{1,2}):([0-5]\d)(?::([0-5]\d))?(?![\d:]))");
    text = regex_sub(text, time_range, [&](const std::smatch& m) {
        if (parse_ull(m[2].str()) > 23 || parse_ull(m[5].str()) > 23) {
            return m.str();
        }
        const auto low = clock_time_words(m[2].str(), m[3].str(), m[4], style);
        const auto high = clock_time_words(m[5].str(), m[6].str(), m[7], style);
        return m[1].str() + range_connector(style, low, high);
    });

    static const std::regex fraction_range(prefix + R"((\d+)/(\d+)\s*)" + separator + R"(\s*(\d+)/(\d+)(?![\d/]))");
    text = regex_sub(text, fraction_range, [&](const std::smatch& m) {
        const auto n1 = try_parse_ull(m[2].str());
        const auto d1 = try_parse_ull(m[3].str());
        const auto n2 = try_parse_ull(m[4].str());
        const auto d2 = try_parse_ull(m[5].str());
        if (!n1 || !d1 || !n2 || !d2 || !*d1 || !*d2) {
            return m.str();
        }
        auto say = [&](unsigned long long numerator, unsigned long long denominator) {
            if (style != RangeStyle::FromTo) {
                return say_fraction(numerator, denominator);
            }
            return join(number_words_for_case(numerator, "gen", 'f')) + " " +
                   number_to_ordinal_words(denominator, numerator % 10 == 1 && numerator % 100 != 11 ? "gen_f" : "pl");
        };
        return m[1].str() + range_connector(style, say(*n1, *d1), say(*n2, *d2));
    });

    auto say_measurement = [&](const std::smatch& m,
                               std::size_t low_index,
                               std::size_t high_index,
                               std::size_t unit_index,
                               bool explicitly_from_to) {
        const auto& measurement = measurements().at(m[unit_index].str());
        const auto grammatical_case = governed_case(m, explicitly_from_to);
        const auto low = signed_number_words(m[low_index].str(), grammatical_case, measurement.gender);
        const auto high = signed_number_words(m[high_index].str(), grammatical_case, measurement.gender);
        if (!low || !high) {
            return m.str();
        }
        std::string unit(measurement.many);
        const auto context = governed_range(m, explicitly_from_to);
        if ((style == RangeStyle::Compact || context == GovernedRange::On || context == GovernedRange::In) &&
            !explicitly_from_to) {
            const auto upper_text = m[high_index].str();
            auto upper = std::string_view(upper_text);
            take_spoken_sign(upper);
            if (upper.find_first_of(".,") != std::string_view::npos) {
                unit = measurement.few;
            } else if (const auto value = try_parse_ull(upper)) {
                unit = plural(*value, {measurement.one, measurement.few, measurement.many});
            }
        }
        return m[1].str() + governed_connector(m, *low, *high, explicitly_from_to) + " " + unit;
    };
    static const std::regex repeated_unit_range(prefix + "(" + number + ")\\s*(" + unit_alt() + ")\\s*" + separator +
                                                "\\s*(" + number + ")\\s*(" + unit_alt() +
                                                R"()(?![A-Za-zА-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, repeated_unit_range, [&](const std::smatch& m) {
        return m[3].str() == m[5].str() ? say_measurement(m, 2, 4, 5, false) : m.str();
    });
    static const std::regex explicit_repeated_unit_range(prefix + "від\\s+(" + number + ")\\s*(" + unit_alt() +
                                                         ")\\s+до\\s+(" + number + ")\\s*(" + unit_alt() +
                                                         R"()(?![A-Za-zА-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, explicit_repeated_unit_range, [&](const std::smatch& m) {
        return m[3].str() == m[5].str() ? say_measurement(m, 2, 4, 5, true) : m.str();
    });
    static const std::regex explicit_unit_range(prefix + "від\\s+(" + number + ")\\s+до\\s+(" + number + ")\\s*(" +
                                                unit_alt() + R"()(?![A-Za-zА-Яа-яЄєІіЇїҐґ]))");
    text =
        regex_sub(text, explicit_unit_range, [&](const std::smatch& m) { return say_measurement(m, 2, 3, 4, true); });
    static const std::regex explicit_separator_unit_range(prefix + "від\\s+(" + number + ")\\s*" + separator + "\\s*(" +
                                                          number + ")\\s*(" + unit_alt() +
                                                          R"()(?![A-Za-zА-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(
        text, explicit_separator_unit_range, [&](const std::smatch& m) { return say_measurement(m, 2, 3, 4, true); });
    static const std::regex unit_range(prefix + "(" + number + ")\\s*" + separator + "\\s*(" + number + ")\\s*(" +
                                       unit_alt() + R"()(?![A-Za-zА-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, unit_range, [&](const std::smatch& m) { return say_measurement(m, 2, 3, 4, false); });

    auto say_currency = [&](const std::smatch& m,
                            std::size_t low_index,
                            std::size_t high_index,
                            std::size_t currency_index,
                            bool explicitly_from_to) {
        const auto currency = range_currency(m[currency_index].str());
        if (!currency) {
            return m.str();
        }
        const auto grammatical_case = governed_case(m, explicitly_from_to);
        const auto low = signed_number_words(m[low_index].str(), grammatical_case, currency->gender);
        const auto high = signed_number_words(m[high_index].str(), grammatical_case, currency->gender);
        if (!low || !high) {
            return m.str();
        }
        return m[1].str() + governed_connector(m, *low, *high, explicitly_from_to) + " " + std::string(currency->many);
    };
    static const std::regex repeated_prefix_currency(prefix + "(" + currency_token_alt() + ")\\s*(" + number + ")\\s*" +
                                                     separator + "\\s*(" + currency_token_alt() + ")\\s*(" + number +
                                                     ")" + number_boundary);
    text = regex_sub(text, repeated_prefix_currency, [&](const std::smatch& m) {
        const auto first = range_currency(m[2].str());
        const auto second = range_currency(m[4].str());
        return first && second && first->many == second->many ? say_currency(m, 3, 5, 4, false) : m.str();
    });
    static const std::regex prefix_currency_range(prefix + "(" + currency_token_alt() + ")\\s*(" + number + ")\\s*" +
                                                  separator + "\\s*(" + number + ")" + number_boundary);
    text =
        regex_sub(text, prefix_currency_range, [&](const std::smatch& m) { return say_currency(m, 3, 4, 2, false); });
    static const std::regex repeated_suffix_currency(prefix + "(" + number + ")\\s*(" + currency_token_alt() + ")\\s*" +
                                                     separator + "\\s*(" + number + ")\\s*(" + currency_token_alt() +
                                                     R"()(?![A-Za-zА-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, repeated_suffix_currency, [&](const std::smatch& m) {
        const auto first = range_currency(m[3].str());
        const auto second = range_currency(m[5].str());
        return first && second && first->many == second->many ? say_currency(m, 2, 4, 5, false) : m.str();
    });
    static const std::regex explicit_repeated_suffix_currency(
        prefix + "від\\s+(" + number + ")\\s*(" + currency_token_alt() + ")\\s+до\\s+(" + number + ")\\s*(" +
        currency_token_alt() + R"()(?![A-Za-zА-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, explicit_repeated_suffix_currency, [&](const std::smatch& m) {
        const auto first = range_currency(m[3].str());
        const auto second = range_currency(m[5].str());
        return first && second && first->many == second->many ? say_currency(m, 2, 4, 5, true) : m.str();
    });
    static const std::regex explicit_currency_range(prefix + "від\\s+(" + number + ")\\s+до\\s+(" + number + ")\\s*(" +
                                                    currency_token_alt() + R"()(?![A-Za-zА-Яа-яЄєІіЇїҐґ]))");
    text =
        regex_sub(text, explicit_currency_range, [&](const std::smatch& m) { return say_currency(m, 2, 3, 4, true); });
    static const std::regex suffix_currency_range(prefix + "(" + number + ")\\s*" + separator + "\\s*(" + number +
                                                  ")\\s*(" + currency_token_alt() + R"()(?![A-Za-zА-Яа-яЄєІіЇїҐґ]))");
    text =
        regex_sub(text, suffix_currency_range, [&](const std::smatch& m) { return say_currency(m, 2, 3, 4, false); });

    auto say_percent =
        [&](const std::smatch& m, std::size_t low_index, std::size_t high_index, bool explicitly_from_to) {
            const auto grammatical_case = governed_case(m, explicitly_from_to);
            const auto low = signed_number_words(m[low_index].str(), grammatical_case);
            const auto high = signed_number_words(m[high_index].str(), grammatical_case);
            if (!low || !high) {
                return m.str();
            }
            std::string unit = "відсотків";
            const auto context = governed_range(m, explicitly_from_to);
            if (context == GovernedRange::On || context == GovernedRange::In) {
                auto high_token = std::string_view(m[high_index].str());
                take_spoken_sign(high_token);
                if (const auto value = try_parse_ull(high_token)) {
                    unit = plural(*value, {"відсоток", "відсотки", "відсотків"});
                }
            }
            return m[1].str() + governed_connector(m, *low, *high, explicitly_from_to) + " " + unit;
        };
    static const std::regex repeated_percent_range(prefix + "(" + number + ")\\s*%\\s*" + separator + "\\s*(" + number +
                                                   R"()\s*%(?!\w))");
    text = regex_sub(text, repeated_percent_range, [&](const std::smatch& m) { return say_percent(m, 2, 3, false); });
    static const std::regex explicit_repeated_percent_range(prefix + "від\\s+(" + number + ")\\s*%\\s+до\\s*(" +
                                                            number + R"()\s*%(?!\w))");
    text = regex_sub(
        text, explicit_repeated_percent_range, [&](const std::smatch& m) { return say_percent(m, 2, 3, true); });
    static const std::regex explicit_percent_range(prefix + "від\\s+(" + number + ")\\s+до\\s+(" + number +
                                                   R"()\s*%(?!\w))");
    text = regex_sub(text, explicit_percent_range, [&](const std::smatch& m) { return say_percent(m, 2, 3, true); });
    static const std::regex explicit_separator_percent_range(prefix + "від\\s+(" + number + ")\\s*" + separator +
                                                             "\\s*(" + number + R"()\s*%(?!\w))");
    text = regex_sub(
        text, explicit_separator_percent_range, [&](const std::smatch& m) { return say_percent(m, 2, 3, true); });
    static const std::regex percent_range(prefix + "(" + number + ")\\s*" + separator + "\\s*(" + number +
                                          R"()\s*%(?!\w))");
    text = regex_sub(text, percent_range, [&](const std::smatch& m) { return say_percent(m, 2, 3, false); });

    static const std::regex paragraph_range(prefix + R"((?:§§|§)\s*(\d+)\s*)" + separator + R"(\s*(\d+)(?!\d))");
    text = regex_sub(text, paragraph_range, [&](const std::smatch& m) {
        const auto low = parse_ull(m[2].str());
        const auto high = parse_ull(m[3].str());
        if (style == RangeStyle::FromTo) {
            return m[1].str() + "від " + number_to_ordinal_words(low, "gen") + " до " +
                   number_to_ordinal_words(high, "gen") + " параграфа";
        }
        return m[1].str() + "параграфи " + number_to_words(low) + " " + number_to_words(high);
    });

    static const std::regex school_grade_range(prefix + R"((\d{1,2})\s*)" + separator +
                                               R"(\s*(\d{1,2})\s+(класах|класів)(?![А-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, school_grade_range, [&](const std::smatch& m) {
        const auto word = preceding_word(m.prefix().str() + m[1].str());
        const auto noun = m[4].str();
        if (!((noun == "класах" && (word == "у" || word == "в")) || (noun == "класів" && word == "учнів"))) {
            return m.str();
        }
        const auto first = parse_ull(m[2].str());
        const auto second = parse_ull(m[3].str());
        if (!first || !second) {
            return m.str();
        }
        return m[1].str() + number_to_ordinal_words(first, "pl") + "–" + number_to_ordinal_words(second, "pl") + " " +
               noun;
    });

    auto say_bare = [&](const std::smatch& m, bool explicitly_from_to) {
        if (!explicitly_from_to && m[2].str().size() == 4 && m[3].str().size() <= 2) {
            const auto possible_year = try_parse_ull(m[2].str());
            if (possible_year && *possible_year >= 1000 && *possible_year <= 2999) {
                return m.str();
            }
        }
        if (governed_range(m, explicitly_from_to) == GovernedRange::In && m.suffix().str().starts_with(" класах")) {
            const auto low = try_parse_ull(m[2].str());
            const auto high = try_parse_ull(m[3].str());
            if (low && high) {
                return m[1].str() + number_to_words_case(*low, "prep") + "–" + number_to_words_case(*high, "prep");
            }
        }
        const auto grammatical_case = governed_case(m, explicitly_from_to);
        const auto gender =
            governed_range(m, explicitly_from_to) == GovernedRange::In && m.suffix().str().starts_with(" лінії") ? 'f'
                                                                                                                 : 'm';
        const auto low = signed_number_words(m[2].str(), grammatical_case, gender);
        const auto high = signed_number_words(m[3].str(), grammatical_case, gender);
        return low && high ? m[1].str() + governed_connector(m, *low, *high, explicitly_from_to) : m.str();
    };
    static const std::regex explicit_bare_range(prefix + "від\\s+(" + number + ")\\s+до\\s+(" + number + ")" +
                                                number_boundary + "(?!\\s+(?:" + month_alt() + R"()(?:\s|$)))");
    text = regex_sub(text, explicit_bare_range, [&](const std::smatch& m) { return say_bare(m, true); });
    static const std::regex explicit_separator_bare_range(prefix + "від\\s+(" + number + ")\\s*" + separator + "\\s*(" +
                                                          number + ")" + number_boundary);
    text = regex_sub(text, explicit_separator_bare_range, [&](const std::smatch& m) { return say_bare(m, true); });
    static const std::regex approximate_bare_range(prefix + "(Понад|понад)\\s+(" + number + ")\\s*" + separator +
                                                   "\\s*(" + number + ")" + number_boundary);
    text = regex_sub(text, approximate_bare_range, [](const std::smatch& m) {
        const auto low = signed_number_words(m[3].str(), "nom");
        const auto high = signed_number_words(m[4].str(), "nom");
        return low && high ? m[1].str() + m[2].str() + " " + *low + " чи " + *high : m.str();
    });
    static const std::regex bare_range(prefix + "(" + number + ")\\s*" + separator + "\\s*(" + number + ")" +
                                       number_boundary + "(?!\\s+(?:" + month_alt() + R"()(?:\s|$)))");
    return regex_sub(text, bare_range, [&](const std::smatch& m) { return say_bare(m, false); });
}
std::string normalize_case_context(std::string text)
{
    static const std::unordered_map<std::string, std::string_view> prep_case = {{"близько", "gen"},
                                                                                {"менше", "gen"},
                                                                                {"більше", "gen"},
                                                                                {"серед", "gen"},
                                                                                {"від", "gen"},
                                                                                {"до", "gen"},
                                                                                {"із", "gen"},
                                                                                {"з", "instr"},
                                                                                {"без", "gen"},
                                                                                {"після", "gen"},
                                                                                {"протягом", "gen"},
                                                                                {"перед", "instr"},
                                                                                {"між", "instr"},
                                                                                {"над", "instr"},
                                                                                {"під", "instr"},
                                                                                {"при", "prep"},
                                                                                {"к", "dat"},
                                                                                {"о", "prep"},
                                                                                {"об", "prep"}};
    static const std::regex quantified_genitive(R"((^|[\s(\[{:;,.!?])((?:З|з))\s+(\d+)\s+([^\s,.;:!?]+))");
    static const std::regex comparative_genitive(
        R"((^|[^А-Яа-яЄєІіЇїҐґ])(Після|після|До|до|Від|від|Без|без)\s+(більш|менш)\s+ніж\s+(\d+)(?!\d))");
    static const std::regex instr(
        R"((^|[^А-Яа-яЄєІіЇїҐґ])([Зз])\s+(\d+)\s+([а-яєіїґ']{3,}(?:ами|ями|ма))(?![А-Яа-яЄєІіЇїҐґ]))");
    static const std::string oblique_nouns = [] {
        std::vector<std::string> keys;
        keys.reserve(counted_oblique_cases().size());
        for (const auto& [key, _] : counted_oblique_cases()) {
            keys.push_back(key);
        }
        return regex_alternation(std::move(keys));
    }();
    static const std::regex oblique("(^|[^А-Яа-яЄєІіЇїҐґ])(У|у|В|в|На|на)\\s+(\\d+)\\s+(" + oblique_nouns +
                                        R"()(?![А-Яа-яЄєІіЇїҐґ]))",
                                    std::regex::icase);
    static const std::regex before_locative_adjective(R"((^|[\s(\[{:;,.!?])(У|у|В|в|На|на)\s+(\d+)\s+([^\s,.;:!?]+))");
    static const std::regex ponad_quantity(R"((^|[^А-Яа-яЄєІіЇїҐґ])(Понад|понад)\s+(\d+)(?!\d))");
    text = regex_sub(text, ponad_quantity, [](const std::smatch& m) {
        auto words = number_to_words(parse_ull(m[3].str()));
        if (words.starts_with("тисяча ")) {
            words.replace(0, std::string("тисяча").size(), "тисячу");
        }
        return m[1].str() + m[2].str() + " " + words;
    });
    text = regex_sub(text, quantified_genitive, [](const std::smatch& m) {
        const auto noun = lower_text(m[4].str());
        if (!noun.ends_with("ів") && !noun.ends_with("їв")) {
            return m.str();
        }
        return m[1].str() + m[2].str() + " " + number_to_words_case(parse_ull(m[3].str()), "gen") + " " + m[4].str();
    });
    text = regex_sub(text, comparative_genitive, [](const std::smatch& m) {
        return m[1].str() + m[2].str() + " " + m[3].str() + " ніж " +
               number_to_words_case(parse_ull(m[4].str()), "gen");
    });
    text = regex_sub(text, instr, [](const std::smatch& m) {
        return m[1].str() + m[2].str() + " " + number_to_words_case(parse_ull(m[3].str()), "instr") + " " + m[4].str();
    });
    text = regex_sub(text, before_locative_adjective, [](const std::smatch& m) {
        const auto count = parse_ull(m[3].str());
        const auto adjective = lower_text(m[4].str());
        if (count == 1 || (!adjective.ends_with("ому") && !adjective.ends_with("ьому") && !adjective.ends_with("ій") &&
                           !adjective.ends_with("их"))) {
            return m.str();
        }
        return m[1].str() + m[2].str() + " " + number_to_words_case(count, "prep") + " " + m[4].str();
    });
    text = regex_sub(text, case_prep_re(), [&](const std::smatch& m) {
        const auto p = lower_text(m[2].str());
        const auto c = prep_case.at(p);
        if (m[4].matched && c != "gen") {
            return m.str();
        }
        std::string out = m[1].str() + m[2].str() + " " + number_to_words_case(parse_ull(m[3].str()), c);
        if (m[4].matched) {
            out += " " + std::string(measurements().at(m[4].str()).many);
            out += m[5].str();
        }
        return out;
    });
    return regex_sub(text, oblique, [](const std::smatch& m) {
        const auto noun = m[4].str();
        const auto it = counted_oblique_cases().find(lower_text(noun));
        if (it == counted_oblique_cases().end()) {
            return m.str();
        }
        return m[1].str() + m[2].str() + " " + number_to_words_case(parse_ull(m[3].str()), it->second) + " " + noun;
    });
}
std::string normalize_counted_noun_context(std::string text)
{
    text = regex_sub(text, counted_ponad_re(), [](const std::smatch& m) {
        const auto n = parse_ull(m[3].str());
        const auto& noun = counted_nouns().at(lower_text(m[4].str()));
        return m[1].str() + m[2].str() + " " + number_words_for_gender(n, noun.gender) + " " +
               plural(n, {noun.one, noun.few, noun.many});
    });
    return regex_sub(text, counted_genitive_re(), [](const std::smatch& m) {
        const auto n = parse_ull(m[3].str());
        const auto& noun = counted_nouns().at(lower_text(m[4].str()));
        if (!prefers_many_after_genitive_number(n)) {
            return m.str();
        }
        return m[1].str() + m[2].str() + " " + number_to_words_case(n, "gen") + " " + std::string(noun.many);
    });
}

std::string normalize_counted_nouns(std::string text)
{
    return regex_sub(text, counted_nouns_re(), [](const std::smatch& m) {
        const auto n = parse_ull(m[2].str());
        const auto key = lower_text(m[3].str());
        const auto& noun = counted_nouns().at(key);
        return m[1].str() + number_words_for_gender(n, noun.gender) + " " + plural(n, {noun.one, noun.few, noun.many});
    });
}

std::string normalize_ordinal_triggers(std::string text)
{
    static const std::regex genitive_class(R"((^|[^\dА-Яа-яЄєІіЇїҐґ])(\d{1,2})\s+(класу)(?![А-Яа-яЄєІіЇїҐґ]))",
                                           std::regex::icase);
    text = regex_sub(text, genitive_class, [](const std::smatch& m) {
        const auto number = parse_ull(m[2].str());
        return number == 0 ? m.str() : m[1].str() + number_to_ordinal_words(number, "gen") + " " + m[3].str();
    });
    static const std::unordered_map<std::string, std::string_view> triggers = {{"місце", "nom_n"},
                                                                               {"село", "nom_n"},
                                                                               {"місто", "nom_n"},
                                                                               {"століття", "nom_n"},
                                                                               {"ст", "nom_n"},
                                                                               {"клас", "nom_m"},
                                                                               {"курс", "nom_m"},
                                                                               {"раунд", "nom_m"},
                                                                               {"сезон", "nom_m"},
                                                                               {"етап", "nom_m"},
                                                                               {"тур", "nom_m"},
                                                                               {"том", "nom_m"},
                                                                               {"під'їзд", "nom_m"},
                                                                               {"поверх", "nom_m"},
                                                                               {"група", "nom_f"},
                                                                               {"квартира", "nom_f"},
                                                                               {"сторінка", "nom_f"}};
    static const std::regex re(
        R"((^|[^\d])(\d{1,4})\s+(місце|село|місто|століття|ст|клас|курс|раунд|сезон|етап|тур|том|під'їзд|поверх|група|квартира|сторінка)(?![А-Яа-яЄєІіЇїҐґ]))",
        std::regex::icase);
    return regex_sub(text, re, [&](const std::smatch& m) {
        const auto noun = lower_text(m[3].str());
        return m[1].str() + number_to_ordinal_words(parse_ull(m[2].str()), triggers.at(noun)) + " " + m[3].str();
    });
}
std::string normalize_compounds(std::string text)
{
    static const std::regex re(
        R"((^|[^\d])(\d+)-(?!(?:ший|ими|им|ім|ою|ій|ше|ша|ге|га|тє|тя|го|му|й|м|а|у|е|х)(?:[^А-Яа-яЄєІіЇїҐґ]|$))([^0-9A-Za-z\s,.;:!?()]+))");
    return regex_sub(text, re, [](const std::smatch& m) {
        std::string prefix;
        for (const auto& w : split_words(number_to_words(parse_ull(m[2].str())))) {
            if (const auto it = compound_prefix_forms().find(w); it != compound_prefix_forms().end()) {
                prefix += it->second;
            } else if (const auto cf = case_forms().find(w); cf != case_forms().end()) {
                prefix += cf->second[0];
            } else {
                prefix += w;
            }
        }
        return m[1].str() + prefix + m[3].str();
    });
}
std::string normalize_time(std::string text, ColonStyle colon_style)
{
    auto clock_words = [](int hour, int minute, std::optional<int> second = std::nullopt) {
        if ((hour == 0 || hour == 24) && minute == 0 && (!second || *second == 0)) {
            return std::string("опівночі");
        }
        std::string out = hours_words(hour);
        if (minute) {
            out += " " + minutes_words(minute);
        }
        if (second && *second) {
            out += " " + minutes_words(*second, {"секунда", "секунди", "секунд"});
        }
        return out;
    };
    static const std::regex variable_ratio(R"((^|[^A-Za-z\d:])(\d+)\s*:\s*([A-Za-z])(?![A-Za-z\d]))");
    text = regex_sub(text, variable_ratio, [](const std::smatch& m) {
        return m[1].str() + number_to_words(parse_ull(m[2].str())) + " до " + spell_identifier_letters(m[3].str());
    });
    static const std::regex am_pm(R"((^|[^\d:])(\d{1,2}):([0-5]\d)(?::([0-5]\d))?\s*(a\.?m\.?|p\.?m\.?)(?![A-Za-z]))",
                                  std::regex::icase);
    text = regex_sub(text, am_pm, [&](const std::smatch& m) {
        const auto hour = parse_int(m[2].str());
        if (hour < 1 || hour > 12) {
            return m.str();
        }
        const auto period = lower_text(m[5].str());
        const bool pm = period.starts_with("p");
        const auto clock_hour = hour == 12 ? (pm ? 12 : 0) : hour;
        const auto suffix = !pm && hour == 12                ? ""
                            : pm && (hour < 6 || hour == 12) ? " дня"
                            : pm                             ? " вечора"
                            : hour < 5                       ? " ночі"
                                                             : " ранку";
        return m[1].str() +
               clock_words(clock_hour,
                           parse_int(m[3].str()),
                           m[4].matched ? std::optional<int>(parse_int(m[4].str())) : std::nullopt) +
               suffix;
    });
    static const std::regex zoned(
        R"((^|[^\d:])(\d{1,2}):([0-5]\d)(?::([0-5]\d))?\s*(UTC|GMT)(?:\s*([+-])\s*(\d{1,2})(?::?([0-5]\d))?)?(?![A-Za-z\d]))",
        std::regex::icase);
    text = regex_sub(text, zoned, [&](const std::smatch& m) {
        const auto hour = parse_int(m[2].str());
        const auto offset = m[7].matched ? parse_int(m[7].str()) : 0;
        const auto offset_minutes = m[8].matched ? parse_int(m[8].str()) : 0;
        if (hour > 23 || offset > 14 || (offset == 14 && offset_minutes != 0)) {
            return m.str();
        }
        std::string out = m[1].str() +
                          clock_words(hour,
                                      parse_int(m[3].str()),
                                      m[4].matched ? std::optional<int>(parse_int(m[4].str())) : std::nullopt) +
                          " за всесвітнім координованим часом";
        if (m[6].matched) {
            out += " " + std::string(m[6].str() == "+" ? "плюс " : "мінус ") + hours_words(offset);
            if (m[8].matched && parse_int(m[8].str())) {
                out += " " + minutes_words(parse_int(m[8].str()));
            }
        }
        return out;
    });
    static const std::regex offset_zoned(
        R"((^|[^\d:])(\d{1,2}):([0-5]\d)(?::([0-5]\d))?\s+([+-])(\d{2}):([0-5]\d)(?!\d))");
    text = regex_sub(text, offset_zoned, [&](const std::smatch& m) {
        const auto hour = parse_int(m[2].str());
        const auto offset = parse_int(m[6].str());
        const auto offset_minutes = parse_int(m[7].str());
        if (hour > 23 || offset > 14 || (offset == 14 && offset_minutes != 0)) {
            return m.str();
        }
        std::string out = m[1].str() +
                          clock_words(hour,
                                      parse_int(m[3].str()),
                                      m[4].matched ? std::optional<int>(parse_int(m[4].str())) : std::nullopt) +
                          " за часовим поясом " + (m[5].str() == "+" ? "плюс " : "мінус ") + hours_words(offset);
        if (offset_minutes) {
            out += " " + minutes_words(offset_minutes);
        }
        return out;
    });
    static const std::regex iana_zoned(
        R"((^|[^\d:])(\d{1,2}):([0-5]\d)(?::([0-5]\d))?\s+([A-Za-z_+-]+/[A-Za-z0-9_+/-]+)(?![A-Za-z0-9_+/-]))",
        std::regex::icase);
    text = regex_sub(text, iana_zoned, [&](const std::smatch& m) {
        static const std::unordered_map<std::string, std::string_view> zone_names = {
            {"europe/kyiv", "за київським часом"},
            {"europe/london", "за лондонським часом"},
            {"europe/warsaw", "за варшавським часом"},
            {"america/new_york", "за нью-йоркським часом"},
            {"america/los_angeles", "за лос-анджелеським часом"},
            {"asia/tokyo", "за токійським часом"}};
        const auto hour = parse_int(m[2].str());
        const auto zone = zone_names.find(lower_text(m[5].str()));
        if (hour > 23 || zone == zone_names.end()) {
            return m.str();
        }
        return m[1].str() +
               clock_words(hour,
                           parse_int(m[3].str()),
                           m[4].matched ? std::optional<int>(parse_int(m[4].str())) : std::nullopt) +
               " " + std::string(zone->second);
    });
    static const std::regex hms(R"((^|[^\d:])(\d{1,2}):([0-5]\d):([0-5]\d)(?![\d:]))");
    text = regex_sub(text, hms, [&](const std::smatch& m) {
        const auto hour = parse_int(m[2].str());
        if (hour > 23) {
            return m.str();
        }
        return m[1].str() + clock_words(hour, parse_int(m[3].str()), parse_int(m[4].str()));
    });
    text = ctre_sub<R"((^|[^А-Яа-яЄєІіЇїҐґ\d])((?:О|о)(?:б)?) (\d{1,2})(?:-|–|—)?(?:й|ій|а|ої)(?![А-Яа-яЄєІіЇїҐґ]))">(
        text, [](const auto& m) {
            return cap_string<1>(m) + cap_string<2>(m) + " " + number_to_ordinal_words(parse_ull(cap<3>(m)), "nom_f") +
                   " година";
        });
    static const std::regex day_period(
        R"((^|[^\d:])(\d{1,2}):([0-5]\d)\s+(ранку|дня|вечора|ночі)(?![А-Яа-яЄєІіЇїҐґ\d:]))", std::regex::icase);
    text = regex_sub(text, day_period, [&](const std::smatch& m) {
        const auto hour = parse_int(m[2].str());
        return hour <= 23 ? m[1].str() + clock_words(hour, parse_int(m[3].str())) + " " + m[4].str() : m.str();
    });
    static const std::regex hm(R"((^|[^\d:])(\d{1,2}):([0-5]\d)(?![\d:]))");
    text = regex_sub(text, hm, [&](const std::smatch& m) {
        if (colon_style == ColonStyle::Ratio) {
            return m.str();
        }
        const auto hour = parse_int(m[2].str());
        if (hour == 24 && parse_int(m[3].str()) == 0) {
            return m[1].str() + clock_words(hour, 0);
        }
        return hour <= 23 ? m[1].str() + clock_words(hour, parse_int(m[3].str())) : m.str();
    });
    static const std::regex ratio(R"((^|[^\d:])(\d+):(\d+)(?![\d:]))");
    return regex_sub(text, ratio, [&](const std::smatch& m) {
        if (colon_style == ColonStyle::Clock) {
            const auto hour = try_parse_ull(m[2].str());
            const auto minute = try_parse_ull(m[3].str());
            if (hour && minute && *hour <= 23 && *minute <= 59) {
                return m[1].str() + clock_words(static_cast<int>(*hour), static_cast<int>(*minute));
            }
            return m.str();
        }
        const auto left = try_parse_ull(m[2].str());
        const auto right = try_parse_ull(m[3].str());
        return left && right ? m[1].str() + number_to_words(*left) + " до " + number_to_words_case(*right, "gen")
                             : m.str();
    });
}
std::string normalize_fractions(std::string text)
{
    static const std::unordered_map<std::string, std::pair<int, int>> vulgar = {{"½", {1, 2}},
                                                                                {"⅓", {1, 3}},
                                                                                {"⅔", {2, 3}},
                                                                                {"¼", {1, 4}},
                                                                                {"¾", {3, 4}},
                                                                                {"⅕", {1, 5}},
                                                                                {"⅖", {2, 5}},
                                                                                {"⅗", {3, 5}},
                                                                                {"⅘", {4, 5}},
                                                                                {"⅙", {1, 6}},
                                                                                {"⅚", {5, 6}},
                                                                                {"⅐", {1, 7}},
                                                                                {"⅛", {1, 8}},
                                                                                {"⅜", {3, 8}},
                                                                                {"⅝", {5, 8}},
                                                                                {"⅞", {7, 8}},
                                                                                {"⅑", {1, 9}},
                                                                                {"⅒", {1, 10}}};
    for (const auto& [sym, nd] : vulgar) {
        const std::regex measured_mixed("(\\d+)\\s*" + sym + "\\s*(" + unit_alt() + R"()(?![A-Za-zА-Яа-яЄєІіЇїҐґ]))");
        text = regex_sub(text, measured_mixed, [&](const std::smatch& m) {
            return number_words_for_gender(parse_ull(m[1].str()), 'f') + " цілих і " +
                   say_fraction(nd.first, nd.second) + " " + std::string(measurements().at(m[2].str()).decimal);
        });
        const std::regex measured_vulgar("(^|[^\\d])" + sym + "\\s*(" + unit_alt() + R"()(?![A-Za-zА-Яа-яЄєІіЇїҐґ]))");
        text = regex_sub(text, measured_vulgar, [&](const std::smatch& m) {
            return m[1].str() + say_fraction(nd.first, nd.second) + " " +
                   std::string(measurements().at(m[2].str()).decimal);
        });
        const std::regex mixed("(\\d+)\\s*" + sym);
        text = regex_sub(text, mixed, [&](const std::smatch& m) {
            return number_words_for_gender(parse_ull(m[1].str()), 'f') + " цілих і " +
                   say_fraction(nd.first, nd.second);
        });
        replace_all(text, sym, " " + say_fraction(nd.first, nd.second));
    }
    static const std::regex measured_fraction("(^|[^\\d.,/])([+\\-]?)(\\d+)/(\\d+)\\s*(" + unit_alt() +
                                              R"()(?![A-Za-zА-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, measured_fraction, [](const std::smatch& m) {
        const auto numerator = try_parse_ull(m[3].str());
        const auto denominator = try_parse_ull(m[4].str());
        if (!numerator || !denominator || !*denominator) {
            return m.str();
        }
        const auto sign = m[2].str() == "-" ? "мінус " : m[2].str() == "+" ? "плюс " : "";
        return m[1].str() + sign + say_fraction(*numerator, *denominator) + " " +
               std::string(measurements().at(m[5].str()).decimal);
    });
    text = ctre_sub<R"((^|[^\d.,/])([+\-−]?)(\d+) (\d+)/(\d+)\b)">(text, [](const auto& m) {
        const auto whole = try_parse_ull(cap<3>(m));
        const auto numerator = try_parse_ull(cap<4>(m));
        const auto denominator = try_parse_ull(cap<5>(m));
        if (!whole || !numerator || !denominator || !*denominator) {
            return whole_string(m);
        }
        const auto sign = cap<2>(m) == "-" || cap<2>(m) == "−" ? "мінус " : cap<2>(m) == "+" ? "плюс " : "";
        return cap_string<1>(m) + sign + number_words_for_gender(*whole, 'f') + " і " +
               say_fraction(*numerator, *denominator);
    });
    return ctre_sub<R"((^|[^\d.,/])([+\-−]?)(\d+)/(\d+)\b)">(text, [](const auto& m) {
        const auto numerator = try_parse_ull(cap<3>(m));
        const auto denominator = try_parse_ull(cap<4>(m));
        if (!numerator || !denominator || !*denominator) {
            return whole_string(m);
        }
        const auto sign = cap<2>(m) == "-" || cap<2>(m) == "−" ? "мінус " : cap<2>(m) == "+" ? "плюс " : "";
        return cap_string<1>(m) + sign + say_fraction(*numerator, *denominator);
    });
}

std::string normalize_percent(std::string text)
{
    static const std::regex governed(
        R"((^|[^А-Яа-яЄєІіЇїҐґ-])(Близько|близько|Після|після|Менше|менше|Більше|більше|Без|без|Від|від|До|до|Із|із)\s+([+\-]?\d+(?:[.,]\d+)?)\s*%)");
    text = regex_sub(text, governed, [](const std::smatch& m) {
        auto token = m[3].str();
        const auto words = signed_number_words(token, "gen");
        if (!words) {
            return m.str();
        }
        const auto unit = token.find_first_of(".,") == std::string::npos ? "відсотків" : "відсотка";
        return m[1].str() + m[2].str() + " " + *words + " " + unit;
    });
    static const std::regex adjacent_signed(R"(([+\-])(\d+(?:[.,]\d+)?)\s*%)");
    text = regex_sub(text, adjacent_signed, [](const std::smatch& m) {
        const auto number = signed_number_words(m[1].str() + m[2].str(), "nom");
        if (!number) {
            return m.str();
        }
        const auto token = m[2].str();
        const auto unsigned_number = std::string_view(token);
        const auto decimal = unsigned_number.find_first_of(".,");
        if (decimal != std::string_view::npos) {
            return " " + *number + " відсотка";
        }
        const auto value = try_parse_ull(unsigned_number);
        return value ? " " + *number + " " + plural(*value, {"відсоток", "відсотки", "відсотків"}) : m.str();
    });
    static const std::regex percent(R"((^|[^\d.,+\-])([+\-]?\d+(?:[.,]\d+)?)\s*%)");
    return regex_sub(text, percent, [](const std::smatch& m) {
        auto num = m[2].str();
        auto unsigned_num = std::string_view(num);
        const auto words = signed_number_words(unsigned_num, "nom");
        if (!words) {
            const auto sign = take_spoken_sign(unsigned_num);
            if (unsigned_num.find_first_of(".,") == std::string_view::npos) {
                return m[1].str() + sign + number_to_words_digit_by_digit(unsigned_num) + " відсотків";
            }
            return m.str();
        }
        take_spoken_sign(unsigned_num);
        const auto pos = num.find_first_of(".,");
        if (pos != std::string::npos) {
            return m[1].str() + *words + " відсотка";
        }
        const auto n = try_parse_ull(unsigned_num);
        if (!n) {
            return m.str();
        }
        return m[1].str() + *words + " " + plural(*n, {"відсоток", "відсотки", "відсотків"});
    });
}
std::string normalize_measurements(std::string text)
{
    static const std::string atomic_unit_alt = [] {
        std::vector<std::string> keys;
        for (const auto& [key, _] : measurements()) {
            if (key.find('/') == std::string_view::npos && key.find("·") == std::string_view::npos &&
                key.find('-') == std::string_view::npos && key.find(' ') == std::string_view::npos) {
                keys.emplace_back(key);
            }
        }
        return regex_alternation(std::move(keys));
    }();
    static const std::string atom_pattern = "(?:" + atomic_unit_alt + ")(?:²|³|2|3)?";
    static const std::regex tolerance("(^|[^\\d.,])(" + signed_number_pattern() + ")\\s*±\\s*(" +
                                      signed_number_pattern() + ")\\s*(" + unit_alt() +
                                      R"()(?![A-Za-zА-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, tolerance, [](const std::smatch& m) {
        const auto& measurement = measurements().at(m[4].str());
        const auto base = signed_number_words(m[2].str(), "nom", measurement.gender);
        return base ? m[1].str() + *base + " плюс мінус " + read_measurement_quantity(m[3].str(), measurement)
                    : m.str();
    });
    static const std::regex parenthesized_denominator(
        "(^|[^\\d.,])(" + signed_number_pattern() + ")\\s*(" + atom_pattern + ")\\s*/\\s*\\(\\s*(" + atom_pattern +
        ")\\s*(?:·|\\*)\\s*(" + atom_pattern + R"()\s*\)(?![A-Za-zА-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, parenthesized_denominator, [](const std::smatch& m) {
        const auto first = measurements().find(m[3].str());
        const auto second = measurements().find(m[4].str());
        const auto third = measurements().find(m[5].str());
        if (first == measurements().end() || second == measurements().end() || third == measurements().end()) {
            return m.str();
        }
        return m[1].str() + read_measurement_quantity(m[2].str(), first->second) + " поділити на " +
               std::string(second->second.one) + " помножити на " + std::string(third->second.one);
    });
    static const std::regex formula("(^|[^\\d.,])([+\\-−]?\\d+(?:[.,]\\d+)?)\\s*(" + atom_pattern +
                                    "\\s*(?:·|\\*|/)\\s*" + atom_pattern + "(?:\\s*(?:·|\\*|/)\\s*" + atom_pattern +
                                    ")*)(?!\\s*/)(?![A-Za-zА-Яа-яЄєІіЇїҐґ])");
    static const std::regex atom(atom_pattern);
    auto factor_words = [](std::string factor, const std::string& quantity, bool first, bool denominator) {
        const Measurement* measurement = nullptr;
        std::string exponent;
        if (const auto it = measurements().find(factor); it != measurements().end()) {
            measurement = &it->second;
        } else {
            for (const auto& [suffix, words] : {std::pair{std::string_view("²"), std::string_view(" у квадраті")},
                                                std::pair{std::string_view("³"), std::string_view(" у кубі")},
                                                std::pair{std::string_view("2"), std::string_view(" у квадраті")},
                                                std::pair{std::string_view("3"), std::string_view(" у кубі")}}) {
                if (!factor.ends_with(suffix)) {
                    continue;
                }
                factor.erase(factor.size() - suffix.size());
                if (const auto it = measurements().find(factor); it != measurements().end()) {
                    measurement = &it->second;
                    exponent = words;
                }
                break;
            }
        }
        if (!measurement) {
            return std::optional<std::string>{};
        }
        if (first) {
            return std::optional<std::string>{read_measurement_quantity(quantity, *measurement) + exponent};
        }
        std::string unit(measurement->one);
        if (denominator) {
            static const std::unordered_map<std::string, std::string_view> accusative = {{"секунда", "секунду"},
                                                                                         {"хвилина", "хвилину"},
                                                                                         {"година", "годину"},
                                                                                         {"миля", "милю"},
                                                                                         {"тонна", "тонну"},
                                                                                         {"унція", "унцію"},
                                                                                         {"атмосфера", "атмосферу"}};
            if (const auto it = accusative.find(unit); it != accusative.end()) {
                unit = it->second;
            }
        }
        return std::optional<std::string>{unit + exponent};
    };
    text = regex_sub(text, formula, [&](const std::smatch& m) {
        const auto expression = m[3].str();
        if (const auto direct = measurements().find(expression); direct != measurements().end()) {
            return m[1].str() + read_measurement_quantity(m[2].str(), direct->second);
        }
        std::string out = m[1].str();
        std::size_t previous_end = 0;
        bool denominator = false;
        bool first = true;
        for (std::sregex_iterator it(expression.begin(), expression.end(), atom), end; it != end; ++it) {
            const auto position = static_cast<std::size_t>((*it).position());
            const auto separator = expression.substr(previous_end, position - previous_end);
            if (separator.find('/') != std::string::npos) {
                denominator = true;
            }
            const auto words = factor_words(it->str(), m[2].str(), first, denominator);
            if (!words) {
                return m.str();
            }
            if (!first) {
                out += denominator && separator.find('/') != std::string::npos ? " поділити на " : " помножити на ";
            }
            out += *words;
            first = false;
            previous_end = position + static_cast<std::size_t>((*it).length());
        }
        return first ? m.str() : out;
    });
    return regex_sub(text, measurements_re(), [](const std::smatch& m) {
        const auto& meas = measurements().at(m[3].str());
        return m[1].str() + read_measurement_quantity(m[2].str(), meas) + m[4].str();
    });
}

std::string normalize_medical(std::string text)
{
    static const std::regex concentration(
        R"((^|[^\d.,])(\d+(?:[.,]\d+)?)\s*(мг|мл|г)\s*/\s*(мл|л)(?![A-Za-zА-Яа-яЄєІіЇїҐґ]))", std::regex::icase);
    static const std::regex pressure(R"((^|[^\d.,])(\d{2,3})\s*/\s*(\d{2,3})\s*мм\s*рт\.?\s*ст\.?)", std::regex::icase);
    static const std::regex labelled_pressure(
        R"((^|[^А-Яа-яЄєІіЇїҐґ\d])(тиск\s+)(\d{2,3})\s*/\s*(\d{2,3})\s*мм\s*рт\.?\s*ст\.?)", std::regex::icase);
    static const std::regex frequency(
        R"((^|[^А-Яа-яЄєІіЇїҐґ\d])(\d+)\s*(?:р\.|раз(?:и|ів)?)(\s+на\s+(?:день|добу|тиждень|місяць))(?![А-Яа-яЄєІіЇїҐґ]))",
        std::regex::icase);
    text = regex_sub(text, concentration, [](const std::smatch& m) {
        const auto from = lower_text(m[3].str());
        const auto to = lower_text(m[4].str());
        return m[1].str() + read_measurement_quantity(m[2].str(), measurements().at(from)) + " на " +
               std::string(measurements().at(to).one);
    });
    text = regex_sub(text, labelled_pressure, [](const std::smatch& m) {
        return m[1].str() + m[2].str() + number_to_words(parse_ull(m[3].str())) + " на " +
               number_to_words(parse_ull(m[4].str())) + " міліметрів ртутного стовпа";
    });
    text = regex_sub(text, pressure, [](const std::smatch& m) {
        return m[1].str() + number_to_words(parse_ull(m[2].str())) + " на " + number_to_words(parse_ull(m[3].str())) +
               " міліметрів ртутного стовпа";
    });
    static const std::regex temperature_tolerance(range_prefix_pattern() + "(" + signed_number_pattern() +
                                                  ")\\s*±\\s*(" + signed_number_pattern() + ")\\s*(" +
                                                  temperature_unit_pattern() + R"()(?![A-Za-zА-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, temperature_tolerance, [](const std::smatch& m) {
        const auto scale = temperature_scale(m[4].str());
        const auto base = signed_number_words(m[2].str(), "nom");
        const auto tolerance = scale ? temperature_quantity_words(m[3].str(), *scale) : std::nullopt;
        return base && tolerance ? m[1].str() + *base + " плюс мінус " + *tolerance : m.str();
    });
    static const std::regex governed_temperature("(^|[^А-Яа-яЄєІіЇїҐґ-])(Близько|близько|Після|після|Менше|менше|"
                                                 "Більше|більше|Без|без|Від|від|До|до|Із|із)\\s+(" +
                                                 signed_number_pattern() + ")\\s*(" + temperature_unit_pattern() +
                                                 R"()(?![A-Za-zА-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, governed_temperature, [](const std::smatch& m) {
        const auto scale = temperature_scale(m[4].str());
        const auto words = scale ? temperature_quantity_words(m[3].str(), *scale, "gen") : std::nullopt;
        return words ? m[1].str() + m[2].str() + " " + *words : m.str();
    });
    static const std::regex temperature(range_prefix_pattern() + "(" + signed_number_pattern() + ")\\s*(" +
                                        temperature_unit_pattern() + R"()(?![A-Za-zА-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, temperature, [](const std::smatch& m) {
        const auto scale = temperature_scale(m[3].str());
        const auto words = scale ? temperature_quantity_words(m[2].str(), *scale) : std::nullopt;
        return words ? m[1].str() + *words : m.str();
    });
    text = regex_sub(text, frequency, [](const std::smatch& m) {
        const auto n = parse_ull(m[2].str());
        return m[1].str() + number_to_words(n) + " " + plural(n, {"раз", "рази", "разів"}) + m[3].str();
    });
    return ctre_sub<R"((^|[^А-Яа-яЄєІіЇїҐґ\d])№\s*(\d{1,4})(?![\d/]))">(
        text, [](const auto& m) { return cap_string<1>(m) + "номер " + number_to_words(parse_ull(cap<2>(m))); });
}
std::string normalize_scientific(std::string text, RangeStyle range_style)
{
    text = [&] {
        static const std::unordered_map<char32_t, char> superscript = {{U'⁰', '0'},
                                                                       {U'¹', '1'},
                                                                       {U'²', '2'},
                                                                       {U'³', '3'},
                                                                       {U'⁴', '4'},
                                                                       {U'⁵', '5'},
                                                                       {U'⁶', '6'},
                                                                       {U'⁷', '7'},
                                                                       {U'⁸', '8'},
                                                                       {U'⁹', '9'},
                                                                       {U'⁻', '-'},
                                                                       {U'⁺', '+'}};
        std::string out;
        bool in_exponent = false;
        for (std::size_t i = 0; i < text.size();) {
            std::size_t next = i + 1;
            const auto cp = decode_one(text, i, next);
            if (const auto it = superscript.find(cp); it != superscript.end()) {
                if (!in_exponent) {
                    if (out.empty() || !std::isdigit(static_cast<unsigned char>(out.back()))) {
                        out.append(text, i, next - i);
                        i = next;
                        continue;
                    }
                    out.push_back('^');
                    in_exponent = true;
                }
                out.push_back(it->second);
            } else {
                in_exponent = false;
                out.append(text, i, next - i);
            }
            i = next;
        }
        return out;
    }();
    auto exponent_words = [](std::string token) {
        if (token.starts_with("−")) {
            token.replace(0, std::string_view("−").size(), "-");
        }
        std::string sign;
        if (!token.empty() && (token.front() == '-' || token.front() == '+')) {
            sign = token.front() == '-' ? "мінус " : "плюс ";
            token.erase(token.begin());
        }
        const auto value = try_parse_ull(token);
        return value ? std::optional<std::string>(sign + number_to_words(*value)) : std::nullopt;
    };
    auto scientific_words = [&](std::string_view base, std::string exponent) {
        const auto base_words = signed_number_words(base, "nom");
        const auto exponent_text = exponent_words(std::move(exponent));
        return base_words && exponent_text
                   ? std::optional<std::string>(*base_words + " помножити на десять у степені " + *exponent_text)
                   : std::nullopt;
    };
    auto scientific_unit = [](std::string_view quantity, const std::ssub_match& unit) {
        if (!unit.matched) {
            return std::string{};
        }
        const auto measurement = measurements().find(unit.str());
        if (measurement == measurements().end()) {
            return std::string{};
        }
        if (!quantity.empty() && (quantity.front() == '+' || quantity.front() == '-')) {
            quantity.remove_prefix(1);
        }
        if (quantity.find_first_of(".,") != std::string_view::npos) {
            return " " + std::string(measurement->second.decimal);
        }
        const auto value = try_parse_ull(quantity);
        return value
                   ? " " + plural(*value, {measurement->second.one, measurement->second.few, measurement->second.many})
                   : std::string{};
    };
    static const std::string unit_suffix = "(?:\\s*(" + unit_alt() + "))?(?![A-Za-zА-Яа-яЄєІіЇїҐґ])";
    static const std::string optional_unit = "(?:\\s*(" + unit_alt() + "))?";
    static const std::regex inverse_celsius_power(
        R"((^|[^\d])([+-]?\d+(?:[.,]\d+)?)\s*(?:×|·|x|X|\*)\s*10−(\d+)\s*°[CС](?:−|-)(\d+)(?!\d))");
    text = regex_sub(text, inverse_celsius_power, [&](const std::smatch& m) {
        const auto words = scientific_words(m[2].str(), "-" + m[3].str());
        if (!words) {
            return m.str();
        }
        const auto inverse = parse_ull(m[4].str());
        return m[1].str() + *words + " на градус Цельсія" +
               (inverse == 1 ? "" : " у степені " + number_to_words(inverse));
    });
    static const std::regex times_ten(
        R"((^|[^A-Za-zА-Яа-яЄєІіЇїҐґ\d.,])([+-]?\d+(?:[.,]\d+)?)\s*(?:×|·|x|X|\*)\s*10\s*\^\s*([+-]?\d+)(?!\d))" +
        unit_suffix);
    text = regex_sub(text, times_ten, [&](const std::smatch& m) {
        const auto words = scientific_words(m[2].str(), m[3].str());
        return words ? m[1].str() + *words + scientific_unit(m[2].str(), m[4]) : m.str();
    });
    static const std::regex times_ten_plain_signed_exponent(
        R"((^|[^A-Za-zА-Яа-яЄєІіЇїҐґ\d.,])([+-]?\d+(?:[.,]\d+)?)\s*(?:×|·|x|X|\*)\s*10\s*((?:\+|-|−)\d+)(?!\d))" +
        optional_unit);
    text = regex_sub(text, times_ten_plain_signed_exponent, [&](const std::smatch& m) {
        const auto words = scientific_words(m[2].str(), m[3].str());
        return words ? m[1].str() + *words + scientific_unit(m[2].str(), m[4]) : m.str();
    });
    static const std::regex e_notation(
        R"((^|[^A-Za-zА-Яа-яЄєІіЇїҐґ\d.,])([+-]?\d+(?:[.,]\d+)?)[eE]([+-]?\d+)(?![A-Za-zА-Яа-яЄєІіЇїҐґ\d]))");
    text = regex_sub(text, e_notation, [&](const std::smatch& m) {
        const auto words = scientific_words(m[2].str(), m[3].str());
        return words ? m[1].str() + *words : m.str();
    });
    static const std::regex negative_power_range(
        R"((^|[^\d])10−(\d+)\s*(?:-|–|—)\s*10−(\d+)(?:\s*(секунди|секунда|секунд|)" + unit_alt() +
        R"())?(?![A-Za-zА-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, negative_power_range, [&](const std::smatch& m) {
        const auto low = exponent_words("-" + m[2].str());
        const auto high = exponent_words("-" + m[3].str());
        if (!low || !high) {
            return m.str();
        }
        std::string unit;
        if (m[4].matched) {
            unit = m[4].str() == "секунди" || m[4].str() == "секунда" || m[4].str() == "секунд"
                       ? " секунд"
                       : scientific_unit("10", m[4]);
        }
        return m[1].str() + range_connector(range_style, "десяти у степені " + *low, "десяти у степені " + *high) +
               unit;
    });
    // In technical prose a tightly joined Unicode minus after 10 denotes a
    // negative power (10−9), not a numeric range from 10 to 9.
    static const std::regex plain_negative_power(R"((^|[^\d])10−(\d+)(?!\d))" + optional_unit);
    text = regex_sub(text, plain_negative_power, [&](const std::smatch& m) {
        const auto exponent = exponent_words("-" + m[2].str());
        return exponent ? m[1].str() + "десять у степені " + *exponent + scientific_unit("10", m[3]) : m.str();
    });
    static const std::regex power(R"((^|[^A-Za-zА-Яа-яЄєІіЇїҐґ\d.,])([+-]?\d+(?:[.,]\d+)?)\s*\^\s*([+-]?\d+)(?!\d))");
    return regex_sub(text, power, [&](const std::smatch& m) {
        const auto base = signed_number_words(m[2].str(), "nom");
        const auto exponent = exponent_words(m[3].str());
        return base && exponent ? m[1].str() + *base + " у степені " + *exponent : m.str();
    });
}
std::string normalize_math(std::string text)
{
    return ctre_sub<R"((\d)\s*\+\s*(?=\d))">(text, [](const auto& m) { return cap_string<1>(m) + " плюс "; });
}

std::string normalize_decimals(std::string text)
{
    return ctre_sub<R"(\b(\d+)[,.](\d+)\b)">(text, [](const auto& m) {
        const auto integer_part = cap_string<1>(m);
        const auto fractional_part = cap_string<2>(m);
        if (fractional_part.find_first_not_of('0') == std::string::npos) {
            return decimal_to_words_or_digits(integer_part, fractional_part);
        }
        return decimal_to_words_or_digits(integer_part, fractional_part);
    });
}

std::string normalize_overprecise_currency_decimals(std::string text)
{
    static const std::regex re(R"(\b(\d+),(\d{3,})(?=\s*(?:грн|UAH|USD|EUR|GBP|[$€£₴]|долар|євро|фунт)))",
                               std::regex::icase);
    return regex_sub(text, re, [](const std::smatch& m) {
        return number_digits_or_words(m[1].str()) + " кома " + number_to_words_digit_by_digit(m[2].str());
    });
}
std::string normalize_multipliers(std::string text, bool governed_only)
{
    struct Multiplier {
        Forms forms;
        std::string_view decimal;
        bool feminine;
    };
    static const std::unordered_map<std::string, Multiplier> mult = {
        {"тис", {{"тисяча", "тисячі", "тисяч"}, "тисячі", true}},
        {"млн", {{"мільйон", "мільйони", "мільйонів"}, "мільйона", false}},
        {"млрд", {{"мільярд", "мільярди", "мільярдів"}, "мільярда", false}},
        {"трлн", {{"трильйон", "трильйони", "трильйонів"}, "трильйона", false}}};
    static const std::regex governed(
        R"((^|[^А-Яа-яЄєІіЇїҐґ-])(Близько|близько|Після|після|Менше|менше|Більше|більше|Серед|серед|Без|без|Від|від|До|до|Із|із)\s+(\d+(?:[.,]\d+)?)\s*(тис|млн|млрд|трлн)\.?(?![а-яіїєґ]))",
        std::regex::icase);
    text = regex_sub(text, governed, [&](const std::smatch& m) {
        const auto key = lower_text(m[4].str());
        const auto& [forms, decimal, feminine] = mult.at(key);
        const auto words = signed_number_words(m[3].str(), "gen", feminine ? 'f' : 'm');
        if (!words) {
            return m.str();
        }
        const auto unit = m[3].str().find_first_of(".,") == std::string::npos ? forms[2] : decimal;
        return m[1].str() + m[2].str() + " " + *words + " " + std::string(unit);
    });
    if (governed_only) {
        return text;
    }
    static const std::regex re(R"(\b(\d+(?:[.,]\d+)?)\s*(тис|млн|млрд|трлн)(\.?)(?![а-яіїєґ]))", std::regex::icase);
    return regex_sub(text, re, [&](const std::smatch& m) {
        const auto key = lower_text(m[2].str());
        const auto& [forms, decimal, feminine] = mult.at(key);
        const auto num = m[1].str();
        const auto pos = num.find_first_of(".,");
        if (pos != std::string::npos) {
            auto words = decimal_to_words(std::string_view(num).substr(0, pos), std::string_view(num).substr(pos + 1));
            return words.empty() ? m.str()
                                 : words + " " + std::string(decimal) +
                                       (m[3].matched && m.suffix().str().empty() ? m[3].str() : "");
        }
        const auto n = try_parse_ull(num);
        if (!n) {
            return number_to_words_digit_by_digit(num) + " " + std::string(forms[2]) +
                   (m[3].matched && m.suffix().str().empty() ? m[3].str() : "");
        }
        auto words = split_words(number_to_words(*n));
        if (feminine) {
            feminine_last(words);
        }
        return join(words) + " " + plural(*n, forms) + (m[3].matched && m.suffix().str().empty() ? m[3].str() : "");
    });
}
std::string normalize_versions(std::string text)
{
    static const std::regex named(
        R"((^|[\s(\[{:,;])((?:(?:В|в)ерсі(?:я|ї|ю|єю)|(?:Р|р)еліз(?:у|ом)?|(?:В|в)ипуск(?:у|ом)?|(?:П|п)ункт(?:у|ом|і|а)?|(?:Р|р)озділ(?:у|ом|і|а)?)\s+)(\d+(?:\.\d+)+)\b)");
    text =
        regex_sub(text, named, [](const std::smatch& m) { return m[1].str() + m[2].str() + read_dotted(m[3].str()); });
    text = ctre_sub<R"(\b([A-Za-z][A-Za-z0-9_\-]*\s+)(\d+(?:\.\d+)+)\b)">(
        text, [](const auto& m) { return cap_string<1>(m) + read_dotted(cap<2>(m)); });
    text = ctre_sub<R"(\b([vV])(\d+(?:\.\d+)+)\b)">(
        text, [](const auto& m) { return spell_identifier_letters(cap<1>(m)) + " " + read_dotted(cap<2>(m)); });
    text = ctre_sub<R"(\b([A-Za-z])\.(\d{1,6})\b)">(text, [](const auto& m) {
        return spell_identifier_letters(cap<1>(m)) + " крапка " + number_digits_or_words(cap<2>(m));
    });
    text = ctre_sub<R"(\b(\d+)\.(\d+)([A-Za-z]{1,6})\b)">(text, [](const auto& m) {
        return number_digits_or_words(cap<1>(m)) + " крапка " + number_digits_or_words(cap<2>(m)) + " " +
               spell_identifier_letters(cap<3>(m));
    });
    return ctre_sub<R"(\b\d+(?:\.\d+){2,}\b)">(text, [](const auto& m) { return read_dotted(cap<0>(m)); });
}

std::string normalize_negatives(std::string text)
{
    static const std::regex negative(R"((^|[\s(\[])(?:-|−|–|—)(\d))");
    return regex_sub(text, negative, [](const std::smatch& m) { return m[1].str() + "мінус " + m[2].str(); });
}

std::string normalize_text_with_numbers(std::string text)
{
    return ctre_sub<R"(\b\d+\b)">(text, [](const auto& m) {
        const auto digits = cap_string<0>(m);
        if (digits.size() > 1 && digits[0] == '0') {
            return number_to_words_digit_by_digit(digits);
        }
        return number_digits_or_words(digits);
    });
}

} // namespace uktextnorm::detail
