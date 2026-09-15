#include "uktextnorm/uktextnorm.hpp"

#include "internal.hpp"

#include "generated/uktextnorm_lexicons.hpp"

namespace uktextnorm::detail {

const std::unordered_map<std::string, CountedNoun>& counted_nouns()
{
    static const std::unordered_map<std::string, CountedNoun> map = [] {
        std::unordered_map<std::string, CountedNoun> out;
        for (const auto& entry : lexicon::kCountedNouns) {
            out.emplace(std::string(entry.key), CountedNoun{entry.one, entry.few, entry.many, entry.gender});
        }
        return out;
    }();
    return map;
}

const std::unordered_map<std::string, std::string_view>& counted_oblique_cases()
{
    static const std::unordered_map<std::string, std::string_view> map = {
        {"користувачами", "instr"}, {"користувачах", "prep"}, {"документами", "instr"}, {"документах", "prep"},
        {"файлами", "instr"},       {"файлах", "prep"},       {"товарами", "instr"},    {"товарах", "prep"},
        {"учасниками", "instr"},    {"учасниках", "prep"},    {"днями", "instr"},       {"днях", "prep"},
        {"тижнями", "instr"},       {"тижнях", "prep"},       {"місяцями", "instr"},    {"місяцях", "prep"},
        {"заявками", "instr"},      {"заявках", "prep"},      {"спробами", "instr"},    {"спробах", "prep"},
        {"людьми", "instr"},        {"людях", "prep"},        {"особами", "instr"},     {"особах", "prep"},
        {"дітьми", "instr"},        {"дітях", "prep"},        {"містами", "instr"},     {"містах", "prep"},
        {"селами", "instr"},        {"селах", "prep"},        {"питаннями", "instr"},   {"питаннях", "prep"}};
    return map;
}

std::string number_words_for_gender(unsigned long long n, char gender)
{
    auto words = split_words(number_to_words(n));
    if (gender == 'f') {
        feminine_last(words);
    } else if (gender == 'n') {
        neuter_last(words);
    }
    return join(words);
}

const std::string& counted_noun_alt()
{
    static const std::string alt = [] {
        std::vector<std::string> keys;
        for (const auto& entry : lexicon::kCountedNouns) {
            keys.emplace_back(entry.key);
        }
        return regex_alternation(keys);
    }();
    return alt;
}

bool prefers_many_after_genitive_number(unsigned long long n)
{
    const auto mod100 = n % 100;
    const auto mod10 = n % 10;
    return mod10 == 0 || mod10 >= 5 || (mod100 >= 11 && mod100 <= 14);
}

const std::unordered_map<std::string, std::string>& compound_prefix_forms()
{
    static const std::unordered_map<std::string, std::string> map = {{"один", "одно"},
                                                                     {"одна", "одно"},
                                                                     {"два", "дво"},
                                                                     {"дві", "дво"},
                                                                     {"три", "три"},
                                                                     {"чотири", "чотири"},
                                                                     {"п'ять", "п'яти"},
                                                                     {"шість", "шести"},
                                                                     {"сім", "семи"},
                                                                     {"вісім", "восьми"},
                                                                     {"дев'ять", "дев'яти"},
                                                                     {"десять", "десяти"},
                                                                     {"одинадцять", "одинадцяти"},
                                                                     {"дванадцять", "дванадцяти"},
                                                                     {"тринадцять", "тринадцяти"},
                                                                     {"чотирнадцять", "чотирнадцяти"},
                                                                     {"п'ятнадцять", "п'ятнадцяти"},
                                                                     {"шістнадцять", "шістнадцяти"},
                                                                     {"сімнадцять", "сімнадцяти"},
                                                                     {"вісімнадцять", "вісімнадцяти"},
                                                                     {"дев'ятнадцять", "дев'ятнадцяти"},
                                                                     {"двадцять", "двадцяти"},
                                                                     {"тридцять", "тридцяти"},
                                                                     {"сорок", "сорока"},
                                                                     {"п'ятдесят", "п'ятдесяти"},
                                                                     {"шістдесят", "шістдесяти"},
                                                                     {"сімдесят", "сімдесяти"},
                                                                     {"вісімдесят", "вісімдесяти"},
                                                                     {"дев'яносто", "дев'яносто"},
                                                                     {"сто", "сто"},
                                                                     {"двісті", "двохсот"},
                                                                     {"триста", "трьохсот"},
                                                                     {"чотириста", "чотирьохсот"},
                                                                     {"п'ятсот", "п'ятисот"},
                                                                     {"шістсот", "шестисот"},
                                                                     {"сімсот", "семисот"},
                                                                     {"вісімсот", "восьмисот"},
                                                                     {"дев'ятсот", "дев'ятисот"},
                                                                     {"тисяча", "тисячо"},
                                                                     {"тисячі", "тисячо"},
                                                                     {"тисяч", "тисячо"}};
    return map;
}
std::string hours_words(int hour)
{
    auto words = split_words(number_to_words(hour));
    feminine_last(words);
    return join(words) + " " + plural(hour, {"година", "години", "годин"});
}

std::string minutes_words(int minute, const Forms& forms)
{
    auto words = split_words(number_to_words(minute));
    feminine_last(words);
    return join(words) + " " + plural(minute, forms);
}
std::string say_fraction(unsigned long long num, unsigned long long den)
{
    auto words = split_words(number_to_words(num));
    feminine_last(words);
    const bool singular = num % 10 == 1 && num % 100 != 11;
    return join(words) + " " + number_to_ordinal_words(den, singular ? "nom_f" : "pl");
}
std::string read_measurement_quantity(std::string_view num, const Measurement& meas)
{
    std::string sign;
    if (!num.empty() && (num.front() == '+' || num.front() == '-')) {
        sign = num.front() == '-' ? "мінус " : "плюс ";
        num.remove_prefix(1);
    }
    const auto pos = num.find_first_of(".,");
    if (pos != std::string_view::npos) {
        const auto integer = num.substr(0, pos).empty() ? std::string_view("0") : num.substr(0, pos);
        return sign + decimal_to_words_or_digits(integer, num.substr(pos + 1)) + " " + std::string(meas.decimal);
    }
    const auto n = try_parse_ull(num);
    if (!n) {
        return sign + number_to_words_digit_by_digit(num) + " " + std::string(meas.many);
    }
    auto words = split_words(number_to_words(*n));
    if (meas.gender == 'f') {
        feminine_last(words);
    }
    return sign + join(words) + " " + plural(*n, {meas.one, meas.few, meas.many});
}
const std::unordered_map<std::string, FinanceUnit>& finance_units()
{
    static const std::unordered_map<std::string, FinanceUnit> map = [] {
        std::unordered_map<std::string, FinanceUnit> out;
        for (const auto& entry : lexicon::kFinanceUnits) {
            out.emplace(std::string(entry.code),
                        FinanceUnit{{entry.one, entry.few, entry.many}, entry.decimal, entry.feminine});
        }
        return out;
    }();
    return map;
}

std::string finance_unit_many(std::string_view ticker)
{
    const auto it = finance_units().find(std::string(ticker));
    return it == finance_units().end() ? std::string(ticker) : std::string(it->second.forms[2]);
}

std::string finance_amount_words(std::string amount, const FinanceUnit& unit)
{
    replace_all(amount, " ", "");
    const auto pos = amount.find_first_of(".,");
    if (pos != std::string::npos) {
        return decimal_to_words_or_digits(std::string_view(amount).substr(0, pos),
                                          std::string_view(amount).substr(pos + 1)) +
               " " + std::string(unit.decimal);
    }
    const auto n = parse_ull(amount);
    auto words = split_words(number_to_words(n));
    if (unit.feminine) {
        feminine_last(words);
    }
    return join(words) + " " + plural(n, unit.forms);
}
std::string normalize_phone_number(std::string_view phone, PhoneStyle style)
{
    const bool international_access = phone.starts_with("00");
    std::string digits;
    std::vector<std::string> groups;
    std::string current_group;
    for (char ch : phone) {
        if (ch >= '0' && ch <= '9') {
            digits.push_back(ch);
            current_group.push_back(ch);
        } else if (!current_group.empty()) {
            groups.push_back(current_group);
            current_group.clear();
        }
    }
    if (!current_group.empty()) {
        groups.push_back(current_group);
    }
    if (international_access) {
        digits.erase(0, std::min<std::size_t>(2, digits.size()));
        if (!groups.empty()) {
            groups.front().erase(0, std::min<std::size_t>(2, groups.front().size()));
            if (groups.front().empty()) {
                groups.erase(groups.begin());
            }
        }
    }
    if (digits.size() == 10 && digits[0] == '0') {
        digits = "38" + digits;
    }
    if (digits.size() != 12 || !digits.starts_with("380")) {
        if ((!phone.starts_with('+') && !international_access) || digits.size() < 7 || digits.size() > 15) {
            return std::string(phone);
        }
        std::vector<std::string> parts = {"плюс"};
        if (style == PhoneStyle::DigitByDigit || groups.size() < 2) {
            parts.push_back(number_to_words_digit_by_digit(digits));
            return join(parts);
        }
        for (const auto& group : groups) {
            if (group.size() > 3 || (group.size() > 1 && group[0] == '0')) {
                parts.push_back(number_to_words_digit_by_digit(group));
            } else {
                parts.push_back(number_to_words(parse_ull(group)));
            }
        }
        return join(parts);
    }
    if (style == PhoneStyle::DigitByDigit) {
        return "плюс " + number_to_words_digit_by_digit(digits);
    }
    std::vector<std::string> parts = {"плюс", "триста вісімдесят"};
    const std::array<std::string, 4> segs = {
        digits.substr(3, 2), digits.substr(5, 3), digits.substr(8, 2), digits.substr(10, 2)};
    for (const auto& seg : segs) {
        if (seg.size() > 1 && seg[0] == '0') {
            parts.push_back(number_to_words_digit_by_digit(seg));
        } else {
            parts.push_back(number_to_words(parse_ull(seg)));
        }
    }
    return join(parts);
}
std::string spell_identifier_letters(std::string_view letters)
{
    static const std::unordered_map<char, std::string_view> latin = {
        {'A', "ей"},  {'B', "бі"},     {'C', "сі"},   {'D', "ді"},  {'E', "і"},  {'F', "еф"}, {'G', "джі"},
        {'H', "ейч"}, {'I', "ай"},     {'J', "джей"}, {'K', "кей"}, {'L', "ел"}, {'M', "ем"}, {'N', "ен"},
        {'O', "оу"},  {'P', "пі"},     {'Q', "к'ю"},  {'R', "ар"},  {'S', "ес"}, {'T', "ті"}, {'U', "ю"},
        {'V', "ві"},  {'W', "дабл ю"}, {'X', "екс"},  {'Y', "вай"}, {'Z', "зед"}};
    std::vector<std::string> parts;
    for (std::size_t i = 0; i < letters.size();) {
        std::size_t next = i + 1;
        const auto cp = decode_one(letters, i, next);
        if (cp < 128) {
            const auto ch = static_cast<char>(cp >= 'a' && cp <= 'z' ? cp - 32 : cp);
            if (const auto it = latin.find(ch); it != latin.end()) {
                parts.emplace_back(it->second);
            }
        } else {
            std::string letter;
            append_utf8(letter, upper_cp(cp));
            if (const auto it = pronunciation_map().find(letter); it != pronunciation_map().end()) {
                parts.push_back(it->second);
            }
        }
        i = next;
    }
    return join(parts);
}

std::string read_identifier_number(std::string_view digits)
{
    if (digits.size() > 4 || (digits.size() > 1 && digits[0] == '0')) {
        return number_to_words_digit_by_digit(digits);
    }
    return number_to_words(parse_ull(digits));
}

std::optional<std::string> read_roman_identifier_segment(std::string_view letters)
{
    std::string roman;
    for (char ch : letters) {
        if (ch >= 'a' && ch <= 'z') {
            ch = static_cast<char>(ch - 32);
        }
        if (ch != 'I' && ch != 'V' && ch != 'X' && ch != 'L' && ch != 'C' && ch != 'D' && ch != 'M') {
            return std::nullopt;
        }
        roman.push_back(ch);
    }
    if (roman.empty() || !valid_roman(roman)) {
        return std::nullopt;
    }
    return number_to_ordinal_words(static_cast<unsigned long long>(roman_to_int(roman)), "nom_m");
}

std::string read_identifier_segment(std::string_view segment)
{
    std::vector<std::string> parts;
    std::string digits;
    std::string letters;
    auto flush_digits = [&] {
        if (!digits.empty()) {
            parts.push_back(read_identifier_number(digits));
            digits.clear();
        }
    };
    auto flush_letters = [&] {
        if (!letters.empty()) {
            if (auto roman = read_roman_identifier_segment(letters)) {
                parts.push_back(*roman);
            } else {
                parts.push_back(spell_identifier_letters(letters));
            }
            letters.clear();
        }
    };
    for (std::size_t i = 0; i < segment.size();) {
        const auto ch = segment[i];
        if (ch >= '0' && ch <= '9') {
            flush_letters();
            digits.push_back(ch);
            ++i;
            continue;
        }
        std::size_t next = i + 1;
        const auto cp = decode_one(segment, i, next);
        if ((cp >= U'A' && cp <= U'Z') || (cp >= U'a' && cp <= U'z') || is_uk(cp)) {
            flush_digits();
            letters.append(segment.substr(i, next - i));
        } else {
            flush_digits();
            flush_letters();
        }
        i = next;
    }
    flush_digits();
    flush_letters();
    return join(parts);
}

std::string read_structured_identifier(std::string_view value)
{
    std::vector<std::string> parts;
    std::string current;
    auto flush = [&] {
        if (current.empty()) {
            return;
        }
        parts.push_back(read_identifier_segment(current));
        current.clear();
    };
    for (std::size_t i = 0; i < value.size();) {
        const auto ch = value[i];
        if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) {
            current.push_back(ch);
            ++i;
            continue;
        }
        std::size_t next = i + 1;
        const auto cp = decode_one(value, i, next);
        if (is_uk(cp)) {
            current.append(value.substr(i, next - i));
            i = next;
            continue;
        }
        flush();
        if (cp == U'/') {
            parts.emplace_back("слеш");
        } else if (cp == U'-' || cp == U'‑' || cp == U'–' || cp == U'—') {
            parts.emplace_back("дефіс");
        }
        i = next;
    }
    flush();
    return join(parts);
}
std::string read_dotted(std::string_view num)
{
    static constexpr std::array<std::string_view, 10> digit_words = {
        "нуль", "один", "два", "три", "чотири", "п'ять", "шість", "сім", "вісім", "дев'ять"};
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= num.size()) {
        const auto pos = num.find('.', start);
        const auto p = num.substr(start, pos == std::string_view::npos ? std::string_view::npos : pos - start);
        if (p.size() > 1 && p[0] == '0') {
            std::string s;
            for (char ch : p) {
                if (!s.empty()) {
                    s += " ";
                }
                s += digit_words[ch - '0'];
            }
            parts.push_back(s);
        } else {
            parts.push_back(number_digits_or_words(p));
        }
        if (pos == std::string_view::npos) {
            break;
        }
        start = pos + 1;
    }
    return join(parts, " крапка ");
}
const std::unordered_map<std::string, std::string>& english_words()
{
    static const std::unordered_map<std::string, std::string> map = [] {
        std::unordered_map<std::string, std::string> out;
        for (const auto& entry : lexicon::kBrands) {
            out.emplace(std::string(entry.latin), std::string(entry.cyrillic));
        }
        for (const auto& entry : lexicon::kEnglishWords) {
            out.emplace(std::string(entry.latin), std::string(entry.cyrillic));
        }
        return out;
    }();
    return map;
}

} // namespace uktextnorm::detail
