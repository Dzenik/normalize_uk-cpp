#include "uktextnorm/uktextnorm.hpp"

#include "internal.hpp"

#include "generated/uktextnorm_lexicons.hpp"

namespace uktextnorm {

using namespace detail;

namespace {

std::string protect_opaque_markup(std::string text, std::vector<std::pair<std::string, std::string>>& protected_spans)
{
    auto protect = [&](std::string value) {
        std::string key;
        append_utf8(key, U'\uE000');
        append_utf8(key, static_cast<char32_t>(U'\uE100' + protected_spans.size()));
        append_utf8(key, U'\uE001');
        protected_spans.emplace_back(key, std::move(value));
        return key;
    };
    static const std::regex opaque(
        R"((<!--[\s\S]*?-->|```[\s\S]*?```|~~~[\s\S]*?~~~|`[^`\r\n]*`|<[^<>]+>|&(?:#[0-9]+|#[xX][0-9A-Fa-f]+|[A-Za-z][A-Za-z0-9]+);))");
    text = regex_sub(text, opaque, [&](const std::smatch& m) { return protect(m.str()); });
    static const std::regex markdown_destination(R"((\]\(\s*)([^)\r\n]+)(\s*\)))");
    text = regex_sub(text, markdown_destination, [&](const std::smatch& m) {
        return m[1].str() + protect(m[2].str()) + m[3].str();
    });
    static const std::regex markdown_reference(R"((^|\n)([ \t]{0,3}\[[^\]\r\n]+\]:[ \t]*)(\S+))", std::regex::icase);
    return regex_sub(
        text, markdown_reference, [&](const std::smatch& m) { return m[1].str() + m[2].str() + protect(m[3].str()); });
}

std::string restore_opaque_markup(std::string text,
                                  const std::vector<std::pair<std::string, std::string>>& protected_spans)
{
    for (auto it = protected_spans.rbegin(); it != protected_spans.rend(); ++it) {
        replace_all(text, it->first, it->second);
    }
    return text;
}

void protect_ambiguous_currency_symbols(std::string& text,
                                        std::vector<std::pair<std::string, std::string>>& protected_spans)
{
    for (const auto symbol : {std::string_view("$"), std::string_view("¥")}) {
        std::size_t pos = 0;
        while ((pos = text.find(symbol, pos)) != std::string::npos) {
            std::string key;
            append_utf8(key, U'\uE000');
            append_utf8(key, static_cast<char32_t>(U'\uE100' + protected_spans.size()));
            append_utf8(key, U'\uE001');
            protected_spans.emplace_back(key, std::string(symbol));
            text.replace(pos, symbol.size(), key);
            pos += key.size();
        }
    }
}

void protect_ambiguous_numeric_dates(std::string& text,
                                     std::vector<std::pair<std::string, std::string>>& protected_spans)
{
    static const std::regex ambiguous(R"(\b(?:0?[1-9]|1[0-2])[./-](?:0?[1-9]|1[0-2])[./-](?:\d{2}|\d{4})\b)");
    text = regex_sub(text, ambiguous, [&](const std::smatch& match) {
        std::string key;
        append_utf8(key, U'\uE000');
        append_utf8(key, static_cast<char32_t>(U'\uE100' + protected_spans.size()));
        append_utf8(key, U'\uE001');
        protected_spans.emplace_back(key, match.str());
        return key;
    });
}

} // namespace

NormalizeOptions options_for_preset(NormalizePreset preset)
{
    NormalizeOptions options;
    switch (preset) {
    case NormalizePreset::Default:
        return options;
    case NormalizePreset::TtsFriendly:
        options.range_style = RangeStyle::FromTo;
        options.phone_style = PhoneStyle::DigitByDigit;
        options.date_style = DateStyle::Spoken;
        options.quote_style = QuoteStyle::Strip;
        return options;
    case NormalizePreset::Conservative:
        options.repair_homoglyphs = false;
        options.expand_known_acronyms = false;
        options.spell_unknown_acronyms = false;
        options.normalize_english_words = false;
        options.transliterate_latin = false;
        options.symbol_style = SymbolStyle::Preserve;
        return options;
    case NormalizePreset::SearchIndexing:
        options.quote_style = QuoteStyle::Straight;
        options.spell_unknown_acronyms = false;
        options.normalize_english_words = false;
        options.transliterate_latin = false;
        options.symbol_style = SymbolStyle::Preserve;
        return options;
    }
    return options;
}

std::string normalize_ukrainian(std::string_view input)
{
    return normalize_ukrainian(input, NormalizeOptions{});
}

std::string normalize_ukrainian(std::string_view input, NormalizePreset preset)
{
    return normalize_ukrainian_with_preset(input, preset);
}

std::string normalize_ukrainian_with_preset(std::string_view input, NormalizePreset preset)
{
    return normalize_ukrainian(input, options_for_preset(preset));
}

std::string normalize_ukrainian(std::string_view input, const NormalizeOptions& options)
{
    std::vector<std::pair<std::string, std::string>> protected_spans;
    std::string text = protect_opaque_markup(std::string(input), protected_spans);
    if (options.currency_symbol_policy == CurrencySymbolPolicy::PreserveAmbiguous) {
        protect_ambiguous_currency_symbols(text, protected_spans);
    }
    if (options.numeric_date_order == NumericDateOrder::PreserveAmbiguous) {
        protect_ambiguous_numeric_dates(text, protected_spans);
    }
    const auto maybe_digits = [&] { return has_ascii_digit(text); };
    const auto maybe_roman = [&] { return has_roman_candidate(text); };
    const auto maybe_currency = [&] { return has_currency_candidate(text); };

    text = normalize_unicode(std::move(text), options.quote_style);
    text = normalize_typography(std::move(text));
    if (maybe_digits() && contains_any_token(text, {"стор.", "Стор.", "с.", "С."})) {
        text = normalize_page_ranges(std::move(text), options.range_style);
    }
    if (options.repair_homoglyphs && has_ascii_alpha(text)) {
        text = normalize_homoglyphs(std::move(text));
    }
    if (contains_any(text, "@#") || (has_ascii_alpha(text) && text.contains('.')) ||
        contains_any_token(text,
                           {"http://",
                            "https://",
                            "ftp://",
                            "www.",
                            ".com",
                            ".ua",
                            ".org",
                            ".net",
                            ".info",
                            ".io",
                            ".edu",
                            ".gov",
                            ".укр"})) {
        text = normalize_web(std::move(text));
    }
    if (contains_any_token(text, {"кв.", "квартал"}) || maybe_roman()) {
        text = normalize_quarters(std::move(text));
    }
    if (text.contains('.')) {
        text = normalize_addresses(std::move(text));
    }
    text = normalize_abbreviations(text);
    if (options.normalize_network_addresses && contains_any(text, ".:")) {
        text = normalize_ip_addresses(std::move(text));
    }
    if (maybe_digits()) {
        text = normalize_number_groups(std::move(text), options.parse_thousand_separators);
        text = normalize_identifiers(std::move(text));
        text = normalize_scientific(std::move(text));
        text = normalize_dates(std::move(text),
                               options.date_style,
                               options.validate_dates,
                               options.range_style,
                               options.numeric_date_order);
        text = normalize_section_ranges(std::move(text), options.range_style);
        text = normalize_ranges(std::move(text), options.range_style);
        text = normalize_discourse_dates(std::move(text));
        if (contains_any(text, "/°№℃℉K") ||
            contains_any_token(text, {"мм рт", "раз", "тиск", "градус", "K", "К", "кельвін"})) {
            text = normalize_medical(std::move(text));
        }
        text = normalize_counted_noun_context(std::move(text));
        text = normalize_case_context(std::move(text));
        if (text.contains('.') || maybe_roman()) {
            text = normalize_sections(std::move(text));
        }
        if (contains_any(text, ":-–—")) {
            text = normalize_time(std::move(text), options.colon_style);
        }
        text = normalize_counted_nouns(std::move(text));
        text = normalize_ordinal_triggers(std::move(text));
        if (contains_any(text, "-–—")) {
            text = normalize_compounds(std::move(text));
        }
        text = normalize_ordinals(std::move(text));
        if (contains_any(text, "/½⅓⅔¼¾⅕⅖⅗⅘⅙⅚⅐⅛⅜⅝⅞⅑⅒")) {
            text = normalize_fractions(std::move(text));
        }
        if (text.contains('%')) {
            text = normalize_percent(std::move(text));
        }
        text = normalize_coordinates(std::move(text));
        if (maybe_currency()) {
            text = normalize_symbol_currency(std::move(text));
        }
        if (contains_any_token(text, {"тис", "млн", "млрд", "трлн"})) {
            text = normalize_multipliers(std::move(text));
        }
        text = normalize_measurements(std::move(text));
    } else if (maybe_roman()) {
        text = normalize_ordinals(std::move(text));
    }
    if (contains_any_token(text,
                           {"BTC", "ETH", "USDT", "BNB", "SOL", "XRP", "ADA", "DOGE", "USD", "EUR", "GBP", "UAH"})) {
        text = normalize_finance(std::move(text));
    }
    if (options.expand_known_acronyms) {
        text = normalize_known_acronyms(std::move(text));
    }
    if (options.spell_unknown_acronyms) {
        text = expand_abbreviations(text);
    }
    if (options.symbol_style == SymbolStyle::Expand) {
        if (text.contains('+') && maybe_digits()) {
            text = normalize_math(std::move(text));
        }
        if (has_symbol_candidate(text)) {
            text = normalize_symbols(std::move(text));
        }
    }
    if (maybe_digits()) {
        if (maybe_currency()) {
            text = normalize_overprecise_currency_decimals(std::move(text));
            text = normalize_currency(std::move(text));
        }
        if (text.contains(',')) {
            text = normalize_decimals(std::move(text));
        }
        text = normalize_text_with_phone_numbers(std::move(text), options.phone_style);
        if (text.contains('.')) {
            text = normalize_versions(std::move(text));
        }
        if (text.contains('-') || text.contains("−")) {
            text = normalize_negatives(std::move(text));
        }
        text = normalize_text_with_numbers(std::move(text));
    }
    if (options.normalize_english_words) {
        if (has_ascii_alpha(text)) {
            text = normalize_english(std::move(text));
        }
    }
    if (options.transliterate_latin) {
        if (has_ascii_alpha(text)) {
            text = transliterate_to_cyrillic(text);
        }
    }
    return restore_opaque_markup(trim_spaces(std::move(text)), protected_spans);
}

std::vector<UncertainSpan> flag_uncertain(std::string_view text)
{
    std::vector<UncertainSpan> spans;
    const auto char_offsets = byte_to_char_offsets(text);
    std::set<std::pair<std::size_t, std::size_t>> seen;
    auto add = [&](std::size_t s,
                   std::size_t e,
                   std::string reason,
                   UncertaintyCategory category,
                   UncertaintySeverity severity) {
        while (s > 0 && is_utf8_continuation(text[s])) {
            --s;
        }
        while (e < text.size() && is_utf8_continuation(text[e])) {
            ++e;
        }
        if (seen.insert({s, e}).second) {
            spans.push_back({char_offsets[s],
                             char_offsets[e],
                             std::string(text.substr(s, e - s)),
                             std::move(reason),
                             category,
                             severity});
        }
    };
    std::string input(text);
    auto decimal_value = [](std::string token) {
        replace_all(token, ",", ".");
        try {
            return std::optional<double>(std::stod(token));
        } catch (...) {
            return std::optional<double>{};
        }
    };
    static const std::regex ambiguous_numeric_date(
        R"(\b(?:0?[1-9]|1[0-2])[./-](?:0?[1-9]|1[0-2])[./-](?:\d{2}|\d{4})\b)");
    for (std::sregex_iterator it(input.begin(), input.end(), ambiguous_numeric_date), end; it != end; ++it) {
        add((*it).position(),
            (*it).position() + (*it).length(),
            "ambiguous numeric date order (day/month or month/day)",
            UncertaintyCategory::Date,
            UncertaintySeverity::Warning);
    }
    static const std::regex ambiguous_colon(R"((^|[^\d:])(\d{1,2}):([0-5]\d)(?![\d:]))");
    for (std::sregex_iterator it(input.begin(), input.end(), ambiguous_colon), end; it != end; ++it) {
        const auto hour = parse_int((*it)[2].str());
        const auto minute = parse_int((*it)[3].str());
        if (hour > 23 && !(hour == 24 && minute == 0)) {
            continue;
        }
        const auto s = static_cast<std::size_t>((*it).position(2));
        add(s,
            s + (*it)[2].length() + 1 + (*it)[3].length(),
            "ambiguous colon pair (clock time or ratio)",
            UncertaintyCategory::Time,
            UncertaintySeverity::Warning);
    }
    static const std::regex ambiguous_currency_symbol(R"((?:\$|¥)\s*\d+(?:[.,]\d+)?)");
    for (std::sregex_iterator it(input.begin(), input.end(), ambiguous_currency_symbol), end; it != end; ++it) {
        add((*it).position(),
            (*it).position() + (*it).length(),
            "ambiguous currency symbol (currency depends on locale)",
            UncertaintyCategory::Currency,
            UncertaintySeverity::Warning);
    }
    ctre_each<R"((^|[^\d])(\d{1,2})\.(\d{1,2})\.(\d{3,4})(?![\d]))">(input, [&](const auto& m) {
        const auto day = parse_int(cap<2>(m));
        const auto month = parse_int(cap<3>(m));
        if (day < 1 || day > 31 || month < 1 || month > 12) {
            const auto s = cap_pos<2>(input, m);
            add(s,
                s + cap<2>(m).size() + 1 + cap<3>(m).size() + 1 + cap<4>(m).size(),
                "invalid or ambiguous numeric date",
                UncertaintyCategory::Date,
                UncertaintySeverity::Error);
        } else if (!is_valid_date(day, month, parse_int(cap<4>(m)))) {
            const auto s = cap_pos<2>(input, m);
            add(s,
                s + cap<2>(m).size() + 1 + cap<3>(m).size() + 1 + cap<4>(m).size(),
                "calendar-invalid date (day does not exist in that month)",
                UncertaintyCategory::InvalidDate,
                UncertaintySeverity::Error);
        }
    });
    static const std::regex invalid_iso_date(R"((^|[^\d])(\d{4})-(\d{1,2})(?:-(\d{1,2}))?(?!\d))");
    for (std::sregex_iterator it(input.begin(), input.end(), invalid_iso_date), end; it != end; ++it) {
        const auto year = parse_int((*it)[2].str());
        const auto month = parse_int((*it)[3].str());
        const auto day = (*it)[4].matched ? parse_int((*it)[4].str()) : 1;
        if (month >= 1 && month <= 12 && (!(*it)[4].matched || is_valid_date(day, month, year))) {
            continue;
        }
        const auto s = static_cast<std::size_t>((*it).position(2));
        const auto e = static_cast<std::size_t>((*it).position(0) + (*it).length(0));
        add(s, e, "invalid ISO date", UncertaintyCategory::InvalidDate, UncertaintySeverity::Error);
    }
    static const std::regex iso_week_candidate(R"(\b(\d{4})-W(\d{2})(?:-(\d))?\b)", std::regex::icase);
    for (std::sregex_iterator it(input.begin(), input.end(), iso_week_candidate), end; it != end; ++it) {
        const auto week = parse_int((*it)[2].str());
        const auto day = (*it)[3].matched ? parse_int((*it)[3].str()) : 1;
        if (is_valid_iso_week(parse_int((*it)[1].str()), week) && day >= 1 && day <= 7) {
            continue;
        }
        add((*it).position(),
            (*it).position() + (*it).length(),
            "invalid ISO week date",
            UncertaintyCategory::InvalidDate,
            UncertaintySeverity::Error);
    }
    static const std::regex iso_ordinal_candidate(R"(\b(\d{4})-(\d{3})\b)");
    for (std::sregex_iterator it(input.begin(), input.end(), iso_ordinal_candidate), end; it != end; ++it) {
        const auto year = parse_int((*it)[1].str());
        const auto day = parse_int((*it)[2].str());
        const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
        if (day >= 1 && day <= (leap ? 366 : 365)) {
            continue;
        }
        add((*it).position(),
            (*it).position() + (*it).length(),
            "invalid ISO ordinal date",
            UncertaintyCategory::InvalidDate,
            UncertaintySeverity::Error);
    }
    static const std::regex timezone_offset(R"((?:UTC|GMT)?\s*([+-])(\d{2}):?(\d{2})(?!\d))", std::regex::icase);
    for (std::sregex_iterator it(input.begin(), input.end(), timezone_offset), end; it != end; ++it) {
        const auto hour = parse_int((*it)[2].str());
        const auto minute = parse_int((*it)[3].str());
        if (hour < 14 || (hour == 14 && minute == 0)) {
            continue;
        }
        add((*it).position(),
            (*it).position() + (*it).length(),
            "invalid timezone offset",
            UncertaintyCategory::Time,
            UncertaintySeverity::Error);
    }
    static const std::regex iana_zone(R"(\b[A-Za-z_+-]+/[A-Za-z0-9_+/-]+\b)");
    static const std::unordered_set<std::string> supported_iana_zones = {
        "Europe/Kyiv", "Europe/London", "Europe/Warsaw", "America/New_York", "America/Los_Angeles", "Asia/Tokyo"};
    for (std::sregex_iterator it(input.begin(), input.end(), iana_zone), end; it != end; ++it) {
        if (supported_iana_zones.contains(it->str())) {
            continue;
        }
        add((*it).position(),
            (*it).position() + (*it).length(),
            "unrecognized IANA timezone name",
            UncertaintyCategory::Time,
            UncertaintySeverity::Warning);
    }
    ctre_each<R"((^|[^\d.,])(\d{1,3},\d{3})(?![\d]))">(input, [&](const auto& m) {
        const auto s = cap_pos<2>(input, m);
        add(s,
            s + cap<2>(m).size(),
            "single comma group (decimal or thousands separator?)",
            UncertaintyCategory::AmbiguousNumberGrouping,
            UncertaintySeverity::Warning);
    });
    static const std::regex invalid_time(R"((^|[^\d:])(\d{1,3}):(\d{2})(?::(\d{2}))?(?![\d:]))");
    for (std::sregex_iterator it(input.begin(), input.end(), invalid_time), end; it != end; ++it) {
        const auto hour = parse_int((*it)[2].str());
        const auto minute = parse_int((*it)[3].str());
        const auto second = (*it)[4].matched ? parse_int((*it)[4].str()) : 0;
        if ((hour <= 23 || (hour == 24 && minute == 0 && second == 0)) && minute <= 59 && second <= 59) {
            continue;
        }
        const auto s = static_cast<std::size_t>((*it).position(2));
        const auto e = static_cast<std::size_t>((*it).position(0) + (*it).length(0));
        add(s, e, "invalid clock time", UncertaintyCategory::Time, UncertaintySeverity::Error);
    }
    static const std::regex invalid_ampm(R"((^|[^\d:])(\d{1,2}):([0-5]\d)(?::([0-5]\d))?\s*(AM|PM)(?![A-Za-z]))",
                                         std::regex::icase);
    for (std::sregex_iterator it(input.begin(), input.end(), invalid_ampm), end; it != end; ++it) {
        const auto hour = parse_int((*it)[2].str());
        if (hour >= 1 && hour <= 12) {
            continue;
        }
        const auto s = static_cast<std::size_t>((*it).position(2));
        const auto e = static_cast<std::size_t>((*it).position(0) + (*it).length(0));
        add(s, e, "invalid 12-hour clock time", UncertaintyCategory::Time, UncertaintySeverity::Error);
    }
    static const std::regex zero_fraction(R"((^|[^\d/])([+\-−]?\d+/0+)(?!\d))");
    for (std::sregex_iterator it(input.begin(), input.end(), zero_fraction), end; it != end; ++it) {
        const auto s = static_cast<std::size_t>((*it).position(2));
        add(s,
            s + (*it)[2].length(),
            "fraction has a zero denominator",
            UncertaintyCategory::Fraction,
            UncertaintySeverity::Error);
    }
    static const std::regex malformed_scientific(R"((^|[^A-Za-z\d])([+\-]?\d+(?:[.,]\d+)?[eE][+\-]?)(?!\d))");
    for (std::sregex_iterator it(input.begin(), input.end(), malformed_scientific), end; it != end; ++it) {
        const auto s = static_cast<std::size_t>((*it).position(2));
        add(s,
            s + (*it)[2].length(),
            "malformed scientific notation",
            UncertaintyCategory::Scientific,
            UncertaintySeverity::Warning);
    }
    static const std::regex ipv4_like(
        R"((^|[^\d.])(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})(?:/(\d{1,3}))?(?::(\d{1,6}))?(?![\d.]))");
    for (std::sregex_iterator it(input.begin(), input.end(), ipv4_like), end; it != end; ++it) {
        bool invalid = false;
        for (std::size_t i = 2; i <= 5; ++i) {
            invalid = invalid || parse_int((*it)[i].str()) > 255;
        }
        invalid = invalid || ((*it)[6].matched && parse_int((*it)[6].str()) > 32) ||
                  ((*it)[7].matched && parse_int((*it)[7].str()) > 65535);
        if (!invalid) {
            continue;
        }
        const auto s = static_cast<std::size_t>((*it).position(2));
        const auto e = static_cast<std::size_t>((*it).position(0) + (*it).length(0));
        add(s, e, "invalid IP address, CIDR prefix, or port", UncertaintyCategory::Network, UncertaintySeverity::Error);
    }
    static const std::regex geo_candidate(
        R"(\bgeo\s*:\s*([+\-]?\d{1,3}(?:\.\d+)?)\s*[,;]\s*([+\-]?\d{1,3}(?:\.\d+)?)(?:\s*[,;]\s*[+\-]?\d+(?:\.\d+)?)?)",
        std::regex::icase);
    for (std::sregex_iterator it(input.begin(), input.end(), geo_candidate), end; it != end; ++it) {
        const auto latitude = decimal_value((*it)[1].str());
        const auto longitude = decimal_value((*it)[2].str());
        if (latitude && longitude && std::abs(*latitude) <= 90.0 && std::abs(*longitude) <= 180.0) {
            continue;
        }
        add((*it).position(),
            (*it).position() + (*it).length(),
            "coordinate outside latitude or longitude bounds",
            UncertaintyCategory::Coordinate,
            UncertaintySeverity::Error);
    }
    static const std::regex dms_candidate(R"((\d{1,3})\s*°\s*(\d{1,2})\s*(?:′|')\s*(\d{1,2})\s*(?:″|")\s*([NSEW]))",
                                          std::regex::icase);
    for (std::sregex_iterator it(input.begin(), input.end(), dms_candidate), end; it != end; ++it) {
        const auto marker = lower_text((*it)[4].str());
        const auto limit = marker == "n" || marker == "s" ? 90 : 180;
        const auto degrees = parse_int((*it)[1].str());
        const auto minutes = parse_int((*it)[2].str());
        const auto seconds = parse_int((*it)[3].str());
        if (degrees <= limit && minutes <= 59 && seconds <= 59 && (degrees < limit || (minutes == 0 && seconds == 0))) {
            continue;
        }
        add((*it).position(),
            (*it).position() + (*it).length(),
            "invalid degrees, minutes, or seconds coordinate",
            UncertaintyCategory::Coordinate,
            UncertaintySeverity::Error);
    }
    static const std::unordered_map<std::string, std::string> multisense = {
        {"р", "рік / рядок / річка"},
        {"м", "метр / місто"},
        {"с", "секунда / село / сторінка"},
        {"в", "вік / вулиця / прийменник"},
        {"кв", "квартира / квартал / квадратний"},
        {"ст", "століття / стаття / станція / сторінка"},
        {"п", "пункт / пан / поверх"},
        {"обл", "область / обліковий"}};
    static const std::regex abbr(R"((^|[^А-Яа-яЄєІіЇїҐґ])(кв|обл|ст|р|м|с|в|п)\.(?![а-яіїєґ]))", std::regex::icase);
    for (std::sregex_iterator it(input.begin(), input.end(), abbr), end; it != end; ++it) {
        const auto s = static_cast<std::size_t>((*it).position(2));
        auto left = input.substr(0, s);
        while (!left.empty() && left.back() == ' ') {
            left.pop_back();
        }
        if (!left.empty() && std::isdigit(static_cast<unsigned char>(left.back()))) {
            continue;
        }
        const auto key = lower_text((*it)[2].str());
        add(s,
            s + (*it)[2].length() + 1,
            "ambiguous abbreviation (" + multisense.at(key) + ")",
            UncertaintyCategory::AmbiguousAbbreviation,
            UncertaintySeverity::Warning);
    }
    for (const auto& word : uncertain_word_spans(input)) {
        const auto token = std::string_view(input).substr(word.start, word.stop - word.start);
        bool has_latin = false;
        bool has_uk = false;
        bool has_non_joiner_uk = false;
        for (std::size_t i = word.start; i < word.stop;) {
            std::size_t next = i + 1;
            const auto cp = decode_one(input, i, next);
            has_latin = has_latin || is_latin(cp);
            has_uk = has_uk || is_uk(cp);
            has_non_joiner_uk = has_non_joiner_uk || (is_uk(cp) && !is_word_joiner(cp));
            i = next;
        }
        if (has_latin && has_non_joiner_uk) {
            add(word.start,
                word.stop,
                "mixed-script word (possible typo or spoofing)",
                UncertaintyCategory::MixedScript,
                UncertaintySeverity::Error);
        }
        std::size_t first_next = word.start + 1;
        if (!has_latin || has_non_joiner_uk || !is_latin(decode_one(input, word.start, first_next))) {
            continue;
        }
        if (is_ascii_acronym(token) || english_words().contains(lower_text(token))) {
            continue;
        }
        if (word.start > 0) {
            std::size_t before_start = 0;
            for (std::size_t i = 0; i < word.start;) {
                before_start = i;
                std::size_t next = i + 1;
                decode_one(input, i, next);
                i = next;
            }
            std::size_t ignored = before_start + 1;
            if (is_uk(decode_one(input, before_start, ignored))) {
                continue;
            }
        }
        if (word.stop < input.size()) {
            std::size_t next = word.stop + 1;
            if (is_uk(decode_one(input, word.stop, next))) {
                continue;
            }
        }
        add(word.start,
            word.stop,
            "foreign word (transliteration is approximate)",
            UncertaintyCategory::ForeignWord,
            UncertaintySeverity::Info);
    }
    static const std::unordered_set<std::string> roman_stop = {
        "CD", "DVD", "MD", "DC", "MC", "MI", "MM", "DI", "DIV", "MIX", "CIV", "LCD"};
    ctre_each<R"(\b[MDCLXVI]{2,}\b)">(input, [&](const auto& m) {
        const auto w = whole_string(m);
        if (!roman_stop.contains(w) && valid_roman(w)) {
            const auto s = cap_pos(input, m);
            add(s,
                s + cap<0>(m).size(),
                "Roman numeral (case defaults to nominative)",
                UncertaintyCategory::RomanNumeral,
                UncertaintySeverity::Info);
        }
    });
    static const std::regex isbn_candidate(
        R"(\bISBN(?:-1[03])?\s*[:№#]?\s*((?:97[89][ -]?)?[0-9Xx](?:[ -]?[0-9Xx]){8,12})\b)", std::regex::icase);
    for (std::sregex_iterator it(input.begin(), input.end(), isbn_candidate), end; it != end; ++it) {
        if (!valid_isbn((*it)[1].str())) {
            add((*it).position(),
                (*it).position() + (*it).length(),
                "invalid ISBN checksum or length",
                UncertaintyCategory::Identifier,
                UncertaintySeverity::Error);
        }
    }
    static const std::regex issn_candidate(R"(\bISSN\s*[:№#]?\s*(\d{4}[ -]?\d{3}[\dXx])\b)", std::regex::icase);
    for (std::sregex_iterator it(input.begin(), input.end(), issn_candidate), end; it != end; ++it) {
        if (!valid_issn((*it)[1].str())) {
            add((*it).position(),
                (*it).position() + (*it).length(),
                "invalid ISSN checksum",
                UncertaintyCategory::Identifier,
                UncertaintySeverity::Error);
        }
    }
    static const std::regex iban_candidate(R"(\b[A-Z]{2}\d{2}(?:[ ]?[A-Z0-9]){11,30}\b)", std::regex::icase);
    for (std::sregex_iterator it(input.begin(), input.end(), iban_candidate), end; it != end; ++it) {
        if (!valid_iban(it->str())) {
            add((*it).position(),
                (*it).position() + (*it).length(),
                "invalid IBAN checksum or length",
                UncertaintyCategory::Identifier,
                UncertaintySeverity::Error);
        }
    }
    static const std::regex full_card_candidate(
        R"((^|[^A-Za-zА-Яа-яЄєІіЇїҐґ])((?:картка|картку|карта|карту)\s+(\d(?:[ -]?\d){11,18}))(?!\d))",
        std::regex::icase);
    for (std::sregex_iterator it(input.begin(), input.end(), full_card_candidate), end; it != end; ++it) {
        if (!valid_luhn((*it)[3].str())) {
            const auto s = static_cast<std::size_t>((*it).position(2));
            add(s,
                s + (*it)[2].length(),
                "invalid payment-card checksum",
                UncertaintyCategory::Identifier,
                UncertaintySeverity::Error);
        }
    }
    static const std::regex vin_candidate(R"(\bVIN\s*[:№#]?\s*([A-HJ-NPR-Z0-9]{17})\b)", std::regex::icase);
    for (std::sregex_iterator it(input.begin(), input.end(), vin_candidate), end; it != end; ++it) {
        if (!valid_vin_checksum((*it)[1].str())) {
            add((*it).position(),
                (*it).position() + (*it).length(),
                "invalid VIN checksum",
                UncertaintyCategory::Identifier,
                UncertaintySeverity::Error);
        }
    }
    static const std::regex uuid_candidate(R"(\b[0-9A-Fa-f]{8}(?:-[0-9A-Fa-f]{4}){3}-[0-9A-Fa-f]{12}\b)");
    for (std::sregex_iterator it(input.begin(), input.end(), uuid_candidate), end; it != end; ++it) {
        if (!valid_uuid_variant(it->str())) {
            add((*it).position(),
                (*it).position() + (*it).length(),
                "invalid UUID version or variant",
                UncertaintyCategory::Identifier,
                UncertaintySeverity::Error);
        }
    }
    static const std::regex hash_candidate(
        R"(\b((?:SHA-?(?:1|224|256|384|512)|SHA3-?(?:256|512)|BLAKE2[bs]|MD5))\s*[:=]?\s*([0-9A-Fa-f]{16,128})\b)",
        std::regex::icase);
    for (std::sregex_iterator it(input.begin(), input.end(), hash_candidate), end; it != end; ++it) {
        if (!valid_hash_length((*it)[1].str(), (*it)[2].str())) {
            add((*it).position(),
                (*it).position() + (*it).length(),
                "hash length does not match its algorithm",
                UncertaintyCategory::Identifier,
                UncertaintySeverity::Error);
        }
    }
    static const std::regex identifier(
        R"((?:№\s*[A-Za-zА-Яа-яЄєІіЇїҐґ0-9]+(?:[-/][A-Za-zА-Яа-яЄєІіЇїҐґ0-9]+)+|(?:ЄДРПОУ|РНОКПП|ІПН|ЄРДР)\.?\s*[:№#]?\s*\d{6,20}|паспорт\s+[A-Za-zА-Яа-яЄєІіЇїҐґ]{2}\s*\d{6,9}|(?:картка|картку|карта|карту)\s*\d{4}[\s-]+(?:(?:\*{4}|xxxx|XXXX)[\s-]+(?:\*{4}|xxxx|XXXX)|\d{4}[\s-]+\d{4})[\s-]+\d{4}|\b[A-Z]{2}\d{2}(?:[ ]?[A-Z0-9]){11,30}\b|\b[0-9A-Fa-f]{8}(?:-[0-9A-Fa-f]{4}){3}-[0-9A-Fa-f]{12}\b|\b(?:ISBN|ISSN|VIN|SWIFT|BIC)\b\s*[:№#]?\s*[A-Z0-9 -]{8,32}))",
        std::regex::icase);
    for (std::sregex_iterator it(input.begin(), input.end(), identifier), end; it != end; ++it) {
        add((*it).position(),
            (*it).position() + (*it).length(),
            "structured identifier (domain-specific reading may vary)",
            UncertaintyCategory::Identifier,
            UncertaintySeverity::Info);
    }
    ctre_each<R"(\b[A-Za-z0-9._%+\-]+@(?:\s|$|[^\s@.]+(?:\s|$)|[^\s@]*\.\s))">(input, [&](const auto& m) {
        const auto s = cap_pos(input, m);
        add(s,
            s + cap<0>(m).size(),
            "malformed email-like contact",
            UncertaintyCategory::Web,
            UncertaintySeverity::Warning);
    });
    static const std::regex malformed_url(R"(\bhttps?://(?:\s|$)|\bwww\.(?:\s|$))", std::regex::icase);
    for (std::sregex_iterator it(input.begin(), input.end(), malformed_url), end; it != end; ++it) {
        add((*it).position(),
            (*it).position() + (*it).length(),
            "malformed URL-like token",
            UncertaintyCategory::Web,
            UncertaintySeverity::Warning);
    }
    static const std::regex unsupported_currency(
        R"((^|[^\dA-Za-zА-Яа-яЄєІіЇїҐґ])(\d+(?:[.,]\d+)?)\s*(AED|SAR|ILS|THB|IDR|MYR)(?![A-Za-zА-Яа-яЄєІіЇїҐґ]))",
        std::regex::icase);
    for (std::sregex_iterator it(input.begin(), input.end(), unsupported_currency), end; it != end; ++it) {
        const auto s = static_cast<std::size_t>((*it).position(2));
        const auto e = static_cast<std::size_t>((*it).position(0) + (*it).length(0));
        add(s,
            e,
            "unsupported or ambiguous currency token",
            UncertaintyCategory::Currency,
            UncertaintySeverity::Warning);
    }
    static const std::unordered_set<std::string> known_unit_words = [] {
        std::unordered_set<std::string> out = {"грн",
                                               "коп",
                                               "btc",
                                               "eth",
                                               "usdt",
                                               "bnb",
                                               "тис",
                                               "млн",
                                               "млрд",
                                               "трлн",
                                               "рік",
                                               "року",
                                               "році",
                                               "раз",
                                               "рази",
                                               "разів"};
        for (const auto& entry : lexicon::kCurrencies) {
            out.insert(lower_text(entry.code));
            out.insert(lower_text(entry.main_one));
            out.insert(lower_text(entry.main_few));
            out.insert(lower_text(entry.main_many));
            out.insert(lower_text(entry.sub_one));
            out.insert(lower_text(entry.sub_few));
            out.insert(lower_text(entry.sub_many));
        }
        return out;
    }();
    ctre_each<R"((^|[^\d.,])(\d+(?:[.,]\d+)?)\s*([A-Za-zА-Яа-яЄєІіЇїҐґ]{1,6})(?![A-Za-zА-Яа-яЄєІіЇїҐґ]))">(
        input, [&](const auto& m) {
            const auto original_unit = cap_string<3>(m);
            const auto unit = lower_text(original_unit);
            if (measurements().contains(original_unit) || measurements().contains(unit) ||
                known_unit_words.contains(unit) || counted_nouns().contains(unit)) {
                return;
            }
            const auto s = cap_pos<2>(input, m);
            const auto e = cap_pos<3>(input, m) + cap<3>(m).size();
            add(s,
                e,
                "unknown unit or unsupported unit spelling",
                UncertaintyCategory::Unit,
                UncertaintySeverity::Warning);
        });
    ctre_each<R"((^|[^\d.,])(\d{4})(?!\d|[.,]\d))">(input, [&](const auto& m) {
        const auto n = parse_int(cap<2>(m));
        if (n < 1000 || n > 2099) {
            return;
        }
        const auto e = cap_pos<2>(input, m) + cap<2>(m).size();
        const auto after = input.substr(e, 16);
        if (ctre::search<R"(^\s*(?:рік|року|році|р\.|рр\.|ст\.))">(after)) {
            return;
        }
        add(cap_pos<2>(input, m),
            e,
            "four-digit number (year or cardinal?)",
            UncertaintyCategory::BareNumber,
            UncertaintySeverity::Warning);
    });
    static const std::regex cue_after("^\\s*(?:" + unit_alt() + R"(|%|грн|коп|рік|року|році|тис|млн|млрд|[-–—]))",
                                      std::regex::icase);
    static const std::unordered_set<std::string> governors = {
        "близько", "понад", "менше", "більше", "від", "до",  "із",  "з",   "без",   "після",
        "к",       "у",     "в",     "о",      "об",  "при", "над", "під", "перед", "між"};
    ctre_each<R"((^|[^\d.,:%\-])(\d{1,4})(?![\d.,:%/\-]))">(input, [&](const auto& m) {
        const auto s = cap_pos<2>(input, m);
        const auto e = s + cap<2>(m).size();
        if (seen.contains({s, e})) {
            return;
        }
        const auto digits = cap_string<2>(m);
        if (digits.size() > 1 && digits[0] == '0') {
            return;
        }
        auto left = input.substr(0, s);
        if (auto prev = ctre::search<R"(([А-Яа-яЄєІіЇїҐґ]+)$)">(left);
            prev && governors.contains(lower_text(cap<1>(prev)))) {
            return;
        }
        if (std::regex_search(input.substr(e), cue_after)) {
            return;
        }
        add(s,
            e,
            "bare number (case / cardinal-vs-ordinal undetermined)",
            UncertaintyCategory::BareNumber,
            UncertaintySeverity::Warning);
    });
    static const std::regex agreement_re(
        R"((^|[^А-Яа-яЄєІіЇїҐґ-])(близько|понад|перед|між|над|під|при|після|без|від|до|із|у|в|на|з)\s+(\d{1,6})\s+([^\s\d,.;:!?()]{3,}))",
        std::regex::icase);
    for (std::sregex_iterator it(input.begin(), input.end(), agreement_re), end; it != end; ++it) {
        const auto noun = lower_text((*it)[4].str());
        if (counted_nouns().contains(noun) || measurements().contains((*it)[4].str()) ||
            measurements().contains(noun)) {
            continue;
        }
        if (counted_oblique_cases().contains(noun)) {
            continue;
        }
        const auto s = static_cast<std::size_t>((*it).position(3));
        const auto e = static_cast<std::size_t>((*it).position(4) + (*it).length(4));
        add(s,
            e,
            "number-noun agreement not verified (noun outside lexicon)",
            UncertaintyCategory::Agreement,
            UncertaintySeverity::Info);
    }

    std::sort(spans.begin(), spans.end(), [](const auto& a, const auto& b) {
        return std::tie(a.start, a.stop) < std::tie(b.start, b.stop);
    });
    return spans;
}

} // namespace uktextnorm
