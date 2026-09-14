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
- Scientific notation support for `e` notation, multiplication by powers of ten, ordinary powers, and superscript
  exponents.
- Broader measurement coverage, including SI and IEC data units, imperial units, pressure, energy, power, frequency,
  acceleration, density, data rates, fuel economy, revolutions, decibels, parts per million, basis points, DPI, and
  frame rates.
- Finance support for SOL, XRP, ADA, and DOGE, plus RUB, KRW, BRL, ZAR, NZD, MXN, SGD, and HKD currencies.
- Structured-data normalization for IPv4 ports and CIDR prefixes, IPv6 CIDR and bracketed endpoints, MAC addresses,
  UUIDs, ISBNs, ISSNs, VINs, SWIFT/BIC codes, and non-Ukrainian IBANs.
- Phone-number support for international `00` prefixes and extension markers such as `доб.`, `дод.`, `ext`, and `x`.
- Decimal and DMS coordinates with Latin or Ukrainian hemisphere markers and coordinate validation.
- Legal ranges for articles, parts, points, subpoints, paragraphs, chapters, tables, and figures.
- URL support for FTP, arbitrary top-level domains, fragments, Unicode email addresses, and punycode-like labels.
- Preservation of HTML/SSML tags, comments, fenced code blocks, and inline code during normalization.
- Uncertainty categories for invalid times, fractions, network values, and scientific notation in the C++, Python,
  and CLI APIs.
- Cross-platform CI for Linux, macOS, and Windows, plus Clang AddressSanitizer and UndefinedBehaviorSanitizer checks.
- CLI integration tests, expanded Python binding tests, and normalization idempotence coverage.

### Changed

- Measurement and finance lexicons now define a dedicated decimal agreement form.
- Finance normalization is generated from the finance lexicon instead of using a fixed ticker list.
- Unicode token detection now compares complete code points instead of individual UTF-8 bytes.
- IPv6 zero compression is pronounced explicitly as `скорочення нулів`.
- Soft-stem ordinal inflection now produces forms such as `третя` and `третього`.

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

[0.4.0]: https://github.com/ThirdLetterC/normalize_uk-cpp/releases/tag/v0.4.0
