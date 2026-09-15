#include "rozpodil/rozpodil.hpp"
#include "uktextnorm/uktextnorm.hpp"

#include <pybind11/native_enum.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <string>
#include <string_view>
#include <vector>

namespace py = pybind11;

namespace {

struct Substring {
    std::size_t start = 0;
    std::size_t stop = 0;
    std::string text;

    friend bool operator==(const Substring&, const Substring&) = default;
};

std::string span_repr(std::string_view type_name, std::size_t start, std::size_t stop, const std::string& text)
{
    return "<" + std::string(type_name) + " start=" + std::to_string(start) + " stop=" + std::to_string(stop) +
           " text=" + py::repr(py::str(text)).cast<std::string>() + ">";
}

std::vector<std::size_t> byte_to_character_offsets(std::string_view text)
{
    std::vector<std::size_t> offsets(text.size() + 1);
    std::size_t characters = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xc0) != 0x80) {
            ++characters;
        }
        offsets[i + 1] = characters;
    }
    return offsets;
}

std::vector<Substring> copy_substrings(const std::vector<rozpodil::Substring>& chunks, std::string_view text)
{
    const auto offsets = byte_to_character_offsets(text);
    std::vector<Substring> out;
    out.reserve(chunks.size());
    for (const auto& chunk : chunks) {
        out.push_back({offsets.at(chunk.start), offsets.at(chunk.stop), std::string(chunk.text)});
    }
    return out;
}

std::vector<Substring> split_sentences(std::string_view text)
{
    return copy_substrings(rozpodil::split_sentences(text), text);
}

std::vector<Substring> legacy_sentenize(std::string_view text)
{
    return split_sentences(text);
}

std::vector<Substring> tokenize(std::string_view text)
{
    return copy_substrings(rozpodil::tokenize(text), text);
}

bool option_bool(py::handle value, std::string_view name)
{
    if (!py::isinstance<py::bool_>(value)) {
        throw py::type_error(std::string(name) + " must be a bool");
    }
    return py::cast<bool>(value);
}

template <typename Enum>
Enum option_enum(py::handle value, std::string_view name)
{
    if (!py::isinstance<Enum>(value)) {
        throw py::type_error(std::string(name) + " must be an enum value of the matching type");
    }
    return py::cast<Enum>(value);
}

void set_option(uktextnorm::NormalizeOptions& options, std::string_view name, py::handle value)
{
    if (name == "expand_known_acronyms")
        options.expand_known_acronyms = option_bool(value, name);
    else if (name == "spell_unknown_acronyms")
        options.spell_unknown_acronyms = option_bool(value, name);
    else if (name == "normalize_english_words")
        options.normalize_english_words = option_bool(value, name);
    else if (name == "transliterate_latin")
        options.transliterate_latin = option_bool(value, name);
    else if (name == "repair_homoglyphs")
        options.repair_homoglyphs = option_bool(value, name);
    else if (name == "validate_dates")
        options.validate_dates = option_bool(value, name);
    else if (name == "parse_thousand_separators")
        options.parse_thousand_separators = option_bool(value, name);
    else if (name == "normalize_network_addresses")
        options.normalize_network_addresses = option_bool(value, name);
    else if (name == "range_style")
        options.range_style = option_enum<uktextnorm::RangeStyle>(value, name);
    else if (name == "phone_style")
        options.phone_style = option_enum<uktextnorm::PhoneStyle>(value, name);
    else if (name == "symbol_style")
        options.symbol_style = option_enum<uktextnorm::SymbolStyle>(value, name);
    else if (name == "date_style")
        options.date_style = option_enum<uktextnorm::DateStyle>(value, name);
    else if (name == "colon_style")
        options.colon_style = option_enum<uktextnorm::ColonStyle>(value, name);
    else if (name == "numeric_date_order")
        options.numeric_date_order = option_enum<uktextnorm::NumericDateOrder>(value, name);
    else if (name == "currency_symbol_policy")
        options.currency_symbol_policy = option_enum<uktextnorm::CurrencySymbolPolicy>(value, name);
    else if (name == "quote_style")
        options.quote_style = option_enum<uktextnorm::QuoteStyle>(value, name);
    else
        throw py::type_error("unknown normalization option: " + std::string(name));
}

