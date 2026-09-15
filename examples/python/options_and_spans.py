#!/usr/bin/env python3
"""Normalize with options and inspect uncertain spans."""

import normalize_uk as nuk


def describe_spans(text: str, options: nuk.NormalizeOptions) -> None:
    for span in nuk.flag_uncertain(text, options=options):
        print(
            f"{span.start}:{span.stop} {span.text!r} {span.category.name}/{span.severity.name}: {span.reason}"
        )


def main() -> None:
    options = nuk.NormalizeOptions(
        range_style=nuk.RangeStyle.FromTo,
        phone_style=nuk.PhoneStyle.DigitByDigit,
        date_style=nuk.DateStyle.Spoken,
    )

    text = "15.06.2026, +380 67 123-45-67, 5-7 кг"
    print(nuk.normalize_ukrainian(text, options))
    print(
        nuk.normalize_ukrainian(
            "OpenAI + ФОП", preset=nuk.NormalizePreset.SearchIndexing
        )
    )

    tts_options = nuk.NormalizeOptions(preset=nuk.NormalizePreset.TtsFriendly)
    tts_options.symbol_style = nuk.SymbolStyle.Preserve
    print(nuk.normalize_ukrainian("2 + 2, 15.06.2026", tts_options))

    describe_spans("Версія XXI і сума 10 PLN", options)


if __name__ == "__main__":
    main()
