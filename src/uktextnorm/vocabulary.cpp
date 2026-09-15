#include "uktextnorm/uktextnorm.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>

namespace uktextnorm {
namespace {

bool latin_word(std::string_view word)
{
    if (word.empty()) {
        return false;
    }
    const auto letter = [](unsigned char ch) { return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'); };
    if (!letter(static_cast<unsigned char>(word.front())) || !letter(static_cast<unsigned char>(word.back()))) {
        return false;
    }
    for (const unsigned char ch : word) {
        if (!letter(ch) && ch != '-' && ch != '\'') {
            return false;
        }
    }
    return true;
}

bool valid_utf8(std::string_view text)
{
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i]);
        if (first < 0x80) {
            ++i;
            continue;
        }
        const std::size_t width = first >= 0xC2 && first <= 0xDF   ? 2
                                  : first >= 0xE0 && first <= 0xEF ? 3
                                  : first >= 0xF0 && first <= 0xF4 ? 4
                                                                   : 0;
        if (!width || i + width > text.size()) {
            return false;
        }
        for (std::size_t j = 1; j < width; ++j) {
            if ((static_cast<unsigned char>(text[i + j]) & 0xC0) != 0x80) {
                return false;
            }
        }
        const auto second = static_cast<unsigned char>(text[i + 1]);
        if ((first == 0xE0 && second < 0xA0) || (first == 0xED && second >= 0xA0) || (first == 0xF0 && second < 0x90) ||
            (first == 0xF4 && second >= 0x90)) {
            return false;
        }
        i += width;
    }
    return true;
}

} // namespace

std::unordered_map<std::string, std::string> load_vocabulary_tsv(std::string_view path)
{
    std::ifstream input(std::string(path), std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open vocabulary file: " + std::string(path));
    }
    std::unordered_map<std::string, std::string> words;
    std::string line;
    std::size_t line_number = 0;
    bool header_seen = false;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (!header_seen) {
            if (line_number == 1 && line.starts_with("\xEF\xBB\xBF")) {
                line.erase(0, 3);
            }
            if (line != "latin\tcyrillic") {
                throw std::invalid_argument("vocabulary line " + std::to_string(line_number) +
                                            ": expected latin<TAB>cyrillic header");
            }
            header_seen = true;
            continue;
        }
        const auto tab = line.find('\t');
        if (tab == std::string::npos || line.find('\t', tab + 1) != std::string::npos) {
            throw std::invalid_argument("vocabulary line " + std::to_string(line_number) + ": expected two columns");
        }
        auto key = line.substr(0, tab);
        auto reading = line.substr(tab + 1);
        if (!latin_word(key)) {
            throw std::invalid_argument("vocabulary line " + std::to_string(line_number) +
                                        ": latin must be one ASCII Latin word");
        }
        if (reading.empty() || std::isspace(static_cast<unsigned char>(reading.front())) ||
            std::isspace(static_cast<unsigned char>(reading.back())) || !valid_utf8(reading)) {
            throw std::invalid_argument("vocabulary line " + std::to_string(line_number) +
                                        ": cyrillic must be nonempty UTF-8 without surrounding whitespace");
        }
        std::transform(
            key.begin(), key.end(), key.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (!words.emplace(std::move(key), std::move(reading)).second) {
            throw std::invalid_argument("vocabulary line " + std::to_string(line_number) + ": duplicate latin word");
        }
    }
    if (!header_seen) {
        throw std::invalid_argument("vocabulary file is missing latin<TAB>cyrillic header");
    }
    return words;
}

} // namespace uktextnorm
