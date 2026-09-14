#include "uktextnorm/uktextnorm.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: uktextnorm_sentence_tests <golden.tsv>\n";
        return 2;
    }
    std::ifstream input(argv[1]);
    if (!input) {
        std::cerr << "cannot open sentence golden file: " << argv[1] << "\n";
        return 2;
    }

    std::unordered_map<std::string, std::size_t> category_counts;
    std::size_t failures = 0;
    std::size_t row = 0;
    std::string line;
    while (std::getline(input, line)) {
        ++row;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto first_tab = line.find('\t');
        const auto second_tab = first_tab == std::string::npos ? std::string::npos : line.find('\t', first_tab + 1);
        if (first_tab == std::string::npos || second_tab == std::string::npos ||
            line.find('\t', second_tab + 1) != std::string::npos) {
            std::cerr << "row " << row << ": expected category, input, and output separated by tabs\n";
            ++failures;
            continue;
        }

        const auto category = line.substr(0, first_tab);
        const auto source = line.substr(first_tab + 1, second_tab - first_tab - 1);
        const auto expected = line.substr(second_tab + 1);
        ++category_counts[category];
        const auto actual = uktextnorm::normalize_ukrainian(source, uktextnorm::NormalizePreset::TtsFriendly);
        if (actual != expected) {
            std::cerr << category << " row " << row << "\ninput:    " << source << "\nexpected: " << expected
                      << "\nactual:   " << actual << "\n";
            ++failures;
        }
        const auto repeated = uktextnorm::normalize_ukrainian(actual, uktextnorm::NormalizePreset::TtsFriendly);
        if (repeated != actual) {
            std::cerr << category << " row " << row << " is not idempotent\nfirst:  " << actual
                      << "\nsecond: " << repeated << "\n";
            ++failures;
        }
    }

    for (const auto& [category, count] : category_counts) {
        if (count < 2) {
            std::cerr << "category " << category << " has only " << count << " sentence case(s); expected at least 2\n";
            ++failures;
        }
    }
    if (category_counts.size() < 20) {
        std::cerr << "only " << category_counts.size()
                  << " normalization categories are covered; expected at least 20\n";
        ++failures;
    }
    return failures == 0 ? 0 : 1;
}