void bind_bool_option(py::class_<uktextnorm::NormalizeOptions>& cls,
                      const char* name,
                      bool uktextnorm::NormalizeOptions::* member)
{
    cls.def_property(
        name,
        [member](const uktextnorm::NormalizeOptions& options) { return options.*member; },
        [member, name](uktextnorm::NormalizeOptions& options, py::handle value) {
            options.*member = option_bool(value, name);
        });
}

} // namespace

PYBIND11_MODULE(_normalize_uk, m)
{
    m.doc() = "Python bindings for Ukrainian text normalization and tokenization utilities.";

    py::native_enum<uktextnorm::UncertaintyCategory>(m, "UncertaintyCategory", "enum.Enum")
        .value("AmbiguousAbbreviation", uktextnorm::UncertaintyCategory::AmbiguousAbbreviation)
        .value("BareNumber", uktextnorm::UncertaintyCategory::BareNumber)
        .value("Currency", uktextnorm::UncertaintyCategory::Currency)
        .value("Date", uktextnorm::UncertaintyCategory::Date)
        .value("Identifier", uktextnorm::UncertaintyCategory::Identifier)
        .value("ForeignWord", uktextnorm::UncertaintyCategory::ForeignWord)
        .value("MixedScript", uktextnorm::UncertaintyCategory::MixedScript)
        .value("RomanNumeral", uktextnorm::UncertaintyCategory::RomanNumeral)
        .value("Unit", uktextnorm::UncertaintyCategory::Unit)
        .value("Web", uktextnorm::UncertaintyCategory::Web)
        .value("InvalidDate", uktextnorm::UncertaintyCategory::InvalidDate)
        .value("AmbiguousNumberGrouping", uktextnorm::UncertaintyCategory::AmbiguousNumberGrouping)
        .value("Agreement", uktextnorm::UncertaintyCategory::Agreement)
        .value("Time", uktextnorm::UncertaintyCategory::Time)
        .value("Fraction", uktextnorm::UncertaintyCategory::Fraction)
        .value("Network", uktextnorm::UncertaintyCategory::Network)
        .value("Scientific", uktextnorm::UncertaintyCategory::Scientific)
        .value("Coordinate", uktextnorm::UncertaintyCategory::Coordinate)
        .finalize();

    py::native_enum<uktextnorm::UncertaintySeverity>(m, "UncertaintySeverity", "enum.Enum")
        .value("Info", uktextnorm::UncertaintySeverity::Info)
        .value("Warning", uktextnorm::UncertaintySeverity::Warning)
        .value("Error", uktextnorm::UncertaintySeverity::Error)
        .finalize();

    py::native_enum<uktextnorm::RangeStyle>(m, "RangeStyle", "enum.Enum")
        .value("Compact", uktextnorm::RangeStyle::Compact)
        .value("FromTo", uktextnorm::RangeStyle::FromTo)
        .finalize();

    py::native_enum<uktextnorm::PhoneStyle>(m, "PhoneStyle", "enum.Enum")
        .value("Grouped", uktextnorm::PhoneStyle::Grouped)
        .value("DigitByDigit", uktextnorm::PhoneStyle::DigitByDigit)
        .finalize();

    py::native_enum<uktextnorm::SymbolStyle>(m, "SymbolStyle", "enum.Enum")
        .value("Expand", uktextnorm::SymbolStyle::Expand)
        .value("Preserve", uktextnorm::SymbolStyle::Preserve)
        .finalize();

    py::native_enum<uktextnorm::DateStyle>(m, "DateStyle", "enum.Enum")
        .value("Formal", uktextnorm::DateStyle::Formal)
        .value("Spoken", uktextnorm::DateStyle::Spoken)
        .finalize();

    py::native_enum<uktextnorm::ColonStyle>(m, "ColonStyle", "enum.Enum")
        .value("Contextual", uktextnorm::ColonStyle::Contextual)
        .value("Clock", uktextnorm::ColonStyle::Clock)
        .value("Ratio", uktextnorm::ColonStyle::Ratio)
        .finalize();

    py::native_enum<uktextnorm::NumericDateOrder>(m, "NumericDateOrder", "enum.Enum")
        .value("DayMonthYear", uktextnorm::NumericDateOrder::DayMonthYear)
        .value("MonthDayYear", uktextnorm::NumericDateOrder::MonthDayYear)
        .value("PreserveAmbiguous", uktextnorm::NumericDateOrder::PreserveAmbiguous)
        .finalize();

    py::native_enum<uktextnorm::CurrencySymbolPolicy>(m, "CurrencySymbolPolicy", "enum.Enum")
        .value("AssumeCommon", uktextnorm::CurrencySymbolPolicy::AssumeCommon)
        .value("PreserveAmbiguous", uktextnorm::CurrencySymbolPolicy::PreserveAmbiguous)
        .finalize();

    py::native_enum<uktextnorm::QuoteStyle>(m, "QuoteStyle", "enum.Enum")
        .value("Keep", uktextnorm::QuoteStyle::Keep)
        .value("Guillemets", uktextnorm::QuoteStyle::Guillemets)
        .value("Straight", uktextnorm::QuoteStyle::Straight)
        .value("Strip", uktextnorm::QuoteStyle::Strip)
        .finalize();

    py::native_enum<uktextnorm::NormalizePreset>(m, "NormalizePreset", "enum.Enum")
        .value("Default", uktextnorm::NormalizePreset::Default)
        .value("TtsFriendly", uktextnorm::NormalizePreset::TtsFriendly)
        .value("Conservative", uktextnorm::NormalizePreset::Conservative)
        .value("SearchIndexing", uktextnorm::NormalizePreset::SearchIndexing)
        .finalize();

    py::class_<uktextnorm::UncertainSpan>(m, "UncertainSpan")
        .def_readonly("start", &uktextnorm::UncertainSpan::start)
        .def_readonly("stop", &uktextnorm::UncertainSpan::stop)
        .def_readonly("text", &uktextnorm::UncertainSpan::text)
        .def_readonly("reason", &uktextnorm::UncertainSpan::reason)
        .def_readonly("category", &uktextnorm::UncertainSpan::category)
        .def_readonly("severity", &uktextnorm::UncertainSpan::severity)
        .def(
            "__eq__",
            [](const uktextnorm::UncertainSpan& left, const uktextnorm::UncertainSpan& right) { return left == right; },
            py::is_operator())
        .def("__repr__", [](const uktextnorm::UncertainSpan& span) {
            return span_repr("UncertainSpan", span.start, span.stop, span.text);
        });

    auto options_class = py::class_<uktextnorm::NormalizeOptions>(m, "NormalizeOptions");
    options_class
        .def(py::init([](uktextnorm::NormalizePreset preset, py::kwargs overrides) {
                 auto options = uktextnorm::options_for_preset(preset);
                 for (auto item : overrides) {
                     set_option(options, py::cast<std::string>(item.first), item.second);
                 }
                 return options;
             }),
             py::arg("preset") = uktextnorm::NormalizePreset::Default)
        .def_readwrite("range_style", &uktextnorm::NormalizeOptions::range_style)
        .def_readwrite("phone_style", &uktextnorm::NormalizeOptions::phone_style)
        .def_readwrite("symbol_style", &uktextnorm::NormalizeOptions::symbol_style)
        .def_readwrite("date_style", &uktextnorm::NormalizeOptions::date_style)
        .def_readwrite("colon_style", &uktextnorm::NormalizeOptions::colon_style)
        .def_readwrite("numeric_date_order", &uktextnorm::NormalizeOptions::numeric_date_order)
        .def_readwrite("currency_symbol_policy", &uktextnorm::NormalizeOptions::currency_symbol_policy)
        .def_readwrite("quote_style", &uktextnorm::NormalizeOptions::quote_style);
    bind_bool_option(options_class, "expand_known_acronyms", &uktextnorm::NormalizeOptions::expand_known_acronyms);
    bind_bool_option(options_class, "spell_unknown_acronyms", &uktextnorm::NormalizeOptions::spell_unknown_acronyms);
    bind_bool_option(options_class, "normalize_english_words", &uktextnorm::NormalizeOptions::normalize_english_words);
    bind_bool_option(options_class, "transliterate_latin", &uktextnorm::NormalizeOptions::transliterate_latin);
    bind_bool_option(options_class, "repair_homoglyphs", &uktextnorm::NormalizeOptions::repair_homoglyphs);
    bind_bool_option(options_class, "validate_dates", &uktextnorm::NormalizeOptions::validate_dates);
    bind_bool_option(
        options_class, "parse_thousand_separators", &uktextnorm::NormalizeOptions::parse_thousand_separators);
    bind_bool_option(
        options_class, "normalize_network_addresses", &uktextnorm::NormalizeOptions::normalize_network_addresses);

    py::class_<Substring>(m, "Substring")
        .def_readonly("start", &Substring::start)
        .def_readonly("stop", &Substring::stop)
        .def_readonly("text", &Substring::text)
        .def(
            "__eq__", [](const Substring& left, const Substring& right) { return left == right; }, py::is_operator())
        .def("__repr__",
             [](const Substring& span) { return span_repr("Substring", span.start, span.stop, span.text); });

    m.def("options_for_preset", &uktextnorm::options_for_preset, py::arg("preset"));
    m.def("number_to_words", &uktextnorm::number_to_words, py::arg("n"));
    m.def("number_to_words_digit_by_digit", &uktextnorm::number_to_words_digit_by_digit, py::arg("digits"));
    m.def("number_to_ordinal_words", &uktextnorm::number_to_ordinal_words, py::arg("n"), py::arg("form") = "nom_m");
    m.def("number_to_words_case", &uktextnorm::number_to_words_case, py::arg("n"), py::arg("grammatical_case"));
    m.def("normalize_abbreviations",
          &uktextnorm::normalize_abbreviations,
          py::arg("text"),
          py::call_guard<py::gil_scoped_release>());
    m.def("expand_abbreviations",
          &uktextnorm::expand_abbreviations,
          py::arg("text"),
          py::call_guard<py::gil_scoped_release>());
    m.def("transliterate_to_cyrillic",
          &uktextnorm::transliterate_to_cyrillic,
          py::arg("text"),
          py::call_guard<py::gil_scoped_release>());
    m.def("cyrilize", &uktextnorm::cyrilize, py::arg("text"), py::call_guard<py::gil_scoped_release>());
    m.def("cyrrilize", &uktextnorm::cyrrilize, py::arg("text"), py::call_guard<py::gil_scoped_release>());
    m.def(
        "normalize_ukrainian",
        [](std::string_view text) { return uktextnorm::normalize_ukrainian(text); },
        py::arg("text"),
        py::call_guard<py::gil_scoped_release>());
    m.def(
        "normalize_ukrainian",
        [](std::string_view text, const uktextnorm::NormalizeOptions& options) {
            const auto snapshot = options;
            py::gil_scoped_release release;
            return uktextnorm::normalize_ukrainian(text, snapshot);
        },
        py::arg("text"),
        py::arg("options"));
    m.def(
        "normalize_ukrainian",
        [](std::string_view text, uktextnorm::NormalizePreset preset) {
            return uktextnorm::normalize_ukrainian(text, preset);
        },
        py::arg("text"),
        py::arg("preset"),
        py::call_guard<py::gil_scoped_release>());
    m.def(
        "normalize_ukrainian_with_preset",
        [](std::string_view text, uktextnorm::NormalizePreset preset) {
            return uktextnorm::normalize_ukrainian_with_preset(text, preset);
        },
        py::arg("text"),
        py::arg("preset") = uktextnorm::NormalizePreset::Default,
        py::call_guard<py::gil_scoped_release>());
    m.def(
        "flag_uncertain",
        [](std::string_view text) { return uktextnorm::flag_uncertain(text); },
        py::arg("text"),
        py::call_guard<py::gil_scoped_release>());
    m.def(
        "flag_uncertain",
        [](std::string_view text, const uktextnorm::NormalizeOptions& options) {
            const auto snapshot = options;
            py::gil_scoped_release release;
            return uktextnorm::flag_uncertain(text, snapshot);
        },
        py::arg("text"),
        py::arg("options"));
    m.def(
        "flag_uncertain",
        [](std::string_view text, uktextnorm::NormalizePreset preset) {
            const auto options = uktextnorm::options_for_preset(preset);
            return uktextnorm::flag_uncertain(text, options);
        },
        py::arg("text"),
        py::arg("preset"),
        py::call_guard<py::gil_scoped_release>());
    m.def("split_sentences", &split_sentences, py::arg("text"), py::call_guard<py::gil_scoped_release>());
    m.def("sentenize", &legacy_sentenize, py::arg("text"), py::call_guard<py::gil_scoped_release>());
    m.def("tokenize", &tokenize, py::arg("text"), py::call_guard<py::gil_scoped_release>());
}
