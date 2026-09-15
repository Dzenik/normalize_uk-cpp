#include "uktextnorm/uktextnorm.hpp"

#include "internal.hpp"

#include "generated/uktextnorm_lexicons.hpp"

namespace uktextnorm::detail {

int roman_to_int(std::string_view s)
{
    static const std::unordered_map<char, int> values = {
        {'I', 1}, {'V', 5}, {'X', 10}, {'L', 50}, {'C', 100}, {'D', 500}, {'M', 1000}};
    int total = 0;
    int prev = 0;
    for (auto it = s.rbegin(); it != s.rend(); ++it) {
        const int v = values.at(*it);
        total += v < prev ? -v : v;
        prev = v;
    }
    return total;
}

bool is_valid_date(int day, int month, int year)
{
    if (month < 1 || month > 12 || day < 1) {
        return false;
    }
    static constexpr std::array<int, 12> lengths = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int max_day = lengths[static_cast<std::size_t>(month - 1)];
    if (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) {
        max_day = 29;
    }
    return day <= max_day;
}

bool is_valid_iso_week(int year, int week)
{
    if (week < 1 || week > 53) {
        return false;
    }
    if (week <= 52) {
        return true;
    }
    const auto previous_year = year - 1;
    const auto january_first_weekday =
        (previous_year + previous_year / 4 - previous_year / 100 + previous_year / 400 + 1) % 7;
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    return january_first_weekday == 4 || (january_first_weekday == 3 && leap);
}

std::string compact_ascii_alnum_upper(std::string_view value)
{
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        if (ch >= 'a' && ch <= 'z') {
            out.push_back(static_cast<char>(ch - 'a' + 'A'));
        } else if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            out.push_back(static_cast<char>(ch));
        }
    }
    return out;
}

bool valid_isbn(std::string_view value)
{
    const auto compact = compact_ascii_alnum_upper(value);
    if (compact.size() == 10) {
        unsigned sum = 0;
        for (std::size_t i = 0; i < compact.size(); ++i) {
            const char ch = compact[i];
            const unsigned digit = ch == 'X' && i == 9 ? 10U : ch >= '0' && ch <= '9' ? ch - '0' : 11U;
            if (digit > 10) {
                return false;
            }
            sum += static_cast<unsigned>(10 - i) * digit;
        }
        return sum % 11 == 0;
    }
    if (compact.size() == 13 && compact.find_first_not_of("0123456789") == std::string::npos &&
        (compact.starts_with("978") || compact.starts_with("979"))) {
        unsigned sum = 0;
        for (std::size_t i = 0; i < compact.size(); ++i) {
            sum += static_cast<unsigned>(compact[i] - '0') * (i % 2 == 0 ? 1U : 3U);
        }
        return sum % 10 == 0;
    }
    return false;
}

bool valid_issn(std::string_view value)
{
    const auto compact = compact_ascii_alnum_upper(value);
    if (compact.size() != 8) {
        return false;
    }
    unsigned sum = 0;
    for (std::size_t i = 0; i < compact.size(); ++i) {
        const char ch = compact[i];
        const unsigned digit = ch == 'X' && i == 7 ? 10U : ch >= '0' && ch <= '9' ? ch - '0' : 11U;
        if (digit > 10) {
            return false;
        }
        sum += static_cast<unsigned>(8 - i) * digit;
    }
    return sum % 11 == 0;
}

bool valid_iban(std::string_view value)
{
    const auto compact = compact_ascii_alnum_upper(value);
    if (compact.size() < 15 || compact.size() > 34 || compact[0] < 'A' || compact[0] > 'Z' || compact[1] < 'A' ||
        compact[1] > 'Z' || compact[2] < '0' || compact[2] > '9' || compact[3] < '0' || compact[3] > '9') {
        return false;
    }
    static const std::unordered_map<std::string_view, std::size_t> country_lengths = {
        {"AL", 28}, {"AD", 24}, {"AT", 20}, {"AZ", 28}, {"BH", 22}, {"BE", 16}, {"BA", 20}, {"BR", 29}, {"BG", 22},
        {"CR", 22}, {"HR", 21}, {"CY", 28}, {"CZ", 24}, {"DK", 18}, {"DO", 28}, {"EE", 20}, {"FO", 18}, {"FI", 18},
        {"FR", 27}, {"GE", 22}, {"DE", 22}, {"GI", 23}, {"GR", 27}, {"GL", 18}, {"GT", 28}, {"HU", 28}, {"IS", 26},
        {"IE", 22}, {"IL", 23}, {"IT", 27}, {"JO", 30}, {"KZ", 20}, {"XK", 20}, {"KW", 30}, {"LV", 21}, {"LB", 28},
        {"LI", 21}, {"LT", 20}, {"LU", 20}, {"MT", 31}, {"MR", 27}, {"MU", 30}, {"MC", 27}, {"MD", 24}, {"ME", 22},
        {"NL", 18}, {"MK", 19}, {"NO", 15}, {"PK", 24}, {"PS", 29}, {"PL", 28}, {"PT", 25}, {"QA", 29}, {"RO", 24},
        {"LC", 32}, {"SM", 27}, {"ST", 25}, {"SA", 24}, {"RS", 22}, {"SC", 31}, {"SK", 24}, {"SI", 19}, {"ES", 24},
        {"SE", 24}, {"CH", 21}, {"TL", 23}, {"TN", 24}, {"TR", 26}, {"UA", 29}, {"AE", 23}, {"GB", 22}, {"VA", 22},
        {"VG", 24}};
    const auto country = std::string_view(compact).substr(0, 2);
    if (const auto it = country_lengths.find(country); it != country_lengths.end() && compact.size() != it->second) {
        return false;
    }
    unsigned remainder = 0;
    auto consume = [&](char ch) {
        if (ch >= '0' && ch <= '9') {
            remainder = (remainder * 10 + static_cast<unsigned>(ch - '0')) % 97;
            return true;
        }
        if (ch < 'A' || ch > 'Z') {
            return false;
        }
        const auto value = static_cast<unsigned>(ch - 'A' + 10);
        remainder = (remainder * 100 + value) % 97;
        return true;
    };
    for (std::size_t i = 4; i < compact.size(); ++i) {
        if (!consume(compact[i])) {
            return false;
        }
    }
    for (std::size_t i = 0; i < 4; ++i) {
        if (!consume(compact[i])) {
            return false;
        }
    }
    return remainder == 1;
}

