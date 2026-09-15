#include "uktextnorm/uktextnorm.hpp"

#include "internal.hpp"

#include "generated/uktextnorm_lexicons.hpp"

namespace uktextnorm {
namespace detail {

namespace {

const std::unordered_map<char, std::string>& latin_letter_names()
{
    static const std::unordered_map<char, std::string> names = {
        {'a', "ей"},  {'b', "бі"},     {'c', "сі"},   {'d', "ді"},  {'e', "і"},  {'f', "еф"}, {'g', "джі"},
        {'h', "ейч"}, {'i', "ай"},     {'j', "джей"}, {'k', "кей"}, {'l', "ел"}, {'m', "ем"}, {'n', "ен"},
        {'o', "оу"},  {'p', "пі"},     {'q', "к'ю"},  {'r', "ар"},  {'s', "ес"}, {'t', "ті"}, {'u', "ю"},
        {'v', "ві"},  {'w', "дабл ю"}, {'x', "екс"},  {'y', "вай"}, {'z', "зед"}};
    return names;
}

std::string spell_latin_run(std::string_view run)
{
    std::vector<std::string> parts;
    parts.reserve(run.size());
    for (const char ch : run) {
        parts.push_back(latin_letter_names().at(static_cast<char>(std::tolower(static_cast<unsigned char>(ch)))));
    }
    return join(parts);
}

std::string read_ascii_digit_run(std::string_view run)
{
    if (run.size() > 1 && run.front() == '0') {
        return number_to_words_digit_by_digit(run);
    }
    const auto value = try_parse_ull(run);
    return value ? number_to_words(*value) : number_to_words_digit_by_digit(run);
}

} // namespace
std::string normalize_english(std::string text, const std::unordered_map<std::string, std::string>& vocabulary)
{
    static const std::regex word(R"(\b[A-Za-z][A-Za-z'’-]*\b)");
    static const std::regex acronym(R"(\b[A-Z]+\b)");
    text = regex_sub(text, word, [&](const std::smatch& m) {
        const auto low = lower_text(m.str());
        if (const auto it = vocabulary.find(low); it != vocabulary.end()) {
            return it->second;
        }
        if (const auto it = english_words().find(low); it != english_words().end()) {
            return it->second;
        }
        return m.str();
    });
    return regex_sub(text, acronym, [&](const std::smatch& m) {
        const auto low = lower_text(m.str());
        if (vocabulary.contains(low) || english_words().contains(low)) {
            return m.str();
        }
        std::vector<std::string> parts;
        for (char ch : low) {
            parts.push_back(latin_letter_names().at(ch));
        }
        return join(parts);
    });
}

std::string normalize_technical_alphanumeric(std::string text)
{
    static const std::regex internet_protocol(R"(\bIPv([46])\b)", std::regex::icase);
    text = regex_sub(text, internet_protocol, [](const std::smatch& m) {
        return "ай пі версії " + read_ascii_digit_run(m[1].str());
    });

    static const std::regex mobile_generation(R"(\b(\d+)G\b)");
    text = regex_sub(
        text, mobile_generation, [](const std::smatch& m) { return read_ascii_digit_run(m[1].str()) + " джі"; });

    static const std::regex dimension(R"(\b(\d+)D\b)");
    text = regex_sub(text, dimension, [](const std::smatch& m) { return read_ascii_digit_run(m[1].str()) + " ді"; });

    static const std::regex x86_family(R"(\bx(86|64)\b)", std::regex::icase);
    text = regex_sub(text, x86_family, [](const std::smatch& m) { return "ікс " + read_ascii_digit_run(m[1].str()); });

    static const std::regex english_ordinal(R"(\b(\d+)(?:st|nd|rd|th)\b)", std::regex::icase);
    text = regex_sub(text, english_ordinal, [](const std::smatch& m) {
        const auto value = try_parse_ull(m[1].str());
        return value ? number_to_ordinal_words(*value, "nom") : m.str();
    });

    static const std::regex mixed(R"(\b[A-Za-z0-9]+\b)");
    return regex_sub(text, mixed, [](const std::smatch& m) {
        const auto token = m.str();
        const bool has_letter = std::ranges::any_of(token, [](unsigned char ch) { return std::isalpha(ch); });
        const bool has_digit = std::ranges::any_of(token, [](unsigned char ch) { return std::isdigit(ch); });
        if (!has_letter || !has_digit) {
            return token;
        }
        std::vector<std::string> parts;
        std::size_t start = 0;
        while (start < token.size()) {
            const bool digits = std::isdigit(static_cast<unsigned char>(token[start]));
            std::size_t stop = start + 1;
            while (stop < token.size() && std::isdigit(static_cast<unsigned char>(token[stop])) == digits) {
                ++stop;
            }
            const auto run = std::string_view(token).substr(start, stop - start);
            if (digits) {
                parts.push_back(read_ascii_digit_run(run));
            } else if (std::ranges::all_of(run, [](unsigned char ch) { return std::isupper(ch); })) {
                parts.push_back(spell_latin_run(run));
            } else {
                parts.emplace_back(run);
            }
            start = stop;
        }
        return join(parts);
    });
}

std::string normalize_cyrillic_alphanumeric(std::string text)
{
    const auto cps = codepoints(text);
    std::string out;
    out.reserve(text.size());
    std::size_t last = 0;
    for (std::size_t index = 0; index < cps.size();) {
        const auto is_token_character = [](char32_t cp) {
            return (is_uk(cp) && !is_word_joiner(cp)) || (cp >= U'0' && cp <= U'9') || cp == U'-' || cp == U'/' ||
                   cp == U'–' || cp == U'—';
        };
        if (!is_token_character(cps[index].value) || cps[index].value == U'-' || cps[index].value == U'/' ||
            cps[index].value == U'–' || cps[index].value == U'—') {
            ++index;
            continue;
        }
        const auto start = index;
        while (index < cps.size() && is_token_character(cps[index].value)) {
            ++index;
        }
        auto stop = index;
        while (stop > start && (cps[stop - 1].value == U'-' || cps[stop - 1].value == U'/' ||
                                cps[stop - 1].value == U'–' || cps[stop - 1].value == U'—')) {
            --stop;
        }
        bool has_digit = false;
        bool has_ukrainian = false;
        bool has_uppercase = false;
        bool has_dimension_sign = false;
        for (std::size_t i = start; i < stop; ++i) {
            const auto cp = cps[i].value;
            has_digit = has_digit || (cp >= U'0' && cp <= U'9');
            has_ukrainian = has_ukrainian || (is_uk(cp) && !is_word_joiner(cp));
            has_uppercase = has_uppercase || is_upper_uk(cp);
            has_dimension_sign = has_dimension_sign ||
                                 (cp == U'х' && i > start && i + 1 < stop && cps[i - 1].value >= U'0' &&
                                  cps[i - 1].value <= U'9' && cps[i + 1].value >= U'0' && cps[i + 1].value <= U'9');
        }
        if (!has_digit || !has_ukrainian || (!has_uppercase && !has_dimension_sign)) {
            continue;
        }

        out.append(text, last, cps[start].start - last);
        std::vector<std::string> parts;
        for (std::size_t i = start; i < stop;) {
            const auto cp = cps[i].value;
            if (cp >= U'0' && cp <= U'9') {
                const auto run_start = cps[i].start;
                while (i < stop && cps[i].value >= U'0' && cps[i].value <= U'9') {
                    ++i;
                }
                parts.push_back(
                    read_identifier_number(std::string_view(text).substr(run_start, cps[i - 1].stop - run_start)));
                continue;
            }
            if (cp == U'-' || cp == U'–' || cp == U'—') {
                parts.emplace_back("дефіс");
                ++i;
                continue;
            }
            if (cp == U'/') {
                parts.emplace_back("слеш");
                ++i;
                continue;
            }
            if (cp == U'х' && i > start && i + 1 < stop && cps[i - 1].value >= U'0' && cps[i - 1].value <= U'9' &&
                cps[i + 1].value >= U'0' && cps[i + 1].value <= U'9') {
                parts.emplace_back("помножити на");
                ++i;
                continue;
            }
            const auto run_start = cps[i].start;
            while (i < stop && is_uk(cps[i].value) && !is_word_joiner(cps[i].value) &&
                   !(cps[i].value == U'х' && i > start && i + 1 < stop && cps[i - 1].value >= U'0' &&
                     cps[i - 1].value <= U'9' && cps[i + 1].value >= U'0' && cps[i + 1].value <= U'9')) {
                ++i;
            }
            parts.push_back(
                spell_identifier_letters(std::string_view(text).substr(run_start, cps[i - 1].stop - run_start)));
        }
        out += join(parts);
        last = cps[stop - 1].stop;
        index = stop;
    }
    if (last == 0) {
        return text;
    }
    out.append(text, last, std::string::npos);
    return out;
}

} // namespace detail

} // namespace uktextnorm
