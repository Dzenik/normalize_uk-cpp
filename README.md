# normalize-uk-cpp

[![CI](https://github.com/ThirdLetterC/normalize_uk-cpp/actions/workflows/ci.yml/badge.svg)](https://github.com/ThirdLetterC/normalize_uk-cpp/actions/workflows/ci.yml)
[![Release](https://github.com/ThirdLetterC/normalize_uk-cpp/actions/workflows/release.yml/badge.svg)](https://github.com/ThirdLetterC/normalize_uk-cpp/actions/workflows/release.yml)

C++23 Ukrainian text normalization and tokenization utilities with optional Python 3.10+ bindings.

## CMake

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

Enable Python bindings explicitly when building with CMake:

```sh
cmake -S . -B build-python -DNORMALIZE_UK_CPP_BUILD_PYTHON=ON
cmake --build build-python
```

## Python

```sh
python -m pip install .
```

```python
import normalize_uk as nuk

print(nuk.number_to_words(123))
print(nuk.normalize_ukrainian("01.05.2024"))
print(nuk.normalize_ukrainian("01.05.2024", preset=nuk.NormalizePreset.TtsFriendly))
print([sentence.text for sentence in nuk.split_sentences("П'ять зв'язків. Два.")])
print([token.text for token in nuk.tokenize("П'ять зв'язків.")])
```

More examples live in `examples/python/`.

Tags matching the version in `pyproject.toml` (for example, `v0.4.2`) trigger
wheel builds for supported Python versions. The workflow uploads the wheels to
GitHub Release Assets, then downloads those Assets and publishes them to PyPI.
To enable PyPI Trusted Publishing, register `ThirdLetterC/normalize_uk-cpp` as
a publisher for `normalize-uk` with workflow `release.yml` and environment
`pypi`. For a new PyPI project, register a
[pending publisher](https://docs.pypi.org/trusted-publishers/creating-a-project-through-oidc/)
first. No PyPI API token is needed.

`NormalizeOptions` accepts a preset and named overrides at construction time:

```python
options = nuk.NormalizeOptions(
    preset=nuk.NormalizePreset.TtsFriendly,
    range_style=nuk.RangeStyle.Compact,
    numeric_date_order=nuk.NumericDateOrder.DayMonthYear,
)
result = nuk.normalize_ukrainian("5–7 кг", options=options)
spans = nuk.flag_uncertain("10:30, $12", options=options)
```

Pass either `options=` or `preset=` to `normalize_ukrainian` and `flag_uncertain`.
The older positional options/preset calls and `normalize_ukrainian_with_preset()` remain available.
Without options, `flag_uncertain()` reports all ambiguity candidates. With explicit options
or a preset, it omits warnings for ambiguous dates, colon pairs, and currency symbols
when the selected policy resolves them; invalid-value diagnostics remain.

`Substring.start`/`stop` and `UncertainSpan.start`/`stop` are Python `str` indexes,
with `stop` exclusive: `span.text == source[span.start:span.stop]`. They count Unicode
code points, matching Python slicing, rather than UTF-8 bytes.

`number_to_words()`, `number_to_ordinal_words()`, and `number_to_words_case()` accept
integers from 0 through `999999999999999999`. Values outside that range raise
`ValueError`; non-integers raise `TypeError`. `number_to_words_digit_by_digit()` accepts
a nonempty string of ASCII digits only and preserves leading zeroes. Its invalid input
raises `ValueError`. Ordinal forms are `nom_m`, `nom_n`, `nom_f`, `nom_pl`, `gen`, `dat`,
`prep`, `loc`, `pl`, `loc_pl`, `acc_f`, `gen_f`, `ins`, `ins_f`, `ins_pl`, and `loc_f`.
Cardinal cases are `gen`, `dat`, `instr`, and `prep`. Unknown forms raise `ValueError`.

The legacy spellings `sentenize()`, `cyrilize()`, and `cyrrilize()` remain available
but issue `DeprecationWarning`; use `split_sentences()` and
`transliterate_to_cyrillic()` in new code.

## Currency and cryptocurrency coverage

Normalization covers all 178 active ISO 4217 List One codes, including their
0-, 2-, 3-, or 4-digit minor-unit rules. More than 70 common cryptocurrency and
finance tickers have natural Ukrainian readings. Other 2–10 character uppercase
alphanumeric tickers are spelled out after amounts and when paired with a known
asset, so newly introduced assets do not require an immediate library release.
Prefix and suffix
amounts, localized thousands separators, signs, decimals, and the `₿` symbol
are supported.

## Ambiguity controls

`NormalizeOptions` keeps backward-compatible defaults while allowing callers to resolve ambiguous input explicitly:

- `colon_style`: contextual clock/ratio detection, forced clock, or forced ratio.
- `numeric_date_order`: day-month-year, month-day-year, or preservation of dates where both fields are at most 12.
- `currency_symbol_policy`: assume the common currency for `$` and `¥`, or preserve those ambiguous symbols.

The CLI exposes the same controls through `--colon-style`, `--date-order`, and
`--preserve-ambiguous-currency`.

## Benchmarks and fuzzing

Build and run the benchmark explicitly:

```sh
cmake --build build --target uktextnorm_benchmark
./build/uktextnorm_benchmark .
```

With Clang and libFuzzer support, build the normalization harness with sanitizers:

```sh
cmake -S . -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DNORMALIZE_UK_CPP_BUILD_FUZZER=ON
cmake --build build-fuzz --target uktextnorm_fuzzer
./build-fuzz/uktextnorm_fuzzer -max_total_time=60 tests/data
```

## Development

Install `uv` and `just` for development. Run `just` to see all recipes. `just format` applies Ruff fixes and Python formatting; `just lint` runs the Python static checks; `just check` also runs Python and C++ tests.

```sh
just setup
just format
just lint
just check
just wheel 3.15
```

The project includes a `.clang-format` file and a CMake formatting target. Install `clang-format`, then run:

```sh
cmake --build build --target format
```