bool valid_luhn(std::string_view value)
{
    std::string digits;
    for (unsigned char ch : value) {
        if (ch >= '0' && ch <= '9') {
            digits.push_back(static_cast<char>(ch));
        } else if (ch != ' ' && ch != '-') {
            return false;
        }
    }
    if (digits.size() < 12 || digits.size() > 19) {
        return false;
    }
    unsigned sum = 0;
    bool double_digit = false;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        unsigned digit = static_cast<unsigned>(*it - '0');
        if (double_digit) {
            digit *= 2;
            if (digit > 9) {
                digit -= 9;
            }
        }
        sum += digit;
        double_digit = !double_digit;
    }
    return sum % 10 == 0;
}

bool valid_vin_checksum(std::string_view value)
{
    const auto compact = compact_ascii_alnum_upper(value);
    if (compact.size() != 17 || compact.find_first_of("IOQ") != std::string::npos) {
        return false;
    }
    if (compact[0] < '1' || compact[0] > '5') {
        return true; // A checksum is mandatory for North American VINs, but not in every region.
    }
    static constexpr std::array<unsigned, 17> weights = {8, 7, 6, 5, 4, 3, 2, 10, 0, 9, 8, 7, 6, 5, 4, 3, 2};
    static const std::unordered_map<char, unsigned> letters = {
        {'A', 1}, {'B', 2}, {'C', 3}, {'D', 4}, {'E', 5}, {'F', 6}, {'G', 7}, {'H', 8},
        {'J', 1}, {'K', 2}, {'L', 3}, {'M', 4}, {'N', 5}, {'P', 7}, {'R', 9}, {'S', 2},
        {'T', 3}, {'U', 4}, {'V', 5}, {'W', 6}, {'X', 7}, {'Y', 8}, {'Z', 9}};
    unsigned sum = 0;
    for (std::size_t i = 0; i < compact.size(); ++i) {
        const char ch = compact[i];
        const auto value = ch >= '0' && ch <= '9' ? static_cast<unsigned>(ch - '0')
                           : letters.contains(ch) ? letters.at(ch)
                                                  : 10U;
        if (value > 9) {
            return false;
        }
        sum += value * weights[i];
    }
    const auto remainder = sum % 11;
    const char expected = remainder == 10 ? 'X' : static_cast<char>('0' + remainder);
    return compact[8] == expected;
}

bool valid_uuid_variant(std::string_view value)
{
    const auto compact = compact_ascii_alnum_upper(value);
    if (compact.size() != 32 || compact.find_first_not_of("0123456789ABCDEF") != std::string::npos) {
        return false;
    }
    if (compact.find_first_not_of('0') == std::string::npos) {
        return true;
    }
    return compact[12] >= '1' && compact[12] <= '8' && std::string_view("89AB").contains(compact[16]);
}

bool valid_hash_length(std::string_view algorithm, std::string_view value)
{
    auto normalized = compact_ascii_alnum_upper(algorithm);
    const auto compact = compact_ascii_alnum_upper(value);
    static const std::unordered_map<std::string, std::size_t> lengths = {{"MD5", 32},
                                                                         {"SHA1", 40},
                                                                         {"SHA224", 56},
                                                                         {"SHA256", 64},
                                                                         {"SHA384", 96},
                                                                         {"SHA512", 128},
                                                                         {"SHA3256", 64},
                                                                         {"SHA3512", 128},
                                                                         {"BLAKE2S", 64},
                                                                         {"BLAKE2B", 128}};
    const auto it = lengths.find(normalized);
    return it != lengths.end() && compact.size() == it->second &&
           compact.find_first_not_of("0123456789ABCDEF") == std::string::npos;
}

bool valid_roman(std::string_view s)
{
    return ctre::match<R"(M{0,4}(?:CM|CD|D?C{0,3})(?:XC|XL|L?X{0,3})(?:IX|IV|V?I{0,3}))">(s);
}

} // namespace uktextnorm::detail
