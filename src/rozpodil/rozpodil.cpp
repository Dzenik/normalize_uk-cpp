#include "internal.hpp"

#include "../common/utf8.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace rozpodil {
namespace detail {

enum class Action {
    None,
    Split,
    Join
};

using normalize_uk_cpp::detail::decode_one;

std::vector<Cp> codepoints(std::string_view text)
{
    std::vector<Cp> out;
    for (std::size_t i = 0; i < text.size();) {
        std::size_t next = i + 1;
        const auto cp = decode_one(text, i, next);
        out.push_back({cp, i, next});
        i = next;
    }
    return out;
}

bool contains(std::u32string_view set, char32_t cp)
{
    return set.contains(cp);
}

bool is_space(char32_t cp)
{
    return cp == U' ' || cp == U'\t' || cp == U'\n' || cp == U'\r' || cp == U'\f' || cp == U'\v' || cp == 0x00A0;
}

bool is_digit(char32_t cp)
{
    return cp >= U'0' && cp <= U'9';
}

bool is_latin(char32_t cp)
{
    return (cp >= U'a' && cp <= U'z') || (cp >= U'A' && cp <= U'Z');
}

bool is_uk(char32_t cp)
{
    return (cp >= U'а' && cp <= U'я') || (cp >= U'А' && cp <= U'Я') || cp == U'ґ' || cp == U'Ґ' || cp == U'є' ||
           cp == U'Є' || cp == U'і' || cp == U'І' || cp == U'ї' || cp == U'Ї';
}

bool is_alpha(char32_t cp)
{
    return is_latin(cp) || is_uk(cp);
}

bool is_uk_apostrophe(char32_t cp)
{
    return cp == U'\'' || cp == U'’' || cp == U'ʼ' || cp == U'`';
}

bool is_word_mark(char32_t cp)
{
    return cp == U'_' || cp == U'-';
}

bool is_inner_uk_apostrophe(const std::vector<Cp>& cps, std::size_t index)
{
    return index > 0 && index + 1 < cps.size() && is_uk_apostrophe(cps[index].value) && is_uk(cps[index - 1].value) &&
           is_uk(cps[index + 1].value);
}

bool is_word_letter(char32_t cp)
{
    return is_alpha(cp) || cp == U'_' || cp == U'Å' || cp == U'å' || cp == U'ğ' || cp == U'Ğ' ||
           (cp >= 0x0370 && cp <= 0x03FF);
}

bool is_python_alpha(char32_t cp)
{
    return is_alpha(cp) || cp == U'Å' || cp == U'å' || cp == U'ğ' || cp == U'Ğ' || (cp >= 0x0370 && cp <= 0x03FF);
}

char32_t lower_cp(char32_t cp)
{
    if (cp >= U'A' && cp <= U'Z') {
        return cp + 32;
    }
    if (cp >= U'А' && cp <= U'Я') {
        return cp + 32;
    }
    if (cp == U'Ґ') {
        return U'ґ';
    }
    if (cp == U'Є') {
        return U'є';
    }
    if (cp == U'І') {
        return U'і';
    }
    if (cp == U'Ї') {
        return U'ї';
    }
    if (cp == U'Å') {
        return U'å';
    }
    if (cp == U'Ğ') {
        return U'ğ';
    }
    if (cp >= 0x0391 && cp <= 0x03A9) {
        return cp + 32;
    }
    return cp;
}

std::string lower_ascii_ukrainian(std::string_view text)
{
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        std::size_t next = i + 1;
        const auto cp = decode_one(text, i, next);
        if (cp >= U'A' && cp <= U'Z') {
            out.push_back(static_cast<char>(cp + 32));
        } else if (cp >= U'А' && cp <= U'Я') {
            const auto low = cp + 32;
            out.push_back(static_cast<char>(0xD0 | ((low >> 6) & 0x1F)));
            out.push_back(static_cast<char>(0x80 | (low & 0x3F)));
        } else if (cp == U'Ґ') {
            out += "\xD2\x91";
        } else if (cp == U'Є') {
            out += "\xD1\x94";
        } else if (cp == U'І') {
            out += "\xD1\x96";
        } else if (cp == U'Ї') {
            out += "\xD1\x97";
        } else {
            out.append(text.substr(i, next - i));
        }
        i = next;
    }
    return out;
}

