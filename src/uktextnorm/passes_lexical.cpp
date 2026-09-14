#include "uktextnorm/uktextnorm.hpp"

#include "internal.hpp"

#include "generated/uktextnorm_lexicons.hpp"

namespace uktextnorm {
namespace detail {

std::string normalize_known_acronyms(std::string text)
{
    static const std::unordered_map<std::string, std::string> map = [] {
        std::unordered_map<std::string, std::string> out;
        for (const auto& entry : lexicon::kAcronyms) {
            out.emplace(std::string(entry.acronym), std::string(entry.expansion));
        }
        return out;
    }();
    static const std::regex re([] {
        std::vector<std::string> keys;
        for (const auto& entry : lexicon::kAcronyms) {
            keys.emplace_back(entry.acronym);
        }
        return "(^|[^А-Яа-яЄєІіЇїҐґ])((?:" + regex_alternation(std::move(keys)) + R"())(?![А-Яа-яЄєІіЇїҐґ]))";
    }());
    return regex_sub(text, re, [](const std::smatch& m) {
        auto out = map.at(m[2].str());
        const auto prefix = m[1].str();
        auto before = m.prefix().str() + prefix;
        while (!before.empty() && std::isspace(static_cast<unsigned char>(before.back()))) {
            before.pop_back();
        }
        if (before.empty() || before.back() == '.' || before.back() == '!' || before.back() == '?') {
            out = capitalize_first_letter(std::move(out));
        }
        return prefix + out;
    });
}
std::string normalize_symbol_currency(std::string text)
{
    static const std::unordered_map<std::string, std::string> genpl = [] {
        std::unordered_map<std::string, std::string> out = {{"грн", "гривень"}};
        for (const auto& entry : lexicon::kCurrencies) {
            out.emplace(std::string(entry.code), std::string(entry.main_many));
            if (!entry.symbol.empty()) {
                out.emplace(std::string(entry.symbol), std::string(entry.main_many));
            }
        }
        return out;
    }();
    text = regex_sub(text, symbol_currency_prefix_re(), [&](const std::smatch& m) {
        return m[2].str() + " " + m[3].str() + " " + genpl.at(m[1].str());
    });
    return regex_sub(text, symbol_currency_suffix_re(), [&](const std::smatch& m) {
        return m[1].str() + " " + m[2].str() + " " + genpl.at(m[3].str());
    });
}
std::string normalize_currency(std::string text)
{
    struct Currency {
        Forms main;
        bool main_fem = false;
        Forms sub;
        bool sub_fem = false;
        std::vector<std::regex> patterns;
    };
    static const std::unordered_map<std::string, std::string_view> regional_aliases = {{"us$", "USD"},
                                                                                       {"ca$", "CAD"},
                                                                                       {"au$", "AUD"},
                                                                                       {"nz$", "NZD"},
                                                                                       {"hk$", "HKD"},
                                                                                       {"sg$", "SGD"},
                                                                                       {"jp¥", "JPY"},
                                                                                       {"cn¥", "CNY"},
                                                                                       {"r$", "BRL"}};
    static const std::regex regional_alias(R"((US\$|CA\$|AU\$|NZ\$|HK\$|SG\$|JP¥|CN¥|R\$))", std::regex::icase);
    text = regex_sub(text, regional_alias, [](const std::smatch& m) {
        return std::string(regional_aliases.at(lower_text(m.str())));
    });
    static const std::regex signed_prefix("([+-])\\s*(" + currency_token_alt() + ")\\s*(?=\\d)", std::regex::icase);
    text = regex_sub(text, signed_prefix, [](const std::smatch& m) { return m[2].str() + m[1].str(); });
    static const std::regex accounting_prefix("\\((" + currency_token_alt() + ")\\s*(\\d+(?:[.,]\\d{1,2})?)\\)",
                                              std::regex::icase);
    text = regex_sub(text, accounting_prefix, [](const std::smatch& m) { return "-" + m[2].str() + " " + m[1].str(); });
    static const std::regex accounting("\\((\\d+(?:[.,]\\d{1,2})?)\\s*(" + currency_token_alt() + ")\\)",
                                       std::regex::icase);
    text = regex_sub(text, accounting, [](const std::smatch& m) { return "-" + m[1].str() + " " + m[2].str(); });
    static const std::vector<Currency> currencies = [] {
        static constexpr std::string_view amount =
            R"(([+-]?(?:[1-9]\d{0,2}(?:,\d{3})+(?:\.\d{1,2})?|[1-9]\d{0,2}(?:\.\d{3})+(?:,\d{1,2})?|\d+[.,]\d{1,2}|\d+|[.,]\d{1,2})(?!\d|[.,]\d)))";
        std::vector<Currency> out;
        for (const auto& entry : lexicon::kCurrencies) {
            Currency c;
            c.main = {entry.main_one, entry.main_few, entry.main_many};
            c.main_fem = entry.main_fem;
            c.sub = {entry.sub_one, entry.sub_few, entry.sub_many};
            c.sub_fem = entry.sub_fem;
            std::string symbol;
            for (const char ch : entry.symbol) {
                if (std::string_view(R"(\-[]{}()*+?.,^$|# )").contains(ch)) {
                    symbol.push_back('\\');
                }
                symbol.push_back(ch);
            }
            const bool has_word = !entry.word_re.empty();
            const bool has_symbol = !symbol.empty();
            if (has_word && has_symbol) {
                c.patterns.emplace_back(std::string(amount) + R"(\s*()" + std::string(entry.word_re) +
                                            R"((?![а-яіїєґ])|)" + symbol + ")",
                                        std::regex::icase);
            } else if (has_word) {
                c.patterns.emplace_back(std::string(amount) + R"(\s*()" + std::string(entry.word_re) +
                                            R"((?![а-яіїєґ])))",
                                        std::regex::icase);
            } else if (has_symbol) {
                c.patterns.emplace_back(std::string(amount) + R"(\s*(?:)" + symbol + ")");
            }
            c.patterns.emplace_back(std::string(amount) + R"(\s*)" + std::string(entry.code) +
                                        R"((?![A-Za-zА-Яа-яЄєІіЇїҐґ]))",
                                    std::regex::icase);
            c.patterns.emplace_back(std::string(entry.code) + R"(\s*)" + std::string(amount) +
                                        R"((?![A-Za-zА-Яа-яЄєІіЇїҐґ]))",
                                    std::regex::icase);
            if (has_symbol) {
                c.patterns.emplace_back(symbol + R"(\s*)" + std::string(amount));
                if (entry.trailing_symbol) {
                    c.patterns.emplace_back(R"((\d+)\s*)" + symbol);
                }
            }
            out.push_back(std::move(c));
        }
        return out;
    }();
    auto amount_words = [](std::string amount_text, const Currency& c) {
        replace_all(amount_text, " ", "");
        std::string sign;
        if (!amount_text.empty() && (amount_text.front() == '+' || amount_text.front() == '-')) {
            sign = amount_text.front() == '-' ? "мінус " : "плюс ";
            amount_text.erase(amount_text.begin());
        }
        const auto comma = amount_text.rfind(',');
        const auto dot = amount_text.rfind('.');
        if (comma != std::string::npos && dot != std::string::npos) {
            const char decimal_separator = comma > dot ? ',' : '.';
            const char grouping_separator = decimal_separator == ',' ? '.' : ',';
            std::erase(amount_text, grouping_separator);
        } else {
            const char separator = comma != std::string::npos ? ',' : '.';
            const auto first = amount_text.find(separator);
            const auto last = amount_text.rfind(separator);
            if (first != std::string::npos && first != last) {
                const auto trailing = amount_text.size() - last - 1;
                std::string normalized;
                normalized.reserve(amount_text.size());
                for (std::size_t i = 0; i < amount_text.size(); ++i) {
                    if (amount_text[i] != separator || (i == last && trailing <= 2)) {
                        normalized.push_back(amount_text[i]);
                    }
                }
                amount_text = std::move(normalized);
            } else if (first != std::string::npos && first >= 1 && first <= 3 && amount_text.front() != '0' &&
                       amount_text.size() - first - 1 == 3) {
                amount_text.erase(first, 1);
            }
        }
        const auto pos = amount_text.find_first_of(".,");
        const auto main_text =
            pos == std::string::npos ? std::string_view(amount_text) : std::string_view(amount_text).substr(0, pos);
        const auto main = main_text.empty() ? 0 : parse_ull(main_text);
        unsigned sub = 0;
        if (pos != std::string::npos) {
            auto frac = amount_text.substr(pos + 1);
            if (frac.size() == 1) {
                frac.push_back('0');
            }
            if (frac.size() >= 2) {
                sub = static_cast<unsigned>(parse_ull(std::string_view(frac).substr(0, 2)));
            }
        }
        auto main_words = split_words(number_to_words(main));
        if (c.main_fem) {
            feminine_last(main_words);
        }
        std::string out = sign + join(main_words) + " " + plural(main, c.main);
        if (sub > 0) {
            auto sub_words = split_words(number_to_words(sub));
            if (c.sub_fem) {
                feminine_last(sub_words);
            }
            out += " " + join(sub_words) + " " + plural(sub, c.sub);
        }
        return out;
    };
    for (const auto& c : currencies) {
        for (const auto& re : c.patterns) {
            text = regex_sub(text, re, [&](const std::smatch& m) { return amount_words(m[1].str(), c); });
        }
    }
    return text;
}
std::string normalize_finance(std::string text)
{
    static const std::string ticker_alt = [] {
        std::vector<std::string> tickers;
        tickers.reserve(finance_units().size());
        for (const auto& [ticker, unused] : finance_units()) {
            (void)unused;
            tickers.push_back(ticker);
        }
        return regex_alternation(std::move(tickers));
    }();
    static const std::regex pair("\\b(" + ticker_alt + ")/(" + ticker_alt + ")\\b");
    text = regex_sub(text, pair, [](const std::smatch& m) {
        return finance_unit_many(m[1].str()) + " до " + finance_unit_many(m[2].str());
    });
    static const std::regex amount("(^|[^\\wА-Яа-яЄєІіЇїҐґ])([+-]?)(\\d+(?:[.,]\\d+)?)\\s*(" + ticker_alt + ")\\b");
    return regex_sub(text, amount, [](const std::smatch& m) {
        const auto& unit = finance_units().at(m[4].str());
        const auto sign = m[2].str() == "-" ? "мінус " : m[2].str() == "+" ? "плюс " : "";
        return m[1].str() + sign + finance_amount_words(m[3].str(), unit);
    });
}
std::string normalize_english(std::string text)
{
    static const std::regex word(R"(\b[A-Za-z][A-Za-z'’-]*\b)");
    static const std::regex acronym(R"(\b[A-Z]{2,6}\b)");
    static const std::unordered_map<char, std::string> names = {
        {'a', "ей"},  {'b', "бі"},     {'c', "сі"},   {'d', "ді"},  {'e', "і"},  {'f', "еф"}, {'g', "джі"},
        {'h', "ейч"}, {'i', "ай"},     {'j', "джей"}, {'k', "кей"}, {'l', "ел"}, {'m', "ем"}, {'n', "ен"},
        {'o', "оу"},  {'p', "пі"},     {'q', "к'ю"},  {'r', "ар"},  {'s', "ес"}, {'t', "ті"}, {'u', "ю"},
        {'v', "ві"},  {'w', "дабл ю"}, {'x', "екс"},  {'y', "вай"}, {'z', "зед"}};
    text = regex_sub(text, word, [](const std::smatch& m) {
        const auto low = lower_text(m.str());
        if (const auto it = english_words().find(low); it != english_words().end()) {
            return it->second;
        }
        return m.str();
    });
    return regex_sub(text, acronym, [&](const std::smatch& m) {
        const auto low = lower_text(m.str());
        if (english_words().contains(low)) {
            return m.str();
        }
        std::vector<std::string> parts;
        for (char ch : low) {
            parts.push_back(names.at(ch));
        }
        return join(parts);
    });
}

} // namespace detail

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
                out += abbreviation_map().at(compact_spaces_lower(std::string_view(text).substr(i, pos - i)));
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
