#include "uktextnorm/uktextnorm.hpp"

#include "internal.hpp"

#include "generated/uktextnorm_lexicons.hpp"

namespace uktextnorm {

using namespace detail;

namespace {

bool preceded_by_classification_label(std::string_view prefix)
{
    auto lowered = lower_text(prefix.substr(prefix.size() > 96 ? prefix.size() - 96 : 0));
    while (!lowered.empty() && std::isspace(static_cast<unsigned char>(lowered.back()))) {
        lowered.pop_back();
    }
    // Dissertation catalogue entries commonly put the speciality code after
    // "... технічних наук:" rather than after an explicit "спеціальність" label.
    if (lowered.ends_with("наук:") && lowered.find("дис") != std::string::npos) {
        return true;
    }
    return std::ranges::any_of(std::array<std::string_view, 8>{"спеціальністю",
                                                               "спеціальностями",
                                                               "спеціальностей",
                                                               "спеціальності",
                                                               "спеціальність",
                                                               "напряму",
                                                               "код",
                                                               "шифр"},
                               [&](std::string_view label) { return lowered.ends_with(label); });
}

std::string protect_opaque_markup(std::string text,
                                  std::vector<std::pair<std::string, std::string>>& protected_spans,
                                  NumericDateOrder numeric_date_order,
                                  bool validate_dates)
{
    // Citation page markers in Wikipedia extracts look like invalid clock
    // values (".:33–34:39–43").  Remove this metadata before invalid-time
    // protection; otherwise only fragments of the marker are spoken.
    static const std::regex wikipedia_page_citation(R"(\.(?::\d+(?:(?:-|–|—)\d+)?)+(?=\s|$))");
    if (text.find(".:") != std::string::npos) {
        text = regex_sub(std::move(text), wikipedia_page_citation, [](const std::smatch&) { return "."; });
    }
    auto protect = [&](std::string value) {
        std::string key;
        append_utf8(key, U'\uE000');
        append_utf8(key, static_cast<char32_t>(U'\uE100' + protected_spans.size()));
        append_utf8(key, U'\uE001');
        protected_spans.emplace_back(key, std::move(value));
        return key;
    };
    // MediaWiki plain-text extracts can retain a TeX serialization after the
    // rendered formula.  Treat a balanced `\displaystyle` group like code:
    // partial number/symbol expansion would corrupt it and is not a meaningful
    // spoken rendering.
    std::string protected_math;
    std::size_t math_copied = 0;
    std::size_t math_search = 0;
    constexpr std::string_view display_math = R"({\displaystyle)";
    while (true) {
        const auto start = text.find(display_math, math_search);
        if (start == std::string::npos) {
            break;
        }
        std::size_t stop = start;
        int depth = 0;
        bool escaped = false;
        for (; stop < text.size(); ++stop) {
            const char ch = text[stop];
            if (escaped) {
                escaped = false;
                continue;
            }
            if (ch == '\\') {
                escaped = true;
                continue;
            }
            if (ch == '{') {
                ++depth;
            } else if (ch == '}' && --depth == 0) {
                ++stop;
                break;
            }
        }
        if (depth != 0) {
            break;
        }
        protected_math.append(text, math_copied, start - math_copied);
        protected_math += protect(text.substr(start, stop - start));
        math_copied = stop;
        math_search = stop;
    }
    if (math_copied != 0) {
        protected_math.append(text, math_copied, std::string::npos);
        text = std::move(protected_math);
    }
    std::string protected_ipa;
    std::size_t ipa_copied = 0;
    std::size_t ipa_search = 0;
    while (true) {
        const auto start = text.find('[', ipa_search);
        if (start == std::string::npos) {
            break;
        }
        const auto stop = text.find(']', start + 1);
        if (stop == std::string::npos || text.find_first_of("\r\n", start + 1) < stop) {
            break;
        }
        const auto candidate = std::string_view(text).substr(start, stop - start + 1);
        const bool has_ipa = std::ranges::any_of(codepoints(candidate), [](const auto& cp) {
            return (cp.value >= U'\u0250' && cp.value <= U'\u02FF') || (cp.value >= U'\u1D00' && cp.value <= U'\u1D7F');
        });
        if (!has_ipa) {
            ipa_search = stop + 1;
            continue;
        }
        protected_ipa.append(text, ipa_copied, start - ipa_copied);
        protected_ipa += protect(text.substr(start, stop - start + 1));
        ipa_copied = stop + 1;
        ipa_search = stop + 1;
    }
    if (ipa_copied != 0) {
        protected_ipa.append(text, ipa_copied, std::string::npos);
        text = std::move(protected_ipa);
    }
    static const std::regex opaque(
        R"((<!--[\s\S]*?-->|```[\s\S]*?```|~~~[\s\S]*?~~~|``(?:[^`\r\n]|`(?!`))*``|`[^`\r\n]*`|<(code|pre)\b[^>]*>[\s\S]*?</\2\s*>|</?[A-Za-z][A-Za-z0-9:_-]*(?:\s+[^<>]*?)?\s*/?>|<![A-Za-z][^<>]*>|&(?:#[0-9]+|#[xX][0-9A-Fa-f]+|[A-Za-z][A-Za-z0-9]+);))",
        std::regex::icase);
    text = regex_sub(std::move(text), opaque, [&](const std::smatch& m) { return protect(m.str()); });
    // std::regex cannot balance parentheses. Scan Markdown destinations so a
    // URL such as `a_(b)` remains opaque all the way to its matching `)`.
    std::string protected_links;
    std::size_t copied = 0;
    std::size_t search = 0;
    while (true) {
        const auto opener = text.find("](", search);
        if (opener == std::string::npos) {
            break;
        }
        const auto destination = opener + 2;
        std::size_t pos = destination;
        int depth = 1;
        bool escaped = false;
        for (; pos < text.size() && text[pos] != '\n' && text[pos] != '\r'; ++pos) {
            const char ch = text[pos];
            if (escaped) {
                escaped = false;
                continue;
            }
            if (ch == '\\') {
                escaped = true;
                continue;
            }
            if (ch == '(') {
                ++depth;
            } else if (ch == ')' && --depth == 0) {
                break;
            }
        }
        if (depth != 0) {
            break;
        }
        protected_links.append(text, copied, destination - copied);
        protected_links += protect(text.substr(destination, pos - destination));
        protected_links.push_back(')');
        copied = pos + 1;
        search = copied;
    }
    if (copied != 0) {
        protected_links.append(text, copied, std::string::npos);
        text = std::move(protected_links);
    }
    static const std::regex markdown_reference(R"((^|\n)([ \t]{0,3}\[[^\]:\r\n]+\]:[ \t]*)(\S+))", std::regex::icase);
    text = regex_sub(
        text, markdown_reference, [&](const std::smatch& m) { return m[1].str() + m[2].str() + protect(m[3].str()); });

    static const std::regex malformed_scientific(R"((^|[^A-Za-z\d])([+\-]?\d+(?:[.,]\d+)?[eE][+\-]?)(?![+\-]?\d))");
    text = regex_sub(text, malformed_scientific, [&](const std::smatch& m) {
        static const std::regex ieee_revision(R"(802\.\d{1,2}[eE])");
        return std::regex_match(m[2].str(), ieee_revision) ? m.str() : m[1].str() + protect(m[2].str());
    });
    static const std::regex isbn_candidate(
        R"(\bISBN(?:-1[03])?\s*[:№#]?\s*((?:97[89][ -]?)?[0-9Xx](?:[ -]?[0-9Xx]){8,12})\b)", std::regex::icase);
    text = regex_sub(text, isbn_candidate, [&](const std::smatch& m) {
        return valid_isbn(m[1].str()) ? m.str() : protect(m.str());
    });
    static const std::regex labelled_hash_candidate(
        R"(\b((?:SHA-?(?:1|224|256|384|512)|SHA3-?(?:256|512)|BLAKE2[bs]|MD5))\s*[:=]?\s*([0-9A-Fa-f]{1,128})\b)",
        std::regex::icase);
    text = regex_sub(text, labelled_hash_candidate, [&](const std::smatch& m) {
        return valid_hash_length(m[1].str(), m[2].str()) ? m.str() : protect(m.str());
    });

    static const std::regex iso_duration_candidate(R"(\bP(?=\d|T(?:\d|[.,]\d))[0-9YMWDTHS.,]+\b)", std::regex::icase);
    static const std::regex valid_iso_duration(
        R"(P(?:(\d+(?:[.,]\d+)?)Y)?(?:(\d+(?:[.,]\d+)?)M)?(?:(\d+(?:[.,]\d+)?)W)?(?:(\d+(?:[.,]\d+)?)D)?(?:T(?:(\d+(?:[.,]\d+)?)H)?(?:(\d+(?:[.,]\d+)?)M)?(?:(\d+(?:[.,]\d+)?)S)?)?)",
        std::regex::icase);
    text = regex_sub(text, iso_duration_candidate, [&](const std::smatch& m) {
        std::smatch parsed;
        const auto token = m.str();
        if (!std::regex_match(token, parsed, valid_iso_duration)) {
            return protect(token);
        }
        bool has_component = false;
        for (std::size_t i = 1; i < parsed.size(); ++i) {
            has_component = has_component || parsed[i].matched;
        }
        return !has_component || token.ends_with('T') || token.ends_with('t') ? protect(token) : token;
    });

    if (text.contains(':')) {
        text = normalize_biblical_references(std::move(text));
    }
    static const std::regex invalid_clock_candidate(R"((^|[^\d:])(\d{1,3}):(\d{2})(?::(\d{2}))?(?![\d:]))");
    text = regex_sub(text, invalid_clock_candidate, [&](const std::smatch& m) {
        const auto invalid = parse_int(m[2].str()) > 24 ||
                             (parse_int(m[2].str()) == 24 &&
                              (parse_int(m[3].str()) != 0 || (m[4].matched && parse_int(m[4].str()) != 0))) ||
                             parse_int(m[3].str()) > 59 || (m[4].matched && parse_int(m[4].str()) > 59);
        return invalid ? m[1].str() + protect(m[2].str() + ":" + m[3].str() + (m[4].matched ? ":" + m[4].str() : ""))
                       : m.str();
    });
    static const std::regex zoned_clock_candidate(
        R"((\d{1,2}):([0-5]\d)(?::([0-5]\d))?\s*(?:UTC|GMT)\s*([+-])(\d{1,2})(?::?(\d{2}))?)", std::regex::icase);
    text = regex_sub(text, zoned_clock_candidate, [&](const std::smatch& m) {
        const auto offset_hour = parse_int(m[5].str());
        const auto offset_minute = m[6].matched ? parse_int(m[6].str()) : 0;
        return offset_hour > 14 || offset_minute > 59 || (offset_hour == 14 && offset_minute != 0) ? protect(m.str())
                                                                                                   : m.str();
    });
    static const std::regex standalone_timezone_candidate(R"(\b(?:UTC|GMT)\s*[+-](\d{2}):?(\d{2})\b)",
                                                          std::regex::icase);
    text = regex_sub(text, standalone_timezone_candidate, [&](const std::smatch& m) {
        const auto hour = parse_int(m[1].str());
        const auto minute = parse_int(m[2].str());
        return hour > 14 || minute > 59 || (hour == 14 && minute != 0) ? protect(m.str()) : m.str();
    });
    static const std::regex bare_zoned_clock_candidate(R"(\b\d{1,2}:[0-5]\d(?::[0-5]\d)?\s+[+-](\d{2}):(\d{2})(?!\d))");
    text = regex_sub(text, bare_zoned_clock_candidate, [&](const std::smatch& m) {
        const auto hour = parse_int(m[1].str());
        const auto minute = parse_int(m[2].str());
        return hour > 14 || minute > 59 || (hour == 14 && minute != 0) ? protect(m.str()) : m.str();
    });

    static const std::regex ipv4_candidate(
        R"((^|[^\d.])(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})(?:/(\d{1,3}))?(?::(\d{1,6}))?(?![\d.]))");
    text = regex_sub(text, ipv4_candidate, [&](const std::smatch& m) {
        bool invalid = false;
        for (std::size_t i = 2; i <= 5; ++i) {
            invalid = invalid || parse_int(m[i].str()) > 255;
        }
        invalid =
            invalid || (m[6].matched && parse_int(m[6].str()) > 32) || (m[7].matched && parse_int(m[7].str()) > 65535);
        return invalid ? m[1].str() + protect(m[0].str().substr(m[1].length())) : m.str();
    });
    static const std::regex bracketed_ipv6_port_candidate(R"((^|[^0-9A-Fa-f:])(\[[0-9A-Fa-f:]+\]):(\d{1,6})(?!\d))");
    text = regex_sub(text, bracketed_ipv6_port_candidate, [&](const std::smatch& m) {
        return parse_int(m[3].str()) > 65535 ? m[1].str() + protect(m[2].str() + ":" + m[3].str()) : m.str();
    });
    static const std::regex invalid_ipv6_prefix(
        R"((^|[^0-9A-Fa-f:])((?:[0-9A-Fa-f]{0,4}:){2,7}[0-9A-Fa-f]{0,4}/(\d{1,3}))(?![0-9A-Fa-f:/]))");
    text = regex_sub(text, invalid_ipv6_prefix, [&](const std::smatch& m) {
        return parse_int(m[3].str()) > 128 ? m[1].str() + protect(m[2].str()) : m.str();
    });

    static const std::regex geo_candidate(
        R"(\bgeo\s*:\s*([+\-]?\d{1,3}(?:\.\d+)?)\s*[,;]\s*([+\-]?\d{1,3}(?:\.\d+)?)(?:\s*[,;]\s*[+\-]?\d+(?:\.\d+)?)?)",
        std::regex::icase);
    text = regex_sub(text, geo_candidate, [&](const std::smatch& m) {
        try {
            if (std::abs(std::stod(m[1].str())) > 90.0 || std::abs(std::stod(m[2].str())) > 180.0) {
                return protect(m.str());
            }
        } catch (...) {
            return protect(m.str());
        }
        return m.str();
    });
    static const std::regex labelled_coordinate(
        R"((?:lat(?:itude)?|широта)\s*[:=]\s*([+\-]?\d{1,3}(?:[.,]\d+)?)\s*[,; ]+\s*(?:lon(?:gitude)?|довгота)\s*[:=]\s*([+\-]?\d{1,3}(?:[.,]\d+)?))",
        std::regex::icase);
    text = regex_sub(text, labelled_coordinate, [&](const std::smatch& m) {
        auto number = [](std::string token) {
            replace_all(token, ",", ".");
            return std::stod(token);
        };
        try {
            return std::abs(number(m[1].str())) <= 90.0 && std::abs(number(m[2].str())) <= 180.0 ? m.str()
                                                                                                 : protect(m.str());
        } catch (...) {
            return protect(m.str());
        }
    });
    static const std::regex dms_coordinate(
        R"((\d{1,3})\s*°\s*(?:(\d{1,2})([.,]\d+)?\s*(?:′|')\s*)?(?:(\d{1,2})\s*(?:″|")\s*)?([NSEW]))",
        std::regex::icase);
    text = regex_sub(text, dms_coordinate, [&](const std::smatch& m) {
        const auto marker = lower_text(m[5].str());
        const auto limit = marker == "n" || marker == "s" ? 90 : 180;
        const auto degrees = parse_int(m[1].str());
        const auto minutes = m[2].matched ? parse_int(m[2].str()) : 0;
        const auto fractional_minutes = m[3].matched && m[3].str().find_first_not_of(".,0") != std::string::npos;
        const auto seconds = m[4].matched ? parse_int(m[4].str()) : 0;
        return degrees > limit || minutes > 59 || seconds > 59 ||
                       (degrees == limit && (minutes != 0 || fractional_minutes || seconds != 0))
                   ? protect(m.str())
                   : m.str();
    });

    if (validate_dates) {
        static const std::regex local_date(
            R"((^|[^\d./-])(\d{1,2})[./-](\d{1,2})[./-](\d{2}|\d{4})(?!\d)(?![./-]\d)(?!x\d)(?!X\d)(?!х\d)(?!Х\d)(?!×\d))");
        text = regex_sub(text, local_date, [&](const std::smatch& m) {
            if (preceded_by_classification_label(
                    std::string_view(text).substr(0, static_cast<std::size_t>(m.position(2))))) {
                return m.str();
            }
            auto first = parse_int(m[2].str());
            auto second = parse_int(m[3].str());
            const auto short_year = parse_int(m[4].str());
            const auto year =
                m[4].length() == 2 ? (short_year < 50 ? 2000 + short_year : 1900 + short_year) : short_year;
            const auto month_first =
                numeric_date_order == NumericDateOrder::MonthDayYear ||
                (first <= 12 && second > 12 &&
                 (numeric_date_order == NumericDateOrder::PreserveAmbiguous ||
                  (numeric_date_order == NumericDateOrder::DayMonthYear && m.str().contains('/'))));
            const auto day = month_first ? second : first;
            const auto month = month_first ? first : second;
            return is_valid_date(day, month, year)
                       ? m.str()
                       : m[1].str() + protect(m[0].str().substr(static_cast<std::size_t>(m[1].length())));
        });
        static const std::regex iso_date(R"(\b(\d{4})-(\d{2})-(\d{2})\b)");
        text = regex_sub(text, iso_date, [&](const std::smatch& m) {
            return is_valid_date(parse_int(m[3].str()), parse_int(m[2].str()), parse_int(m[1].str()))
                       ? m.str()
                       : protect(m.str());
        });
        static const std::regex iso_week(R"(\b(\d{4})-W(\d{2})(?:-(\d))?\b)", std::regex::icase);
        text = regex_sub(text, iso_week, [&](const std::smatch& m) {
            const auto valid = is_valid_iso_week(parse_int(m[1].str()), parse_int(m[2].str())) &&
                               (!m[3].matched || (parse_int(m[3].str()) >= 1 && parse_int(m[3].str()) <= 7));
            return valid ? m.str() : protect(m.str());
        });
        static const std::regex iso_ordinal(R"(\b(\d{4})-(\d{3})\b)");
        text = regex_sub(text, iso_ordinal, [&](const std::smatch& m) {
            const auto year = parse_int(m[1].str());
            const auto day = parse_int(m[2].str());
            const auto leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
            return day >= 1 && day <= (leap ? 366 : 365) ? m.str() : protect(m.str());
        });
    }

    static const std::regex iana_zone(R"((\b\d{1,2}:[0-5]\d(?::[0-5]\d)?\s+)([A-Za-z_+-]+/[A-Za-z0-9_+/-]+)\b)");
    static const std::unordered_set<std::string> known_zones = {
        "europe/kyiv", "europe/london", "europe/warsaw", "america/new_york", "america/los_angeles", "asia/tokyo"};
    text = regex_sub(text, iana_zone, [&](const std::smatch& m) {
        return known_zones.contains(lower_text(m[2].str())) ? m.str() : m[1].str() + protect(m[2].str());
    });
    return text;
}

