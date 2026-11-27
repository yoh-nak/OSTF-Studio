"""Check both native installers against the stock VST3 catalogue."""
import plistlib
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "packaging"))
from macos_package import distribution
from plugin_choices import GROUPS, catalog, daw_demo_plugins, windows_files


class InstallerChoices(unittest.TestCase):
    def check_choices(self, plugins):
        version = "1.0.0"
        display_version = "1.0.0-public-alpha.1-dev"
        mac = distribution(plugins, version, display_version).getroot()
        self.assertEqual(mac.findtext("title"), "OSTF Studio " + display_version)
        self.assertEqual(mac.find("options").get("customize"), "always")
        app = mac.find("choice[@id='application']")
        self.assertEqual(app.get("start_selected"), "true")
        required = [p for p in plugins if p.get("requiresStudio")]
        expression = " || ".join(f"choices['{p['code']}'].selected" for p in required)
        self.assertEqual(app.get("enabled"), f"!({expression})" if required else None)
        self.assertEqual(app.get("selected"), f"my.choice.selected || {expression}" if required else None)
        groups = {p["group"] for p in plugins}
        self.assertEqual({line.get("choice") for line in mac.findall("choices-outline/line")},
                         {"application"} | groups)
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            windows_files(directory, plugins)
            def lines(name):
                return (directory / (name + ".iss")).read_text(encoding="utf-8-sig").strip().splitlines()
            components = lines("components")
            dep = " or ".join(p["group"] + "\\" + p["code"] for p in required)
            self.assertEqual(lines("studio-dependency"),
                             [f"Result := WizardIsComponentSelected('{dep}');" if dep else "Result := False;"])
            self.assertEqual(len(components), len(plugins) + len(groups))
            selections, payloads, helper = lines("selection"), lines("files"), lines("helper")
            self.assertEqual(len(selections), len(plugins))
            self.assertEqual(len(payloads), len(plugins))
            self.assertEqual(len(helper), len(plugins))
            for p in plugins:
                code, group, name = p["code"], p["group"], p["name"]
                component = group + "\\" + code
                label = name if name in {"OSTF Formant Singer", "OSTF Scale Editor"} else name.removeprefix("OSTF ")
                if code == "Osd1":
                    label = "OSTF Studio (VST3)"
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
        for p in plugins:
            if p["code"] in {"Osd1", "Ose1", "Osg1", "Ogm1"}:
                self.assertFalse(p.get("requiresStudio", False))
        self.check_choices(plugins)

    def test_mixed_dependencies(self):
        self.check_choices(wizard_fixture())

    def test_daw_demo_preset(self):
        plugins = catalog()
        selected = daw_demo_plugins(plugins)
        self.assertTrue({"Osd1", "Ogm1", "Osg1", "Ofm1", "Owt1", "Of01"} <= selected)
        self.assertNotIn("Ose1", selected)
        with tempfile.TemporaryDirectory() as folder:
            stage = Path(folder)
            windows_files(stage, plugins, selected)
            lines = (stage / "components.iss").read_text(encoding="utf-8-sig").splitlines()
            components = {line.split('"')[1] for line in lines if line.endswith("Types: full daw")}
            self.assertEqual(components, {path for p in plugins if p["code"] in selected
                                          for path in (p["group"], p["group"] + "\\" + p["code"])})

    def test_empty_and_populated_categories(self):
        self.check_choices([])
        self.check_choices([{"code": f"T{i:03}", "name": group, "group": group}
                            for i, group in enumerate(GROUPS)])


def wizard_fixture():
    return [{"code": "Fx01", "name": "Independent FX", "group": "effects"},
            {"code": "Tx01", "name": "Dependent FX", "group": "effects", "requiresStudio": True},
            {"code": "Tx02", "name": "Dependent tool", "group": "utilities", "requiresStudio": True}]


def windows_wizard(iscc):
    tests = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory() as folder:
        stage = Path(folder)
        windows_files(stage, wizard_fixture(), {"Fx01"})
        subprocess.run([iscc, "/Q", f"/O{stage}", f"/DStage={stage}",
                        f"/DChoiceScript={tests.parent / 'packaging/windows/StudioChoice.iss'}",
                        str(tests / "installer_wizard_smoke.iss")], check=True)
        result = stage / "result.txt"
        subprocess.run([str(stage / "wizard-test.exe"), "/VERYSILENT", "/SP-", "/SUPPRESSMSGBOXES",
                        "/TYPE=custom", r"/COMPONENTS=effects\Tx01", f"/RESULT={result}"],
                       check=True, timeout=60)
        if result.read_text() != "ok":
            raise RuntimeError(result.read_text())


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--mac-selection":
        changes = [{"choiceIdentifier": p["code"], "choiceAttribute": "selected",
                    "attributeSetting": int(p["name"] == "OSTF Scale Editor")} for p in catalog()]
        changes.append({"choiceIdentifier": "application", "choiceAttribute": "selected", "attributeSetting": 0})
        Path(sys.argv[2]).write_bytes(plistlib.dumps(changes))
    elif len(sys.argv) == 3 and sys.argv[1] == "--windows-wizard":
        windows_wizard(sys.argv[2])
    else:
        unittest.main()
