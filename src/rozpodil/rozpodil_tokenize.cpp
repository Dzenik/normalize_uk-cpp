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

bool is_token_punct(char32_t cp)
{
    static constexpr std::u32string_view puncts = U"\\/!#$%&*+,.:;<=>?@^_`|~№…‑–—−-«“‘»”’\"„'()[]{}";
    return contains(puncts, cp);
}

bool starts_with_at(std::string_view text, std::size_t pos, std::string_view prefix)
{
    return pos + prefix.size() <= text.size() && text.substr(pos, prefix.size()) == prefix;
}

bool is_web_stop(char32_t cp)
{
    return is_space(cp) || cp == U'<' || cp == U'>' || cp == U'"' || cp == U'«' || cp == U'»' || cp == U'“' ||
           cp == U'”' || cp == U'(' || cp == U')' || cp == U'[' || cp == U']' || cp == U'{' || cp == U'}';
}

bool is_trailing_url_punct(char32_t cp)
{
    return cp == U'.' || cp == U',' || cp == U';' || cp == U':' || cp == U'!' || cp == U'?';
}

std::optional<std::size_t> legal_number_atom_stop(const std::vector<Cp>& cps, std::size_t index)
{
    if (cps[index].value != U'№') {
        return std::nullopt;
    }
    std::size_t i = index + 1;
    if (i < cps.size() && cps[i].value == U'-') {
        ++i;
    }
    const auto body_begin = i;
    while (i < cps.size() &&
           (is_alpha(cps[i].value) || is_digit(cps[i].value) || cps[i].value == U'/' || cps[i].value == U'-')) {
        ++i;
    }
    return i > body_begin ? std::optional<std::size_t>(i) : std::nullopt;
}

std::optional<std::size_t> web_atom_stop(std::string_view text, const std::vector<Cp>& cps, std::size_t index)
{
    const auto start = cps[index].start;
    const bool url = starts_with_at(text, start, "http://") || starts_with_at(text, start, "https://");
    if (url) {
        std::size_t i = index;
        while (i < cps.size() && !is_web_stop(cps[i].value)) {
            ++i;
        }
        while (i > index && is_trailing_url_punct(cps[i - 1].value)) {
            --i;
        }
        return i > index ? std::optional<std::size_t>(i) : std::nullopt;
    }

    if (cps[index].value == U'@' || cps[index].value == U'#') {
        std::size_t i = index + 1;
        while (i < cps.size() && (is_alpha(cps[i].value) || is_digit(cps[i].value) || is_word_mark(cps[i].value))) {
            ++i;
        }
        return i > index + 1 ? std::optional<std::size_t>(i) : std::nullopt;
    }

    if (!is_alpha(cps[index].value) && !is_digit(cps[index].value)) {
        return std::nullopt;
    }
    std::size_t i = index;
    bool saw_at = false;
    bool saw_dot_after_at = false;
    while (i < cps.size()) {
        const auto cp = cps[i].value;
        if (is_alpha(cp) || is_digit(cp) || cp == U'_' || cp == U'-' || cp == U'.') {
            if (saw_at && cp == U'.') {
                saw_dot_after_at = true;
            }
            ++i;
            continue;
        }
        if (cp == U'@' && !saw_at && i > index) {
            saw_at = true;
            ++i;
            continue;
        }
        break;
    }
    while (i > index && is_trailing_url_punct(cps[i - 1].value)) {
        --i;
    }
    return saw_at && saw_dot_after_at && i > index ? std::optional<std::size_t>(i) : std::nullopt;
}

std::vector<Atom> atoms(std::string_view text)
{
    std::vector<Atom> out;
    auto cps = codepoints(text);
    for (std::size_t i = 0; i < cps.size();) {
        if (is_space(cps[i].value)) {
            ++i;
            continue;
        }
        const std::size_t begin = i;
        AtomType type = AtomType::Other;
        if (auto stop = legal_number_atom_stop(cps, i)) {
            type = AtomType::Other;
            i = *stop;
        } else if (auto stop = web_atom_stop(text, cps, i)) {
            type = AtomType::Other;
            i = *stop;
        } else if (is_uk(cps[i].value)) {
            type = AtomType::Uk;
            while (i < cps.size() && (is_uk(cps[i].value) || is_inner_uk_apostrophe(cps, i))) {
                ++i;
            }
        } else if (is_latin(cps[i].value)) {
            type = AtomType::Lat;
            while (i < cps.size() && is_latin(cps[i].value)) {
                ++i;
            }
        } else if (is_digit(cps[i].value)) {
            type = AtomType::Int;
            while (i < cps.size() && is_digit(cps[i].value)) {
                ++i;
            }
        } else {
            type = is_token_punct(cps[i].value) ? AtomType::Punct : AtomType::Other;
            ++i;
        }
        auto sv = text.substr(cps[begin].start, cps[i - 1].stop - cps[begin].start);
        out.push_back({cps[begin].start, cps[i - 1].stop, type, sv});
    }
    return out;
}

