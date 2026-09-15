from collections.abc import Iterable
from typing import Literal, overload

from ._normalize_uk import (
    ColonStyle as ColonStyle,
)
from ._normalize_uk import (
    CurrencySymbolPolicy as CurrencySymbolPolicy,
)
from ._normalize_uk import (
    DateStyle as DateStyle,
)
from ._normalize_uk import (
    NormalizeOptions as NormalizeOptions,
)
from ._normalize_uk import (
    NormalizePreset as NormalizePreset,
)
from ._normalize_uk import (
    NumericDateOrder as NumericDateOrder,
)
from ._normalize_uk import (
    PhoneStyle as PhoneStyle,
)
from ._normalize_uk import (
    QuoteStyle as QuoteStyle,
)
from ._normalize_uk import (
    RangeStyle as RangeStyle,
)
from ._normalize_uk import (
    Substring as Substring,
)
from ._normalize_uk import (
    SymbolStyle as SymbolStyle,
)
from ._normalize_uk import (
    UncertainSpan as UncertainSpan,
)
from ._normalize_uk import (
    UncertaintyCategory as UncertaintyCategory,
)
from ._normalize_uk import (
    UncertaintySeverity as UncertaintySeverity,
)
from ._normalize_uk import (
    expand_abbreviations as expand_abbreviations,
)
from ._normalize_uk import (
    load_vocabulary_tsv as load_vocabulary_tsv,
)
from ._normalize_uk import (
    normalize_abbreviations as normalize_abbreviations,
)
from ._normalize_uk import (
    options_for_preset as options_for_preset,
)
from ._normalize_uk import (
    split_sentences as split_sentences,
)
from ._normalize_uk import (
    tokenize as tokenize,
)
from ._normalize_uk import (
    transliterate_to_cyrillic as transliterate_to_cyrillic,
)

def number_to_words(n: int) -> str: ...
def number_to_words_digit_by_digit(digits: str) -> str: ...
def number_to_ordinal_words(
    n: int,
    form: Literal[
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
    ] = "nom_m",
) -> str: ...
def number_to_words_case(
    n: int, grammatical_case: Literal["gen", "dat", "instr", "prep"]
) -> str: ...
def cyrilize(text: str) -> str: ...
def cyrrilize(text: str) -> str: ...
@overload
def normalize_ukrainian(
    text: str,
    options: NormalizeOptions | None = None,
    *,
    preset: None = None,
    vocabulary: dict[str, str] | None = None,
) -> str: ...
@overload
def normalize_ukrainian(
    text: str,
    options: NormalizePreset,
    *,
    preset: None = None,
    vocabulary: dict[str, str] | None = None,
) -> str: ...
@overload
def normalize_ukrainian(
    text: str,
    options: None = None,
    *,
    preset: NormalizePreset,
    vocabulary: dict[str, str] | None = None,
) -> str: ...
@overload
def normalize_ukrainian(
    text: str,
    options: dict[str, str],
    *,
    preset: None = None,
    vocabulary: dict[str, str] | None = None,
) -> str: ...
def normalize_ukrainian_with_preset(
    text: str, preset: NormalizePreset = NormalizePreset.Default
) -> str: ...
@overload
def normalize_ukrainian_many(
    texts: Iterable[str],
    options: NormalizeOptions | None = None,
    *,
    preset: None = None,
    vocabulary: dict[str, str] | None = None,
) -> list[str]: ...
@overload
def normalize_ukrainian_many(
    texts: Iterable[str],
    options: NormalizePreset,
    *,
    preset: None = None,
    vocabulary: dict[str, str] | None = None,
) -> list[str]: ...
@overload
def normalize_ukrainian_many(
    texts: Iterable[str],
    options: None = None,
    *,
    preset: NormalizePreset,
    vocabulary: dict[str, str] | None = None,
) -> list[str]: ...
@overload
def normalize_ukrainian_many(
    texts: Iterable[str],
    options: dict[str, str],
    *,
    preset: None = None,
    vocabulary: dict[str, str] | None = None,
) -> list[str]: ...
@overload
def flag_uncertain(
    text: str,
    options: NormalizeOptions | None = None,
    *,
    preset: None = None,
    vocabulary: dict[str, str] | None = None,
) -> list[UncertainSpan]: ...
@overload
def flag_uncertain(
    text: str,
    options: NormalizePreset,
    *,
    preset: None = None,
    vocabulary: dict[str, str] | None = None,
) -> list[UncertainSpan]: ...
@overload
def flag_uncertain(
    text: str,
    options: None = None,
    *,
    preset: NormalizePreset,
    vocabulary: dict[str, str] | None = None,
) -> list[UncertainSpan]: ...
@overload
def flag_uncertain(
    text: str,
    options: dict[str, str],
    *,
    preset: None = None,
    vocabulary: dict[str, str] | None = None,
) -> list[UncertainSpan]: ...
def sentenize(text: str) -> list[Substring]: ...
