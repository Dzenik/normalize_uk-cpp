#include "rozpodil/rozpodil.hpp"
#include "uktextnorm/uktextnorm.hpp"

#include <pybind11/native_enum.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <array>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
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

std::vector<Substring> copy_substrings(const std::vector<rozpodil::Substring>& chunks, std::string_view text)
{
    std::size_t scanned_bytes = 0;
    std::size_t characters = 0;
    auto character_offset = [&](std::size_t target) {
        if (target < scanned_bytes || target > text.size()) {
            throw std::out_of_range("invalid substring offset");
        }
        while (scanned_bytes < target) {
            if ((static_cast<unsigned char>(text[scanned_bytes]) & 0xc0) != 0x80) {
                ++characters;
            }
            ++scanned_bytes;
        }
        return characters;
    };
    std::vector<Substring> out;
    out.reserve(chunks.size());
    for (const auto& chunk : chunks) {
        const auto start = character_offset(chunk.start);
        const auto stop = character_offset(chunk.stop);
        out.push_back({start, stop, std::string(chunk.text)});
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

template <typename Fn>
auto with_text(py::str value, Fn fn)
{
    const auto text = py::cast<std::string_view>(value);
    py::gil_scoped_release release;
    return fn(text);
}

std::vector<std::string> normalize_many(py::iterable texts, const uktextnorm::NormalizeOptions& options)
{
    // Convert and retain every Python string before releasing the GIL. The views
    // point into immutable UTF-8 storage owned by these Python objects.
    std::vector<py::str> owners;
    std::vector<std::string_view> views;
    for (py::handle item : texts) {
        if (!py::isinstance<py::str>(item)) {
            throw py::type_error("texts must contain str values only");
        }
        owners.push_back(py::reinterpret_borrow<py::str>(item));
        views.push_back(py::cast<std::string_view>(owners.back()));
    }

    std::vector<std::string> results;
    results.reserve(views.size());
    std::unordered_map<std::string_view, std::size_t> seen;
    {
        py::gil_scoped_release release;
        for (std::string_view view : views) {
            if (const auto found = seen.find(view); found != seen.end()) {
                results.push_back(results[found->second]);
            } else {
                results.push_back(uktextnorm::normalize_ukrainian(view, options));
                seen.emplace(view, results.size() - 1);
            }
        }
    }
    return results;
}

template <typename Enum>
Enum option_enum(py::handle value, std::string_view name)
{
    if (!py::isinstance<Enum>(value)) {
        const auto enum_type = py::type::of(py::cast(Enum{}));
        const auto enum_name = py::cast<std::string>(enum_type.attr("__name__"));
        throw py::type_error(std::string(name) + " must be a " + enum_name + " value");
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

template <typename Enum>
void bind_enum_option(py::class_<uktextnorm::NormalizeOptions>& cls,
                      const char* name,
                      Enum uktextnorm::NormalizeOptions::* member)
{
    cls.def_property(
        name,
        [member](const uktextnorm::NormalizeOptions& options) { return options.*member; },
        [member, name](uktextnorm::NormalizeOptions& options, py::handle value) {
            options.*member = option_enum<Enum>(value, name);
        });
}

template <typename T>
void bind_copy(py::class_<T>& cls)
{
    cls.def("__copy__", [](const T& self) { return T(self); });
    cls.def("__deepcopy__", [](const T& self, py::dict) { return T(self); }, py::arg("memo"));
}

constexpr std::array<std::string_view, 16> option_names = {
    "expand_known_acronyms", "spell_unknown_acronyms", "normalize_english_words", "transliterate_latin",
    "repair_homoglyphs", "validate_dates", "parse_thousand_separators", "normalize_network_addresses",
    "range_style", "phone_style", "symbol_style", "date_style", "colon_style", "numeric_date_order",
    "currency_symbol_policy", "quote_style"};

py::tuple options_state(const uktextnorm::NormalizeOptions& options)
{
    return py::make_tuple(options.expand_known_acronyms, options.spell_unknown_acronyms,
                          options.normalize_english_words, options.transliterate_latin, options.repair_homoglyphs,
                          options.validate_dates, options.parse_thousand_separators, options.normalize_network_addresses,
                          options.range_style, options.phone_style, options.symbol_style, options.date_style,
                          options.colon_style, options.numeric_date_order, options.currency_symbol_policy,
                          options.quote_style);
}

uktextnorm::NormalizeOptions options_from_state(py::tuple state)
{
    if (state.size() != option_names.size()) {
        throw py::value_error("invalid NormalizeOptions pickle state");
    }
    uktextnorm::NormalizeOptions options;
    for (std::size_t index = 0; index < option_names.size(); ++index) {
        set_option(options, option_names[index], state[index]);
    }
    return options;
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

    auto uncertain_span_class = py::class_<uktextnorm::UncertainSpan>(m, "UncertainSpan");
    uncertain_span_class
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
        })
        .def(py::pickle(
            [](const uktextnorm::UncertainSpan& span) {
                return py::make_tuple(span.start, span.stop, span.text, span.reason, span.category, span.severity);
            },
            [](py::tuple state) {
                if (state.size() != 6) {
                    throw py::value_error("invalid UncertainSpan pickle state");
                }
                return uktextnorm::UncertainSpan{state[0].cast<std::size_t>(), state[1].cast<std::size_t>(),
                                                 state[2].cast<std::string>(), state[3].cast<std::string>(),
                                                 state[4].cast<uktextnorm::UncertaintyCategory>(),
                                                 state[5].cast<uktextnorm::UncertaintySeverity>()};
            }));
    bind_copy(uncertain_span_class);

    auto options_class = py::class_<uktextnorm::NormalizeOptions>(m, "NormalizeOptions");
    options_class.def(py::init([](uktextnorm::NormalizePreset preset, py::kwargs overrides) {
                          auto options = uktextnorm::options_for_preset(preset);
                          for (auto item : overrides) {
                              set_option(options, py::cast<std::string>(item.first), item.second);
                          }
                          return options;
                      }),
                      py::arg("preset") = uktextnorm::NormalizePreset::Default);
    bind_enum_option(options_class, "range_style", &uktextnorm::NormalizeOptions::range_style);
    bind_enum_option(options_class, "phone_style", &uktextnorm::NormalizeOptions::phone_style);
    bind_enum_option(options_class, "symbol_style", &uktextnorm::NormalizeOptions::symbol_style);
    bind_enum_option(options_class, "date_style", &uktextnorm::NormalizeOptions::date_style);
    bind_enum_option(options_class, "colon_style", &uktextnorm::NormalizeOptions::colon_style);
    bind_enum_option(options_class, "numeric_date_order", &uktextnorm::NormalizeOptions::numeric_date_order);
    bind_enum_option(options_class, "currency_symbol_policy", &uktextnorm::NormalizeOptions::currency_symbol_policy);
    bind_enum_option(options_class, "quote_style", &uktextnorm::NormalizeOptions::quote_style);
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
    options_class.def(py::pickle(
        [](const uktextnorm::NormalizeOptions& options) { return options_state(options); },
        [](py::tuple state) { return options_from_state(state); }));
    bind_copy(options_class);

    auto substring_class = py::class_<Substring>(m, "Substring");
    substring_class
        .def_readonly("start", &Substring::start)
        .def_readonly("stop", &Substring::stop)
        .def_readonly("text", &Substring::text)
        .def(
            "__eq__", [](const Substring& left, const Substring& right) { return left == right; }, py::is_operator())
        .def("__repr__",
             [](const Substring& span) { return span_repr("Substring", span.start, span.stop, span.text); })
        .def(py::pickle(
            [](const Substring& span) { return py::make_tuple(span.start, span.stop, span.text); },
            [](py::tuple state) {
                if (state.size() != 3) {
                    throw py::value_error("invalid Substring pickle state");
                }
                return Substring{state[0].cast<std::size_t>(), state[1].cast<std::size_t>(),
                                 state[2].cast<std::string>()};
            }));
    bind_copy(substring_class);

    m.def("options_for_preset", &uktextnorm::options_for_preset, py::arg("preset"));
    m.def("number_to_words", &uktextnorm::number_to_words, py::arg("n"));
    m.def("number_to_words_digit_by_digit", &uktextnorm::number_to_words_digit_by_digit, py::arg("digits"));
    m.def("number_to_ordinal_words", &uktextnorm::number_to_ordinal_words, py::arg("n"), py::arg("form") = "nom_m");
    m.def("number_to_words_case", &uktextnorm::number_to_words_case, py::arg("n"), py::arg("grammatical_case"));
    m.def(
        "normalize_abbreviations",
        [](py::str text) { return with_text(text, uktextnorm::normalize_abbreviations); },
        py::arg("text"));
    m.def(
        "expand_abbreviations",
        [](py::str text) { return with_text(text, uktextnorm::expand_abbreviations); },
        py::arg("text"));
    m.def(
        "transliterate_to_cyrillic",
        [](py::str text) { return with_text(text, uktextnorm::transliterate_to_cyrillic); },
        py::arg("text"));
    m.def("cyrilize", [](py::str text) { return with_text(text, uktextnorm::cyrilize); }, py::arg("text"));
    m.def("cyrrilize", [](py::str text) { return with_text(text, uktextnorm::cyrrilize); }, py::arg("text"));
    m.def(
        "normalize_ukrainian",
        [](py::str text) {
            return with_text(text, [](std::string_view view) { return uktextnorm::normalize_ukrainian(view); });
        },
        py::arg("text"));
    m.def(
        "normalize_ukrainian",
        [](py::str text, const uktextnorm::NormalizeOptions& options) {
            const auto snapshot = options;
            return with_text(text,
                             [&](std::string_view view) { return uktextnorm::normalize_ukrainian(view, snapshot); });
        },
        py::arg("text"),
        py::arg("options"));
    m.def(
        "normalize_ukrainian",
        [](py::str text, uktextnorm::NormalizePreset preset) {
            return with_text(text,
                             [&](std::string_view view) { return uktextnorm::normalize_ukrainian(view, preset); });
        },
        py::arg("text"),
        py::arg("preset"));
    m.def("normalize_ukrainian_many",
          [](py::iterable texts) { return normalize_many(texts, uktextnorm::NormalizeOptions{}); }, py::arg("texts"));
    m.def("normalize_ukrainian_many",
          [](py::iterable texts, const uktextnorm::NormalizeOptions& options) {
              const auto snapshot = options;
              return normalize_many(texts, snapshot);
          },
          py::arg("texts"), py::arg("options"));
    m.def("normalize_ukrainian_many",
          [](py::iterable texts, uktextnorm::NormalizePreset preset) {
              return normalize_many(texts, uktextnorm::options_for_preset(preset));
          },
          py::arg("texts"), py::arg("preset"));
    m.def(
        "normalize_ukrainian_with_preset",
        [](py::str text, uktextnorm::NormalizePreset preset) {
            return with_text(
                text, [&](std::string_view view) { return uktextnorm::normalize_ukrainian_with_preset(view, preset); });
        },
        py::arg("text"),
        py::arg("preset") = uktextnorm::NormalizePreset::Default);
    m.def(
        "flag_uncertain",
        [](py::str text) {
            return with_text(text, [](std::string_view view) { return uktextnorm::flag_uncertain(view); });
        },
        py::arg("text"));
    m.def(
        "flag_uncertain",
        [](py::str text, const uktextnorm::NormalizeOptions& options) {
            const auto snapshot = options;
            return with_text(text, [&](std::string_view view) { return uktextnorm::flag_uncertain(view, snapshot); });
        },
        py::arg("text"),
        py::arg("options"));
    m.def(
        "flag_uncertain",
        [](py::str text, uktextnorm::NormalizePreset preset) {
            const auto options = uktextnorm::options_for_preset(preset);
            return with_text(text, [&](std::string_view view) { return uktextnorm::flag_uncertain(view, options); });
        },
        py::arg("text"),
        py::arg("preset"));
    m.def("split_sentences", [](py::str text) { return with_text(text, split_sentences); }, py::arg("text"));
    m.def("sentenize", [](py::str text) { return with_text(text, legacy_sentenize); }, py::arg("text"));
    m.def("tokenize", [](py::str text) { return with_text(text, tokenize); }, py::arg("text"));
}
