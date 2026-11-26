import json
from fractions import Fraction
from pathlib import Path
import tempfile
import unittest

from check_coverage import compare, read_summary


class CoverageCheckTest(unittest.TestCase):
    def summary(self, line="99", branch="95", function="99"):
        return {key: Fraction(value) / 100 for key, value in
                zip(("line", "branch", "function"), (line, branch, function))}

    def test_equal_ratios_and_improvements_pass(self):
        base = self.summary()
        self.assertFalse(compare(base, base)[1])
        self.assertFalse(compare(self.summary("99.1", "95.1", "100"), base)[1])

    def test_tiny_drop_fails_even_when_display_rounds_the_same(self):
        for metric in ("line", "branch", "function"):
            base = self.summary()
            head = dict(base)
            head[metric] -= Fraction(1, 100000000)
            self.assertIn(f"{metric} coverage decreased from the base commit", compare(head, base)[1])

    def test_floors_apply_without_a_base_and_even_if_base_is_worse(self):
        head = self.summary("97.7", "93.2")
        self.assertEqual(len(compare(head)[1]), 2)
        self.assertEqual(len(compare(head, self.summary("90", "90"))[1]), 2)
        self.assertFalse(compare(self.summary("97.8", "93.3"))[1])

    def test_reads_counts_without_using_rounded_percentages(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "summary.json"
            data = {f"{metric}_{suffix}": value
                    for metric in ("line", "branch", "function")
                    for suffix, value in (("total", 101), ("covered", 100), ("percent", 99.0))}
            path.write_text(json.dumps(data))
            self.assertEqual(read_summary(path)["line"], Fraction(100, 101))
            for invalid in (0, -1, "100", True):
                data["line_total"] = invalid
                path.write_text(json.dumps(data))
                with self.assertRaises(ValueError):
                    read_summary(path)
            data["line_total"] = 99
            path.write_text(json.dumps(data))
            with self.assertRaises(ValueError):
                read_summary(path)


if __name__ == "__main__":
    unittest.main()
