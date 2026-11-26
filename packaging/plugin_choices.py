"""Build the installer choices from the catalogues used to compile the VST3s."""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GROUPS = {
    "instruments": ("Instruments", "インストゥルメント"),
    "composition": ("Composition tools", "作曲支援"),
    "utilities": ("Utilities", "ユーティリティー"),
    "learning": ("Learning", "学習"),
    "games": ("Games", "ゲーム"),
    "effects": ("Effects", "エフェクト"),
}


def display_name(plugin):
    if plugin["code"] in {"Osd1", "Ose1", "Osg1"}:
        return plugin["name"]
    return plugin["name"].removeprefix("OSTF ")


def catalog():
    plugins = [{"code": "Ogm1", "name": "OSTF GM Sound Module", "group": "instruments"},
               {"code": "Osd1", "name": "OSTF Studio", "group": "composition"}]
    for group in ("effects", "instruments"):
        path = ROOT / "src/plugins" / group / "catalog.json"
        for item in json.loads(path.read_text(encoding="utf-8")):
            plugins.append({"code": item["code"], "name": item["name"],
                            "group": item.get("installerGroup", group)})
    for p in plugins:
        if p["group"] not in GROUPS or not re.fullmatch("[A-Za-z0-9]{4}", p["code"]):
            raise ValueError(f"Invalid installer choice: {p}")
    for key in ("code", "name"):
        if len({p[key].casefold() for p in plugins}) != len(plugins):
            raise ValueError(f"Duplicate plug-in {key}")
    return sorted(plugins, key=lambda p: p["name"].casefold())


def windows_files(stage, plugins):
    components, flat, files, selection, labels = [], [], [], [], []
    for group, (english, japanese) in GROUPS.items():
        children = [p for p in plugins if p["group"] == group]
        if not children:
            continue
        labels.extend([f"en.group{group}={english}", f"ja.group{group}={japanese}"])
        components.append(f'Name: "{group}"; Description: "{{cm:group{group}}}"; Types: full')
        for p in children:
            code, name = p["code"], p["name"]
            component = group + "\\" + code
            label = display_name(p)
            components.append(f'Name: "{component}"; Description: "{label}"; Types: full')
            flat.append(f'Name: "{code}"; Description: "{label}"; Types: full')
            bundle = name + ".vst3"
            source = "{#GmBundle}" if code == "Ogm1" else "{#EffectsDir}\\" + bundle
            files.append(f'Source: "{source}\\*"; DestDir: "{{commoncf64}}\\VST3\\OSTF Studio\\{bundle}"; '
                         f'Components: {code}; Flags: replacesameversion recursesubdirs createallsubdirs')
            selection.append(f"if WizardIsComponentSelected('{component}') then Result := Result + ',{code}';")
    for name, lines in {"components": components, "helper": flat, "files": files,
                        "selection": selection, "groups": labels}.items():
        (stage / (name + ".iss")).write_text("\n".join(lines) + "\n", encoding="utf-8-sig")


if __name__ == "__main__":
    directory = Path(sys.argv[1])
    plugins = catalog()
    windows_files(directory, plugins)
    (directory / "plugins.json").write_text(json.dumps(plugins), encoding="ascii")
