"""Compile and run the CPU shader demo: python3 main.py."""

from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

from scripts.compile_in_memory import main as compile_shader

def main() -> int:
    root = Path(__file__).resolve().parent
    nurbs = "--nurbs" in sys.argv[1:]
    options = [arg for arg in sys.argv[1:] if arg != "--nurbs"]
    if options == ["--build"]:
        build_directory = root / "build"
        build_directory.mkdir(exist_ok=True)
        return build(root, build_directory / ("nurbs_cpu_demo" if nurbs else "shader_cpp_demo"), nurbs)

    # Preserve compiler-launcher usage for callers supplying compiler arguments.
    if options:
        return compile_shader()

    with tempfile.TemporaryDirectory(prefix="shader-demo-") as directory:
        executable = Path(directory) / "shader_cpp_demo"
        result = build(root, executable, nurbs)
        if result:
            return result
        return subprocess.run([str(executable)]).returncode


def build(root: Path, executable: Path, nurbs: bool = False) -> int:
    source = root / ("nurbs_test.cpp" if nurbs else "main.cpp")
    shader = root / ("nurbs.vert" if nurbs else "example.vert")
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
