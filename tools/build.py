"""Incremental CMake/Ninja builds. Run from a native compiler environment."""
import argparse
import ctypes
import os
import platform
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GIB = 1024 ** 3


def available_memory():
    if os.name == "nt":
        class Memory(ctypes.Structure):
            _fields_ = [("length", ctypes.c_ulong), ("load", ctypes.c_ulong),
                        ("total", ctypes.c_ulonglong), ("free", ctypes.c_ulonglong),
                        ("remaining", ctypes.c_ulonglong * 5)]
        memory = Memory()
        memory.length = ctypes.sizeof(memory)
        if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(memory)):
            return memory.free
    elif platform.system() == "Darwin":
        import re
        stats = subprocess.check_output(["vm_stat"], text=True)
        size = re.search(r"page size of (\d+) bytes", stats)
        pages = re.findall(r"Pages (?:free|inactive|speculative):\s+(\d+)\.", stats)
        if size and pages:
            return int(size[1]) * sum(map(int, pages))
    elif Path("/proc/meminfo").is_file():
        for line in Path("/proc/meminfo").read_text().splitlines():
            if line.startswith("MemAvailable:"):
                return int(line.split()[1]) * 1024
    return 4 * GIB


def parallel_jobs():
    override = os.getenv("CMAKE_BUILD_PARALLEL_LEVEL")
    if override:
        if not override.isdecimal() or int(override) < 1:
            raise SystemExit("CMAKE_BUILD_PARALLEL_LEVEL must be a positive integer")
        return int(override)
    cpus = len(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else os.cpu_count() or 1
    return max(1, min(cpus, (available_memory() - GIB) // (2 * GIB)))


def package(tree):
    version, display, arch = (tree / "studio-package-version.txt").read_text().splitlines()
    output = tree / "packages"
    output.mkdir(exist_ok=True)
    if os.name == "nt":
        shell = shutil.which("pwsh.exe") or "powershell.exe"
        command = [shell, "-NoProfile", "-ExecutionPolicy", "RemoteSigned", "-File",
                   str(ROOT / "packaging/windows/Package.ps1"), "-AppVersion", display, "-BinaryVersion", version,
                   "-AppExe", str(tree / "OSTFStudio_artefacts/Release/OSTF Studio.exe"),
                   "-GmBundle", str(tree / "OSTFGMSoundModule_artefacts/Release/VST3/OSTF GM Sound Module.vst3"),
                   "-EffectsDir", str(tree / "StudioEffects/Release"), "-OutputDir", str(output), "-Python", sys.executable]
        if compiler := shutil.which("ISCC.exe"):
            command.extend(["-InnoCompiler", compiler])
    else:
        arch = arch or platform.machine()
        command = [sys.executable, "packaging/macos_package.py", version, "--display-version", display,
                   "--arch", arch, "--build", str(tree), "--output", str(output / f"OSTF_Studio_macOS_{arch}.dmg")]
    subprocess.run(command, cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=["dev", "release", "clean"], default="dev")
    parser.add_argument("--core", action="store_true", help="only the dependency-free core and its tests")
    parser.add_argument("--target", action="append", help="build a particular CMake target (repeatable)")
    parser.add_argument("--test", action="store_true", help="run CTest after building all targets")
    parser.add_argument("--build-dir", type=Path, help="persistent directory, or parent of a new clean tree")
    parser.add_argument("--jobs-only", action="store_true", help="print the safe parallel build count for CI")
    parser.add_argument("-D", dest="definitions", action="append", default=[], metavar="NAME=VALUE")
    args = parser.parse_args()
    jobs = parallel_jobs()
    if args.jobs_only:
        print(jobs)
        return
    if args.target and (args.test or args.mode == "release"):
        parser.error("Full testing and release builds require all targets.")
    if args.mode == "release":
        if args.core or platform.system() not in ("Windows", "Darwin"):
            parser.error("Release packaging requires a complete native Windows or macOS build.")
        subprocess.run([sys.executable, "packaging/prepare_fluid_r3.py"], cwd=ROOT, check=True)
    profile = "core" if args.core else "studio"
    tree = args.build_dir or ROOT / f"build-{args.mode}-{profile}"
    if args.mode == "clean":
        tree.mkdir(parents=True, exist_ok=True)
        tree = Path(tempfile.mkdtemp(prefix="clean-", dir=tree))
    tree = tree.resolve()
    env = os.environ.copy()
    env["CMAKE_BUILD_PARALLEL_LEVEL"] = str(jobs)
    configure = ["cmake", "-S", str(ROOT), "-B", str(tree), "-G", "Ninja",
                 "-DCMAKE_BUILD_TYPE=Release", f"-DOSTF_STUDIO_BUILD_GUI={'OFF' if args.core else 'ON'}"]
    cache = shutil.which("sccache") or shutil.which("ccache")
    if cache and not (tree / "CMakeCache.txt").exists():
        configure.extend(f"-DCMAKE_{lang}_COMPILER_LAUNCHER={cache}" for lang in ("C", "CXX", "OBJCXX"))
    configure.extend("-D" + definition for definition in args.definitions)
    build = ["cmake", "--build", str(tree), "--parallel", str(jobs)]
    if args.target:
        build.extend(["--target", *args.target])
    print(f"{tree} ({jobs} parallel jobs)", flush=True)
    cache_file = tree / "CMakeCache.txt"
    existing = {}
    if cache_file.is_file():
        for line in cache_file.read_text(encoding="utf-8", errors="replace").splitlines():
            key, separator, value = line.partition("=")
            if separator:
                existing[key.split(":", 1)[0]] = value
        if (existing.get("CMAKE_GENERATOR") != "Ninja" or
                Path(existing.get("CMAKE_HOME_DIRECTORY", "")).resolve() != ROOT):
            parser.error("Choose a separate Ninja build directory for this checkout.")
    if (args.definitions or not (tree / "build.ninja").exists() or
            existing.get("OSTF_STUDIO_BUILD_GUI") != ("OFF" if args.core else "ON") or
            existing.get("CMAKE_BUILD_TYPE") != "Release"):
        subprocess.run(configure, cwd=ROOT, env=env, check=True)
    # Ninja runs CMake automatically when its tracked configuration inputs change.
    subprocess.run(build, cwd=ROOT, env=env, check=True)
    if args.test or args.mode == "release":
        subprocess.run(["ctest", "--test-dir", str(tree), "-C", "Release", "--output-on-failure",
                        "--no-tests=error"], cwd=ROOT, env=env, check=True)

    if args.mode == "release":
        package(tree)


if __name__ == "__main__":
    main()
