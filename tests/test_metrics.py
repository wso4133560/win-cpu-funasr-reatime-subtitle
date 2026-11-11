import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('wer', Path(__file__).parents[1] / 'scripts/wer.py')
wer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wer)


class ErrorRateTests(unittest.TestCase):
    def test_chinese_characters_are_measured(self):
        self.assertTrue(hasattr(wer, 'measure_cer'), 'Chinese CER is missing')
        result = wer.measure_cer('欢迎大家。', '欢应大家！')
        self.assertEqual(result['reference_characters'], 4)
        self.assertEqual(result['substitutions'], 1)
        self.assertEqual(result['cer'], 0.25)

    def test_cer_counts_missing_and_added_characters(self):
        self.assertTrue(hasattr(wer, 'measure_cer'), 'Chinese CER is missing')
        self.assertEqual(wer.measure_cer('甲乙丙', '甲丙')['deletions'], 1)
        self.assertEqual(wer.measure_cer('甲乙', '甲乙丙')['insertions'], 1)
        self.assertEqual(wer.measure_cer('你好Ａ１。', '你好a1')['cer'], 0)
        with self.assertRaises(ValueError):
            wer.measure_cer('，。', '你好')

    def test_existing_english_normalization_is_preserved(self):
        self.assertEqual(wer.measure("Don't ask!", "don't ask")['wer'], 0)
        self.assertEqual(wer.measure('ask not', 'ask')['deletions'], 1)


if __name__ == '__main__':
    unittest.main()