bool is_lower_alpha(std::string_view token)
{
    if (token.empty()) {
        return false;
    }
    bool saw_alpha = false;
    for (std::size_t i = 0; i < token.size();) {
        std::size_t next = i + 1;
        const auto cp = decode_one(token, i, next);
        if (!is_python_alpha(cp)) {
            return false;
        }
        if (lower_cp(cp) != cp) {
            return false;
        }
        saw_alpha = true;
        i = next;
    }
    return saw_alpha;
}

bool is_upper_one(std::string_view token)
{
    auto cps = codepoints(token);
    if (cps.size() != 1) {
        return false;
    }
    const auto cp = cps[0].value;
    return is_python_alpha(cp) && lower_cp(cp) != cp;
}

std::string_view trim_view(std::string_view text, std::size_t& offset)
{
    auto cps = codepoints(text);
    std::size_t first = 0;
    std::size_t last = cps.size();
    while (first < last && is_space(cps[first].value)) {
        ++first;
    }
    while (last > first && is_space(cps[last - 1].value)) {
        --last;
    }
    const std::size_t start = first < cps.size() ? cps[first].start : text.size();
    const std::size_t stop = last > 0 ? cps[last - 1].stop : start;
    offset += start;
    return text.substr(start, stop - start);
}

bool starts_with_space(std::string_view text)
{
    if (text.empty()) {
        return false;
    }
    std::size_t next = 0;
    return is_space(decode_one(text, 0, next));
}

bool ends_with_space(std::string_view text)
{
    auto cps = codepoints(text);
    return !cps.empty() && is_space(cps.back().value);
}

std::optional<std::string_view> first_token(std::string_view text)
{
    auto cps = codepoints(text);
    for (std::size_t i = 0; i < cps.size(); ++i) {
        if (is_space(cps[i].value)) {
            continue;
        }
        if (is_word_letter(cps[i].value)) {
            std::size_t j = i + 1;
            while (j < cps.size() && is_word_letter(cps[j].value)) {
                ++j;
            }
            return text.substr(cps[i].start, cps[j - 1].stop - cps[i].start);
        }
        if (is_digit(cps[i].value)) {
            std::size_t j = i + 1;
            while (j < cps.size() && is_digit(cps[j].value)) {
                ++j;
            }
            return text.substr(cps[i].start, cps[j - 1].stop - cps[i].start);
        }
        return text.substr(cps[i].start, cps[i].stop - cps[i].start);
    }
    return std::nullopt;
}

std::optional<std::string_view> first_word(std::string_view text)
{
    auto cps = codepoints(text);
    for (std::size_t i = 0; i < cps.size(); ++i) {
        if (is_word_letter(cps[i].value)) {
            std::size_t j = i + 1;
            while (j < cps.size() && is_word_letter(cps[j].value)) {
                ++j;
            }
            return text.substr(cps[i].start, cps[j - 1].stop - cps[i].start);
        }
        if (is_digit(cps[i].value)) {
            std::size_t j = i + 1;
            while (j < cps.size() && is_digit(cps[j].value)) {
                ++j;
            }
            return text.substr(cps[i].start, cps[j - 1].stop - cps[i].start);
        }
    }
    return std::nullopt;
}

bool starts_with_letter_bullet(std::string_view text)
{
    auto cps = codepoints(text);
    std::size_t i = 0;
    while (i < cps.size() && is_space(cps[i].value)) {
        ++i;
    }
    if (i + 1 >= cps.size() || !is_alpha(cps[i].value)) {
        return false;
    }
    if (cps[i + 1].value != U')') {
        return false;
    }
    return i + 2 >= cps.size() || is_space(cps[i + 2].value);
}

std::optional<std::string_view> last_token(std::string_view text)
{
    auto cps = codepoints(text);
    for (std::size_t n = cps.size(); n > 0; --n) {
        const std::size_t i = n - 1;
        if (is_space(cps[i].value)) {
            continue;
        }
        if (is_word_letter(cps[i].value)) {
            std::size_t j = i;
            while (j > 0 && is_word_letter(cps[j - 1].value)) {
                --j;
            }
            return text.substr(cps[j].start, cps[i].stop - cps[j].start);
        }
        if (is_digit(cps[i].value)) {
            std::size_t j = i;
            while (j > 0 && is_digit(cps[j - 1].value)) {
                --j;
            }
            return text.substr(cps[j].start, cps[i].stop - cps[j].start);
        }
        return text.substr(cps[i].start, cps[i].stop - cps[i].start);
    }
    return std::nullopt;
}

