"""Compile and run the NURBS CPU checks: python3 main.py."""

from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

from scripts.compile_in_memory import main as compile_shader

def main() -> int:
    root = Path(__file__).resolve().parent
    options = [arg for arg in sys.argv[1:] if arg != "--nurbs"]
    if options == ["--build"]:
        build_directory = root / "build"
        build_directory.mkdir(exist_ok=True)
        return build(root, build_directory / "nurbs_cpu_demo")

    # Preserve compiler-launcher usage for callers supplying compiler arguments.
    if options:
        return compile_shader()

    with tempfile.TemporaryDirectory(prefix="nurbs-demo-") as directory:
        executable = Path(directory) / "nurbs_cpu_demo"
        result = build(root, executable)
        if result:
            return result
        return subprocess.run([str(executable)]).returncode


def build(root: Path, executable: Path) -> int:
    source = root / "nurbs_test.cpp"
    shader = root / "nurbs.vert"
    command = [
            sys.executable, str(root / "scripts/compile_in_memory.py"),
            str(source), str(shader),
            *shlex.split(os.environ.get("CXX", "g++")),
            "-std=c++17", "-O0", "-g3", str(source),
            "-o", str(executable),
    ]
    return subprocess.run(command).returncode


if __name__ == "__main__":
    sys.exit(main())
