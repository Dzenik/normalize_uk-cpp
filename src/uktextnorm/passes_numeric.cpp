#include "uktextnorm/uktextnorm.hpp"

#include "generated/uktextnorm_lexicons.hpp"
#include "numeric_internal.hpp"

namespace uktextnorm::detail {

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

} // namespace uktextnorm::detail
