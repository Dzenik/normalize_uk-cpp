#pragma once

#include "../common/utf8.hpp"
#include "rozpodil/rozpodil.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rozpodil::detail {

using normalize_uk_cpp::detail::decode_one;

enum class AtomType {
    Uk,
    Lat,
    Int,
    Punct,
    Other
};

struct Cp {
    char32_t value = 0;
    std::size_t start = 0;
    std::size_t stop = 0;
};

struct Atom {
    std::size_t start = 0;
    std::size_t stop = 0;
    AtomType type = AtomType::Other;
    std::string_view text;
};

std::vector<Cp> codepoints(std::string_view text);
bool contains(std::u32string_view set, char32_t cp);
bool is_space(char32_t cp);
bool is_digit(char32_t cp);
bool is_latin(char32_t cp);
bool is_alpha(char32_t cp);
bool is_word_mark(char32_t cp);
bool is_uk(char32_t cp);
bool is_inner_uk_apostrophe(const std::vector<Cp>& cps, std::size_t index);
bool is_known_abbreviation(std::string_view value);
bool is_smile_at(std::string_view text, std::size_t pos, std::size_t& stop);
std::string lower_ascii_ukrainian(std::string_view text);
void append_substring(
    std::vector<Substring>& out, std::string_view text, std::size_t start, std::size_t stop, bool trim);

} // namespace rozpodil::detail
