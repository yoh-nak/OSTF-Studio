"""Build a native macOS DMG with a customizable Apple Installer distribution."""
import argparse
import plistlib
import subprocess
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

from plugin_choices import GROUPS, ROOT, catalog, display_name


def distribution(plugins, version, display_version=None, arch="arm64"):
    doc = ET.Element("installer-gui-script", minSpecVersion="2")
    ET.SubElement(doc, "title").text = f"OSTF Studio {display_version or version}"
    ET.SubElement(doc, "options", customize="always", hostArchitectures=arch)
    ET.SubElement(doc, "domains", enable_anywhere="false", enable_currentUserHome="false", enable_localSystem="true")
    outline = ET.SubElement(doc, "choices-outline")

    def leaf(parent, code, label):
        ET.SubElement(parent, "line", choice=code)
        choice = ET.SubElement(doc, "choice", id=code, title=label, description=label, start_selected="true")
        receipt = "org.ostf.studio." + code.lower()
        ET.SubElement(choice, "pkg-ref", id=receipt)
        ET.SubElement(doc, "pkg-ref", id=receipt, version=version).text = code + ".pkg"
        return choice

    app = leaf(outline, "application", "OSTF Studio application")
    required = " || ".join(f"choices['{p['code']}'].selected" for p in plugins if p.get("requiresStudio"))
    if required:
        app.set("enabled", f"!({required})")
        app.set("selected", f"my.choice.selected || {required}")
    for group, (title, _) in GROUPS.items():
        children = [p for p in plugins if p["group"] == group]
        if children:
            parent = ET.SubElement(outline, "line", choice=group)
            ET.SubElement(doc, "choice", id=group, title=title, description=title)
            for p in children:
                choice = leaf(parent, p["code"], display_name(p))
                if p.get("requiresStudio"):
                    # A scripted installer can override a disabled checkbox.
                    # Bind the dependency to the payload as well as to the UI.
                    ET.SubElement(choice, "pkg-ref", id=app.find("pkg-ref").get("id"))
    return ET.ElementTree(doc)


def run(*args):
    subprocess.run(args, check=True)


def build_component(bundle, location, code, version, work):
    # Each receipt owns one fixed-location bundle, independently of the other choices.
    with tempfile.TemporaryDirectory(dir=work) as temporary:
        stage = Path(temporary)
        root = stage / "root"
        root.mkdir()
        run("ditto", bundle, root / bundle.name)
        components = stage / "components.plist"
        run("pkgbuild", "--analyze", "--root", root, components)
        entries = plistlib.loads(components.read_bytes())

        def fix_locations(bundles):
            for entry in bundles:
                entry["BundleIsRelocatable"] = False
                fix_locations(entry.get("ChildBundles", []))

        fix_locations(entries)
        components.write_bytes(plistlib.dumps(entries))
        run("pkgbuild", "--root", root, "--component-plist", components, "--install-location", location,
            "--identifier", "org.ostf.studio." + code.lower(), "--version", version, work / (code + ".pkg"))


def build(build_dir, output, version, display_version=None, arch="arm64"):
    plugins = catalog()
    app = build_dir / "OSTFStudio_artefacts/Release/OSTF Studio.app"
    run("lipo", app / "Contents/MacOS/OSTF Studio", "-verify_arch", arch)
    run("/usr/libexec/PlistBuddy", "-c", "Print :NSMicrophoneUsageDescription", app / "Contents/Info.plist")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as temporary:
        work = Path(temporary)
        dmgroot = work / "image"
        dmgroot.mkdir()
        run("ditto", app, dmgroot / app.name)
        (dmgroot / "Applications").symlink_to("/Applications")
        run("ditto", ROOT / "third_party/tinysoundfont/LICENSE.txt", dmgroot / "TinySoundFont-License.txt")
        build_component(app, "/Applications", "application", version, work)
        for p in plugins:
            folder = "OSTFGMSoundModule_artefacts/Release/VST3" if p["code"] == "Ogm1" else "StudioEffects/Release"
            bundle = build_dir / folder / (p["name"] + ".vst3")
            run("lipo", bundle / "Contents/MacOS" / p["name"], "-verify_arch", arch)
            if p["code"] != "Ogm1" and not (bundle / "Contents/Resources/Licenses/GPL-3.0.txt").stat().st_size:
                raise ValueError(f"Missing plug-in license: {bundle}")
            build_component(bundle, "/Library/Audio/Plug-Ins/VST3", p["code"], version, work)
        xml = work / "Distribution.xml"
        distribution(plugins, version, display_version, arch).write(xml, encoding="utf-8", xml_declaration=True)
        resources = work / "Resources"
        for lang, index in (("en", 0), ("ja", 1)):
            folder = resources / (lang + ".lproj")
            folder.mkdir(parents=True)
            strings = [f'"{labels[0]}" = "{labels[index]}";' for labels in GROUPS.values()]
            if lang == "ja":
                strings.append('"OSTF Studio application" = "OSTF Studio 本体";')
            (folder / "Localizable.strings").write_text("\n".join(strings), encoding="utf-16")
        run("productbuild", "--distribution", xml, "--package-path", work, "--resources", resources,
            dmgroot / "Install OSTF Studio and VST3.pkg")
        run("hdiutil", "create", "-volname", "OSTF Studio", "-srcfolder", dmgroot, "-format", "UDZO", output)
        run("hdiutil", "verify", output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version")
    parser.add_argument("--display-version", help="Version shown in Installer, including the prerelease suffix")
    parser.add_argument("--build", type=Path, default=Path("build"))
    parser.add_argument("--arch", choices=("arm64", "x86_64"), default="arm64")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    build(args.build, args.output or Path(f"dist/OSTF_Studio_macOS_{args.arch}.dmg"),
          args.version, args.display_version, args.arch)
