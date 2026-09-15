"""Ukrainian text normalization and tokenization."""

from __future__ import annotations

import warnings as _warnings
from collections.abc import Iterable
from copy import copy as _copy

from ._normalize_uk import (
    ColonStyle,
    CurrencySymbolPolicy,
    DateStyle,
    NormalizeOptions,
    NormalizePreset,
    NumericDateOrder,
    PhoneStyle,
    QuoteStyle,
    RangeStyle,
    Substring,
    SymbolStyle,
    UncertainSpan,
    UncertaintyCategory,
    UncertaintySeverity,
    expand_abbreviations,
    load_vocabulary_tsv,
    normalize_abbreviations,
    options_for_preset,
    split_sentences,
    tokenize,
    transliterate_to_cyrillic,
)
from ._normalize_uk import (
    cyrilize as _native_cyrilize,
)
from ._normalize_uk import (
    cyrrilize as _native_cyrrilize,
)
from ._normalize_uk import (
    flag_uncertain as _native_flag_uncertain,
)
from ._normalize_uk import (
    normalize_ukrainian as _native_normalize_ukrainian,
)
from ._normalize_uk import (
    normalize_ukrainian_many as _native_normalize_ukrainian_many,
)
from ._normalize_uk import (
    number_to_ordinal_words as _native_number_to_ordinal_words,
)
from ._normalize_uk import (
    number_to_words as _native_number_to_words,
)
from ._normalize_uk import (
    number_to_words_case as _native_number_to_words_case,
)
from ._normalize_uk import (
    number_to_words_digit_by_digit as _native_number_to_words_digit_by_digit,
)
from ._normalize_uk import (
    sentenize as _native_sentenize,
)

__all__ = (
    "ColonStyle",
    "CurrencySymbolPolicy",
    "DateStyle",
    "NormalizeOptions",
    "NormalizePreset",
    "NumericDateOrder",
    "PhoneStyle",
    "QuoteStyle",
    "RangeStyle",
    "Substring",
    "SymbolStyle",
    "UncertainSpan",
    "UncertaintyCategory",
    "UncertaintySeverity",
    "cyrilize",
    "cyrrilize",
    "expand_abbreviations",
    "flag_uncertain",
    "load_vocabulary_tsv",
    "normalize_abbreviations",
    "normalize_ukrainian",
    "normalize_ukrainian_many",
    "normalize_ukrainian_with_preset",
    "number_to_ordinal_words",
    "number_to_words",
    "number_to_words_case",
    "number_to_words_digit_by_digit",
    "options_for_preset",
    "sentenize",
    "split_sentences",
    "tokenize",
    "transliterate_to_cyrillic",
)

_MAX_SPOKEN_NUMBER = 10**18 - 1
_ORDINAL_FORMS = frozenset(
    (
        "nom_m",
        "nom_n",
        "nom_f",
        "nom_pl",
        "gen",
        "dat",
        "prep",
        "loc",
        "pl",
        "loc_pl",
        "acc_f",
        "gen_f",
        "ins",
        "ins_f",
        "ins_pl",
        "loc_f",
    )
)
_CARDINAL_CASES = frozenset(("gen", "dat", "instr", "prep"))


def _spoken_number(n: int) -> int:
    if isinstance(n, bool) or not isinstance(n, int):
        raise TypeError("n must be an integer")
    if not 0 <= n <= _MAX_SPOKEN_NUMBER:
        raise ValueError(f"n must be between 0 and {_MAX_SPOKEN_NUMBER}")
    return n


def number_to_words(n: int) -> str:
    """Spell a nonnegative integer smaller than 10**18 in Ukrainian."""
    return _native_number_to_words(_spoken_number(n))


def number_to_words_digit_by_digit(digits: str) -> str:
    """Spell each ASCII digit, preserving leading zeroes."""
    if not isinstance(digits, str):
        raise TypeError("digits must be a string")
    if not digits or any(ch < "0" or ch > "9" for ch in digits):
        raise ValueError("digits must contain one or more ASCII digits only")
    return _native_number_to_words_digit_by_digit(digits)


def number_to_ordinal_words(n: int, form: str = "nom_m") -> str:
    """Spell an ordinal using one of the supported grammatical forms."""
    if not isinstance(form, str):
        raise TypeError("form must be a string")
    if form not in _ORDINAL_FORMS:
        raise ValueError(
            f"unknown ordinal form {form!r}; expected one of {', '.join(sorted(_ORDINAL_FORMS))}"
        )
    return _native_number_to_ordinal_words(_spoken_number(n), form)


