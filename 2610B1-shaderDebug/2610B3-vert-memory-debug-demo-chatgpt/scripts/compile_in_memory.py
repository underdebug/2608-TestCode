"""CMake compiler launcher: expand one shader include in memory, preserving locations."""

from pathlib import Path
import re
import subprocess
import sys


def line_directive(number: int, path: Path) -> str:
    filename = str(path).replace('\\', '\\\\').replace('"', '\\"')
    return f'#line {number} "{filename}"\n'


def main() -> int:
    main_path = Path(sys.argv[1]).resolve()
    shader_path = Path(sys.argv[2]).resolve()
    command = sys.argv[3:]
    shader = shader_path.read_text(encoding="utf-8")
    shader, entry_points = re.subn(r"\bvoid\s+main\s*\(\s*\)", "void shader()", shader)
    if entry_points != 1:
        raise RuntimeError("Expected exactly one void main() entry point in the shader")
    shader = re.sub(r"^([ \t]*)#version\b", r"\1//version", shader, flags=re.MULTILINE)

    # Convert this demo's GLSL input qualifiers without changing line numbers.
    shader = re.sub(
        r"\blayout\s*\(\s*location\s*=\s*\d+\s*\)\s*in\s+",
        "", shader,
    )

    parts = [line_directive(1, main_path)]
    replacements = 0
    for number, line in enumerate(main_path.read_text(encoding="utf-8").splitlines(keepends=True), 1):
        if re.fullmatch(r'\s*#\s*include\s*"shaders/example\.vert"\s*', line):
            parts.extend([line_directive(1, shader_path), shader])
            if not shader.endswith("\n"):
                parts.append("\n")
            parts.append(line_directive(number + 1, main_path))
            replacements += 1
        else:
            parts.append(line)
    if replacements != 1:
        raise RuntimeError('Expected exactly one #include "shaders/example.vert" in main.cpp')

    # Keep CMake's flags, object output and dependency-generation arguments.
    source_index = next(
        index for index, arg in enumerate(command[1:], 1)
        if not arg.startswith("-") and Path(arg).resolve() == main_path
    )
    command[source_index:source_index + 1] = ["-x", "c++", "-", "-x", "none"]
    command.extend(["-iquote", str(main_path.parent)])
    return subprocess.run(command, input="".join(parts), encoding="utf-8").returncode


if __name__ == "__main__":
    sys.exit(main())
