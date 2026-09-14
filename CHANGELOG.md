# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.4.0] - 2026-09-14

### Added

- Temperature normalization for Kelvin, Rankine, Réaumur, Delisle, and Rømer scales, including symbols, names,
  signed values, decimal values, and ranges.
- Date support for `DD-MM-YYYY`, two-digit years, `YYYY/MM/DD`, `YYYY-MM`, month-year expressions, dates without a
  year, cross-month ranges, and ISO date-time values.
- Time support for seconds, AM/PM suffixes, UTC/GMT offsets, midnight, and numeric ratios.
- Explicit ambiguity policies for colon-delimited clock/ratio values, local numeric date order, and ambiguous currency
  symbols, exposed through the C++, Python, and CLI APIs.
- ISO 8601 duration, week-date, and ordinal-date readings, selected IANA timezone names, and validation diagnostics
  for invalid week dates, ordinal dates, and UTC/GMT offsets.
- Scientific notation support for `e` notation, multiplication by powers of ten, ordinary powers, and superscript
  exponents.
- Broader measurement coverage, including SI and IEC data units, imperial units, pressure, energy, power, frequency,
  acceleration, density, data rates, fuel economy, revolutions, decibels, parts per million, basis points, DPI, and
  frame rates.
- Compound engineering and medical measurements for force, torque, viscosity, irradiance, molarity, dosage,
  particulate concentration, and electric-vehicle energy use, plus a composable fallback for products, quotients,
  powers, and compact or percentage tolerances.
- Complete active ISO 4217 List One coverage (178 currency, fund, metal, and reserved codes as published by SIX on
  2026-01-01), including correct 0-, 2-, 3-, and 4-digit minor-unit handling and additional unambiguous symbols.
- Named readings for more than 70 established fiat/crypto finance tickers, plus a conservative fallback that spells a new
  2–10 character uppercase alphanumeric code after an amount or when paired with a recognized asset.
- Bitcoin amounts written with the `₿` symbol, both before and after the amount.
- Crypto amounts with prefix or suffix tickers, localized thousands separators, signs, decimals, and
  case-insensitive known market pairs.
- Structured-data normalization for IPv4 ports and CIDR prefixes, IPv6 CIDR and bracketed endpoints, MAC addresses,
  UUIDs, ISBNs, ISSNs, VINs, SWIFT/BIC codes, and non-Ukrainian IBANs.
- Phone-number support for international `00` prefixes and extension markers such as `доб.`, `дод.`, `ext`, and `x`.
- Decimal and DMS coordinates with Latin or Ukrainian hemisphere markers and coordinate validation.
- Geo URI coordinates, labeled latitude/longitude pairs, optional altitude, and degrees with decimal minutes.
- Legal ranges for articles, parts, points, subpoints, paragraphs, chapters, tables, and figures.
- URL support for FTP, arbitrary top-level domains, fragments, Unicode email addresses, and punycode-like labels.
- Preservation of HTML/SSML tags, comments, fenced code blocks, and inline code during normalization.
- Preservation of balanced MediaWiki `\displaystyle` TeX expressions during surrounding prose normalization.
- Preservation of Markdown link destinations, reference URLs, tilde fences, and HTML character entities while visible
  link text remains normalizable.
- Uncertainty categories for invalid times, fractions, network values, and scientific notation in the C++, Python,
  and CLI APIs.
- Cross-platform CI for Linux, macOS, and Windows, plus Clang AddressSanitizer and UndefinedBehaviorSanitizer checks.
- A Clang/libFuzzer harness covering every preset and uncertainty scanning, with a sanitizer CI smoke test.
- CLI integration tests, expanded Python binding tests, and normalization idempotence coverage.
- Full-sentence TTS golden tests with 56 cases across 28 normalization categories and an idempotence assertion for every
  sentence.

### Changed

- Measurement and finance lexicons now define a dedicated decimal agreement form.
- Finance normalization is generated from the finance lexicon instead of using a fixed ticker list.
- Unicode token detection now compares complete code points instead of individual UTF-8 bytes.
- Common Latin diacritics are handled by approximate Cyrillic transliteration, while bracketed IPA remains opaque.
- Numeric dates governed by `від`, `до`, `з`, `із`, `після`, or `станом на` now use the Ukrainian genitive day form.
- Sentence-initial numeric governors such as `До`, `Від`, and `Близько` now apply the same grammatical cases as their
  lowercase forms.
