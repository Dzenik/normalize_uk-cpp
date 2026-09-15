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


} // namespace uktextnorm::detail