bool is_word_cp(char32_t cp)
{
    return is_word_letter(cp) || is_digit(cp);
}

std::optional<std::string_view> last_compound_abbrev_token(std::string_view text)
{
    auto cps = codepoints(text);
    for (std::size_t n = cps.size(); n > 0; --n) {
        const std::size_t i = n - 1;
        if (is_space(cps[i].value)) {
            continue;
        }
        if (!is_word_cp(cps[i].value)) {
            return std::nullopt;
        }
        bool saw_compound_mark = false;
        std::size_t j = i;
        while (j > 0 && (is_word_cp(cps[j - 1].value) || cps[j - 1].value == U'-' || cps[j - 1].value == U'.' ||
                         cps[j - 1].value == U'/')) {
            if (cps[j - 1].value == U'-' || cps[j - 1].value == U'.' || cps[j - 1].value == U'/') {
                saw_compound_mark = true;
            }
            --j;
        }
        while (j <= i && (cps[j].value == U'-' || cps[j].value == U'.' || cps[j].value == U'/')) {
            ++j;
        }
        if (j > i || !saw_compound_mark) {
            return std::nullopt;
        }
        return text.substr(cps[j].start, cps[i].stop - cps[j].start);
    }
    return std::nullopt;
}

std::optional<std::string_view> trailing_dot_abbrev_token(std::string_view text)
{
    auto cps = codepoints(text);
    if (cps.empty()) {
        return std::nullopt;
    }
    std::size_t i = cps.size();
    while (i > 0 && is_space(cps[i - 1].value)) {
        --i;
    }
    if (i == 0 || cps[i - 1].value != U'.') {
        return std::nullopt;
    }
    --i;
    while (i > 0 && is_space(cps[i - 1].value)) {
        --i;
    }
    const std::size_t stop = i;
    while (i > 0 && is_word_cp(cps[i - 1].value)) {
        --i;
    }
    if (i == stop) {
        return std::nullopt;
    }
    return text.substr(cps[i].start, cps[stop - 1].stop - cps[i].start);
}

std::optional<std::pair<std::string_view, std::string_view>> left_abbreviation_pair(std::string_view text)
{
    auto cps = codepoints(text);
    if (cps.empty()) {
        return std::nullopt;
    }
    std::size_t i = cps.size();
    while (i > 0 && is_space(cps[i - 1].value)) {
        --i;
    }
    const std::size_t b_stop = i;
    while (i > 0 && is_word_cp(cps[i - 1].value)) {
        --i;
    }
    if (i == b_stop) {
        return std::nullopt;
    }
    const std::size_t b_start = i;
    while (i > 0 && is_space(cps[i - 1].value)) {
        --i;
    }
    if (i == 0 || cps[i - 1].value != U'.') {
        return std::nullopt;
    }
    --i;
    while (i > 0 && is_space(cps[i - 1].value)) {
        --i;
    }
    const std::size_t a_stop = i;
    while (i > 0 && is_word_cp(cps[i - 1].value)) {
        --i;
    }
    if (i == a_stop) {
        return std::nullopt;
    }
    const std::size_t a_start = i;
    return std::pair<std::string_view, std::string_view>{
        text.substr(cps[a_start].start, cps[a_stop - 1].stop - cps[a_start].start),
        text.substr(cps[b_start].start, cps[b_stop - 1].stop - cps[b_start].start)};
}

std::vector<std::string_view> tokens_in(std::string_view text)
{
    std::vector<std::string_view> out;
    auto cps = codepoints(text);
    for (std::size_t i = 0; i < cps.size();) {
        if (is_space(cps[i].value)) {
            ++i;
            continue;
        }
        const auto begin = i;
        if (is_word_letter(cps[i].value)) {
            while (i < cps.size() && is_word_letter(cps[i].value)) {
                ++i;
            }
        } else if (is_digit(cps[i].value)) {
            while (i < cps.size() && is_digit(cps[i].value)) {
                ++i;
            }
        } else {
            ++i;
        }
        out.push_back(text.substr(cps[begin].start, cps[i - 1].stop - cps[begin].start));
    }
    return out;
}

std::unordered_set<std::string_view> words(std::initializer_list<std::string_view> values)
{
    return {values.begin(), values.end()};
}

