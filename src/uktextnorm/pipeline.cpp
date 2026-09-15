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

static std::vector<UncertainSpan> flag_uncertain_impl(std::string_view text, const NormalizeOptions* options)
{
    std::vector<UncertainSpan> spans;
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
            spans.push_back({s, e, std::string(text.substr(s, e - s)), std::move(reason), category, severity});
        }
    };
    const std::string_view input = text;
    using ViewRegexIterator = std::regex_iterator<std::string_view::const_iterator>;
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
    if (!options || options->numeric_date_order == NumericDateOrder::PreserveAmbiguous) {
        for (ViewRegexIterator it(input.begin(), input.end(), ambiguous_numeric_date), end; it != end; ++it) {
            add((*it).position(),
                (*it).position() + (*it).length(),
                "ambiguous numeric date order (day/month or month/day)",
                UncertaintyCategory::Date,
                UncertaintySeverity::Warning);
        }
    }
    static const std::regex ambiguous_colon(R"((^|[^\d:])(\d{1,2}):([0-5]\d)(?![\d:]))");
    if (!options || options->colon_style == ColonStyle::Contextual) {
        for (ViewRegexIterator it(input.begin(), input.end(), ambiguous_colon), end; it != end; ++it) {
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
    }
    static const std::regex ambiguous_currency_symbol(R"((?:\$|¥)\s*\d+(?:[.,]\d+)?)");
    if (!options || options->currency_symbol_policy == CurrencySymbolPolicy::PreserveAmbiguous) {
        for (ViewRegexIterator it(input.begin(), input.end(), ambiguous_currency_symbol), end; it != end; ++it) {
            add((*it).position(),
                (*it).position() + (*it).length(),
                "ambiguous currency symbol (currency depends on locale)",
                UncertaintyCategory::Currency,
                UncertaintySeverity::Warning);
        }
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
    for (ViewRegexIterator it(input.begin(), input.end(), invalid_iso_date), end; it != end; ++it) {
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
    for (ViewRegexIterator it(input.begin(), input.end(), iso_week_candidate), end; it != end; ++it) {
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
    for (ViewRegexIterator it(input.begin(), input.end(), iso_ordinal_candidate), end; it != end; ++it) {
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
    static const std::regex timezone_offset(R"((?:UTC|GMT)\s*([+-])(\d{2}):?(\d{2})(?!\d))", std::regex::icase);
    for (ViewRegexIterator it(input.begin(), input.end(), timezone_offset), end; it != end; ++it) {
        const auto hour = parse_int((*it)[2].str());
        const auto minute = parse_int((*it)[3].str());
        if (minute <= 59 && (hour < 14 || (hour == 14 && minute == 0))) {
            continue;
        }
        add((*it).position(),
            (*it).position() + (*it).length(),
            "invalid timezone offset",
            UncertaintyCategory::Time,
            UncertaintySeverity::Error);
    }
    static const std::regex bare_timezone_offset(R"((^|[\s(])([+-])(\d{2}):(\d{2})(?!\d))");
    for (ViewRegexIterator it(input.begin(), input.end(), bare_timezone_offset), end; it != end; ++it) {
        const auto hour = parse_int((*it)[3].str());
        const auto minute = parse_int((*it)[4].str());
        if (minute <= 59 && (hour < 14 || (hour == 14 && minute == 0))) {
            continue;
        }
        const auto s = static_cast<std::size_t>((*it).position(2));
        add(s,
            s + (*it).length(2) + (*it).length(3) + (*it).length(4) + 1,
            "invalid timezone offset",
            UncertaintyCategory::Time,
            UncertaintySeverity::Error);
    }
    static const std::regex iana_zone(R"((\b\d{1,2}:[0-5]\d(?::[0-5]\d)?\s+)([A-Za-z_+-]+/[A-Za-z0-9_+/-]+)\b)");
    static const std::unordered_set<std::string> supported_iana_zones = {
        "europe/kyiv", "europe/london", "europe/warsaw", "america/new_york", "america/los_angeles", "asia/tokyo"};
    for (ViewRegexIterator it(input.begin(), input.end(), iana_zone), end; it != end; ++it) {
        if (supported_iana_zones.contains(lower_text((*it)[2].str()))) {
            continue;
        }
        const auto start = static_cast<std::size_t>((*it).position(2));
        add(start,
            start + static_cast<std::size_t>((*it).length(2)),
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
    for (ViewRegexIterator it(input.begin(), input.end(), invalid_time), end; it != end; ++it) {
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
    for (ViewRegexIterator it(input.begin(), input.end(), invalid_ampm), end; it != end; ++it) {
        const auto hour = parse_int((*it)[2].str());
        if (hour >= 1 && hour <= 12) {
            continue;
        }
        const auto s = static_cast<std::size_t>((*it).position(2));
        const auto e = static_cast<std::size_t>((*it).position(0) + (*it).length(0));
        add(s, e, "invalid 12-hour clock time", UncertaintyCategory::Time, UncertaintySeverity::Error);
    }
    static const std::regex zero_fraction(R"((^|[^\d/])([+\-−]?\d+/0+)(?!\d))");
    for (ViewRegexIterator it(input.begin(), input.end(), zero_fraction), end; it != end; ++it) {
        const auto s = static_cast<std::size_t>((*it).position(2));
        add(s,
            s + (*it)[2].length(),
            "fraction has a zero denominator",
            UncertaintyCategory::Fraction,
            UncertaintySeverity::Error);
    }
    static const std::regex malformed_scientific(R"((^|[^A-Za-z\d])([+\-]?\d+(?:[.,]\d+)?[eE][+\-]?)(?!\d))");
    for (ViewRegexIterator it(input.begin(), input.end(), malformed_scientific), end; it != end; ++it) {
        static const std::regex ieee_revision(R"(802\.\d{1,2}[eE])");
        if (std::regex_match((*it)[2].str(), ieee_revision)) {
            continue;
        }
        const auto s = static_cast<std::size_t>((*it).position(2));
        add(s,
            s + (*it)[2].length(),
            "malformed scientific notation",
            UncertaintyCategory::Scientific,
            UncertaintySeverity::Warning);
    }
    static const std::regex ipv4_like(
        R"((^|[^\d.])(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})(?:/(\d{1,3}))?(?::(\d{1,6}))?(?![\d.]))");
    for (ViewRegexIterator it(input.begin(), input.end(), ipv4_like), end; it != end; ++it) {
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
    for (ViewRegexIterator it(input.begin(), input.end(), geo_candidate), end; it != end; ++it) {
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
    for (ViewRegexIterator it(input.begin(), input.end(), dms_candidate), end; it != end; ++it) {
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
    for (ViewRegexIterator it(input.begin(), input.end(), abbr), end; it != end; ++it) {
        const auto s = static_cast<std::size_t>((*it).position(2));
        auto left = input.substr(0, s);
        while (!left.empty() && left.back() == ' ') {
            left.remove_suffix(1);
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
        if (is_ascii_acronym(token) || english_words().contains(lower_text(token)) ||
            (options && options->vocabulary.contains(lower_text(token)))) {
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
    for (ViewRegexIterator it(input.begin(), input.end(), isbn_candidate), end; it != end; ++it) {
        if (!valid_isbn((*it)[1].str())) {
            add((*it).position(),
                (*it).position() + (*it).length(),
                "invalid ISBN checksum or length",
                UncertaintyCategory::Identifier,
                UncertaintySeverity::Error);
        }
    }
    static const std::regex issn_candidate(R"(\bISSN(?:-L)?\s*[:№#]?\s*(\d{4}[ -]?\d{3}[\dXx])\b)", std::regex::icase);
    for (ViewRegexIterator it(input.begin(), input.end(), issn_candidate), end; it != end; ++it) {
        if (!valid_issn((*it)[1].str())) {
            add((*it).position(),
                (*it).position() + (*it).length(),
                "invalid ISSN checksum",
                UncertaintyCategory::Identifier,
                UncertaintySeverity::Error);
        }
    }
    static const std::regex iban_candidate(R"(\b[A-Z]{2}[ -]?\d{2}(?:[ -]?[A-Z0-9]){11,30}\b)", std::regex::icase);
    for (ViewRegexIterator it(input.begin(), input.end(), iban_candidate), end; it != end; ++it) {
        if (!valid_iban(it->str())) {
            add((*it).position(),
                (*it).position() + (*it).length(),
                "invalid IBAN checksum or length",
                UncertaintyCategory::Identifier,
                UncertaintySeverity::Error);
        }
    }
    static const std::regex full_card_candidate(
        R"((^|[^A-Za-zА-Яа-яЄєІіЇїҐґ])((?:номер\s+картки|картка|картку|картки|карта|карту)\s+(\d(?:[ -]?\d){11,18}))(?!\d))",
        std::regex::icase);
    for (ViewRegexIterator it(input.begin(), input.end(), full_card_candidate), end; it != end; ++it) {
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
    for (ViewRegexIterator it(input.begin(), input.end(), vin_candidate), end; it != end; ++it) {
        if (!valid_vin_checksum((*it)[1].str())) {
            add((*it).position(),
                (*it).position() + (*it).length(),
                "invalid VIN checksum",
                UncertaintyCategory::Identifier,
                UncertaintySeverity::Error);
        }
    }
    static const std::regex uuid_candidate(
        R"(\b(?:([0-9A-Fa-f]{8}(?:-[0-9A-Fa-f]{4}){3}-[0-9A-Fa-f]{12})|UUID\s*[:=]?\s*([0-9A-Fa-f]{32}))\b)",
        std::regex::icase);
    for (ViewRegexIterator it(input.begin(), input.end(), uuid_candidate), end; it != end; ++it) {
        const auto value = (*it)[1].matched ? (*it)[1].str() : (*it)[2].str();
        if (!valid_uuid_variant(value)) {
            add((*it).position(),
                (*it).position() + (*it).length(),
                "invalid UUID version or variant",
                UncertaintyCategory::Identifier,
                UncertaintySeverity::Error);
        }
    }
    static const std::regex hash_candidate(
        R"(\b((?:SHA-?(?:1|224|256|384|512)|SHA3-?(?:256|512)|BLAKE2[bs]|MD5))\s*[:=]?\s*([0-9A-Fa-f]{1,128})\b)",
        std::regex::icase);
    for (ViewRegexIterator it(input.begin(), input.end(), hash_candidate), end; it != end; ++it) {
        if (!valid_hash_length((*it)[1].str(), (*it)[2].str())) {
            add((*it).position(),
                (*it).position() + (*it).length(),
                "hash length does not match its algorithm",
                UncertaintyCategory::Identifier,
                UncertaintySeverity::Error);
        }
    }
    static const std::regex identifier(
        R"((?:№\s*[A-Za-zА-Яа-яЄєІіЇїҐґ0-9]+(?:[-/][A-Za-zА-Яа-яЄєІіЇїҐґ0-9]+)+|(?:ЄДРПОУ|РНОКПП|ІПН|ЄРДР)\.?\s*[:№#]?\s*\d{6,20}|паспорт\s+[A-Za-zА-Яа-яЄєІіЇїҐґ]{2}\s*\d{6,9}|(?:номер\s+картки|картка|картку|картки|карта|карту)\s*\d(?:[ -]?\d){11,18}|\b[A-Z]{2}[ -]?\d{2}(?:[ -]?[A-Z0-9]){11,30}\b|\b(?:UUID\s*[:=]?\s*)?(?:[0-9A-Fa-f]{8}(?:-[0-9A-Fa-f]{4}){3}-[0-9A-Fa-f]{12}|[0-9A-Fa-f]{32})\b|\b(?:SHA-?(?:1|224|256|384|512)|SHA3-?(?:256|512)|BLAKE2[bs]|MD5)\s*[:=]?\s*[0-9A-Fa-f]{1,128}\b|\b(?:ISBN|ISSN(?:-L)?|VIN|SWIFT|BIC)\b\s*[:№#]?\s*[A-Z0-9 -]{8,32}))",
        std::regex::icase);
    for (ViewRegexIterator it(input.begin(), input.end(), identifier), end; it != end; ++it) {
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
    for (ViewRegexIterator it(input.begin(), input.end(), malformed_url), end; it != end; ++it) {
        add((*it).position(),
            (*it).position() + (*it).length(),
            "malformed URL-like token",
            UncertaintyCategory::Web,
            UncertaintySeverity::Warning);
    }
    static const std::unordered_set<std::string> known_unit_words = [] {
        std::unordered_set<std::string> out = {"грн",
                                               "коп",
                                               "btc",
                                               "eth",
                                               "usdt",
                                               "bnb",
                                               "у",
                                               "в",
                                               "і",
                                               "й",
                                               "та",
                                               "до",
                                               "від",
                                               "на",
                                               "за",
                                               "з",
                                               "із",
                                               "зі",
                                               "по",
                                               "для",
                                               "р",
                                               "рр",
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
    // CTRE's byte-oriented Cyrillic character class captures only the first
    // byte of a UTF-8 letter here, making known units such as "т" look unknown.
    // Match complete two-byte Cyrillic code points instead.
    static const std::regex potential_unit(
        R"((^|[^\d.,:A-Za-z\xD0-\xD3\x80-\xBF])(\d+(?:[.,]\d+)?)\s*((?:[A-Za-z]|[\xD0-\xD3][\x80-\xBF]){1,6})(?!(?:[A-Za-z]|[\xD0-\xD3][\x80-\xBF])))");
    for (ViewRegexIterator it(input.begin(), input.end(), potential_unit), end; it != end; ++it) {
        const auto& m = *it;
        const auto original_unit = m[3].str();
        const auto unit = lower_text(original_unit);
        if (m[2].str().starts_with("802.") && m[2].length() <= 6) {
            continue; // IEEE 802 revision suffix, not a measurement unit.
        }
        if (unit == "g" && m[2].length() == 1 && m[2].str().front() >= '2' && m[2].str().front() <= '6') {
            continue; // 2G–6G mobile-network generation.
        }
        if (measurements().contains(original_unit) || measurements().contains(unit) ||
            known_unit_words.contains(unit) || counted_nouns().contains(unit) || is_ascii_acronym(original_unit)) {
            continue;
        }
        if (m[2].length() == 4 && m.position(2) + m.length(2) < m.position(3)) {
            const auto value = parse_int(m[2].str());
            std::size_t next = 1;
            const auto first_letter = decode_one(original_unit, 0, next);
            if (value >= 1000 && value <= 2099 && is_uk(first_letter) && !is_upper_uk(first_letter)) {
                continue; // A probable year followed by ordinary lower-case prose.
            }
        }
        const auto unit_cps = codepoints(original_unit);
        if (unit_cps.size() > 3 && std::ranges::all_of(unit_cps, [](const auto& cp) {
                return is_uk(cp.value) && !is_upper_uk(cp.value);
            })) {
            continue; // A full lower-case Ukrainian word is usually prose.
        }
        const auto s = static_cast<std::size_t>(m.position(2));
        const auto e = static_cast<std::size_t>(m.position(3) + m.length(3));
        if (e < input.size() && input[e] == '/') {
            std::size_t end_of_denominator = e + 1;
            int letters = 0;
            while (end_of_denominator < input.size() && letters < 4) {
                std::size_t next = end_of_denominator + 1;
                const auto cp = decode_one(input, end_of_denominator, next);
                if (!is_latin(cp) && !(is_uk(cp) && !is_word_joiner(cp))) {
                    break;
                }
                end_of_denominator = next;
                ++letters;
            }
            auto full_unit = original_unit;
            full_unit.append(input.substr(e, end_of_denominator - e));
            if (measurements().contains(full_unit) || measurements().contains(lower_text(full_unit))) {
                continue;
            }
            if (full_unit.ends_with("/c") && measurements().contains(full_unit.substr(0, full_unit.size() - 1) + "с")) {
                continue; // Latin c is a common homoglyph in a /с rate.
            }
        }
        add(s,
            e,
            "unknown unit or unsupported unit spelling",
            UncertaintyCategory::Unit,
            UncertaintySeverity::Warning);
    }
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
        const auto suffix = input.substr(e);
        if (std::regex_search(suffix.begin(), suffix.end(), cue_after)) {
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
    for (ViewRegexIterator it(input.begin(), input.end(), agreement_re), end; it != end; ++it) {
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

    std::vector<std::pair<std::size_t, std::size_t>> accepted_networks;
    static const std::regex accepted_ipv4(
        R"(\b(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})(?:/(\d{1,3}))?(?::(\d{1,5}))?\b)");
    for (ViewRegexIterator it(input.begin(), input.end(), accepted_ipv4), end; it != end; ++it) {
        bool valid = true;
        for (std::size_t i = 1; i <= 4; ++i) {
            valid = valid && parse_int((*it)[i].str()) <= 255;
        }
        valid = valid && (!(*it)[5].matched || parse_int((*it)[5].str()) <= 32) &&
                (!(*it)[6].matched || parse_int((*it)[6].str()) <= 65535);
        if (valid) {
            const auto start = static_cast<std::size_t>((*it).position());
            accepted_networks.emplace_back(start, start + (*it).length());
        }
    }
    std::vector<std::pair<std::size_t, std::size_t>> structured_ranges;
    for (const auto& span : spans) {
        if (span.category == UncertaintyCategory::Identifier || span.category == UncertaintyCategory::Time) {
            structured_ranges.emplace_back(span.start, span.stop);
        }
    }
    std::erase_if(spans, [&](const auto& candidate) {
        if ((candidate.category == UncertaintyCategory::Date || candidate.category == UncertaintyCategory::Fraction ||
             candidate.category == UncertaintyCategory::BareNumber) &&
            std::ranges::any_of(accepted_networks, [&](const auto& network) {
                return network.first <= candidate.start && network.second >= candidate.stop;
            })) {
            return true;
        }
        if (candidate.category != UncertaintyCategory::BareNumber && candidate.category != UncertaintyCategory::Unit &&
            candidate.category != UncertaintyCategory::ForeignWord && candidate.category != UncertaintyCategory::Time) {
            return false;
        }
        return std::ranges::any_of(structured_ranges, [&](const auto& container) {
            return container.first <= candidate.start && container.second >= candidate.stop &&
                   (container.first != candidate.start || container.second != candidate.stop);
        });
    });
    std::sort(spans.begin(), spans.end(), [](const auto& a, const auto& b) {
        return std::tie(a.start, a.stop) < std::tie(b.start, b.stop);
    });
    // Only returned span boundaries need character offsets. Keep filtering in
    // byte offsets, then convert the boundaries in one UTF-8 scan.
    std::vector<std::pair<std::size_t, std::size_t*>> boundaries;
    boundaries.reserve(spans.size() * 2);
    for (auto& span : spans) {
        boundaries.emplace_back(span.start, &span.start);
        boundaries.emplace_back(span.stop, &span.stop);
    }
    std::sort(boundaries.begin(), boundaries.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::size_t byte_offset = 0;
    std::size_t character_offset = 0;
    for (const auto& [target, destination] : boundaries) {
        while (byte_offset < target) {
            std::size_t next = byte_offset + 1;
            decode_one(text, byte_offset, next);
            if (next > target) {
                break;
            }
            byte_offset = next;
            ++character_offset;
        }
        *destination = character_offset;
    }
    return spans;
}

std::vector<UncertainSpan> flag_uncertain(std::string_view text)
{
    return flag_uncertain_impl(text, nullptr);
}

std::vector<UncertainSpan> flag_uncertain(std::string_view text, const NormalizeOptions& options)
{
    return flag_uncertain_impl(text, &options);
}

} // namespace uktextnorm
