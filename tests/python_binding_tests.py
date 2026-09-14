import unittest

import normalize_uk as nuk


class NormalizeUkBindingTests(unittest.TestCase):
    def setUp(self) -> None:
        self.options = nuk.NormalizeOptions(preset=nuk.NormalizePreset.TtsFriendly)
        self.options.range_style = nuk.RangeStyle.FromTo

    def test_temperature_range(self) -> None:
        self.assertEqual(
            nuk.normalize_ukrainian("5–7 °C", self.options),
            "від п'яти до семи градусів Цельсія",
        )

    def test_signed_temperature_range(self) -> None:
        self.assertEqual(
            nuk.normalize_ukrainian("від -5 до +7 °C", self.options),
            "від мінус п'яти до плюс семи градусів Цельсія",
        )

    def test_decimal_and_unicode_temperature_range(self) -> None:
        self.assertEqual(
            nuk.normalize_ukrainian("5,5–7,5 ℃", self.options),
            "від п'яти цілих і п'яти десятих до семи цілих і п'яти десятих градуса Цельсія",
        )

    def test_temperature_scales_beyond_celsius_and_fahrenheit(self) -> None:
        cases = {
            "250–300 K": "від двохсот п'ятдесяти до трьохсот кельвінів",
            "5–7 °R": "від п'яти до семи градусів Ранкіна",
            "5–7 °Ré": "від п'яти до семи градусів Реомюра",
        }
        for source, expected in cases.items():
            with self.subTest(source=source):
                self.assertEqual(nuk.normalize_ukrainian(source, self.options), expected)

    def test_dates_times_and_scientific_notation(self) -> None:
        cases = {
            "2026-09": "вересень дві тисячі двадцять шостого року",
            "14.09.26": "чотирнадцятого вересня дві тисячі двадцять шостого року",
            "2026-09-14T10:30:00Z": (
                "чотирнадцятого вересня дві тисячі двадцять шостого року "
                "о десять годин тридцять хвилин за всесвітнім координованим часом"
            ),
            "10:30 PM": "десять годин тридцять хвилин вечора",
            "6.02×10²³": "шість цілих і дві сотих помножити на десять у степені двадцять три",
        }
        for source, expected in cases.items():
            with self.subTest(source=source):
                self.assertEqual(nuk.normalize_ukrainian(source, self.options), expected)

    def test_units_finance_and_structured_data(self) -> None:
        cases = {
            "6.5 L/100km": "шість цілих і п'ять десятих літра на сто кілометрів",
            "12 oz": "дванадцять унцій",
            "0.5 DOGE": "нуль цілих і п'ять десятих доджкоїна",
            "-$5": "мінус п'ять доларів",
            "10.0.0.0/24": "ай пі десять нуль нуль нуль префікс двадцять чотири",
            "ст. 5–7": "від п'ятої до сьомої статті",
        }
        for source, expected in cases.items():
            with self.subTest(source=source):
                self.assertEqual(nuk.normalize_ukrainian(source, self.options), expected)

    def test_ambiguity_policies(self) -> None:
        options = nuk.NormalizeOptions()
        options.colon_style = nuk.ColonStyle.Ratio
        self.assertEqual(nuk.normalize_ukrainian("10:30", options), "десять до тридцяти")
        options.numeric_date_order = nuk.NumericDateOrder.MonthDayYear
        self.assertEqual(
            nuk.normalize_ukrainian("03/04/2026", options),
            "четверте березня дві тисячі двадцять шостого року",
        )
        options.currency_symbol_policy = nuk.CurrencySymbolPolicy.PreserveAmbiguous
        self.assertEqual(nuk.normalize_ukrainian("$12", options), "$дванадцять")

    def test_uncertainty_categories(self) -> None:
        cases = {
            "99:30": nuk.UncertaintyCategory.Time,
            "1/0": nuk.UncertaintyCategory.Fraction,
            "999.1.1.1/40": nuk.UncertaintyCategory.Network,
            "1e+": nuk.UncertaintyCategory.Scientific,
        }
        for source, category in cases.items():
            with self.subTest(source=source):
                self.assertTrue(any(span.category == category for span in nuk.flag_uncertain(source)))

    def test_public_helpers_and_tokenization(self) -> None:
        self.assertEqual(nuk.number_to_words(21), "двадцять один")
        self.assertEqual(nuk.number_to_ordinal_words(3, "nom_f"), "третя")
        self.assertEqual(nuk.number_to_words_case(5, "gen"), "п'яти")
        text = "Привіт. Світ!"
        sentences = nuk.split_sentences(text)
        self.assertEqual([part.text for part in sentences], ["Привіт.", "Світ!"])
        for sentence in sentences:
            self.assertEqual(sentence.text, text.encode()[sentence.start : sentence.stop].decode())
        self.assertEqual(nuk.sentenize(text), sentences)
        tokens = nuk.tokenize("Два слова")
        self.assertTrue(tokens)
        for token in tokens:
            self.assertEqual(token.text, "Два слова".encode()[token.start : token.stop].decode())

    def test_markup_is_preserved(self) -> None:
        self.assertEqual(
            nuk.normalize_ukrainian("<speak>5 кг</speak>", self.options),
            "<speak>п'ять кілограмів</speak>",
        )
        self.assertEqual(
            nuk.normalize_ukrainian("[5 кг](https://example.com?a=1&amp;b=2)", self.options),
            "[п'ять кілограмів](https://example.com?a=1&amp;b=2)",
        )


if __name__ == "__main__":
    unittest.main()
