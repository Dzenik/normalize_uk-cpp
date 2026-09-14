#include "uktextnorm/uktextnorm.hpp"

#include "generated/uktextnorm_lexicons.hpp"
#include "internal.hpp"

namespace uktextnorm::detail {

namespace {

const std::string& signed_number_pattern()
{
    static const std::string pattern = R"((?:\+|-|−|–|—)?\d+(?:[.,]\d+)?)";
    return pattern;
}

const std::string& range_separator_pattern()
{
    static const std::string pattern = R"((?:-|−|–|—))";
    return pattern;
}

const std::string& range_prefix_pattern()
{
    static const std::string pattern = R"((^|[\s(\[{:;,.!?=]))";
    return pattern;
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

    const auto integer = try_parse_ull(token.substr(0, pos));
    const auto fraction = try_parse_ull(token.substr(pos + 1));
    static const std::unordered_map<std::size_t, std::string_view> places = {
        {1, "десятих"}, {2, "сотих"}, {3, "тисячних"}, {4, "десятитисячних"}, {5, "стотисячних"}, {6, "мільйонних"}};
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
    return sign + integer_words + whole + fraction_words + " " + std::string(place->second);
}

std::string temperature_scale_name(std::string_view scale)
{
    const auto lowered = lower_text(scale);
    return lowered.contains("f") || lowered.contains("фаренгейт") || scale == "℉" ? "Фаренгейта" : "Цельсія";
}

std::optional<std::string> temperature_quantity_words(std::string_view token)
{
    const auto words = signed_number_words(token, "nom");
    if (!words) {
        return std::nullopt;
    }
    if (token.find_first_of(".,") != std::string_view::npos) {
        return *words + " градуса";
    }
    auto unsigned_token = token;
    take_spoken_sign(unsigned_token);
    const auto value = try_parse_ull(unsigned_token);
    return *words + " " + plural(*value, {"градус", "градуси", "градусів"});
}

std::string temperature_range_unit(std::string_view upper)
{
    return upper.find_first_of(".,") == std::string_view::npos ? "градусів" : "градуса";
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
        if (token == entry.code || (!entry.symbol.empty() && token == entry.symbol)) {
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

std::string normalize_dates(std::string text, DateStyle style, bool validate, RangeStyle range_style)
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
    auto day_words = [style](std::string_view day, std::string_view formal_form = "nom_n") {
        return number_to_ordinal_words(parse_ull(day), style == DateStyle::Spoken ? "gen" : formal_form);
    };
    auto range_day_words = [&](std::string_view day) {
        return range_style == RangeStyle::FromTo ? number_to_ordinal_words(parse_ull(day), "gen") : day_words(day);
    };
    text = regex_sub(text, date_day_range_re(), [&](const std::smatch& m) {
        const auto low = number_to_ordinal_words(parse_ull(m[1].str()), "gen");
        const auto high = number_to_ordinal_words(parse_ull(m[2].str()), "gen");
        return range_connector(range_style, low, high) + " " + month_name(m[3].str()) + " " +
               number_to_ordinal_words(parse_ull(m[4].str()), "gen") + " року";
    });
    static const std::regex numeric_range(
        R"(\b(\d{1,2})\.(\d{1,2})\.(\d{4})\s*(?:-|−|–|—)\s*(\d{1,2})\.(\d{1,2})\.(\d{4})\b)");
    text = regex_sub(text, numeric_range, [&](const std::smatch& m) {
        const int m1 = parse_int(m[2].str());
        const int m2 = parse_int(m[5].str());
        if (m1 < 1 || m1 > 12 || m2 < 1 || m2 > 12) {
            return m.str();
        }
        if (validate && (!is_valid_date(parse_int(m[1].str()), m1, parse_int(m[3].str())) ||
                         !is_valid_date(parse_int(m[4].str()), m2, parse_int(m[6].str())))) {
            return m.str();
        }
        const auto low = range_day_words(m[1].str()) + " " + std::string(months[m1 - 1]) + " " +
                         number_to_ordinal_words(parse_ull(m[3].str()), "gen") + " року";
        const auto high = range_day_words(m[4].str()) + " " + std::string(months[m2 - 1]) + " " +
                          number_to_ordinal_words(parse_ull(m[6].str()), "gen") + " року";
        return range_connector(range_style, low, high);
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
    text = ctre_sub<R"(\b(\d{1,2})\.(\d{1,2})\.(\d{4})\b)">(text, [&](const auto& m) {
        const int month = parse_int(cap<2>(m));
        if (month < 1 || month > 12) {
            return whole_string(m);
        }
        if (validate && !is_valid_date(parse_int(cap<1>(m)), month, parse_int(cap<3>(m)))) {
            return whole_string(m);
        }
        return day_words(cap<1>(m)) + " " + std::string(months[month - 1]) + " " +
               number_to_ordinal_words(parse_ull(cap<3>(m)), "gen") + " року";
    });
    text = ctre_sub<R"(\b(\d{1,2})/(\d{1,2})/(\d{4})\b)">(text, [&](const auto& m) {
        const int month = parse_int(cap<2>(m));
        if (month < 1 || month > 12) {
            return whole_string(m);
        }
        if (validate && !is_valid_date(parse_int(cap<1>(m)), month, parse_int(cap<3>(m)))) {
            return whole_string(m);
        }
        return day_words(cap<1>(m)) + " " + std::string(months[month - 1]) + " " +
               number_to_ordinal_words(parse_ull(cap<3>(m)), "gen") + " року";
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
    text = ctre_sub<R"((^|[^\d])(\d{3,4})\s+(році|року|рік)(?![А-Яа-яЄєІіЇїҐґ]))">(text, [](const auto& m) {
        static const std::unordered_map<std::string, std::string_view> forms = {
            {"рік", "nom_m"}, {"року", "gen"}, {"році", "prep"}};
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
    static const std::regex season_year(
        R"((^|[^А-Яа-яЄєІіЇїҐґ])((?:весна|літо|осінь|зима))\s+(\d{3,4})(?![\dА-Яа-яЄєІіЇїҐґ]))",
        std::regex::icase);
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
std::string normalize_ordinals(std::string text)
{
    static const std::unordered_map<std::string, std::string_view> suffix_form = {{"й", "nom_m"},
                                                                                  {"го", "gen"},
                                                                                  {"му", "dat"},
                                                                                  {"м", "prep"},
                                                                                  {"а", "nom_f"},
                                                                                  {"у", "acc_f"},
                                                                                  {"е", "nom_n"},
                                                                                  {"х", "pl"},
                                                                                  {"им", "ins"},
                                                                                  {"ім", "ins"},
                                                                                  {"ою", "ins_f"},
                                                                                  {"ій", "loc_f"},
                                                                                  {"ими", "ins_pl"}};
    static const std::unordered_set<std::string> stop = {
        "CD", "DVD", "MD", "DC", "MC", "MI", "MM", "DI", "DIV", "MIX", "CIV", "LCD"};
    text = ctre_sub<R"((\d+)(?:-|–|—)(ими|им|ім|ою|ій|го|му|й|м|а|у|е|х)(?![А-Яа-яЄєІіЇїҐґ]))">(text, [&](const auto& m) {
        return number_to_ordinal_words(parse_ull(cap<1>(m)), suffix_form.at(cap_string<2>(m)));
    });
    text = ctre_sub<
        R"((^|[^A-Za-z])([MDCLXVI]{1,6})\s*(?:-|–|—)\s*([MDCLXVI]{1,6})\s*(?:ст\.|століття)(?![А-Яа-яЄєІіЇїҐґ]))">(
        text, [](const auto& m) {
            const auto start = cap_string<2>(m);
            const auto stop = cap_string<3>(m);
            if (!valid_roman(start) || !valid_roman(stop)) {
                return whole_string(m);
            }
            return cap_string<1>(m) + number_to_ordinal_words(roman_to_int(start), "nom_n") + " " +
                   number_to_ordinal_words(roman_to_int(stop), "nom_n") + " століття";
        });
    text = ctre_sub<
        R"((^|[^A-Za-z])([MDCLXVI]{1,6})\s*(?:-|–|—)\s*([MDCLXVI]{1,6})\s*(розд\.|розділ)(?![А-Яа-яЄєІіЇїҐґ]))">(
        text, [](const auto& m) {
            const auto start = cap_string<2>(m);
            const auto stop = cap_string<3>(m);
            if (!valid_roman(start) || !valid_roman(stop)) {
                return whole_string(m);
            }
            return cap_string<1>(m) + number_to_ordinal_words(roman_to_int(start), "nom_m") + " " +
                   number_to_ordinal_words(roman_to_int(stop), "nom_m") + " розділ";
        });
    text =
        ctre_sub<R"((^|[^A-Za-z])([MDCLXVI]{1,6})\s*(?:ст\.|століття)(?![А-Яа-яЄєІіЇїҐґ]))">(text, [](const auto& m) {
            const auto tok = cap_string<2>(m);
            if (!valid_roman(tok)) {
                return whole_string(m);
            }
            return cap_string<1>(m) + number_to_ordinal_words(roman_to_int(tok), "nom_n") + " століття";
        });
    return ctre_sub<R"(\b[MDCLXVI]{2,}\b)">(text, [&](const auto& m) {
        const auto tok = whole_string(m);
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
    static const std::regex page_range(
        R"((^|[^А-Яа-яЄєІіЇїҐґA-Za-z])(?:стор\.|Стор\.|СТОР\.|с\.|С\.)\s*(\d+)\s*(?:-|−|–|—)\s*(\d+)(?!\d))");
    return regex_sub(text, page_range, [&](const std::smatch& m) {
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
}

std::string normalize_ranges(std::string text, RangeStyle style)
{
    const auto& number = signed_number_pattern();
    const auto& separator = range_separator_pattern();
    const auto& prefix = range_prefix_pattern();
    static const std::string scale_symbol = R"((?:°\s*(?:C|c|С|с|F|f)|℃|℉))";
    static const std::string scale_name =
        R"((?:C|c|С|с|F|f|(?:Ц|ц)ельсія|ЦЕЛЬСІЯ|(?:Ф|ф)аренгейта|ФАРЕНГЕЙТА|за\s+(?:(?:Ц|ц)ельсієм|ЦЕЛЬСІЄМ|(?:Ф|ф)аренгейтом|ФАРЕНГЕЙТОМ)))";
    static const std::string degrees = R"((?:градус(?:а|и|ів)?|Градус(?:а|и|ів)?|ГРАДУС(?:А|И|ІВ)?))";
    static const std::string number_boundary = R"((?![\d.,:/+\-−–—]))";

    auto say_temperature = [&](const std::smatch& m,
                               std::size_t low_index,
                               std::size_t high_index,
                               std::size_t scale_index,
                               bool explicitly_from_to) {
        const auto grammatical_case = style == RangeStyle::FromTo || explicitly_from_to ? "gen" : "nom";
        const auto low = signed_number_words(m[low_index].str(), grammatical_case);
        const auto high = signed_number_words(m[high_index].str(), grammatical_case);
        if (!low || !high) {
            return m.str();
        }
        return m[1].str() + range_connector(style, *low, *high, explicitly_from_to) + " " +
               temperature_range_unit(m[high_index].str()) + " " + temperature_scale_name(m[scale_index].str());
    };

    static const std::regex explicit_repeated_temperature(prefix + "від\\s+(" + number + ")\\s*(" + scale_symbol +
                                                          ")\\s+до\\s+(" + number + ")\\s*(" + scale_symbol + ")" +
                                                          number_boundary);
    text = regex_sub(text, explicit_repeated_temperature, [&](const std::smatch& m) {
        if (temperature_scale_name(m[3].str()) != temperature_scale_name(m[5].str())) {
            return m.str();
        }
        return say_temperature(m, 2, 4, 5, true);
    });
    static const std::regex repeated_temperature(prefix + "(" + number + ")\\s*(" + scale_symbol + ")\\s*" + separator +
                                                 "\\s*(" + number + ")\\s*(" + scale_symbol + ")" + number_boundary);
    text = regex_sub(text, repeated_temperature, [&](const std::smatch& m) {
        if (temperature_scale_name(m[3].str()) != temperature_scale_name(m[5].str())) {
            return m.str();
        }
        return say_temperature(m, 2, 4, 5, false);
    });
    static const std::regex explicit_temperature(prefix + "від\\s+(" + number + ")\\s+до\\s+(" + number + ")\\s*(" +
                                                 scale_symbol + ")" + number_boundary);
    text =
        regex_sub(text, explicit_temperature, [&](const std::smatch& m) { return say_temperature(m, 2, 3, 4, true); });
    static const std::regex temperature_range(prefix + "(" + number + ")\\s*" + separator + "\\s*(" + number +
                                              ")\\s*(" + scale_symbol + ")" + number_boundary);
    text = regex_sub(text, temperature_range, [&](const std::smatch& m) { return say_temperature(m, 2, 3, 4, false); });

    static const std::regex explicit_repeated_named_temperature(
        prefix + "від\\s+(" + number + ")\\s+" + degrees + "\\s+(" + scale_name + ")\\s+до\\s+(" + number + ")\\s+" +
        degrees + "\\s+(" + scale_name + ")" + number_boundary);
    text = regex_sub(text, explicit_repeated_named_temperature, [&](const std::smatch& m) {
        if (temperature_scale_name(m[3].str()) != temperature_scale_name(m[5].str())) {
            return m.str();
        }
        return say_temperature(m, 2, 4, 5, true);
    });
    static const std::regex repeated_named_temperature(prefix + "(" + number + ")\\s+" + degrees + "\\s+(" +
                                                       scale_name + ")\\s*" + separator + "\\s*(" + number + ")\\s+" +
                                                       degrees + "\\s+(" + scale_name + ")" + number_boundary);
    text = regex_sub(text, repeated_named_temperature, [&](const std::smatch& m) {
        if (temperature_scale_name(m[3].str()) != temperature_scale_name(m[5].str())) {
            return m.str();
        }
        return say_temperature(m, 2, 4, 5, false);
    });
    static const std::regex explicit_named_temperature(prefix + "від\\s+(" + number + ")\\s+до\\s+(" + number +
                                                       ")\\s+" + degrees + "\\s+(" + scale_name + ")" +
                                                       number_boundary);
    text = regex_sub(
        text, explicit_named_temperature, [&](const std::smatch& m) { return say_temperature(m, 2, 3, 4, true); });
    static const std::regex named_temperature_range(prefix + "(" + number + ")\\s*" + separator + "\\s*(" + number +
                                                    ")\\s+" + degrees + "\\s+(" + scale_name + ")" + number_boundary);
    text = regex_sub(
        text, named_temperature_range, [&](const std::smatch& m) { return say_temperature(m, 2, 3, 4, false); });

    static const std::regex year_range(R"(\b(\d{3,4})\s*(?:-|−|–|—)\s*(\d{3,4})\s*(?:рр\.?|роки)(?![а-яіїєґ]))");
    text = regex_sub(text, year_range, [&](const std::smatch& m) {
        const auto low = parse_ull(m[1].str());
        const auto high = parse_ull(m[2].str());
        if (style == RangeStyle::FromTo) {
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
        const auto grammatical_case = style == RangeStyle::FromTo || explicitly_from_to ? "gen" : "nom";
        const auto low = signed_number_words(m[low_index].str(), grammatical_case, measurement.gender);
        const auto high = signed_number_words(m[high_index].str(), grammatical_case, measurement.gender);
        if (!low || !high) {
            return m.str();
        }
        std::string unit(measurement.many);
        if (style == RangeStyle::Compact && !explicitly_from_to) {
            const auto upper_text = m[high_index].str();
            auto upper = std::string_view(upper_text);
            take_spoken_sign(upper);
            if (upper.find_first_of(".,") != std::string_view::npos) {
                unit = measurement.few;
            } else if (const auto value = try_parse_ull(upper)) {
                unit = plural(*value, {measurement.one, measurement.few, measurement.many});
            }
        }
        return m[1].str() + range_connector(style, *low, *high, explicitly_from_to) + " " + unit;
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
        const auto grammatical_case = style == RangeStyle::FromTo || explicitly_from_to ? "gen" : "nom";
        const auto low = signed_number_words(m[low_index].str(), grammatical_case, currency->gender);
        const auto high = signed_number_words(m[high_index].str(), grammatical_case, currency->gender);
        if (!low || !high) {
            return m.str();
        }
        return m[1].str() + range_connector(style, *low, *high, explicitly_from_to) + " " + std::string(currency->many);
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
            const auto grammatical_case = style == RangeStyle::FromTo || explicitly_from_to ? "gen" : "nom";
            const auto low = signed_number_words(m[low_index].str(), grammatical_case);
            const auto high = signed_number_words(m[high_index].str(), grammatical_case);
            if (!low || !high) {
                return m.str();
            }
            return m[1].str() + range_connector(style, *low, *high, explicitly_from_to) + " відсотків";
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
    static const std::regex percent_range(prefix + "(" + number + ")\\s*" + separator + "\\s*(" + number +
                                          R"()\s*%(?!\w))");
    text = regex_sub(text, percent_range, [&](const std::smatch& m) { return say_percent(m, 2, 3, false); });

    auto say_bare = [&](const std::smatch& m, bool explicitly_from_to) {
        const auto grammatical_case = style == RangeStyle::FromTo || explicitly_from_to ? "gen" : "nom";
        const auto low = signed_number_words(m[2].str(), grammatical_case);
        const auto high = signed_number_words(m[3].str(), grammatical_case);
        return low && high ? m[1].str() + range_connector(style, *low, *high, explicitly_from_to) : m.str();
    };
    static const std::regex explicit_bare_range(prefix + "від\\s+(" + number + ")\\s+до\\s+(" + number + ")" +
                                                number_boundary + "(?!\\s+(?:" + month_alt() + R"()(?:\s|$)))");
    text = regex_sub(text, explicit_bare_range, [&](const std::smatch& m) { return say_bare(m, true); });
    static const std::regex bare_range(prefix + "(" + number + ")\\s*" + separator + "\\s*(" + number + ")" +
                                       number_boundary + "(?!\\s+(?:" + month_alt() + R"()(?:\s|$)))");
    return regex_sub(text, bare_range, [&](const std::smatch& m) { return say_bare(m, false); });
}
std::string normalize_case_context(std::string text)
{
    static const std::unordered_map<std::string, std::string_view> prep_case = {{"близько", "gen"},
                                                                                {"понад", "gen"},
                                                                                {"менше", "gen"},
                                                                                {"більше", "gen"},
                                                                                {"від", "gen"},
                                                                                {"до", "gen"},
                                                                                {"із", "gen"},
                                                                                {"з", "instr"},
                                                                                {"без", "gen"},
                                                                                {"після", "gen"},
                                                                                {"перед", "instr"},
                                                                                {"між", "instr"},
                                                                                {"над", "instr"},
                                                                                {"під", "instr"},
                                                                                {"при", "prep"},
                                                                                {"к", "dat"},
                                                                                {"о", "prep"},
                                                                                {"об", "prep"},
                                                                                {"у", "prep"},
                                                                                {"в", "prep"},
                                                                                {"на", "prep"}};
    static const std::regex instr(R"((^|[^А-Яа-яЄєІіЇїҐґ])([Зз])\s+(\d+)\s+([а-яєіїґ']{3,}(?:ами|ями|ма))\b)");
    static const std::regex oblique(R"(\b(\d+)\s+([а-яєіїґ']{3,}(?:ами|ями|ах|ях))\b)");
    text = regex_sub(text, instr, [](const std::smatch& m) {
        return m[1].str() + m[2].str() + " " + number_to_words_case(parse_ull(m[3].str()), "instr") + " " + m[4].str();
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
        }
        return out;
    });
    return regex_sub(text, oblique, [](const std::smatch& m) {
        const auto noun = m[2].str();
        const auto it = counted_oblique_cases().find(lower_text(noun));
        if (it == counted_oblique_cases().end()) {
            return m.str();
        }
        return number_to_words_case(parse_ull(m[1].str()), it->second) + " " + noun;
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
        R"((^|[^\d])(\d+)-(?!(?:ими|им|ім|ою|ій|го|му|й|м|а|у|е|х)(?:[^А-Яа-яЄєІіЇїҐґ]|$))([^0-9A-Za-z\s,.;:!?()]+))");
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
std::string normalize_time(std::string text)
{
    text = ctre_sub<R"((^|[^\d:])(\d{1,2}):([0-5]\d):([0-5]\d)(?![\d:]))">(text, [](const auto& m) {
        return cap_string<1>(m) + hours_words(parse_int(cap<2>(m))) + " " + minutes_words(parse_int(cap<3>(m))) + " " +
               minutes_words(parse_int(cap<4>(m)), {"секунда", "секунди", "секунд"});
    });
    text = ctre_sub<R"((^|[^А-Яа-яЄєІіЇїҐґ\d])((?:О|о)(?:б)?) (\d{1,2})(?:-|–|—)?(?:й|ій|а|ої)(?![А-Яа-яЄєІіЇїҐґ]))">(
        text, [](const auto& m) {
            return cap_string<1>(m) + cap_string<2>(m) + " " +
                   number_to_ordinal_words(parse_ull(cap<3>(m)), "nom_f") + " година";
        });
    text = ctre_sub<R"((^|[^\d:])(\d{1,2}):([0-5]\d)\s+(ранку|дня|вечора|ночі)(?![А-Яа-яЄєІіЇїҐґ\d:]))">(
        text, [](const auto& m) {
            const int hour = parse_int(cap<2>(m));
            const int minute = parse_int(cap<3>(m));
            std::string out = cap_string<1>(m) + hours_words(hour);
            if (minute) {
                out += " " + minutes_words(minute);
            }
            return out + " " + cap_string<4>(m);
        });
    return ctre_sub<R"((^|[^\d:])(\d{1,2}):([0-5]\d)(?![\d:]))">(text, [](const auto& m) {
        const int hour = parse_int(cap<2>(m));
        const int minute = parse_int(cap<3>(m));
        std::string out = cap_string<1>(m) + hours_words(hour);
        if (minute) {
            out += " " + minutes_words(minute);
        }
        return out;
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
        replace_all(text, sym, say_fraction(nd.first, nd.second));
    }
    text = ctre_sub<R"((^|[^\d.,/])(\d+) (\d+)/(\d+)\b)">(text, [](const auto& m) {
        const auto whole = try_parse_ull(cap<2>(m));
        const auto numerator = try_parse_ull(cap<3>(m));
        const auto denominator = try_parse_ull(cap<4>(m));
        if (!whole || !numerator || !denominator || !*denominator) {
            return whole_string(m);
        }
        return cap_string<1>(m) + number_words_for_gender(*whole, 'f') + " і " +
               say_fraction(*numerator, *denominator);
    });
    return ctre_sub<R"(\b(\d+)/(\d+)\b)">(text, [](const auto& m) {
        const auto numerator = try_parse_ull(cap<1>(m));
        const auto denominator = try_parse_ull(cap<2>(m));
        if (!numerator || !denominator) {
            return whole_string(m);
        }
        return say_fraction(*numerator, *denominator);
    });
}

std::string normalize_percent(std::string text)
{
    return ctre_sub<R"((\d+(?:[.,]\d+)?)\s*%)">(text, [](const auto& m) {
        auto num = cap_string<1>(m);
        const auto pos = num.find_first_of(".,");
        if (pos != std::string::npos) {
            auto words = decimal_to_words(std::string_view(num).substr(0, pos), std::string_view(num).substr(pos + 1));
            return words.empty() ? whole_string(m) : words + " відсотка";
        }
        const auto n = try_parse_ull(num);
        if (!n) {
            return number_to_words_digit_by_digit(num) + " відсотків";
        }
        return number_to_words(*n) + " " + plural(*n, {"відсоток", "відсотки", "відсотків"});
    });
}
std::string normalize_measurements(std::string text)
{
    return regex_sub(text, measurements_re(), [](const std::smatch& m) {
        const auto& meas = measurements().at(m[3].str());
        return m[1].str() + read_measurement_quantity(m[2].str(), meas);
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
    static const std::regex temperature(range_prefix_pattern() + "(" + signed_number_pattern() +
                                        R"()\s*(°\s*(?:C|c|С|с|F|f)|℃|℉)(?![A-Za-zА-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, temperature, [](const std::smatch& m) {
        const auto words = temperature_quantity_words(m[2].str());
        return words ? m[1].str() + *words + " " + temperature_scale_name(m[3].str()) : m.str();
    });
    static const std::regex named_temperature(
        range_prefix_pattern() + "(" + signed_number_pattern() +
        R"()\s+(?:градус(?:а|и|ів)?|Градус(?:а|и|ів)?|ГРАДУС(?:А|И|ІВ)?)\s+((?:C|c|С|с|F|f|(?:Ц|ц)ельсія|ЦЕЛЬСІЯ|(?:Ф|ф)аренгейта|ФАРЕНГЕЙТА|за\s+(?:(?:Ц|ц)ельсієм|ЦЕЛЬСІЄМ|(?:Ф|ф)аренгейтом|ФАРЕНГЕЙТОМ)))(?![A-Za-zА-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, named_temperature, [](const std::smatch& m) {
        const auto words = temperature_quantity_words(m[2].str());
        return words ? m[1].str() + *words + " " + temperature_scale_name(m[3].str()) : m.str();
    });
    text = regex_sub(text, frequency, [](const std::smatch& m) {
        const auto n = parse_ull(m[2].str());
        return m[1].str() + number_to_words(n) + " " + plural(n, {"раз", "рази", "разів"}) + m[3].str();
    });
    return ctre_sub<R"((^|[^А-Яа-яЄєІіЇїҐґ\d])№\s*(\d{1,4})(?![\d/]))">(
        text, [](const auto& m) { return cap_string<1>(m) + "номер " + number_to_words(parse_ull(cap<2>(m))); });
}
std::string normalize_math(std::string text)
{
    return ctre_sub<R"((\d)\s*\+\s*(?=\d))">(text, [](const auto& m) { return cap_string<1>(m) + " плюс "; });
}

std::string normalize_decimals(std::string text)
{
    return ctre_sub<R"(\b(\d+),(\d+)\b)">(text, [](const auto& m) {
        const auto integer_part = cap_string<1>(m);
        const auto fractional_part = cap_string<2>(m);
        if (fractional_part.find_first_not_of('0') == std::string::npos) {
            return number_digits_or_words(integer_part);
        }
        auto words = decimal_to_words(integer_part, fractional_part);
        return words.empty()
                   ? number_digits_or_words(integer_part) + " кома " + number_to_words_digit_by_digit(fractional_part)
                   : words;
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
std::string normalize_multipliers(std::string text)
{
    static const std::unordered_map<std::string, std::pair<Forms, bool>> mult = {
        {"тис", {{"тисяча", "тисячі", "тисяч"}, true}},
        {"млн", {{"мільйон", "мільйони", "мільйонів"}, false}},
        {"млрд", {{"мільярд", "мільярди", "мільярдів"}, false}},
        {"трлн", {{"трильйон", "трильйони", "трильйонів"}, false}}};
    static const std::regex re(R"(\b(\d+(?:[.,]\d+)?)\s*(тис|млн|млрд|трлн)\.?(?![а-яіїєґ]))", std::regex::icase);
    return regex_sub(text, re, [&](const std::smatch& m) {
        const auto key = lower_text(m[2].str());
        const auto [forms, feminine] = mult.at(key);
        const auto num = m[1].str();
        const auto pos = num.find_first_of(".,");
        if (pos != std::string::npos) {
            auto words = decimal_to_words(std::string_view(num).substr(0, pos), std::string_view(num).substr(pos + 1));
            return words.empty() ? m.str() : words + " " + std::string(forms[1]);
        }
        const auto n = try_parse_ull(num);
        if (!n) {
            return number_to_words_digit_by_digit(num) + " " + std::string(forms[2]);
        }
        auto words = split_words(number_to_words(*n));
        if (feminine) {
            feminine_last(words);
        }
        return join(words) + " " + plural(*n, forms);
    });
}
std::string normalize_versions(std::string text)
{
    text = ctre_sub<R"(\b([A-Za-z][A-Za-z0-9_\-]*\s+)(\d+(?:\.\d+)+)\b)">(
        text, [](const auto& m) { return cap_string<1>(m) + read_dotted(cap<2>(m)); });
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
