# normalize-uk-cpp

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
print(nuk.normalize_ukrainian_with_preset("01.05.2024", nuk.NormalizePreset.TtsFriendly))
print([sentence.text for sentence in nuk.split_sentences("П'ять зв'язків. Два.")])
print([token.text for token in nuk.tokenize("П'ять зв'язків.")])
```

More examples live in `examples/python/`.

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

## Formatting

The project includes a `.clang-format` file and a CMake formatting target. Install `clang-format`, then run:

```sh
cmake --build build --target format
```
