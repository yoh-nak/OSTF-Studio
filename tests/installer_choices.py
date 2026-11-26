"""Check both native installers against the stock VST3 catalogue."""
import plistlib
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "packaging"))
from macos_package import distribution
from plugin_choices import GROUPS, catalog, windows_files


class InstallerChoices(unittest.TestCase):
    def check_choices(self, plugins):
        version = "1.0.0"
        display_version = "1.0.0-public-alpha.1-dev"
        mac = distribution(plugins, version, display_version).getroot()
        self.assertEqual(mac.findtext("title"), "OSTF Studio " + display_version)
        self.assertEqual(mac.find("options").get("customize"), "always")
        self.assertEqual(mac.find("choice[@id='application']").get("enabled"), "false")
        self.assertEqual(mac.find("choice[@id='application']").get("selected"), "true")
        groups = {p["group"] for p in plugins}
        self.assertEqual({line.get("choice") for line in mac.findall("choices-outline/line")},
                         {"application"} | groups)
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            windows_files(directory, plugins)
            def lines(name):
                return (directory / (name + ".iss")).read_text(encoding="utf-8-sig").strip().splitlines()
            components = lines("components")
            self.assertEqual(len(components), len(plugins) + len(groups))
            selections, payloads, helper = lines("selection"), lines("files"), lines("helper")
            self.assertEqual(len(selections), len(plugins))
            self.assertEqual(len(payloads), len(plugins))
            self.assertEqual(len(helper), len(plugins))
            for p in plugins:
                code, group, name = p["code"], p["group"], p["name"]
                component = group + "\\" + code
                label = name if name in {"OSTF Studio", "OSTF Formant Singer", "OSTF Scale Editor"} else name.removeprefix("OSTF ")
                with self.subTest(code=code):
                    self.assertIn(f'Name: "{component}"; Description: "{label}"; Types: full', components)
                    self.assertIn(f'Name: "{code}"; Description: "{label}"; Types: full', helper)
                    self.assertTrue(any(f"Selected('{component}')" in line and f"',{code}'" in line
                                        for line in selections))
                    self.assertTrue(any(f'{name}.vst3"; Components: {code};' in line for line in payloads))
                    self.assertIsNotNone(mac.find(f"choices-outline/line[@choice='{group}']/line[@choice='{code}']"))
                    choice = mac.find(f"choice[@id='{code}']")
                    self.assertEqual(choice.get("title"), label)
                    self.assertEqual(choice.get("start_selected"), "true")
                    self.assertIsNone(choice.get("selected"))
                    receipt = choice.find("pkg-ref").get("id")
                    package = mac.find(f"pkg-ref[@id='{receipt}']")
                    self.assertEqual(package.text, code + ".pkg")
                    self.assertEqual(package.get("version"), version)

    def test_stock_choices(self):
        plugins = catalog()
        groups = {p["name"]: p["group"] for p in plugins}
        self.assertEqual(groups["OSTF GM Sound Module"], "instruments")
        self.assertEqual(groups["OSTF Reverb"], "effects")
        self.assertEqual(groups["OSTF Scale Editor"], "composition")
        self.assertEqual(groups["OSTF Studio"], "composition")
        self.assertEqual(groups["OSTF Wave Analyzer"], "utilities")
        self.assertEqual(groups["OSTF Stem Separator"], "utilities")
        self.check_choices(plugins)

    def test_empty_and_populated_categories(self):
        self.check_choices([])
        self.check_choices([{"code": f"T{i:03}", "name": group, "group": group}
                            for i, group in enumerate(GROUPS)])


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--mac-selection":
        changes = [{"choiceIdentifier": p["code"], "choiceAttribute": "selected",
                    "attributeSetting": int(p["name"] == "OSTF Scale Editor")} for p in catalog()]
        Path(sys.argv[2]).write_bytes(plistlib.dumps(changes))
    else:
        unittest.main()