std::string restore_opaque_markup(std::string text,
                                  const std::vector<std::pair<std::string, std::string>>& protected_spans)
{
    for (auto it = protected_spans.rbegin(); it != protected_spans.rend(); ++it) {
        replace_all(text, it->first, it->second);
    }
    return text;
}

std::string normalize_output_spacing(std::string text)
{
    static const std::regex before_punctuation(R"([ \t]+([,.;:!?]))");
    return std::regex_replace(text, before_punctuation, "$1");
}

std::string strip_mediawiki_heading_markup(std::string text)
{
    std::string out;
    out.reserve(text.size());
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto newline = text.find('\n', start);
        const auto stop = newline == std::string::npos ? text.size() : newline;
        auto line = std::string_view(text).substr(start, stop - start);
        auto first = line.find_first_not_of(" \t\r");
        auto last = line.find_last_not_of(" \t\r");
        bool stripped = false;
        if (first != std::string_view::npos && line[first] == '=' && line[last] == '=') {
            std::size_t left = first;
            while (left <= last && line[left] == '=') {
                ++left;
            }
            std::size_t right = last + 1;
            while (right > left && line[right - 1] == '=') {
                --right;
            }
            const auto left_marks = left - first;
            const auto right_marks = last + 1 - right;
            if (left_marks >= 2 && left_marks <= 6 && left_marks == right_marks) {
                const auto content_first = line.find_first_not_of(" \t", left);
                const auto content_last = right == 0 ? std::string_view::npos : line.find_last_not_of(" \t", right - 1);
                if (content_first != std::string_view::npos && content_first < right && content_last >= content_first) {
                    out.append(line.substr(content_first, content_last - content_first + 1));
                    stripped = true;
                }
            }
        }
        if (!stripped) {
            out.append(line);
        }
        if (newline == std::string::npos) {
            break;
        }
        out.push_back('\n');
        start = newline + 1;
    }
    return out;
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
    std::string text =
        protect_opaque_markup(std::string(input), protected_spans, options.numeric_date_order, options.validate_dates);
    if (options.currency_symbol_policy == CurrencySymbolPolicy::PreserveAmbiguous) {
        protect_ambiguous_currency_symbols(text, protected_spans);
    }
    if (options.numeric_date_order == NumericDateOrder::PreserveAmbiguous) {
        protect_ambiguous_numeric_dates(text, protected_spans);
    }
    const auto maybe_digits = [&] { return has_ascii_digit(text); };
    const auto maybe_roman = [&] { return has_roman_candidate(text); };
    const auto maybe_currency = [&] { return has_currency_candidate(text); };
    const auto maybe_finance = [&] {
        if (text.contains("₿")) {
            return true;
        }
        const auto lowered = lower_text(text);
        for (const auto& entry : lexicon::kFinanceUnits) {
            const auto code = lower_text(entry.code);
            std::size_t pos = 0;
            while ((pos = lowered.find(code, pos)) != std::string::npos) {
                const auto is_ascii_word = [](unsigned char ch) { return std::isalnum(ch) || ch == '_'; };
                const bool left_boundary = pos == 0 || !is_ascii_word(lowered[pos - 1]);
                const auto end = pos + code.size();
                const bool right_boundary = end == lowered.size() || !is_ascii_word(lowered[end]);
                if (left_boundary && right_boundary) {
                    return true;
                }
                pos = end;
            }
        }
        return false;
    };

    text = normalize_unicode(std::move(text), options.quote_style);
    text = normalize_typography(std::move(text));
    // Isolated mathematical variables must not pass through Latin/Cyrillic
    // homoglyph repair (ρh would otherwise become the unreadable ρг).
    if (text.contains("ρh")) {
        replace_all(text, "ρh", " ро аш");
    }
    if (text.contains("з α =")) {
        replace_all(text, "з α =", "з альфою, що дорівнює");
    }
    if (text.contains("З α =")) {
        replace_all(text, "З α =", "З альфою, що дорівнює");
    }
    if (text.contains("α =")) {
        replace_all(text, "α =", "альфа =");
    }
    if (text.contains('$') || text.contains("¥")) {
        text = normalize_regional_currency_aliases(std::move(text));
    }
    if (text.contains("==")) {
        text = strip_mediawiki_heading_markup(std::move(text));
    }
    if (options.normalize_network_addresses && contains_any(text, ".:-")) {
        text = normalize_ip_addresses(std::move(text));
    }
    if (maybe_digits()) {
        text = normalize_page_ranges(std::move(text), options.range_style);
    }
    if (options.normalize_english_words && has_ascii_digit(text) && has_ascii_alpha(text)) {
        // Progressive-scan resolution suffixes must be read before homoglyph
        // repair can turn the Latin p in "1080p-якістю" into Cyrillic р.
        static const std::regex quality_resolution(
            R"((^|[^A-Za-z0-9\xD0-\xD3\x80-\xBF])(480|576|720|1080|1440|2160|4320)[pP]-(якістю|якість))");
        text = regex_sub(text, quality_resolution, [](const std::smatch& m) {
            return m[1].str() + m[3].str() + " " + number_to_words(parse_ull(m[2].str())) + " пі";
        });
        static const std::regex progressive_resolution(
            R"((^|[^A-Za-z0-9\xD0-\xD3\x80-\xBF])(480|576|720|1080|1440|2160|4320)[pP](?![A-Za-z0-9]))");
        text = regex_sub(text, progressive_resolution, [](const std::smatch& m) {
            return m[1].str() + number_to_words(parse_ull(m[2].str())) + " пі";
        });
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
    if (maybe_finance()) {
        text = normalize_finance(std::move(text), false);
    }
    if (maybe_digits()) {
        static const std::regex grouped_currency("(?:" + currency_token_alt() +
                                                     R"()\s*[1-9]\d{0,2}(?:(?:,\d{3})+\.\d{1,4}|(?:\.\d{3})+,\d{1,4}))",
                                                 std::regex::icase);
        if (std::regex_search(text, grouped_currency)) {
            text = normalize_currency(std::move(text));
        }
        text = normalize_number_groups(std::move(text), options.parse_thousand_separators);
        text = normalize_identifiers(std::move(text));
        text = normalize_cyrillic_alphanumeric(std::move(text));
        text = normalize_text_with_phone_numbers(std::move(text), options.phone_style);
        text = normalize_scientific(std::move(text), options.range_style);
        text = normalize_dates(std::move(text),
                               options.date_style,
                               options.validate_dates,
                               options.range_style,
                               options.numeric_date_order);
        text = normalize_section_ranges(std::move(text), options.range_style);
        text = normalize_ranges(std::move(text), options.range_style);
        text = normalize_discourse_dates(std::move(text));
        text = normalize_coordinates(std::move(text));
        if (contains_any(text, "/°№℃℉K") ||
            contains_any_token(text, {"мм рт", "раз", "тиск", "градус", "град.", "K", "К", "кельвін"})) {
            text = normalize_medical(std::move(text));
        }
        text = normalize_counted_noun_context(std::move(text));
        if (text.contains('%')) {
            text = normalize_percent(std::move(text));
        }
        if (contains_any_token(text, {"тис", "млн", "млрд", "трлн"})) {
            text = normalize_multipliers(std::move(text), true);
        }
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
        if (maybe_currency()) {
            text = normalize_symbol_currency(std::move(text));
        }
        if (contains_any_token(text, {"тис", "млн", "млрд", "трлн"})) {
            text = normalize_multipliers(std::move(text));
        }
        text = normalize_measurements(std::move(text));
    } else if (maybe_roman() || text.contains("ХХ") || text.contains("ХІ") || text.contains("ІХ")) {
        text = normalize_ordinals(std::move(text));
    }
    if (!maybe_digits() && contains_any(text, "½⅓⅔¼¾⅕⅖⅗⅘⅙⅚⅐⅛⅜⅝⅞⅑⅒")) {
        text = normalize_fractions(std::move(text));
    }
    if (maybe_digits() && maybe_currency()) {
        text = normalize_currency(std::move(text));
        text = normalize_overprecise_currency_decimals(std::move(text));
    }
    text = normalize_finance(std::move(text));
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
        if (text.contains('.')) {
            text = normalize_versions(std::move(text));
        }
        if (text.contains(',') || text.contains('.')) {
            text = normalize_decimals(std::move(text));
        }
        text = normalize_text_with_phone_numbers(std::move(text), options.phone_style);
        if (text.contains('-') || text.contains("−")) {
            text = normalize_negatives(std::move(text));
        }
        text = normalize_text_with_numbers(std::move(text));
    }
    if (has_ascii_alpha(text) && has_ascii_digit(text)) {
        text = normalize_technical_alphanumeric(std::move(text));
    }
    if (options.normalize_english_words) {
        if (has_ascii_alpha(text)) {
            text = normalize_english(std::move(text), options.vocabulary);
        }
    }
    if (options.transliterate_latin) {
        text = transliterate_to_cyrillic(text);
    }
    return restore_opaque_markup(normalize_output_spacing(trim_spaces(std::move(text))), protected_spans);
}


} // namespace uktextnorm