const auto trailing_abbreviations =
    words({"тис",  "млн", "млрд", "грн", "коп", "проц", "га",  "кг",  "г",   "т",   "куб", "кв",   "км",   "м",
           "см",   "мм",  "л",    "год", "хв",  "сек",  "ст",  "р",   "рр",  "с",   "к",   "руб",  "крб",  "co",
           "corp", "inc", "ed",   "al",  "мон", "моз",  "мвс", "сбу", "нбу", "дпс", "дбр", "набу", "назк", "ова",
           "ода",  "рда", "кмда", "мкм", "нм",  "квт",  "мвт", "шт",  "од",  "екз", "вс",  "оаск", "єрдр", "ecli"});
const auto leading_abbreviations = words(
    {"ст",     "укр",  "англ",  "нім",  "фр",     "італ",   "грец", "лат",     "mr",     "mrs",    "ms",      "dr",
     "vs",     "св",   "проф",  "акад", "доц",    "канд",   "д-р",  "ред",     "гр",     "ім",     "тов",     "п",
     "пп",     "ч",    "чч",    "гл",   "абз",    "пт",     "no",   "просп",   "пр",     "вул",    "ш",       "м",
     "смт",    "с",    "обл",   "р-н",  "корп",   "пер",    "пл",   "буд",     "кв",     "оф",     "каб",     "літ",
     "р",      "а",    "оз",    "г",    "напр",   "дод",    "юр",   "фіз",     "тел",    "тобто",  "див",     "розд",
     "табл",   "мал",  "рис",   "пор",  "упоряд", "мкр",    "наб",  "пров",    "шос",    "бул",    "деп",     "пост",
     "наказ",  "підп", "арк",   "вип",  "стор",   "ухв",    "ріш",  "провадж", "спр",    "поз",    "позов",   "відп",
     "заявн",  "оск",  "адмін", "крим", "цив",    "госп",   "док",  "прим",    "перекл", "вид",    "т",       "тт",
     "зб",     "зош",  "журн",  "газ",  "асист",  "викл",   "зав",  "лаб",     "інж",    "н",      "чл.-кор", "м-н",
     "ж/м",    "в/ч",  "остр",  "річ",  "станц",  "залізн", "бл",   "прибл",   "зокр",   "порівн", "підрозд", "тр",
     "скаржн", "кк",   "кпк",   "цк",   "цпк",    "гк",     "гпк",  "кас",     "купап",  "кзпп",   "пку",     "мку",
     "зку",    "ску",  "вс",    "вп",   "кцс",    "кгс",    "ккс",  "оаск",    "v",      "єрдр",   "ecli"});
const auto other_abbreviations = words({"скор", "рис", "винят", "прим", "заст", "жарт"});
const auto initials = words({"дж", "ed"});

const std::unordered_set<std::string> leading_abbreviation_pairs = {
    "т е", "т к", "т н", "и о", "к н", "к п", "п н", "к т", "л д", "і т", "ст ст", "а с"};
const std::unordered_set<std::string> abbreviation_pairs = {
    "т п", "т д", "у е", "н э", "p m", "a m", "с г", "р х",  "с ш",  "з д",        "л с",        "ч т", "т е",   "т к",
    "т н", "и о", "к н", "к п", "п н", "к т", "л д", "ед ч", "мн ч", "повел накл", "жен рмуж р", "і т", "ст ст", "а с"};

bool is_known_abbreviation(std::string_view value)
{
    return trailing_abbreviations.contains(value) || leading_abbreviations.contains(value) ||
           other_abbreviations.contains(value);
}

bool can_follow_abbreviation(std::string_view token)
{
    auto cps = codepoints(token);
    if (cps.empty()) {
        return false;
    }
    if (std::ranges::all_of(cps, [](const Cp& cp) { return is_digit(cp.value); })) {
        return true;
    }
    if (!std::ranges::all_of(cps, [](const Cp& cp) { return is_alpha(cp.value); })) {
        return true;
    }
    return is_lower_alpha(token);
}

bool is_roman_token(std::string_view token)
{
    if (token.empty()) {
        return false;
    }
    for (char c : token) {
        if (!std::string_view("IVXLCDM").contains(c)) {
            return false;
        }
    }
    return true;
}

bool is_article_abbrev_right(std::string_view token)
{
    const auto cps = codepoints(token);
    if (cps.empty()) {
        return false;
    }
    if (std::ranges::all_of(cps, [](const Cp& cp) { return is_digit(cp.value); })) {
        return true;
    }
    return is_roman_token(token);
}

bool roman(std::string_view token)
{
    if (token.empty()) {
        return false;
    }
    for (char c : token) {
        if (!std::string_view("IVXML").contains(c)) {
            return false;
        }
    }
    return true;
}

