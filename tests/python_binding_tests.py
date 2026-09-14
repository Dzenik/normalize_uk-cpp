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


if __name__ == "__main__":
    unittest.main()