def number_to_words_case(n: int, grammatical_case: str) -> str:
    """Spell a cardinal in the genitive, dative, instrumental, or prepositional case."""
    if not isinstance(grammatical_case, str):
        raise TypeError("grammatical_case must be a string")
    if grammatical_case not in _CARDINAL_CASES:
        raise ValueError(
            f"unknown grammatical case {grammatical_case!r}; expected one of "
            f"{', '.join(sorted(_CARDINAL_CASES))}"
        )
    return _native_number_to_words_case(_spoken_number(n), grammatical_case)


def _selection(
    options: NormalizeOptions | NormalizePreset | dict[str, str] | None,
    preset: NormalizePreset | None,
) -> NormalizeOptions | NormalizePreset | None:
    if isinstance(options, dict):
        if preset is not None:
            raise ValueError(
                "provide either a positional vocabulary or preset, not both"
            )
        return NormalizeOptions(vocabulary=options)
    if isinstance(options, NormalizePreset):
        if preset is not None:
            raise ValueError("provide either options or preset, not both")
        preset, options = options, None
    if options is not None and preset is not None:
        raise ValueError("provide either options or preset, not both")
    if options is not None:
        if not isinstance(options, NormalizeOptions):
            raise TypeError("options must be a NormalizeOptions instance")
        return options
    if preset is not None:
        if not isinstance(preset, NormalizePreset):
            raise TypeError("preset must be a NormalizePreset value")
        return preset
    return None


def _with_vocabulary(
    selected: NormalizeOptions | NormalizePreset | None,
    vocabulary: dict[str, str] | None,
) -> NormalizeOptions | NormalizePreset | None:
    if vocabulary is None:
        return selected
    added = NormalizeOptions(vocabulary=vocabulary).vocabulary
    if selected is None:
        snapshot = NormalizeOptions()
    elif isinstance(selected, NormalizePreset):
        snapshot = options_for_preset(selected)
    else:
        snapshot = _copy(selected)
    combined = snapshot.vocabulary
    combined.update(added)
    snapshot.vocabulary = combined
    return snapshot


def normalize_ukrainian(
    text: str,
    options: NormalizeOptions | NormalizePreset | dict[str, str] | None = None,
    *,
    preset: NormalizePreset | None = None,
    vocabulary: dict[str, str] | None = None,
) -> str:
    """Normalize text with either an options object or a preset."""
    selected = _with_vocabulary(_selection(options, preset), vocabulary)
    if selected is None:
        return _native_normalize_ukrainian(text)
    if isinstance(selected, NormalizePreset):
        return _native_normalize_ukrainian(text, selected)
    return _native_normalize_ukrainian(text, selected)


def normalize_ukrainian_many(
    texts: Iterable[str],
    options: NormalizeOptions | NormalizePreset | dict[str, str] | None = None,
    *,
    preset: NormalizePreset | None = None,
    vocabulary: dict[str, str] | None = None,
) -> list[str]:
    """Normalize an iterable of strings using one options snapshot for the batch."""
    selected = _with_vocabulary(_selection(options, preset), vocabulary)
    if selected is None:
        return _native_normalize_ukrainian_many(texts)
    if isinstance(selected, NormalizePreset):
        return _native_normalize_ukrainian_many(texts, selected)
    return _native_normalize_ukrainian_many(texts, selected)


def flag_uncertain(
    text: str,
    options: NormalizeOptions | NormalizePreset | dict[str, str] | None = None,
    *,
    preset: NormalizePreset | None = None,
    vocabulary: dict[str, str] | None = None,
) -> list[UncertainSpan]:
    """Find uncertain spans; explicit options suppress resolved ambiguity warnings."""
    selected = _with_vocabulary(_selection(options, preset), vocabulary)
    if selected is None:
        return _native_flag_uncertain(text)
    if isinstance(selected, NormalizePreset):
        return _native_flag_uncertain(text, selected)
    return _native_flag_uncertain(text, selected)


def normalize_ukrainian_with_preset(
    text: str, preset: NormalizePreset = NormalizePreset.Default
) -> str:
    """Compatibility alias for normalize_ukrainian(text, preset=preset)."""
    return normalize_ukrainian(text, preset=preset)


def sentenize(text: str) -> list[Substring]:
    _warnings.warn(
        "sentenize() is deprecated; use split_sentences()",
        DeprecationWarning,
        stacklevel=2,
    )
    return _native_sentenize(text)


def cyrilize(text: str) -> str:
    _warnings.warn(
        "cyrilize() is deprecated; use transliterate_to_cyrillic()",
        DeprecationWarning,
        stacklevel=2,
    )
    return _native_cyrilize(text)


def cyrrilize(text: str) -> str:
    _warnings.warn(
        "cyrrilize() is deprecated; use transliterate_to_cyrillic()",
        DeprecationWarning,
        stacklevel=2,
    )
    return _native_cyrrilize(text)