bool is_bullet(std::string_view token)
{
    if (token.empty()) {
        return false;
    }
    if (std::ranges::all_of(token, [](unsigned char ch) { return std::isdigit(ch); })) {
        return true;
    }
    if (token == "." || token == ")") {
        return true;
    }
    const auto lower = lower_ascii_ukrainian(token);
    return std::string_view("§абвгдеabcdef").contains(std::string_view(lower)) || roman(token);
}

bool is_smile_at(std::string_view text, std::size_t pos, std::size_t& stop)
{
    if (pos >= text.size() || (text[pos] != '=' && text[pos] != ':' && text[pos] != ';')) {
        return false;
    }
    std::size_t i = pos + 1;
    if (i < text.size() && text[i] == '-') {
        ++i;
    }
    std::size_t count = 0;
    while (i < text.size() && (text[i] == '(' || text[i] == ')') && count < 3) {
        ++i;
        ++count;
    }
    if (count == 0) {
        return false;
    }
    stop = i;
    return true;
}

bool smile_prefix(std::string_view text)
{
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t next = i + 1;
        if (!is_space(decode_one(text, i, next))) {
            break;
        }
        i = next;
    }
    std::size_t stop = i;
    return is_smile_at(text, i, stop);
}

struct SentSplit {
    std::string_view left;
    std::string_view delimiter;
    std::string_view right;
    std::string_view buffer;
};

Action sent_join(const SentSplit& split)
{
    static constexpr std::u32string_view endings = U".?!…";
    static constexpr std::u32string_view dashes = U"‑–—−-";
    static constexpr std::u32string_view generic_quotes = U"\"„'";
    static constexpr std::u32string_view close_quotes = U"»”’";
    static constexpr std::u32string_view close_brackets = U")]}";
    static constexpr std::u32string_view delimiters = U".?!…;\"„'»”’)]}";

    const auto left = last_token(split.left);
    const auto right = first_token(split.right);
    if (!left || !right) {
        return Action::Join;
    }
    if (!starts_with_space(split.right)) {
        return Action::Join;
    }
    const auto first_non_space = split.right.find_first_not_of(" \t\r\n");
    const auto leading_space = split.right.substr(0, first_non_space);
    const auto line_start = split.buffer.find_last_of("\r\n");
    const auto current_line =
        std::string_view(split.buffer).substr(line_start == std::string_view::npos ? 0 : line_start + 1);
    const auto heading_start = current_line.find_first_not_of(" \t");
    if (heading_start != std::string_view::npos && current_line.substr(heading_start).starts_with("==") &&
        first_non_space != std::string_view::npos && split.right.substr(first_non_space).starts_with("==")) {
        return Action::Join;
    }
    if (leading_space.contains('\n') || leading_space.contains('\r')) {
        return Action::None;
    }
    if (starts_with_letter_bullet(split.right)) {
        return Action::None;
    }
    if (is_lower_alpha(*right)) {
        return Action::Join;
    }
    std::size_t n = 0;
    const auto right_cp = decode_one(*right, 0, n);
    if (!contains(generic_quotes, right_cp) && (contains(delimiters, right_cp) || smile_prefix(split.right))) {
        return Action::Join;
    }
    std::size_t dnext = 0;
    const auto delimiter_cp = decode_one(split.delimiter, 0, dnext);
    const auto left_lower = lower_ascii_ukrainian(last_compound_abbrev_token(split.left).value_or(*left));
    if (split.delimiter == ".") {
        if (left_lower == "м" || left_lower == "с") {
            const auto tokens = tokens_in(split.left);
            if (tokens.size() >= 2 && std::ranges::all_of(codepoints(tokens[tokens.size() - 2]),
                                                          [](const Cp& cp) { return is_digit(cp.value); })) {
                return Action::None;
            }
        }
        bool skip_single_abbreviation = false;
        if (std::ranges::all_of(codepoints(*left), [](const Cp& cp) { return is_digit(cp.value); }) &&
            std::ranges::all_of(codepoints(*right), [](const Cp& cp) { return is_digit(cp.value); })) {
            return Action::Join;
        }
        if (auto dotted = trailing_dot_abbrev_token(split.left)) {
            const auto dotted_lower = lower_ascii_ukrainian(*dotted);
            if (is_known_abbreviation(dotted_lower) && can_follow_abbreviation(*right)) {
                return Action::Join;
            }
        }
        if (auto pair_match = left_abbreviation_pair(split.left)) {
            const auto a = lower_ascii_ukrainian(pair_match->first);
            const auto b = lower_ascii_ukrainian(pair_match->second);
            const std::string pair = a + " " + b;
            if (leading_abbreviation_pairs.contains(pair)) {
                return Action::Join;
            }
            if (abbreviation_pairs.contains(pair)) {
                if (can_follow_abbreviation(*right)) {
                    return Action::Join;
                }
                skip_single_abbreviation = true;
            }
        }
        if (!skip_single_abbreviation) {
            if ((left_lower == "ст" && is_article_abbrev_right(*right)) ||
                (left_lower != "ст" && leading_abbreviations.contains(left_lower)) ||
                (is_known_abbreviation(left_lower) && can_follow_abbreviation(*right))) {
                return Action::Join;
            }
            const auto right_lower = lower_ascii_ukrainian(*right);
            if (abbreviation_pairs.contains(left_lower + " " + right_lower)) {
                return Action::Join;
            }
        }
        if (is_upper_one(*left) || initials.contains(left_lower)) {
            return Action::Join;
        }
    }
    if ((split.delimiter == "." || split.delimiter == ")") && split.buffer.size() <= 20) {
        const auto toks = tokens_in(split.buffer);
        if (!toks.empty() && std::ranges::all_of(toks, is_bullet)) {
            return Action::Join;
        }
    }
    if (contains(close_quotes, delimiter_cp) || contains(generic_quotes, delimiter_cp) ||
        contains(close_brackets, delimiter_cp)) {
        std::size_t lnext = 0;
        const auto left_cp = decode_one(*left, 0, lnext);
        if (!contains(endings, left_cp)) {
            return Action::Join;
        }
        if (contains(generic_quotes, delimiter_cp) && ends_with_space(split.left)) {
            return Action::Join;
        }
    }
    if (contains(dashes, right_cp)) {
        auto rw = first_word(split.right);
        if (rw && is_lower_alpha(*rw)) {
            return Action::Join;
        }
    }
    return Action::None;
}

