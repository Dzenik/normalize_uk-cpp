#include "uktextnorm/uktextnorm.hpp"

#include "internal.hpp"

#include "generated/uktextnorm_lexicons.hpp"

namespace uktextnorm {
using namespace detail;

std::string normalize_abbreviations(std::string_view text)
{
    static const std::vector<std::string> keys = [] {
        std::vector<std::string> out;
        for (const auto& entry : lexicon::kAbbreviations) {
            out.emplace_back(entry.key);
        }
        return out;
    }();
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        bool matched = false;
        for (const auto& key : keys) {
            std::size_t pos = i;
            std::size_t kpos = 0;
            while (kpos < key.size()) {
                if (key[kpos] == ' ') {
                    while (pos < text.size() && text[pos] == ' ') {
                        ++pos;
                    }
                    ++kpos;
                } else if (key[kpos] == '.') {
                    while (pos < text.size() && text[pos] == ' ') {
                        ++pos;
                    }
                    if (pos >= text.size() || text[pos] != '.') {
                        break;
                    }
                    ++pos;
                    ++kpos;
                    if (kpos < key.size()) {
                        while (pos < text.size() && text[pos] == ' ') {
                            ++pos;
                        }
                    }
                } else {
                    std::size_t next_key = kpos + 1;
                    std::size_t next_text = pos + 1;
                    const auto kc = lower_cp(decode_one(key, kpos, next_key));
                    const auto tc = pos < text.size() ? lower_cp(decode_one(text, pos, next_text)) : U'\0';
                    if (kc != tc) {
                        break;
                    }
                    kpos = next_key;
                    pos = next_text;
                }
            }
            if (kpos == key.size()) {
                const auto is_word_character = [](char32_t cp) {
                    return is_uk(cp) || is_latin(cp) || (cp >= U'0' && cp <= U'9');
                };
                bool left_boundary = true;
                if (i != 0) {
                    auto previous = i - 1;
                    while (previous > 0 && is_utf8_continuation(text[previous])) {
                        --previous;
                    }
                    std::size_t previous_stop = previous + 1;
                    left_boundary = !is_word_character(decode_one(text, previous, previous_stop));
                }
                std::size_t key_start_stop = 1;
                const auto key_start = decode_one(key, 0, key_start_stop);
                auto key_end_start = key.size() - 1;
                while (key_end_start > 0 && is_utf8_continuation(key[key_end_start])) {
                    --key_end_start;
                }
                std::size_t key_end_stop = key_end_start + 1;
                const auto key_end = decode_one(key, key_end_start, key_end_stop);
                std::size_t following_stop = pos + 1;
                const auto following = pos < text.size() ? decode_one(text, pos, following_stop) : U'\0';
                const bool right_boundary = !is_word_character(key_end) || !is_word_character(following);
                if ((is_word_character(key_start) && !left_boundary) || !right_boundary) {
                    continue;
                }
                auto expansion = abbreviation_map().at(compact_spaces_lower(std::string_view(text).substr(i, pos - i)));
                std::size_t first_stop = i + 1;
                if (is_upper_uk(decode_one(text, i, first_stop))) {
                    expansion = capitalize_first_letter(std::move(expansion));
                }
                out += expansion;
                if (key.ends_with('.') && pos == text.size()) {
                    out.push_back('.');
                }
                i = pos;
                matched = true;
                break;
            }
        }
        if (!matched) {
            std::size_t next = i + 1;
            decode_one(text, i, next);
            out.append(text.substr(i, next - i));
            i = next;
        }
    }
    return out;
}

std::string expand_abbreviations(std::string_view text)
{
    static const std::u32string_view vowels = U"АЕЄИІЇОУЮЯ";
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        std::size_t next = i + 1;
        const auto cp = decode_one(text, i, next);
        if (!is_upper_uk(cp)) {
            out.append(text.substr(i, next - i));
            i = next;
            continue;
        }
        const auto start = i;
        std::size_t count = 0;
        while (i < text.size()) {
            std::size_t n = i + 1;
            if (!is_upper_uk(decode_one(text, i, n))) {
                break;
            }
            i = n;
            ++count;
        }
        const auto token = std::string(text.substr(start, i - start));
        if (count < 2) {
            out += token;
            continue;
        }
        bool has_vowel = false;
        for (const auto& cp : codepoints(token)) {
            if (vowels.contains(cp.value)) {
                has_vowel = true;
                break;
            }
        }
        if (has_vowel) {
            out += token;
            continue;
        }
        std::vector<std::string> parts;
        for (const auto& cp : codepoints(token)) {
            std::string letter;
            append_utf8(letter, cp.value);
            if (const auto it = pronunciation_map().find(letter); it != pronunciation_map().end()) {
                parts.push_back(it->second);
            }
        }
        out += join(parts);
    }
    return out;
}

