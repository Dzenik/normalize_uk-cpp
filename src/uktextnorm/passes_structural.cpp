#include "uktextnorm/uktextnorm.hpp"

#include "internal.hpp"

namespace uktextnorm::detail {

namespace {

bool is_quote_cp(char32_t cp)
{
    return cp == U'«' || cp == U'»' || cp == U'„' || cp == U'“' || cp == U'”' || cp == U'‟' || cp == U'‹' ||
           cp == U'›' || cp == U'"';
}

bool is_opening_quote_cp(char32_t cp)
{
    return cp == U'«' || cp == U'„' || cp == U'‟' || cp == U'‹';
}

bool is_apostrophe_variant(char32_t cp)
{
    // U+02BC modifier apostrophe, U+00B4 acute, U+2032 prime, U+201B and U+2018 single quotes.
    return cp == U'ʼ' || cp == U'´' || cp == U'′' || cp == U'‛' || cp == U'‘';
}

bool is_uk_letter(char32_t cp)
{
    return is_uk(cp) && !is_word_joiner(cp);
}

bool is_word_alphanumeric(char32_t cp)
{
    return is_uk_letter(cp) || is_latin(cp) || (cp >= U'0' && cp <= U'9');
}

bool parse_octet(std::string_view text, int& value)
{
    if (text.empty() || text.size() > 3) {
        return false;
    }
    value = 0;
    for (const char ch : text) {
        if (ch < '0' || ch > '9') {
            return false;
        }
        value = value * 10 + (ch - '0');
    }
    return value <= 255;
}

bool has_hex_letter(std::string_view text)
{
    return std::any_of(
        text.begin(), text.end(), [](char ch) { return (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f'); });
}

std::string read_ipv6_group(std::string_view group);

std::optional<std::string> read_ipv4_address(const std::array<std::string, 4>& groups)
{
    std::vector<std::string> parts = {"ай пі"};
    for (const auto& group : groups) {
        int octet = 0;
        if (!parse_octet(group, octet)) {
            return std::nullopt;
        }
        parts.push_back(number_to_words(static_cast<unsigned long long>(octet)));
    }
    return join(parts);
}

bool valid_ipv6(std::string_view value)
{
    const auto compression = value.find("::");
    if (compression != std::string_view::npos && compression != value.rfind("::")) {
        return false;
    }
    std::size_t groups = 0;
    for (std::size_t start = 0; start <= value.size();) {
        const auto end = value.find(':', start);
        const auto group = value.substr(start, end == std::string_view::npos ? value.size() - start : end - start);
        if (!group.empty()) {
            if (group.size() > 4 || !std::ranges::all_of(group, [](unsigned char ch) { return std::isxdigit(ch); })) {
                return false;
            }
            ++groups;
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return compression == std::string_view::npos ? groups == 8 : groups < 8;
}

std::optional<std::string> read_ipv6_address(std::string_view value)
{
    if (!valid_ipv6(value)) {
        return std::nullopt;
    }
    std::vector<std::string> groups;
    for (std::size_t start = 0; start < value.size();) {
        if (start + 1 < value.size() && value[start] == ':' && value[start + 1] == ':') {
            groups.emplace_back("скорочення нулів");
            start += 2;
            continue;
        }
        if (value[start] == ':') {
            ++start;
            continue;
        }
        const auto end = value.find(':', start);
        groups.push_back(read_ipv6_group(value.substr(
            start, end == std::string_view::npos ? std::string_view::npos : static_cast<std::size_t>(end - start))));
        start = end == std::string_view::npos ? value.size() : end;
    }
    return "ай пі версії шість " + join(groups, " двокрапка ");
}

bool preceded_by_version_label(std::string_view prefix)
{
    std::size_t end = prefix.size();
    while (end > 0 && static_cast<unsigned char>(prefix[end - 1]) <= ' ') {
        --end;
    }
    std::size_t start = end;
    while (start > 0) {
        const unsigned char ch = static_cast<unsigned char>(prefix[start - 1]);
        if (ch <= ' ' || ch == '(' || ch == '[' || ch == ':' || ch == '=') {
            break;
        }
        --start;
    }
    const auto label = lower_text(prefix.substr(start, end - start));
    return label == "версія" || label == "версії" || label == "version" || label == "ver" || label == "v";
}

std::string read_ipv6_group(std::string_view group)
{
    if (group.empty()) {
        return "порожня група";
    }
    std::vector<std::string> parts;
    std::string current_digits;
    auto flush_digits = [&] {
        if (!current_digits.empty()) {
            parts.push_back(number_to_words_digit_by_digit(current_digits));
            current_digits.clear();
        }
    };
    for (const char ch : group) {
        if (ch >= '0' && ch <= '9') {
            current_digits.push_back(ch);
            continue;
        }
        flush_digits();
        const char letter = static_cast<char>(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
        parts.push_back(spell_identifier_letters(std::string_view(&letter, 1)));
    }
    flush_digits();
    return join(parts);
}

std::string read_coordinate_number(std::string_view digits, char gender = 'm')
{
    return number_words_for_gender(parse_ull(digits), gender);
}

std::string coordinate_hemisphere(std::string marker)
{
    marker = lower_text(marker);
    replace_all(marker, " ", "");
    if (marker == "n") {
        return "північної широти";
    }
    if (marker == "s") {
        return "південної широти";
    }
    if (marker == "e") {
        return "східної довготи";
    }
    if (marker == "w") {
        return "західної довготи";
    }
    if (marker.starts_with("пн") || marker.contains("північ")) {
        return "північної широти";
    }
    if (marker.starts_with("пд") || marker.contains("півден")) {
        return "південної широти";
    }
    if (marker.starts_with("сх") || marker.contains("схід")) {
        return "східної довготи";
    }
    if (marker.starts_with("зх") || marker.contains("зах")) {
        return "західної довготи";
    }
    return marker;
}

std::string read_code_characters(std::string_view value)
{
    std::vector<std::string> parts;
    for (std::size_t i = 0; i < value.size();) {
        const auto ch = static_cast<unsigned char>(value[i]);
        if (ch >= '0' && ch <= '9') {
            parts.push_back(number_to_words_digit_by_digit(value.substr(i, 1)));
            ++i;
            continue;
        }
        std::size_t next = i + 1;
        const auto cp = decode_one(value, i, next);
        if ((cp >= U'A' && cp <= U'Z') || (cp >= U'a' && cp <= U'z') || is_uk(cp)) {
            parts.push_back(spell_identifier_letters(value.substr(i, next - i)));
        }
        i = next;
    }
    return join(parts);
}

} // namespace

std::string normalize_unicode(std::string text, QuoteStyle quote_style)
{
    const auto cps = codepoints(text);
    std::string out;
    out.reserve(text.size());
    for (std::size_t idx = 0; idx < cps.size(); ++idx) {
        const auto cp = cps[idx].value;
        const auto next = idx + 1 < cps.size() ? cps[idx + 1].value : U'\0';
        const auto prev = idx > 0 ? cps[idx - 1].value : U'\0';
        // Targeted NFC for Ukrainian: compose и/і + combining breve/diaeresis.
        if (next == U'̆' && (cp == U'и' || cp == U'И')) {
            append_utf8(out, cp == U'и' ? U'й' : U'Й');
            ++idx;
            continue;
        }
        if (next == U'̈' && (cp == U'і' || cp == U'І')) {
            append_utf8(out, cp == U'і' ? U'ї' : U'Ї');
            ++idx;
            continue;
        }
        const bool prime = cp == U'′';
        if (is_apostrophe_variant(cp) &&
            (prime ? (is_uk_letter(prev) || is_latin(prev)) && (is_uk_letter(next) || is_latin(next))
                   : is_word_alphanumeric(prev) && is_word_alphanumeric(next))) {
            out.push_back('\'');
            continue;
        }
        // Canonicalize visually equivalent mathematical signs and dash-like range
        // separators before any byte-oriented regular expressions see them.
        // The tightly joined technical notation 10−n is a power of ten.
        // Keep this particular mathematical minus until the scientific pass;
        // other minus signs still follow the ordinary signed/range rules.
        if (cp == U'−' && idx >= 2 && cps[idx - 2].value == U'1' && prev == U'0' &&
            (idx == 2 || cps[idx - 3].value < U'0' || cps[idx - 3].value > U'9') && next >= U'0' && next <= U'9') {
            append_utf8(out, cp);
            continue;
        }
        if (cp == U'−' || cp == U'－') {
            out.push_back('-');
            continue;
        }
        if (cp == U'＋') {
            out.push_back('+');
            continue;
        }
        if (cp == U'‐' || cp == U'‑' || cp == U'‒') {
            append_utf8(out, U'–');
            continue;
        }
        if (is_quote_cp(cp) && quote_style != QuoteStyle::Keep) {
            switch (quote_style) {
            case QuoteStyle::Straight:
                out.push_back('"');
                break;
            case QuoteStyle::Guillemets:
                append_utf8(out, is_opening_quote_cp(cp) || cp == U'“' ? U'«' : U'»');
                break;
            case QuoteStyle::Strip:
                if (is_word_alphanumeric(prev) && is_word_alphanumeric(next) && (out.empty() || out.back() != ' ')) {
                    out.push_back(' ');
                }
                break;
            default:
                break;
            }
            continue;
        }
        out.append(text.substr(cps[idx].start, cps[idx].stop - cps[idx].start));
    }
    return out;
}

std::string normalize_homoglyphs(std::string text)
{
    static const std::unordered_map<char32_t, char32_t> latin_to_cyr = {
        {U'a', U'а'}, {U'e', U'е'}, {U'i', U'і'}, {U'o', U'о'}, {U'p', U'р'}, {U'c', U'с'}, {U'x', U'х'},
        {U'y', U'у'}, {U'A', U'А'}, {U'B', U'В'}, {U'C', U'С'}, {U'E', U'Е'}, {U'H', U'Н'}, {U'I', U'І'},
        {U'K', U'К'}, {U'M', U'М'}, {U'O', U'О'}, {U'P', U'Р'}, {U'T', U'Т'}, {U'X', U'Х'}};
    static const std::unordered_map<char32_t, char32_t> cyr_to_latin = [] {
        std::unordered_map<char32_t, char32_t> out;
        for (const auto& [latin, cyr] : latin_to_cyr) {
            out.emplace(cyr, latin);
        }
        return out;
    }();
    const auto words = uncertain_word_spans(text);
    if (words.empty()) {
        return text;
    }
    std::string out;
    out.reserve(text.size());
    std::size_t last = 0;
    for (const auto& word : words) {
        std::size_t latin_count = 0;
        std::size_t cyr_count = 0;
        for (std::size_t i = word.start; i < word.stop;) {
            std::size_t next = i + 1;
            const auto cp = decode_one(text, i, next);
            if (is_latin(cp)) {
                ++latin_count;
            } else if (is_uk_letter(cp)) {
                ++cyr_count;
            }
            i = next;
        }
        if (!latin_count || !cyr_count) {
            continue;
        }
        const bool to_cyrillic = cyr_count >= latin_count;
        const auto& map = to_cyrillic ? latin_to_cyr : cyr_to_latin;
        bool repairable = true;
        std::string repaired;
        for (std::size_t i = word.start; i < word.stop && repairable;) {
            std::size_t next = i + 1;
            const auto cp = decode_one(text, i, next);
            const bool minority = to_cyrillic ? is_latin(cp) : is_uk_letter(cp);
            if (minority) {
                if (const auto it = map.find(cp); it != map.end()) {
                    append_utf8(repaired, it->second);
                } else {
                    repairable = false;
                }
            } else {
                repaired.append(text.substr(i, next - i));
            }
            i = next;
        }
        if (!repairable) {
            continue;
        }
        out.append(text, last, word.start - last);
        out += repaired;
        last = word.stop;
    }
    out.append(text, last, std::string::npos);
    return out;
}

std::string normalize_typography(std::string text)
{
    for (auto space : {"\xC2\xA0", "\xE2\x80\x89", "\xE2\x80\xAF", "\xE2\x81\xA0"}) {
        replace_all(text, space, " ");
    }
    auto strip_exact_pair = [](std::string value, char marker) {
        std::string out;
        for (std::size_t i = 0; i < value.size();) {
            std::size_t j = i;
            while (j < value.size() && value[j] == marker) {
                ++j;
            }
            const auto run = j - i;
            if (run == 2) {
                i = j;
                continue;
            }
            out.append(value.substr(i, run == 0 ? 1 : run));
            i += run == 0 ? 1 : run;
        }
        return out;
    };
    text = strip_exact_pair(std::move(text), '*');
    text = strip_exact_pair(std::move(text), '_');
    replace_all(text, "`", "");
    replace_all(text, "’", "'");
    static const std::regex space_before_punctuation(R"([ \t]+(\.(?=\d|[.,;:!?]|$)|[,;:!?]))");
    text = std::regex_replace(text, space_before_punctuation, "$1");
    static const std::regex spaced_unit_slash(
        R"((км|м|см³|см3|кбіт|Кбіт|мбіт|Мбіт|гбіт|Гбіт)[ \t]*/[ \t]*(год|с(?:²|2)?|c(?:²|2)?)(?![A-Za-z]))");
    text = regex_sub(text, spaced_unit_slash, [](const std::smatch& m) {
        auto denominator = m[2].str();
        if (denominator.starts_with('c')) {
            denominator.replace(0, 1, "с"); // Latin c in a mixed-script /с unit.
        }
        return m[1].str() + "/" + denominator;
    });
    static const std::regex spaced_ascii_slash(R"(([A-Za-z])[ \t]*/[ \t]*([A-Za-z]))");
    text = std::regex_replace(text, spaced_ascii_slash, "$1/$2");
    static const std::regex spaced_cyrillic_dimension(R"((\d)\s+(?:х|Х)\s+(\d))");
    text = std::regex_replace(text, spaced_cyrillic_dimension, "$1 × $2");
    return text;
}

std::string normalize_web(std::string text)
{
    static const std::regex email(
        R"((^|[^A-Za-zА-Яа-яЄєІіЇїҐґ0-9._%+\-])([A-Za-zА-Яа-яЄєІіЇїҐґ0-9._%+\-]+@[A-Za-zА-Яа-яЄєІіЇїҐґ0-9\-]+(?:\.[A-Za-zА-Яа-яЄєІіЇїҐґ0-9\-]+)+))");
    static const std::regex url(
        R"(\b(?:(?:https?|ftp)://|www\.)\S+|\b(?:[A-Za-zА-Яа-яЄєІіЇїҐґ0-9-]+\.)+[A-Za-zА-Яа-яЄєІіЇїҐґ]{2,63}(?:[/?#]\S*)?)",
        std::regex::icase);
    auto spell = [](std::string s) {
        while (!s.empty() && std::string_view(".,!?").contains(s.back())) {
            s.pop_back();
        }
        static const std::regex ukrainian_domain_label(R"(\.(ua|укр)(?=$|[/?#]))", std::regex::icase);
        s = regex_sub(s, ukrainian_domain_label, [](const std::smatch& m) {
            return lower_text(m[1].str()) == "ua" ? " крапка ю ей " : " крапка укр ";
        });
        for (const auto& [sym, word] : std::array<std::pair<std::string_view, std::string_view>, 5>{
                 {{"@", " равлик "}, {".", " крапка "}, {"/", " слеш "}, {":", " двокрапка "}, {"-", " дефіс "}}}) {
            replace_all(s, sym, word);
        }
        replace_all(s, "_", " підкреслення ");
        replace_all(s, "?", " знак питання ");
        replace_all(s, "=", " дорівнює ");
        replace_all(s, "&", " амперсанд ");
        replace_all(s, "#", " решітка ");
        replace_all(s, "+", " плюс ");
        return trim_spaces(std::move(s));
    };
    static const std::regex doi(R"(\bdoi\s*:\s*(10\.\d{4,9}/[-._;()/:A-Za-z0-9]*[A-Za-z0-9]))", std::regex::icase);
    text = regex_sub(text, doi, [&](const std::smatch& m) { return "ді оу ай " + spell(m[1].str()); });
    text = regex_sub(text, email, [&](const std::smatch& m) { return m[1].str() + spell(m[2].str()); });
    text = regex_sub(text, url, [&](const std::smatch& m) { return spell(m.str()); });
    static const std::regex standalone_ukrainian_domain(
        R"((^|[^A-Za-zА-Яа-яЄєІіЇїҐґ0-9])\.(ua|укр)(?![A-Za-zА-Яа-яЄєІіЇїҐґ0-9]))", std::regex::icase);
    text = regex_sub(text, standalone_ukrainian_domain, [](const std::smatch& m) {
        return m[1].str() + (lower_text(m[2].str()) == "ua" ? "крапка ю ей" : "крапка укр");
    });
    text =
        ctre_sub<R"(#([A-Za-zА-Яа-яЄєІіЇїҐґ0-9_]+))">(text, [](const auto& m) { return "хештег " + cap_string<1>(m); });
    static const std::regex handle(R"((^|[^A-Za-zА-Яа-яЄєІіЇїҐґ0-9._%+-])@([A-Za-z][A-Za-z0-9_]{1,30}))");
    return regex_sub(text, handle, [](const std::smatch& m) { return m[1].str() + "акаунт " + m[2].str(); });
}
std::string normalize_addresses(std::string text)
{
    static const std::unordered_map<std::string, std::string> words = {{"м", "місто"},
                                                                       {"с", "село"},
                                                                       {"смт", "селище міського типу"},
                                                                       {"вул", "вулиця"},
                                                                       {"просп", "проспект"},
                                                                       {"пр", "проспект"},
                                                                       {"пров", "провулок"},
                                                                       {"пл", "площа"},
                                                                       {"бул", "бульвар"},
                                                                       {"наб", "набережна"},
                                                                       {"буд", "будинок"},
                                                                       {"б", "будинок"},
                                                                       {"кв", "квартира"},
                                                                       {"оф", "офіс"},
                                                                       {"корп", "корпус"},
                                                                       {"під", "під'їзд"},
                                                                       {"пов", "поверх"},
                                                                       {"обл", "область"},
                                                                       {"р-н", "район"}};
    static const std::regex re(
        R"(((?:смт|просп|пров|корп|буд|вул|наб|бул|оф|кв|обл|під|пов|р-н|пр|пл|м|с|б(?!\.п)))\.(?=\s*[A-Za-zА-Яа-яЄєІіЇїҐґ0-9]))",
        std::regex::icase);
    return regex_sub(text, re, [&](const std::smatch& m) {
        const auto key = lower_text(m[1].str());
        const auto position = static_cast<std::size_t>(m.position());
        if (position != 0) {
            auto previous = position - 1;
            while (previous > 0 && (static_cast<unsigned char>(text[previous]) & 0xC0U) == 0x80U) {
                --previous;
            }
            std::size_t stop = previous + 1;
            const auto cp = decode_one(text, previous, stop);
            if (is_uk_letter(cp) || is_latin(cp) || (cp >= U'0' && cp <= U'9') || cp == U'/') {
                return m.str();
            }
        }
        if (key == "м" || key == "с" || key == "корп") {
            auto next = position + static_cast<std::size_t>(m.length());
            while (next < text.size() && std::isspace(static_cast<unsigned char>(text[next]))) {
                ++next;
            }
            std::size_t stop = next + 1;
            const auto cp = next < text.size() ? decode_one(text, next, stop) : U'\0';
            if (!is_upper_uk(cp) && !(cp >= U'A' && cp <= U'Z') && !(cp >= U'0' && cp <= U'9')) {
                return m.str();
            }
        }
        if (key == "м") {
            const auto preceding = lower_text(text.substr(0, position));
            static const std::regex locative_preposition(R"((^|[\s(])(?:у|в)\s+$)");
            if (std::regex_search(preceding, locative_preposition)) {
                return std::string("місті");
            }
            static const std::regex genitive_preposition(R"((^|[\s(])(?:від|до|з|із|зі|для)\s+$)");
            if (std::regex_search(preceding, genitive_preposition)) {
                return std::string("міста");
            }
            // "м. Києва" is an unambiguous genitive form even when no
            // governing preposition immediately precedes the abbreviation.
            auto next = position + static_cast<std::size_t>(m.length());
            while (next < text.size() && std::isspace(static_cast<unsigned char>(text[next]))) {
                ++next;
            }
            constexpr std::string_view kyiv_genitive = "Києва";
            if (std::string_view(text).substr(next).starts_with(kyiv_genitive)) {
                std::size_t stop = next + kyiv_genitive.size() + 1;
                const auto following = next + kyiv_genitive.size() < text.size()
                                           ? decode_one(text, next + kyiv_genitive.size(), stop)
                                           : U'\0';
                if (!is_uk_letter(following)) {
                    return std::string("міста");
                }
            }
        }
        if (key == "с" && lower_text(text.substr(0, position)).ends_with("вакуумі ")) {
            // Here "с." is the speed-of-light variable ending a sentence, not
            // a village abbreviation introducing the following sentence.
            return m.str();
        }
        return words.at(key);
    });
}

std::string normalize_number_groups(std::string text, bool parse_thousand_separators)
{
    static const std::regex leading_decimal(R"((^|[\s(\[{=:;])([+\-]?)([.,])(\d+)(?![\d.,]))");
    text = regex_sub(text, leading_decimal, [](const std::smatch& m) {
        return m[1].str() + m[2].str() + "0" + m[3].str() + m[4].str();
    });
    text = ctre_sub<R"(\b\d{1,3}(?: \d{3})+\b)">(text, [](const auto& m) {
        auto s = whole_string(m);
        replace_all(s, " ", "");
        return s;
    });
    if (!parse_thousand_separators) {
        return text;
    }
    // 1,234,567 — at least two comma groups of exactly three digits.
    text = ctre_sub<R"((^|[^\d.,])(\d{1,3}(?:,\d{3}){2,})(?!\d))">(text, [](const auto& m) {
        auto s = cap_string<2>(m);
        replace_all(s, ",", "");
        return cap_string<1>(m) + s;
    });
    // 1.234.567 — exactly two dot groups, so IPv4 addresses (three groups) stay intact.
    return ctre_sub<R"((^|[^\d.,])(\d{1,3}(?:\.\d{3}){2})(?!\.?\d))">(text, [](const auto& m) {
        auto s = cap_string<2>(m);
        replace_all(s, ".", "");
        return cap_string<1>(m) + s;
    });
}
std::string normalize_sections(std::string text)
{
    static const std::unordered_map<std::string, std::string> section = {{"ст", "стаття"},
                                                                         {"ч", "частина"},
                                                                         {"пп", "підпункт"},
                                                                         {"п", "пункт"},
                                                                         {"абз", "абзац"},
                                                                         {"розд", "розділ"},
                                                                         {"гл", "глава"},
                                                                         {"табл", "таблиця"},
                                                                         {"рис", "рисунок"}};
    static const std::regex re(R"((^|[^А-Яа-яЄєІіЇїҐґA-Za-z])(ст|ч|пп|п|абз|розд|гл|табл|рис)\.\s*(?=\d|[MDCLXVI]))",
                               std::regex::icase);
    return regex_sub(text, re, [&](const std::smatch& m) {
        const auto key = lower_text(m[2].str());
        if (key == "ст") {
            const auto prefix = m.prefix().str();
            std::smatch prev;
            if (std::regex_search(prefix, prev, std::regex(R"(([MDCLXVI]{1,6})\s*$)"))) {
                if (valid_roman(prev[1].str())) {
                    return m.str();
                }
            }
        }
        return m[1].str() + section.at(key) + " ";
    });
}
std::string normalize_symbols(std::string text)
{
    static const std::vector<std::pair<std::string, std::string>> symbols = {{"°C", "градусів Цельсія"},
                                                                             {"°С", "градусів Цельсія"},
                                                                             {"°F", "градусів Фаренгейта"},
                                                                             {"℃", "градусів Цельсія"},
                                                                             {"℉", "градусів Фаренгейта"},
                                                                             {"°Ra", "градусів Ранкіна"},
                                                                             {"°Ré", "градусів Реомюра"},
                                                                             {"°Re", "градусів Реомюра"},
                                                                             {"°De", "градусів Деліля"},
                                                                             {"°Rø", "градусів Ремера"},
                                                                             {"°Rō", "градусів Ремера"},
                                                                             {"°R", "градусів Ранкіна"},
                                                                             {"°K", "кельвінів"},
                                                                             {"°К", "кельвінів"},
                                                                             {"K", "кельвінів"},
                                                                             {"±", "плюс мінус"},
                                                                             {"≈", "приблизно дорівнює"},
                                                                             {"≠", "не дорівнює"},
                                                                             {"≤", "менше або дорівнює"},
                                                                             {"≥", "більше або дорівнює"},
                                                                             {"×", "помножити на"},
                                                                             {"÷", "поділити на"},
                                                                             {"=", "дорівнює"},
                                                                             {"<", "менше"},
                                                                             {">", "більше"},
                                                                             {"‰", "проміле"},
                                                                             {"§", "параграф"},
                                                                             {"₿", "біткоїн"},
                                                                             {"•", " "},
                                                                             {"·", " "},
                                                                             {"~", "тильда"},
                                                                             {"&", "і"},
                                                                             {"#", "решітка"},
                                                                             {"_", "нижнє підкреслення"},
                                                                             {"²", "у квадраті"},
                                                                             {"³", "у кубі"},
                                                                             {"№", "номер"}};
    for (const auto& [sym, word] : symbols) {
        replace_all(text, sym, " " + word + " ");
    }
    return trim_spaces(std::move(text));
}
std::string normalize_text_with_phone_numbers(std::string text, PhoneStyle style)
{
    text = ctre_sub<
        R"((^|[^\d.,])((?:\+?380|00380|0)\s*\(?\d{2}\)?[\-\s]?\d{3}[\-\s]?\d{2}[\-\s]?\d{2})(?:\s*(?:доб\.?|дод\.?|ext\.?|x)\s*(\d{1,6}))?(?!\d))">(
        text, [&](const auto& m) {
            auto out = cap_string<1>(m) + normalize_phone_number(cap<2>(m), style);
            if (!cap<3>(m).empty()) {
                out += " додатковий " + number_to_words_digit_by_digit(cap<3>(m));
            }
            return out;
        });
    static const std::regex international(
        R"((^|[^\d.,])((?:\+|00)\d{1,3}(?:[\s().-]*\d{1,4}){2,})(?:\s*(?:доб\.?|дод\.?|ext\.?|x)\s*(\d{1,6}))?(?!\d))",
        std::regex::icase);
    return regex_sub(text, international, [&](const std::smatch& m) {
        auto out = m[1].str() + normalize_phone_number(m[2].str(), style);
        if (m[3].matched) {
            out += " додатковий " + number_to_words_digit_by_digit(m[3].str());
        }
        return out;
    });
}

std::string normalize_ip_addresses(std::string text)
{
    static const std::regex cisco_mac(
        R"((^|[^0-9A-Fa-f])([0-9A-Fa-f]{2})([0-9A-Fa-f]{2})\.([0-9A-Fa-f]{2})([0-9A-Fa-f]{2})\.([0-9A-Fa-f]{2})([0-9A-Fa-f]{2})(?![0-9A-Fa-f]))");
    text = regex_sub(text, cisco_mac, [](const std::smatch& m) {
        std::vector<std::string> groups;
        for (std::size_t i = 2; i <= 7; ++i) {
            groups.push_back(read_ipv6_group(m[i].str()));
        }
        return m[1].str() + "мак адреса " + join(groups, " двокрапка ");
    });
    static const std::regex mac(
        R"((^|[^0-9A-Fa-f])([0-9A-Fa-f]{2})[:-]([0-9A-Fa-f]{2})[:-]([0-9A-Fa-f]{2})[:-]([0-9A-Fa-f]{2})[:-]([0-9A-Fa-f]{2})[:-]([0-9A-Fa-f]{2})(?![0-9A-Fa-f]))");
    text = regex_sub(text, mac, [](const std::smatch& m) {
        std::vector<std::string> groups;
        for (std::size_t i = 2; i <= 7; ++i) {
            groups.push_back(read_ipv6_group(m[i].str()));
        }
        return m[1].str() + "мак адреса " + join(groups, " двокрапка ");
    });

    static const std::regex ipv4(
        R"((^|[^\d.])(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})(?:/(\d{1,3}))?(?::(\d{1,5}))?(?![\d:/]|\.\d))");
    text = regex_sub(text, ipv4, [](const std::smatch& m) {
        if (preceded_by_version_label(m.prefix().str())) {
            return m.str();
        }
        const auto address = read_ipv4_address({m[2].str(), m[3].str(), m[4].str(), m[5].str()});
        if (!address) {
            return m.str();
        }
        std::string out = m[1].str() + *address;
        if (m[6].matched) {
            const auto prefix = parse_int(m[6].str());
            if (prefix > 32) {
                return m.str();
            }
            out += " префікс " + number_to_words(static_cast<unsigned long long>(prefix));
        }
        if (m[7].matched) {
            const auto port = parse_int(m[7].str());
            if (port > 65535) {
                return m.str();
            }
            out += " порт " + number_to_words(static_cast<unsigned long long>(port));
        }
        return out;
    });

    static const std::regex bracketed_ipv6_port(R"((^|[^0-9A-Fa-f:])\[([0-9A-Fa-f:]+)\]:(\d{1,5})(?!\d))");
    text = regex_sub(text, bracketed_ipv6_port, [](const std::smatch& m) {
        const auto address = read_ipv6_address(m[2].str());
        if (!address) {
            return m.str();
        }
        const auto port = parse_int(m[3].str());
        if (port > 65535) {
            return m.str();
        }
        return m[1].str() + *address + " порт " + number_to_words(static_cast<unsigned long long>(port));
    });
    static const std::regex bracketed_ipv6(R"((^|[^0-9A-Fa-f:])\[([0-9A-Fa-f:]+)\](?!:))");
    text = regex_sub(text, bracketed_ipv6, [](const std::smatch& m) {
        const auto address = read_ipv6_address(m[2].str());
        return address ? m[1].str() + *address : m.str();
    });

    static const std::regex ipv6(
        R"((^|[^0-9A-Fa-f:])((?:[0-9A-Fa-f]{0,4}:){2,7}[0-9A-Fa-f]{0,4})(?:/(\d{1,3}))?(?![0-9A-Fa-f:/]))");
    return regex_sub(text, ipv6, [](const std::smatch& m) {
        const auto value = m[2].str();
        if (!has_hex_letter(value) && value.find("::") == std::string::npos) {
            return m.str();
        }
        const auto address = read_ipv6_address(value);
        if (!address) {
            return m.str();
        }
        std::string out = m[1].str() + *address;
        if (m[3].matched) {
            const auto prefix = parse_int(m[3].str());
            if (prefix > 128) {
                return m.str();
            }
            out += " префікс " + number_to_words(static_cast<unsigned long long>(prefix));
        }
        return out;
    });
}

std::string normalize_coordinates(std::string text)
{
    auto coordinate_magnitude = [](std::string token) {
        if (token.starts_with("−")) {
            token.erase(0, std::string_view("−").size());
        } else if (!token.empty() && (token.front() == '+' || token.front() == '-')) {
            token.erase(token.begin());
        }
        return token;
    };
    auto decimal_coordinate = [](std::string token) {
        if (token.starts_with("−")) {
            token.erase(0, std::string_view("−").size());
        } else if (!token.empty() && (token.front() == '+' || token.front() == '-')) {
            token.erase(token.begin());
        }
        const auto decimal = token.find_first_of(".,");
        return decimal == std::string::npos ? number_to_words(parse_ull(token))
                                            : decimal_to_words_or_digits(std::string_view(token).substr(0, decimal),
                                                                         std::string_view(token).substr(decimal + 1));
    };
    auto exceeds_coordinate_limit = [&](const std::string& token, int limit) {
        const auto magnitude = coordinate_magnitude(token);
        const auto decimal = magnitude.find_first_of(".,");
        const auto degrees = parse_int(std::string_view(magnitude).substr(0, decimal));
        const bool nonzero_fraction =
            decimal != std::string::npos && magnitude.substr(decimal + 1).find_first_not_of('0') != std::string::npos;
        return degrees > limit || (degrees == limit && nonzero_fraction);
    };
    auto is_negative_coordinate = [](std::string_view token) {
        if (!token.starts_with('-') && !token.starts_with("−")) {
            return false;
        }
        token.remove_prefix(token.starts_with("−") ? std::string_view("−").size() : 1);
        return token.find_first_of("123456789") != std::string_view::npos;
    };
    auto signed_quantity = [&](const std::string& token) {
        const auto sign = token.starts_with('-') || token.starts_with("−") ? "мінус "
                          : token.starts_with('+')                         ? "плюс "
                                                                           : "";
        return std::string(sign) + decimal_coordinate(token);
    };
    static const std::regex geo_uri(
        R"((^|[^A-Za-zА-Яа-яЄєІіЇїҐґ])((?:geo|координати)\s*[:=]\s*)((?:[+\-]|−)?\d{1,2}(?:\.\d+)?)\s*[,;]\s*((?:[+\-]|−)?\d{1,3}(?:\.\d+)?)(?:\s*[,;]\s*((?:[+\-]|−)?\d+(?:\.\d+)?))?)",
        std::regex::icase);
    text = regex_sub(text, geo_uri, [&](const std::smatch& m) {
        auto latitude = m[3].str();
        auto longitude = m[4].str();
        if (exceeds_coordinate_limit(latitude, 90) || exceeds_coordinate_limit(longitude, 180)) {
            return m.str();
        }
        const bool south = is_negative_coordinate(latitude);
        const bool west = is_negative_coordinate(longitude);
        std::string out = m[1].str() + "географічні координати: " + decimal_coordinate(latitude) + " градуса " +
                          (south ? "південної" : "північної") + " широти, " + decimal_coordinate(longitude) +
                          " градуса " + (west ? "західної" : "східної") + " довготи";
        if (m[5].matched) {
            out += ", висота " + signed_quantity(m[5].str()) + " метрів";
        }
        return out;
    });
    static const std::regex labelled_pair(
        R"((^|[^A-Za-z])(?:lat(?:itude)?|широта)\s*[:=]\s*((?:[+\-]|−)?\d{1,2}(?:[.,]\d+)?)\s*[,; ]+\s*(?:lon(?:gitude)?|довгота)\s*[:=]\s*((?:[+\-]|−)?\d{1,3}(?:[.,]\d+)?))",
        std::regex::icase);
    text = regex_sub(text, labelled_pair, [&](const std::smatch& m) {
        const auto latitude = m[2].str();
        const auto longitude = m[3].str();
        if (exceeds_coordinate_limit(latitude, 90) || exceeds_coordinate_limit(longitude, 180)) {
            return m.str();
        }
        return m[1].str() + "широта " + decimal_coordinate(latitude) +
               (is_negative_coordinate(latitude) ? " південна" : " північна") + ", довгота " +
               decimal_coordinate(longitude) + (is_negative_coordinate(longitude) ? " західна" : " східна");
    });
    auto genitive_degrees = [](std::string_view digits) {
        const auto value = parse_ull(digits);
        return number_to_words_case(value, "gen") + " " + (value == 1 ? "градуса" : "градусів");
    };
    static const std::regex governed_marker(
        R"((^|[\s(\[{:,;])((?:Від|від|До|до))\s+(\d{1,3})\s*°\s*([NSEW])(?:\s+(?:широти|довготи))?(?![A-Za-zА-Яа-яЄєІіЇїҐґ]))",
        std::regex::icase);
    text = regex_sub(text, governed_marker, [&](const std::smatch& m) {
        const auto marker = lower_text(m[4].str());
        const bool latitude = marker == "n" || marker == "s";
        const auto degrees = parse_int(m[3].str());
        if (degrees > (latitude ? 90 : 180)) {
            return m.str();
        }
        return m[1].str() + m[2].str() + " " + genitive_degrees(m[3].str()) + " " + coordinate_hemisphere(m[4].str());
    });
    static const std::regex governed_axis(
        R"((^|[\s(\[{:,;])((?:Від|від|До|до))\s+(\d{1,3})\s*°\s*(широти|довготи)(?![А-Яа-яЄєІіЇїҐґ]))",
        std::regex::icase);
    text = regex_sub(text, governed_axis, [&](const std::smatch& m) {
        const auto latitude = lower_text(m[4].str()).starts_with("широт");
        const auto degrees = parse_int(m[3].str());
        if (degrees > (latitude ? 90 : 180)) {
            return m.str();
        }
        return m[1].str() + m[2].str() + " " + genitive_degrees(m[3].str()) + " " + m[4].str();
    });
    static const std::regex decimal_minutes(
        R"((^|[^\d])(\d{1,3})\s*°\s*(\d{1,2})[.,](\d+)\s*(?:′|')\s*((?:N|S|E|W)|(?:пн|пд|сх|зх)\.?\s*(?:ш|д)\.?))",
        std::regex::icase);
    text = regex_sub(text, decimal_minutes, [](const std::smatch& m) {
        const auto degrees = parse_int(m[2].str());
        const auto minutes = parse_int(m[3].str());
        const auto marker = lower_text(m[5].str());
        const bool latitude = marker == "n" || marker == "s" || marker.starts_with("пн") || marker.starts_with("пд");
        if (degrees > (latitude ? 90 : 180) || minutes > 59 ||
            (degrees == (latitude ? 90 : 180) &&
             (minutes != 0 || m[4].str().find_first_not_of('0') != std::string::npos))) {
            return m.str();
        }
        return m[1].str() + number_to_words(static_cast<unsigned long long>(degrees)) + " " +
               plural(static_cast<unsigned long long>(degrees), {"градус", "градуси", "градусів"}) + " " +
               decimal_to_words(m[3].str(), m[4].str()) + " хвилини " + coordinate_hemisphere(m[5].str());
    });
    // A bare integer followed by N/S/E/W is too ambiguous: N, S and W are
    // also common SI symbols. Require either a degree sign or a decimal value.
    static const std::regex decimal(
        R"((^|[^\d.,])((?:[+\-])?)(\d{1,3})(?:(?:[.,](\d+)\s*(?:°)?)|(?:°\s*))\s*([NSEW])(?:\s+(?:широт[а-яіїєґ]*|довгот[а-яіїєґ]*))?(?![A-Za-zА-Яа-яЄєІіЇїҐґ]))",
        std::regex::icase);
    text = regex_sub(text, decimal, [](const std::smatch& m) {
        const auto degrees = parse_int(m[3].str());
        const auto marker = lower_text(m[5].str());
        const bool latitude = marker == "n" || marker == "s" || marker.starts_with("пн") || marker.starts_with("пд") ||
                              marker.contains("широт");
        const auto limit = latitude ? 90 : 180;
        const bool nonzero_fraction = m[4].matched && m[4].str().find_first_not_of('0') != std::string::npos;
        if (degrees > limit || (degrees == limit && nonzero_fraction)) {
            return m.str();
        }
        std::string value;
        std::string unit;
        if (m[4].matched) {
            value = decimal_to_words_or_digits(m[3].str(), m[4].str());
            unit = "градуса";
        } else {
            value = number_to_words(static_cast<unsigned long long>(degrees));
            unit = plural(static_cast<unsigned long long>(degrees), {"градус", "градуси", "градусів"});
        }
        return m[1].str() + value + " " + unit + " " + coordinate_hemisphere(m[5].str());
    });
    static const std::regex dms(
        R"((^|[^\d])(\d{1,3})\s*°\s*(\d{1,2})\s*(?:′|')\s*(?:(\d{1,2})\s*(?:″|")\s*)?((?:N|S|E|W)|(?:пн|пд|сх|зх)\.?\s*(?:ш|д)\.?|північн[а-яіїєґ]+\s+широт[а-яіїєґ]+|південн[а-яіїєґ]+\s+широт[а-яіїєґ]+|східн[а-яіїєґ]+\s+довгот[а-яіїєґ]+|західн[а-яіїєґ]+\s+довгот[а-яіїєґ]+))",
        std::regex::icase);
    return regex_sub(text, dms, [](const std::smatch& m) {
        const auto degrees = parse_int(m[2].str());
        const auto marker = lower_text(m[5].str());
        const bool latitude = marker == "n" || marker == "s" || marker.starts_with("пн") || marker.starts_with("пд") ||
                              marker.contains("широт");
        if (degrees > (latitude ? 90 : 180) || (m[3].matched && parse_int(m[3].str()) > 59) ||
            (m[4].matched && parse_int(m[4].str()) > 59)) {
            return m.str();
        }
        std::vector<std::string> parts = {
            read_coordinate_number(m[2].str()),
            plural(static_cast<unsigned long long>(degrees), {"градус", "градуси", "градусів"})};
        if (m[3].matched) {
            const auto minutes = parse_int(m[3].str());
            parts.push_back(read_coordinate_number(m[3].str(), 'f'));
            parts.push_back(plural(static_cast<unsigned long long>(minutes), {"хвилина", "хвилини", "хвилин"}));
        }
        if (m[4].matched) {
            const auto seconds = parse_int(m[4].str());
            parts.push_back(read_coordinate_number(m[4].str(), 'f'));
            parts.push_back(plural(static_cast<unsigned long long>(seconds), {"секунда", "секунди", "секунд"}));
        }
        parts.push_back(coordinate_hemisphere(m[5].str()));
        return m[1].str() + join(parts);
    });
}
std::string normalize_identifiers(std::string text)
{
    static const std::string standard_label =
        R"((?:(?:ДСТУ|ТУ\s+У)(?:\s+(?:EN\s+)?ISO)?|ДНАОП|ISO(?:\s*/\s*IEC)?|IEC|IEEE|ГОСТ|ДБН))";
    static const std::string ukrainian_standard_letter =
        R"((?:А|Б|В|Г|Ґ|Д|Е|Є|Ж|З|И|І|Ї|Й|К|Л|М|Н|О|П|Р|С|Т|У|Ф|Х|Ц|Ч|Ш|Щ|Ю|Я))";
    static const std::string standard_atom = "(?:[A-Za-z0-9]+|" + ukrainian_standard_letter + ")";
    const auto read_standard_body = [](std::string_view body) {
        std::vector<std::string> parts;
        for (std::size_t i = 0; i < body.size();) {
            const auto ch = static_cast<unsigned char>(body[i]);
            if (std::isspace(ch)) {
                ++i;
                continue;
            }
            if (std::isdigit(ch)) {
                const auto start = i++;
                while (i < body.size() && std::isdigit(static_cast<unsigned char>(body[i]))) {
                    ++i;
                }
                const auto digits = body.substr(start, i - start);
                parts.push_back(digits.size() > 1 && digits.front() == '0' ? number_to_words_digit_by_digit(digits)
                                                                           : number_digits_or_words(digits));
                continue;
            }
            std::size_t next = i + 1;
            const auto cp = decode_one(body, i, next);
            if (cp == U'.') {
                parts.emplace_back("крапка");
            } else if (cp == U'-' || cp == U'–' || cp == U'—') {
                parts.emplace_back("дефіс");
            } else if (cp == U':') {
                parts.emplace_back("двокрапка");
            } else if (cp == U'/') {
                parts.emplace_back("слеш");
            } else if (is_latin(cp) || is_uk(cp)) {
                const auto start = i;
                while (next < body.size()) {
                    std::size_t following = next + 1;
                    const auto following_cp = decode_one(body, next, following);
                    if (!is_latin(following_cp) && !is_uk(following_cp)) {
                        break;
                    }
                    next = following;
                }
                parts.push_back(spell_identifier_letters(body.substr(start, next - start)));
            }
            i = next;
        }
        return join(parts);
    };
    static const std::regex technical_standard(
        "(^|[\\s(\\[{:,;])(" + standard_label + ")(\\s+|-|–|—)((?:(?:[A-Za-z]|" + ukrainian_standard_letter +
            ")\\.)?\\d+(?:(?:\\s*(?:\\.|:|/)\\s*|(?:-|–|—))" + standard_atom + ")*)(?![A-Za-z0-9])",
        std::regex::icase);
    text = regex_sub(text, technical_standard, [&](const std::smatch& m) {
        auto label = m[2].str();
        static const std::regex slash(R"(\s*/\s*)");
        label = std::regex_replace(label, slash, " слеш ");
        const auto delimiter = m[3].str();
        const auto spoken_delimiter = delimiter.find_first_not_of(" \t\r\n") == std::string::npos ? " " : " дефіс ";
        return m[1].str() + label + spoken_delimiter + read_standard_body(m[4].str());
    });
    // IEEE 802 revisions also occur without the "IEEE" label.  They are
    // identifiers, not decimals, ranges, or unit-bearing measurements (802.16m).
    static const std::regex bare_ieee_revision(
        R"((^|[^A-Za-z0-9.])(802\.\d{1,2}(?:[A-Za-z]{1,3})?(?:(?:-|–|—)\d{4})?)(?![A-Za-z0-9]|\.\d))",
        std::regex::icase);
    text = regex_sub(text, bare_ieee_revision, [&](const std::smatch& m) {
        return m[1].str() + read_standard_body(m[2].str());
    });
    static const std::regex uuid(
        R"(\b([0-9A-Fa-f]{8})-([0-9A-Fa-f]{4})-([0-9A-Fa-f]{4})-([0-9A-Fa-f]{4})-([0-9A-Fa-f]{12})\b)");
    static const std::regex compact_uuid(R"(\bUUID\s*[:=]?\s*([0-9A-Fa-f]{32})\b)", std::regex::icase);
    static const std::regex labelled_hash(
        R"(\b((?:SHA-?(?:1|224|256|384|512)|SHA3-?(?:256|512)|BLAKE2[bs]|MD5))\s*[:=]?\s*([0-9A-Fa-f]{16,128})\b)",
        std::regex::icase);
    static const std::regex isbn(R"(\b(ISBN(?:-1[03])?)\s*[:№#]?\s*((?:97[89][ -]?)?[0-9Xx](?:[ -]?[0-9Xx]){8,12})\b)",
                                 std::regex::icase);
    static const std::regex issn(R"(\b(ISSN(?:-L)?)\s*[:№#]?\s*(\d{4})[ -]?(\d{3}[\dXx])\b)", std::regex::icase);
    static const std::regex vin(R"(\b(VIN)\s*[:№#]?\s*([A-HJ-NPR-Z0-9]{17})\b)", std::regex::icase);
    static const std::regex swift(R"(\b((?:SWIFT|BIC))\s*[:№#]?\s*([A-Z]{4}[A-Z]{2}[A-Z0-9]{2}(?:[A-Z0-9]{3})?)\b)",
                                  std::regex::icase);
    static const std::regex iban(R"(\bUA\s*(\d{2})(?:\s*(\d{4})){6}\s*(\d{1})\b)", std::regex::icase);
    static const std::regex foreign_iban(R"(\b([A-Z]{2})[ -]?(\d{2})((?:[ -]?[A-Z0-9]){11,30})\b)", std::regex::icase);
    static const std::regex edrpou(R"((ЄДРПОУ|ЄДР|код\s+ЄДРПОУ)\s*[:№#]?\s*(\d{8})\b)", std::regex::icase);
    static const std::regex tax_id(R"((РНОКПП|ІПН|податковий\s+номер)\s*[:№#]?\s*(\d{10})\b)", std::regex::icase);
    static const std::regex postcode(R"((індекс|поштовий\s+індекс)\s*[:№#]?\s*(\d{5})\b)", std::regex::icase);
    static const std::regex legal_number(
        R"((^|[^A-Za-zА-Яа-яЄєІіЇїҐґ])((?:справа|Справа|справі|Справі|справу|Справу|провадження|Провадження|закон|Закон|закону|Закону|наказ|Наказ|постанова|Постанова|розпорядження|Розпорядження|рішення|Рішення|ухвала|Ухвала|договір|Договір|контракт|Контракт|рахунок|Рахунок|замовлення|Замовлення|акт|Акт|лист|Лист)(?:\s+[^№\s]+)?\s*)№\s*([^\s,.;:!?()]+))");
    static const std::regex erdr(R"((^|[^A-Za-zА-Яа-яЄєІіЇїҐґ])(ЄРДР\.?\s*)№?\s*(\d{8,20})(?!\d))", std::regex::icase);
    static const std::regex passport(R"((^|[^A-Za-zА-Яа-яЄєІіЇїҐґ])(паспорт\s+)([^\s\d]+)\s*(\d{6,9})(?!\d))",
                                     std::regex::icase);
    static const std::regex bank_card(
        R"((^|[^A-Za-zА-Яа-яЄєІіЇїҐґ])((?:картка|картку|карта|карту)\s+)(\d{4})[\s-]+(?:\*{4}|xxxx|XXXX)[\s-]+(?:\*{4}|xxxx|XXXX)[\s-]+(\d{4})(?!\d))",
        std::regex::icase);
    static const std::regex full_bank_card(
        R"((^|[^A-Za-zА-Яа-яЄєІіЇїҐґ])((?:картка|картку|карта|карту)\s+)(\d{4})[\s-]+(\d{4})[\s-]+(\d{4})[\s-]+(\d{4})(?!\d))",
        std::regex::icase);
    static const std::regex variable_bank_card(
        R"((^|[^A-Za-zА-Яа-яЄєІіЇїҐґ])((?:номер\s+картки|картка|картку|картки|карта|карту)\s+)(\d(?:[ -]?\d){11,18})(?!\d))",
        std::regex::icase);
    static const std::regex plate(
        R"((^|[\s,.;:!?()])((?:А|В|Е|І|К|М|Н|О|Р|С|Т|Х){2})\s*(\d{4})\s*((?:А|В|Е|І|К|М|Н|О|Р|С|Т|Х){2})(?![А-Яа-яЄєІіЇїҐґ]))");
    text = regex_sub(text, uuid, [](const std::smatch& m) {
        std::vector<std::string> groups;
        for (std::size_t i = 1; i <= 5; ++i) {
            groups.push_back(read_code_characters(m[i].str()));
        }
        return "ю у ай ді " + join(groups, " дефіс ");
    });
    text = regex_sub(text, compact_uuid, [](const std::smatch& m) {
        const auto value = m[1].str();
        std::vector<std::string> groups;
        static constexpr std::array group_spans = {std::pair<std::size_t, std::size_t>{0, 8},
                                                   std::pair<std::size_t, std::size_t>{8, 4},
                                                   std::pair<std::size_t, std::size_t>{12, 4},
                                                   std::pair<std::size_t, std::size_t>{16, 4},
                                                   std::pair<std::size_t, std::size_t>{20, 12}};
        for (const auto& [start, length] : group_spans) {
            groups.push_back(read_code_characters(std::string_view(value).substr(start, length)));
        }
        return "ю у ай ді " + join(groups, " дефіс ");
    });
    text = regex_sub(text, labelled_hash, [](const std::smatch& m) {
        return spell_identifier_letters(m[1].str()) + " хеш " + read_code_characters(m[2].str());
    });
    text = regex_sub(text, isbn, [](const std::smatch& m) {
        std::string value;
        for (const char ch : m[2].str()) {
            if (std::isalnum(static_cast<unsigned char>(ch))) {
                value.push_back(ch);
            }
        }
        return "ай ес бі ен " + read_code_characters(value);
    });
    text = regex_sub(text, issn, [](const std::smatch& m) {
        const auto label = lower_text(m[1].str()).contains("-l") ? "ай ес ес ен ел " : "ай ес ес ен ";
        return std::string(label) + number_to_words_digit_by_digit(m[2].str()) + " " + read_code_characters(m[3].str());
    });
    text = regex_sub(text, vin, [](const std::smatch& m) { return "він номер " + read_code_characters(m[2].str()); });
    text = regex_sub(text, swift, [](const std::smatch& m) { return "свіфт код " + read_code_characters(m[2].str()); });
    text = regex_sub(text, iban, [](const std::smatch& m) {
        std::string digits;
        for (char ch : m.str()) {
            if (ch >= '0' && ch <= '9') {
                digits.push_back(ch);
            }
        }
        if (digits.size() != 27) {
            return m.str();
        }
        return "айбан " + spell_identifier_letters("UA") + " " + number_to_words_digit_by_digit(digits);
    });
    text = regex_sub(text, foreign_iban, [](const std::smatch& m) {
        std::string tail;
        for (const char ch : m[3].str()) {
            if (std::isalnum(static_cast<unsigned char>(ch))) {
                tail.push_back(ch);
            }
        }
        return "айбан " + spell_identifier_letters(m[1].str()) + " " + number_to_words_digit_by_digit(m[2].str()) +
               " " + read_code_characters(tail);
    });
    text = regex_sub(text, edrpou, [](const std::smatch& m) {
        return "єдиний державний реєстр підприємств та організацій України " +
               number_to_words_digit_by_digit(m[2].str());
    });
    text = regex_sub(text, tax_id, [](const std::smatch& m) {
        return lower_text(m[1].str()) + " " + number_to_words_digit_by_digit(m[2].str());
    });
    text = regex_sub(text, postcode, [](const std::smatch& m) {
        return lower_text(m[1].str()) + " " + number_to_words_digit_by_digit(m[2].str());
    });
    text = regex_sub(text, legal_number, [](const std::smatch& m) {
        return m[1].str() + m[2].str() + "номер " + read_structured_identifier(m[3].str());
    });
    text = regex_sub(text, erdr, [](const std::smatch& m) {
        return m[1].str() + "єдиний реєстр досудових розслідувань номер " + number_to_words_digit_by_digit(m[3].str());
    });
    text = regex_sub(text, passport, [](const std::smatch& m) {
        return m[1].str() + m[2].str() + spell_identifier_letters(m[3].str()) + " " +
               number_to_words_digit_by_digit(m[4].str());
    });
    text = regex_sub(text, bank_card, [](const std::smatch& m) {
        return m[1].str() + m[2].str() + number_to_words_digit_by_digit(m[3].str()) + " зірочки зірочки " +
               number_to_words_digit_by_digit(m[4].str());
    });
    text = regex_sub(text, full_bank_card, [](const std::smatch& m) {
        return m[1].str() + m[2].str() + number_to_words_digit_by_digit(m[3].str()) + " " +
               number_to_words_digit_by_digit(m[4].str()) + " " + number_to_words_digit_by_digit(m[5].str()) + " " +
               number_to_words_digit_by_digit(m[6].str());
    });
    text = regex_sub(text, variable_bank_card, [](const std::smatch& m) {
        std::string digits;
        for (const char ch : m[3].str()) {
            if (ch >= '0' && ch <= '9') {
                digits.push_back(ch);
            }
        }
        return m[1].str() + m[2].str() + number_to_words_digit_by_digit(digits);
    });
    return regex_sub(text, plate, [](const std::smatch& m) {
        return m[1].str() + "номерний знак " + spell_identifier_letters(m[2].str()) + " " +
               number_to_words_digit_by_digit(m[3].str()) + " " + spell_identifier_letters(m[4].str());
    });
}

} // namespace uktextnorm::detail