void append_substring(
    std::vector<Substring>& out, std::string_view text, std::size_t start, std::size_t stop, bool trim)
{
    auto view = text.substr(start, stop - start);
    if (trim) {
        view = trim_view(view, start);
    }
    if (!view.empty()) {
        out.push_back({start, start + view.size(), view});
    }
}


} // namespace detail

using namespace detail;

std::vector<Substring> split_sentences(std::string_view text)
{
    auto cps = codepoints(text);
    if (std::ranges::all_of(cps, [](const Cp& cp) { return is_space(cp.value); })) {
        return {};
    }
    static constexpr std::u32string_view delimiters = U".?!…;\"„'»”’)]}";
    std::vector<Substring> out;
    std::size_t current_start = 0;
    for (std::size_t ci = 0; ci < cps.size(); ++ci) {
        std::size_t stop = cps[ci].stop;
        bool is_delim = contains(delimiters, cps[ci].value);
        std::size_t delimiter_end_index = ci + 1;
        if (std::size_t smile_stop = 0; is_smile_at(text, cps[ci].start, smile_stop)) {
            stop = smile_stop;
            is_delim = true;
            while (delimiter_end_index < cps.size() && cps[delimiter_end_index].start < stop) {
                ++delimiter_end_index;
            }
        }
        if (!is_delim) {
            continue;
        }
        const auto start = cps[ci].start;
        const auto delimiter = text.substr(start, stop - start);
        const auto left_start = ci > 10 ? cps[ci - 10].start : 0;
        std::size_t right_stop = text.size();
        const auto right_begin_index = delimiter_end_index;
        if (right_begin_index < cps.size()) {
            const auto right_end_index = std::min(cps.size(), right_begin_index + 10);
            right_stop = cps[right_end_index - 1].stop;
        }
        const SentSplit split{text.substr(left_start, start - left_start),
                              delimiter,
                              text.substr(stop, right_stop - stop),
                              text.substr(current_start, start - current_start)};
        if (sent_join(split) != Action::Join) {
            append_substring(out, text, current_start, stop, true);
            current_start = stop;
        }
        ci = delimiter_end_index - 1;
    }
    append_substring(out, text, current_start, text.size(), true);
    return out;
}

std::vector<Substring> sentenize(std::string_view text)
{
    return split_sentences(text);
}


} // namespace rozpodil