- IPv4 addresses, CIDR blocks, and endpoints are recognized immediately before sentence-ending punctuation.
- Golden TSV fixtures are pinned to LF in Git and their readers also accept CRLF checkouts on Windows.
- IPv6 zero compression is pronounced explicitly as `скорочення нулів`.
- Soft-stem ordinal inflection now produces forms such as `третя` and `третього`.
- Technical acronym/version expressions and protocol names such as `ISO 3166`, `IEEE 802.3`, `ALGOL 58`, and `TCP/IP`
  are no longer misclassified as financial amounts or market pairs.
- Ukrainian domain names, bibliographic volume counts, locative numeric phrases, variable ratios, common English tonne
  spellings, and capitalization variants of bit-rate units now receive context-appropriate readings.

### Fixed

- Temperature and measurement ranges now handle hyphen-minus, minus, en dash, and em dash consistently with
  `RangeStyle.FromTo`.
- Standalone and ranged negative temperatures now pronounce their signs naturally.
- Decimal measurements, multipliers, and financial amounts now use grammatically correct agreement, for example
  `2,5 кг` → `дві цілих і п'ять десятих кілограма`.
- Decimal fractions ending in one now use the singular denominator form.
- Signed, symbol-prefixed, code-prefixed, accounting-style, and repeated currency amounts are normalized correctly.
- Signed and mixed fractions are supported, while fractions with a zero denominator are preserved and flagged.
- Structured dates are normalized before generic numeric ranges, preventing date components from being interpreted as
  ranges.
- Invalid clock values, AM/PM values, dates, coordinates, IP addresses, CIDR prefixes, and ports are rejected or
  reported as uncertain instead of receiving misleading readings.
- Multiple occurrences of the same currency in one input are all normalized.
- ISBN-10/13, ISSN, IBAN, payment-card, VIN, UUID, and labeled hash candidates now receive checksum, length, version,
  or variant validation and error-level uncertainty metadata when invalid.
- Unicode minus and Unicode hyphen variants are canonicalized without producing malformed UTF-8, including in
  signed fractions, percentages, measurements, and temperature ranges.
- Signed and leading-dot compound measurements, Latin SI aliases, parenthesized denominators, tolerances, and
  measured fractions now normalize consistently.
- Fractional and week-based ISO durations are supported; malformed scientific notation and invalid structured values
  are preserved instead of being partially normalized.
- Bracketed IPv6 endpoints, Cisco-style MAC addresses, compact labeled UUIDs, ISSN-L values, hyphenated IBANs, and
  labeled 12–19 digit payment-card numbers are recognized without cross-parser collisions.
- Regional currency symbols, lowercase currency codes, locale-grouped amounts, and symbol-prefixed accounting values
  now resolve to the intended currency and sign.
- Markdown destinations with balanced parentheses and double-backtick code spans remain opaque during normalization.
- Legal ranges with word labels, dotted subpoints, and paragraph symbols now honor `RangeStyle.FromTo`.
- Python binding tests now resolve the configuration-specific extension directory correctly with multi-configuration
  generators such as Visual Studio on Windows.
- The Windows CLI now reads command-line arguments as UTF-16 and converts them to UTF-8, preserving Ukrainian text,
  degree symbols, and Unicode dashes passed directly on the command line.
- Mathematical comparisons containing both `<` and `>` are no longer mistaken for HTML tags, and stripping adjacent
  quotation marks no longer joins neighboring words.
- Address abbreviations no longer match suffixes inside ordinary words or reinterpret generated measurement
  abbreviations on a second normalization pass.
- High-precision decimals are no longer parsed as phone numbers; unsupported decimal precision now falls back to a
  digit-by-digit fractional reading without dropping the associated unit.
- Dot decimals, compact and named versions, single-letter recommendations, classification codes, and common technical
  standard designations now receive stable spoken readings.
- Numeric dates consume an already written `року`/`р.` suffix, coordinate directions do not duplicate
  `широти`/`довготи`, and governed coordinate bounds use the genitive case.
- Native Windows normalization no longer exhausts the default executable stack while matching ordinal and Roman
  numeral expressions.

[0.4.0]: https://github.com/ThirdLetterC/normalize_uk-cpp/releases/tag/v0.4.0