bool token_smile(std::string_view text)
{
    std::size_t stop = 0;
    return is_smile_at(text, 0, stop) && stop == text.size();
}

bool token_join(const Atom& left_1,
                const std::optional<Atom>& left_2,
                std::string_view delimiter,
                const Atom& right_1,
                const std::optional<Atom>& right_2,
                std::string_view buffer)
{
    auto rule2112 = [&](char32_t delim, auto pred) {
        std::size_t next = 0;
        if (!delimiter.empty() && decode_one(delimiter, 0, next) == delim) {
            if (!left_1.text.empty() && !right_1.text.empty()) {
                return pred(left_1, right_1);
            }
        }
        if (delimiter.empty() && left_2 && right_2) {
            std::size_t ln = 0;
            if (decode_one(left_1.text, 0, ln) == delim) {
                return pred(*left_2, right_1);
            }
            std::size_t rn = 0;
            if (decode_one(right_1.text, 0, rn) == delim) {
                return pred(left_1, *right_2);
            }
        }
        if (delimiter.empty() && left_2) {
            std::size_t ln = 0;
            if (decode_one(left_1.text, 0, ln) == delim) {
                return pred(*left_2, right_1);
            }
        }
        if (delimiter.empty() && right_2) {
            std::size_t rn = 0;
            if (decode_one(right_1.text, 0, rn) == delim) {
                return pred(left_1, *right_2);
            }
        }
        return false;
    };
    auto dash_or_underscore = [](const Atom& l, const Atom& r) {
        return l.type != AtomType::Punct && r.type != AtomType::Punct;
    };
    for (char32_t dash : std::u32string_view(U"‑–—−-")) {
        if (rule2112(dash, dash_or_underscore)) {
            return true;
        }
    }
    if (rule2112(U'_', dash_or_underscore)) {
        return true;
    }
    for (char32_t dot : std::u32string_view(U".,")) {
        if (rule2112(dot,
                     [](const Atom& l, const Atom& r) { return l.type == AtomType::Int && r.type == AtomType::Int; })) {
            return true;
        }
    }
    for (char32_t slash : std::u32string_view(U"/\\")) {
        if (rule2112(slash,
                     [](const Atom& l, const Atom& r) { return l.type == AtomType::Int && r.type == AtomType::Int; })) {
            return true;
        }
    }
    if (left_1.type == AtomType::Punct && right_1.type == AtomType::Punct) {
        std::string candidate(buffer);
        candidate.append(right_1.text);
        if (token_smile(candidate)) {
            return true;
        }
        if (std::string_view(".?!…").contains(left_1.text) && std::string_view(".?!…").contains(right_1.text)) {
            return true;
        }
        if (left_1.text == right_1.text && (left_1.text == "-" || left_1.text == "*")) {
            return true;
        }
    }
    if (left_1.type == AtomType::Other &&
        (right_1.type == AtomType::Other || right_1.type == AtomType::Uk || right_1.type == AtomType::Lat)) {
        return true;
    }
    if ((left_1.type == AtomType::Other || left_1.type == AtomType::Uk || left_1.type == AtomType::Lat) &&
        right_1.type == AtomType::Other) {
        return true;
    }
    if (delimiter.empty() && left_1.type == AtomType::Int &&
        (right_1.type == AtomType::Uk || right_1.type == AtomType::Lat) &&
        is_known_abbreviation(lower_ascii_ukrainian(right_1.text))) {
        return true;
    }
    return right_1.text == "!" && lower_ascii_ukrainian(left_1.text) == "yahoo";
}


} // namespace detail

using namespace detail;

std::vector<Substring> tokenize(std::string_view text)
{
    const auto as = atoms(text);
    if (as.empty()) {
        return {};
    }
    std::vector<Substring> out;
    std::size_t start = as.front().start;
    std::size_t stop = as.front().stop;
    for (std::size_t i = 1; i < as.size(); ++i) {
        const auto delimiter = text.substr(as[i - 1].stop, as[i].start - as[i - 1].stop);
        std::optional<Atom> left_2;
        std::optional<Atom> right_2;
        if (i >= 2) {
            left_2 = as[i - 2];
        }
        if (i + 1 < as.size()) {
            right_2 = as[i + 1];
        }
        const auto buffer = text.substr(start, stop - start);
        if (delimiter.empty() && token_join(as[i - 1], left_2, delimiter, as[i], right_2, buffer)) {
            stop = as[i].stop;
        } else {
            append_substring(out, text, start, stop, false);
            start = as[i].start;
            stop = as[i].stop;
        }
    }
    append_substring(out, text, start, stop, false);
    return out;
}

} // namespace rozpodil
