"""Compile and run the NURBS CPU checks: python3 nurbs.vert.py."""

from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

from runpy import run_path

compile_in_memory = run_path(str(Path(__file__).resolve().parent / "nurbs.vert.py"))["compile_in_memory"]

def main() -> int:
    root = Path(__file__).resolve().parent
    options = [arg for arg in sys.argv[1:] if arg != "--nurbs"]
    if options == ["--build"]:
        build_directory = root / "build"
        build_directory.mkdir(exist_ok=True)
        return build(root, build_directory / "nurbs_cpu_demo")

    # Preserve compiler-launcher usage for callers supplying compiler arguments.
    if options:
        return compile_in_memory(Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3:])

    with tempfile.TemporaryDirectory(prefix="nurbs-demo-") as directory:
        executable = Path(directory) / "nurbs_cpu_demo"
        result = build(root, executable)
        if result:
            return result
        return subprocess.run([str(executable)]).returncode


def build(root: Path, executable: Path) -> int:
    source = root / "nurbs.vert.cpp"
    shader = root / "nurbs.vert"
    command = [
            *shlex.split(os.environ.get("CXX", "g++")),
            "-std=c++17", "-O0", "-g3", str(source),
            "-o", str(executable),
    ]
    return compile_in_memory(source, shader, command)


if __name__ == "__main__":
    sys.exit(main())
