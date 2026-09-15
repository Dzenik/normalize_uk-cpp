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
        unsigned minor_digits = 2;
        std::vector<std::regex> patterns;
    };
    static constexpr std::string_view amount_token =
        R"([+-]?(?:[1-9]\d{0,2}(?:,\d{3})+(?:\.\d{1,4})?|[1-9]\d{0,2}(?:\.\d{3})+(?:,\d{1,4})?|\d+[.,]\d{1,4}|\d+|[.,]\d{1,4})(?!\d|[.,]\d))";
    static const std::string amount = "(" + std::string(amount_token) + ")";
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
    static const std::regex accounting_prefix("\\((" + currency_token_alt() + ")\\s*(\\d+(?:[.,]\\d{1,4})?)\\)",
                                              std::regex::icase);
    text = regex_sub(text, accounting_prefix, [](const std::smatch& m) { return "-" + m[2].str() + " " + m[1].str(); });
    static const std::regex accounting("\\((\\d+(?:[.,]\\d{1,4})?)\\s*(" + currency_token_alt() + ")\\)",
                                       std::regex::icase);
    text = regex_sub(text, accounting, [](const std::smatch& m) { return "-" + m[1].str() + " " + m[2].str(); });
    static const std::vector<Currency> currencies = [] {
        std::vector<Currency> out;
        for (const auto& entry : lexicon::kCurrencies) {
            Currency c;
            c.main = {entry.main_one, entry.main_few, entry.main_many};
            c.main_fem = entry.main_fem;
            c.sub = {entry.sub_one, entry.sub_few, entry.sub_many};
            c.sub_fem = entry.sub_fem;
            c.minor_digits = entry.minor_digits;
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
                c.patterns.emplace_back(amount + R"(\s*()" + std::string(entry.word_re) + R"((?![а-яіїєґ])|)" + symbol +
                                            ")",
                                        std::regex::icase);
            } else if (has_word) {
                c.patterns.emplace_back(amount + R"(\s*()" + std::string(entry.word_re) + R"((?![а-яіїєґ])))",
                                        std::regex::icase);
            } else if (has_symbol) {
                c.patterns.emplace_back(amount + R"(\s*(?:)" + symbol + ")");
            }
            if (has_symbol) {
                c.patterns.emplace_back(symbol + R"(\s*)" + amount);
                if (entry.trailing_symbol) {
                    c.patterns.emplace_back(R"((\d+)\s*)" + symbol);
                }
            }
            out.push_back(std::move(c));
        }
        return out;
    }();
    static const std::unordered_map<std::string, std::size_t> currency_by_code = [] {
        std::unordered_map<std::string, std::size_t> out;
        for (std::size_t i = 0; i < lexicon::kCurrencies.size(); ++i) {
            out.emplace(lower_text(lexicon::kCurrencies[i].code), i);
        }
        return out;
    }();
    static const std::string currency_code_alt = [] {
        std::vector<std::string> codes;
        codes.reserve(lexicon::kCurrencies.size());
        for (const auto& entry : lexicon::kCurrencies) {
            codes.emplace_back(entry.code);
        }
        return regex_alternation(std::move(codes));
    }();
    auto amount_words = [](std::string amount_text, const Currency& c) -> std::optional<std::string> {
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
                if (trailing > c.minor_digits && trailing != 3) {
                    return std::nullopt;
                }
                std::string normalized;
                normalized.reserve(amount_text.size());
                for (std::size_t i = 0; i < amount_text.size(); ++i) {
                    if (amount_text[i] != separator || (i == last && trailing == c.minor_digits)) {
                        normalized.push_back(amount_text[i]);
                    }
                }
                amount_text = std::move(normalized);
            } else if (first != std::string::npos && first >= 1 && first <= 3 && amount_text.front() != '0' &&
                       amount_text.size() - first - 1 == 3 && c.minor_digits != 3) {
                amount_text.erase(first, 1);
            }
        }
        const auto pos = amount_text.find_first_of(".,");
        const auto main_text =
            pos == std::string::npos ? std::string_view(amount_text) : std::string_view(amount_text).substr(0, pos);
        const auto main = main_text.empty() ? 0 : parse_ull(main_text);
        if (pos != std::string::npos && c.minor_digits == 0) {
            const auto words = decimal_to_words(main_text.empty() ? std::string_view("0") : main_text,
                                                std::string_view(amount_text).substr(pos + 1));
            return words.empty() ? std::nullopt
                                 : std::optional<std::string>{sign + words + " " + std::string(c.main[2])};
        }
        unsigned long long sub = 0;
        if (pos != std::string::npos) {
            auto frac = amount_text.substr(pos + 1);
            if (frac.size() > c.minor_digits) {
                return std::nullopt;
            }
            while (frac.size() < c.minor_digits) {
                frac.push_back('0');
            }
            sub = parse_ull(frac);
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
        return std::optional<std::string>{out};
    };
    auto starts_inside_number = [](const std::smatch& match) {
        const auto prefix = match.prefix().str();
        return !prefix.empty() && (std::isdigit(static_cast<unsigned char>(prefix.back())) || prefix.back() == '.' ||
                                   prefix.back() == ',');
    };
    for (const auto& c : currencies) {
        for (const auto& re : c.patterns) {
            text = regex_sub(text, re, [&](const std::smatch& m) {
                if (starts_inside_number(m)) {
                    return m.str();
                }
                const auto words = amount_words(m[1].str(), c);
                return words ? *words : m.str();
            });
        }
    }
    static const std::regex suffix_code(amount + "\\s*(" + currency_code_alt + R"()(?![A-Za-zА-Яа-яЄєІіЇїҐґ]))",
                                        std::regex::icase);
    text = regex_sub(text, suffix_code, [&](const std::smatch& m) {
        if (starts_inside_number(m)) {
            return m.str();
        }
        const auto& c = currencies[currency_by_code.at(lower_text(m[2].str()))];
        const auto words = amount_words(m[1].str(), c);
        return words ? *words : m.str();
    });
    static const std::regex prefix_code("(" + currency_code_alt + ")\\s*" + amount + R"((?![A-Za-zА-Яа-яЄєІіЇїҐґ]))",
                                        std::regex::icase);
    text = regex_sub(text, prefix_code, [&](const std::smatch& m) {
        const auto& c = currencies[currency_by_code.at(lower_text(m[1].str()))];
        const auto words = amount_words(m[2].str(), c);
        return words ? *words : m.str();
    });
    return text;
}
std::string normalize_finance(std::string text, bool include_generic)
{
    auto canonical_code = [](std::string_view code) {
        std::string out(code);
        std::ranges::transform(out, out.begin(), [](unsigned char ch) {
            return ch >= 'a' && ch <= 'z' ? static_cast<char>(ch - 'a' + 'A') : static_cast<char>(ch);
        });
        return out;
    };
    auto currency_many = [](std::string_view code) -> std::optional<std::string> {
        for (const auto& entry : lexicon::kCurrencies) {
            if (code == entry.code) {
                return std::string(entry.main_many);
            }
        }
        return std::nullopt;
    };
    auto ticker_words = [&](std::string_view ticker) {
        const auto code = canonical_code(ticker);
        if (const auto it = finance_units().find(code); it != finance_units().end()) {
            return std::string(it->second.forms[2]);
        }
        if (const auto currency = currency_many(code)) {
            return *currency;
        }
        std::vector<std::string> parts;
        for (const char ch : code) {
            if (ch >= '0' && ch <= '9') {
                parts.push_back(number_to_words(static_cast<unsigned long long>(ch - '0')));
            } else {
                parts.push_back(spell_identifier_letters(std::string_view(&ch, 1)));
            }
        }
        return join(parts);
    };
    auto canonical_amount = [](std::string amount) -> std::optional<std::string> {
        replace_all(amount, " ", "");
        replace_all(amount, " ", "");
        const auto comma = amount.rfind(',');
        const auto dot = amount.rfind('.');
        if (comma != std::string::npos && dot != std::string::npos) {
            const char decimal_separator = comma > dot ? ',' : '.';
            const char grouping_separator = decimal_separator == ',' ? '.' : ',';
            std::erase(amount, grouping_separator);
            if (decimal_separator == ',') {
                amount[amount.rfind(',')] = '.';
            }
        } else if (comma != std::string::npos || dot != std::string::npos) {
            const char separator = comma != std::string::npos ? ',' : '.';
            const auto first = amount.find(separator);
            const auto last = amount.rfind(separator);
            bool grouped = first != last;
            if (grouped) {
                std::size_t group_start = first + 1;
                while (group_start < amount.size()) {
                    const auto group_end = amount.find(separator, group_start);
                    const auto length = (group_end == std::string::npos ? amount.size() : group_end) - group_start;
                    if (length != 3) {
                        grouped = false;
                        break;
                    }
                    if (group_end == std::string::npos) {
                        break;
                    }
                    group_start = group_end + 1;
                }
            } else {
                const auto integer = amount.substr(0, first);
                grouped = amount.size() - first - 1 == 3 && integer != "0";
            }
            if (grouped) {
                std::erase(amount, separator);
            } else if (first != last) {
                return std::nullopt;
            } else if (separator == ',') {
                amount[first] = '.';
            }
        }
        const auto decimal = amount.find('.');
        if (!try_parse_ull(amount.substr(0, decimal))) {
            return std::nullopt;
        }
        return amount;
    };
    static const std::string ticker_alt = [] {
        std::vector<std::string> tickers;
        tickers.reserve(finance_units().size());
        for (const auto& [ticker, unused] : finance_units()) {
            (void)unused;
            tickers.push_back(ticker);
        }
        return regex_alternation(std::move(tickers));
    }();
    static const std::string recognized_ticker_alt = [] {
        std::vector<std::string> tickers;
        tickers.reserve(finance_units().size() + lexicon::kCurrencies.size());
        for (const auto& [ticker, unused] : finance_units()) {
            (void)unused;
            tickers.push_back(ticker);
        }
        for (const auto& entry : lexicon::kCurrencies) {
            tickers.emplace_back(entry.code);
        }
        return regex_alternation(std::move(tickers));
    }();
    static const std::string generic_ticker = R"((?![0-9]{2,10}\b)[A-Z0-9]{2,10})";
    static const std::string raw_amount = R"((?:\d{1,3}(?:[ ,. ]\d{3})+(?:[.,]\d+)?|\d+(?:[.,]\d+)?))";

    static const std::regex bitcoin_prefix("₿\\s*([+-]?)(" + raw_amount + ")");
    text = regex_sub(text, bitcoin_prefix, [](const std::smatch& m) { return m[1].str() + m[2].str() + " BTC"; });
    static const std::regex bitcoin_suffix("([+-]?)(" + raw_amount + ")\\s*₿");
    text = regex_sub(text, bitcoin_suffix, [](const std::smatch& m) { return m[1].str() + m[2].str() + " BTC"; });

    static const std::regex known_pair("\\b(" + recognized_ticker_alt + ")/(" + recognized_ticker_alt + ")\\b",
                                       std::regex::icase);
    text = regex_sub(text, known_pair, [&](const std::smatch& m) {
        return ticker_words(m[1].str()) + " до " + ticker_words(m[2].str());
    });
    auto render_amount = [&](std::string_view raw, std::string_view ticker, std::string_view sign) {
        const auto code = canonical_code(ticker);
        if (currency_many(code)) {
            return std::optional<std::string>{};
        }
        const auto amount = canonical_amount(std::string(raw));
        if (!amount) {
            return std::optional<std::string>{};
        }
        const auto spoken_sign = sign == "-" ? "мінус " : sign == "+" ? "плюс " : "";
        if (const auto it = finance_units().find(code); it != finance_units().end()) {
            return std::optional<std::string>{spoken_sign + finance_amount_words(*amount, it->second)};
        }
        const auto spoken_ticker = ticker_words(code);
        const FinanceUnit fallback{{spoken_ticker, spoken_ticker, spoken_ticker}, spoken_ticker, false};
        return std::optional<std::string>{spoken_sign + finance_amount_words(*amount, fallback)};
    };
    auto normalize_amounts = [&](const std::string& ticker_pattern,
                                 std::regex_constants::syntax_option_type flags,
                                 const std::string& prefix_pattern) {
        const auto boundary = std::string(R"([^\wА-Яа-яЄєІіЇїҐґ])");
        const std::regex suffix("(^|" + boundary + ")([+-]?)((?:" + raw_amount + "))\\s*(" + ticker_pattern + ")\\b",
                                flags);
        text = regex_sub(text, suffix, [&](const std::smatch& m) {
            const auto words = render_amount(m[3].str(), m[4].str(), m[2].str());
            return words ? m[1].str() + *words : m.str();
        });
        const std::regex prefix("\\b(" + prefix_pattern + ")\\s*([+-]?)((?:" + raw_amount + "))(?![\\w.,])", flags);
        text = regex_sub(text, prefix, [&](const std::smatch& m) {
            const auto words = render_amount(m[3].str(), m[1].str(), m[2].str());
            return words ? *words : m.str();
        });
    };
    normalize_amounts(ticker_alt, std::regex::icase, ticker_alt);
    if (!include_generic) {
        return text;
    }
    static const std::regex pair("\\b(" + generic_ticker + ")/(" + generic_ticker + ")\\b");
    text = regex_sub(text, pair, [&](const std::smatch& m) {
        const auto is_recognized_ticker = [&](std::string_view ticker) {
            const auto code = canonical_code(ticker);
            return finance_units().contains(code) || currency_many(code).has_value();
        };
        // An arbitrary A/B token is more often a protocol or standard (TCP/IP,
        // ISO/IEC) than a market pair.  Use "slash" unless one side anchors the
        // expression in the finance lexicon.
        if (!is_recognized_ticker(m[1].str()) && !is_recognized_ticker(m[2].str())) {
            return ticker_words(m[1].str()) + " слеш " + ticker_words(m[2].str());
        }
        return ticker_words(m[1].str()) + " до " + ticker_words(m[2].str());
    });
    // Unknown suffix tickers ("5 NEWCOIN") are unambiguous enough to spell.
    // Unknown prefix tokens are deliberately left alone: "ISO 3166",
    // "IEEE 802.3", and "ALGOL 58" have the same surface form as "XYZ 5".
    normalize_amounts(generic_ticker, std::regex::ECMAScript, "(?!)");
    return text;
}
std::string normalize_english(std::string text)
{
    static const std::regex word(R"(\b[A-Za-z][A-Za-z'’-]*\b)");
    static const std::regex acronym(R"(\b[A-Z]+\b)");
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
                out += abbreviation_map().at(compact_spaces_lower(std::string_view(text).substr(i, pos - i)));
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