std::string transliterate_to_cyrillic(std::string_view text)
{
    static const std::unordered_map<char32_t, std::string_view> latin_diacritics = {
        {U'á', "а"}, {U'à', "а"}, {U'â', "а"},  {U'ã', "а"},  {U'å', "а"},  {U'ā', "а"}, {U'ă', "а"}, {U'ą', "а"},
        {U'Á', "а"}, {U'À', "а"}, {U'Â', "а"},  {U'Ã', "а"},  {U'Å', "а"},  {U'Ā', "а"}, {U'Ă', "а"}, {U'Ą', "а"},
        {U'ä', "е"}, {U'Ä', "е"}, {U'é', "е"},  {U'è', "е"},  {U'ê', "е"},  {U'ë', "е"}, {U'ē', "е"}, {U'ė', "е"},
        {U'ę', "е"}, {U'É', "е"}, {U'È', "е"},  {U'Ê', "е"},  {U'Ë', "е"},  {U'Ē', "е"}, {U'Ė', "е"}, {U'Ę', "е"},
        {U'í', "і"}, {U'ì', "і"}, {U'î', "і"},  {U'ï', "і"},  {U'ī', "і"},  {U'Í', "і"}, {U'Ì', "і"}, {U'Î', "і"},
        {U'Ï', "і"}, {U'Ī', "і"}, {U'ó', "о"},  {U'ò', "о"},  {U'ô', "о"},  {U'õ', "о"}, {U'ö', "о"}, {U'ō', "о"},
        {U'Ó', "о"}, {U'Ò', "о"}, {U'Ô', "о"},  {U'Õ', "о"},  {U'Ö', "о"},  {U'Ō', "о"}, {U'ú', "у"}, {U'ù', "у"},
        {U'û', "у"}, {U'ū', "у"}, {U'Ú', "у"},  {U'Ù', "у"},  {U'Û', "у"},  {U'Ū', "у"}, {U'ü', "ю"}, {U'Ü', "ю"},
        {U'ç', "с"}, {U'Ç', "с"}, {U'ñ', "нь"}, {U'Ñ', "нь"}, {U'ß', "сс"}, {U'ł', "л"}, {U'Ł', "л"}, {U'ý', "и"},
        {U'ÿ', "и"}, {U'Ý', "и"}, {U'Ÿ', "и"}};
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        std::size_t next = i + 1;
        auto cp = decode_one(text, i, next);
        if (cp < 128 && ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z'))) {
            std::string tri;
            if (i + 2 < text.size()) {
                tri = lower_text(text.substr(i, 3));
            }
            if (const auto it = cyrillic_transliteration_map().find(tri); it != cyrillic_transliteration_map().end()) {
                out += it->second;
                i += 3;
                continue;
            }
            std::string di;
            if (i + 1 < text.size()) {
                di = lower_text(text.substr(i, 2));
            }
            if (const auto it = cyrillic_transliteration_map().find(di); it != cyrillic_transliteration_map().end()) {
                out += it->second;
                i += 2;
                continue;
            }
            const std::string one = lower_text(text.substr(i, 1));
            if (const auto it = cyrillic_transliteration_map().find(one); it != cyrillic_transliteration_map().end()) {
                out += it->second;
            } else {
                out.append(text.substr(i, next - i));
            }
            i = next;
        } else if (const auto it = latin_diacritics.find(cp); it != latin_diacritics.end()) {
            out += it->second;
            i = next;
        } else {
            out.append(text.substr(i, next - i));
            i = next;
        }
    }
    return out;
}

std::string cyrilize(std::string_view text)
{
    return transliterate_to_cyrillic(text);
}

std::string cyrrilize(std::string_view text)
{
    return transliterate_to_cyrillic(text);
}

} // namespace uktextnorm
