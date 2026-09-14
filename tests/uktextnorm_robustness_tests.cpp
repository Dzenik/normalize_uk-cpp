#include "uktextnorm/uktextnorm.hpp"

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void fail(const std::string& name, const std::string& message)
{
    ++failures;
    std::cerr << name << ": " << message << "\n";
}

std::size_t utf8_char_count(std::string_view text)
{
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size();) {
        const auto ch = static_cast<unsigned char>(text[i]);
        if (ch < 0x80) {
            i += 1;
        } else if ((ch & 0xE0) == 0xC0 && i + 1 < text.size()) {
            i += 2;
        } else if ((ch & 0xF0) == 0xE0 && i + 2 < text.size()) {
            i += 3;
        } else if ((ch & 0xF8) == 0xF0 && i + 3 < text.size()) {
            i += 4;
        } else {
            i += 1;
        }
        ++count;
    }
    return count;
}

bool valid_utf8(std::string_view text)
{
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::size_t length = 0;
        char32_t value = 0;
        if (lead < 0x80) {
            length = 1;
            value = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            length = 2;
            value = lead & 0x1F;
        } else if ((lead & 0xF0) == 0xE0) {
            length = 3;
            value = lead & 0x0F;
        } else if ((lead & 0xF8) == 0xF0) {
            length = 4;
            value = lead & 0x07;
        } else {
            return false;
        }
        if (i + length > text.size()) {
            return false;
        }
        for (std::size_t j = 1; j < length; ++j) {
            const auto continuation = static_cast<unsigned char>(text[i + j]);
            if ((continuation & 0xC0) != 0x80) {
                return false;
            }
            value = (value << 6) | (continuation & 0x3F);
        }
        if ((length == 2 && value < 0x80) || (length == 3 && value < 0x800) || (length == 4 && value < 0x10000) ||
            value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
            return false;
        }
        i += length;
    }
    return true;
}

void exercise_one(const std::string& name, const std::string& text)
{
    const std::vector<uktextnorm::NormalizePreset> presets = {uktextnorm::NormalizePreset::Default,
                                                              uktextnorm::NormalizePreset::TtsFriendly,
                                                              uktextnorm::NormalizePreset::Conservative,
                                                              uktextnorm::NormalizePreset::SearchIndexing};

    for (const auto preset : presets) {
        try {
            const auto normalized = uktextnorm::normalize_ukrainian(text, preset);
            if (!text.empty() && normalized.empty()) {
                fail(name, "normalization returned empty output for non-empty input");
            }
            if (!valid_utf8(normalized)) {
                fail(name, "normalization returned invalid UTF-8");
            }
        } catch (const std::exception& ex) {
            fail(name, std::string("normalize_ukrainian threw: ") + ex.what());
        } catch (...) {
            fail(name, "normalize_ukrainian threw an unknown exception");
        }
    }

    try {
        const auto spans = uktextnorm::flag_uncertain(text);
        const auto text_chars = utf8_char_count(text);
        for (const auto& span : spans) {
            if (span.start > span.stop || span.stop > text_chars) {
                fail(name, "uncertainty span is outside source bounds");
            }
            if (span.stop - span.start != utf8_char_count(span.text)) {
                fail(name, "uncertainty span text size does not match character range");
            }
        }
    } catch (const std::exception& ex) {
        fail(name, std::string("flag_uncertain threw: ") + ex.what());
    } catch (...) {
        fail(name, "flag_uncertain threw an unknown exception");
    }
}

} // namespace

int main()
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"empty", ""},
        {"spaces", " \t  \n "},
        {"oversized number", "Номер 1234567890123456789012345678901234567890"},
        {"oversized dotted", "Версія 999999999999999999999999.999999999999999999999999.1"},
        {"malformed date", "Дата 99.99.9999 і 2026-99-99"},
        {"malformed url email", "Контакти https:// test@ @ _"},
        {"mixed scripts", "FooКиїв BarЛьвів АAАA"},
        {"dangling signs", "Ціна ₴ $ € £ № + - / : ;"},
        {"overprecise money", "Сума 1,234567890123456789 грн і $999999999999999999999999.99"},
        {"long phone-like", "Телефон 380671234567890123456789 і 0671234567890"},
        {"legal soup", "ч. ст. п. розд. № -- 910//1234///24"},
        {"roman soup", "IIII ст. VX розд. XIX-INVALID"},
        {"unicode punctuation", "«Тест» – — … ½ ⅞ 50/0"},
        {"combining apostrophes", "П'ять зв’язків мʼясо ІМ`Я"},
        {"latin products", "OpenAI ChatGPT GitHub Kubernetes TypeScript v999999999999999999999.1"},
        {"query string", "https://example.com/a?x=1&y=2&&&&"},
        {"compact finance", "BTC/UAH ETH/USD 000000000000000000000001 BTC"},
        {"measurement soup", "999999999999999999999999 кг 1,23456789 мг/мл -999999999999999999999999%"},
        {"range soup", "999999999999999999999999–1000000000000000000000000 °C, -5,5–+7,25 кг, 10:30–12:45, 1/0–3/4"},
    };

    for (const auto& [name, text] : cases) {
        exercise_one(name, text);
    }

    const std::vector<std::string> idempotent_cases = {"5 кг",
                                                       "2026-09",
                                                       "10.0.0.0/24",
                                                       "0.5 DOGE",
                                                       "<speak>5 кг</speak>",
                                                       "6.02×10²³",
                                                       "ст. 5–7",
                                                       "-1/2",
                                                       "−1/2",
                                                       "-2,5 м/с²",
                                                       "PT1.5H",
                                                       "[2001:db8::1]:443",
                                                       "[5 кг](https://example.com/a_(b)?x=1)",
                                                       "5‐7 °C",
                                                       "$1,234.56",
                                                       "3 N*m"};
    for (const auto& text : idempotent_cases) {
        const auto once = uktextnorm::normalize_ukrainian(text, uktextnorm::NormalizePreset::TtsFriendly);
        const auto twice = uktextnorm::normalize_ukrainian(once, uktextnorm::NormalizePreset::TtsFriendly);
        if (once != twice) {
            fail("idempotence", text + " normalized differently on the second pass");
        }
    }

    return failures == 0 ? 0 : 1;
}
