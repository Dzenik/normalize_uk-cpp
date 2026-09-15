"""Measure Python binding costs for scalar, batched, and span-returning calls."""

from __future__ import annotations

import statistics
import timeit

import normalize_uk as nuk


def measure(
    label: str, statement: str, namespace: dict[str, object], number: int
) -> None:
    samples = timeit.repeat(statement, globals=namespace, number=number, repeat=3)
    print(f"{label:28} {statistics.median(samples) / number * 1e6:9.1f} µs/call")


def main() -> None:
    options = nuk.NormalizeOptions(preset=nuk.NormalizePreset.TtsFriendly)
    short_texts = ["5 кг", "10:30", "Привіт!", "2026-09-14"] * 250
    empty_texts = [""] * 1000
    unique_texts = [f"Текст {index}" for index in range(100)]
    sentence_text = "Привіт, світе! Це 5 кг. " * 100
    token_text = "Слово 123, ще слово. " * 100
    uncertain_text = "10:30, $12, 03/04/2026. " * 100
    namespace = {
        "nuk": nuk,
        "options": options,
        "short_texts": short_texts,
        "empty_texts": empty_texts,
        "unique_texts": unique_texts,
        "sentence_text": sentence_text,
        "token_text": token_text,
        "uncertain_text": uncertain_text,
    }
    measure(
        "scalar: 1000 short strings",
        "[nuk.normalize_ukrainian(t, options) for t in short_texts]",
        namespace,
        3,
    )
    measure(
        "batch: 1000 short strings",
        "nuk.normalize_ukrainian_many(short_texts, options)",
        namespace,
        3,
    )
    measure(
        "scalar: 1000 empty strings",
        "[nuk.normalize_ukrainian(t, options) for t in empty_texts]",
        namespace,
        3,
    )
    measure(
        "batch: 1000 empty strings",
        "nuk.normalize_ukrainian_many(empty_texts, options)",
        namespace,
        3,
    )
    measure(
        "scalar: 100 unique strings",
        "[nuk.normalize_ukrainian(t, options) for t in unique_texts]",
        namespace,
        3,
    )
    measure(
        "batch: 100 unique strings",
        "nuk.normalize_ukrainian_many(unique_texts, options)",
        namespace,
        3,
    )
    print(
        f"span counts: sentences={len(nuk.split_sentences(sentence_text))}, "
        f"tokens={len(nuk.tokenize(token_text))}, "
        f"uncertain={len(nuk.flag_uncertain(uncertain_text))}"
    )
    measure("split_sentences: 200", "nuk.split_sentences(sentence_text)", namespace, 30)
    measure("tokenize: long text", "nuk.tokenize(token_text)", namespace, 30)
    measure(
        "flag_uncertain: long text", "nuk.flag_uncertain(uncertain_text)", namespace, 30
    )


if __name__ == "__main__":
    main()
