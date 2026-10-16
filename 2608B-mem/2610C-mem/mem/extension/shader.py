# MIT License

# Copyright (c) 2026 Mingjie Zhang (mingjie2026@gmail.com)

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

from pathlib import Path
import os
import re
import shutil
import shlex
import subprocess
import sys

def line_directive(number: int, path: Path):
    # line_directive(1, Path("shader/nurbs.comp")) -> #line 1 "shader/nurbs.comp"
    filename = str(path).replace('\\', '\\\\').replace('"', '\\"')
    return f'#line {number} "{filename}"\n'

def translate_shader(shader_path: Path):
    shader = shader_path.read_text(encoding="utf-8")
    shader, entry_points = re.subn(r"\bvoid\s+(?:main|shader)\s*\(\s*\)", "void shader()", shader)
    if entry_points != 1:
        raise RuntimeError("Expected exactly one void main() or void shader() entry point in the shader")
 
    shader = re.sub(r"^([ \t]*)#version\b", r"\1//version", shader, flags=re.MULTILINE)

    # const float arr[5] = float[](1.1, 1.2, 1.3, 1.4, 1.5); 
    # => const float arr[5] = {1.1, 1.2, 1.3, 1.4, 1.5};
    shader = re.sub(r"\bfloat\s*\[\s*\]\s*\(([^()]*)\)", r"{\1}", shader)
    
    # vec4 position[]; => vec4* position = NULL;
    shader = re.sub(r"\b(\w+)\s+(\w+)\s*\[\s*\]\s*;", r"\1* \2 = NULL;", shader)
    return shader


def compile_in_memory(main_path: Path, shader_path: Path, command: list[str]):
    main_path = main_path.resolve()
    shader_path = shader_path.resolve()
    command = list(command)
    shader = translate_shader(shader_path)

    parts = [line_directive(1, main_path)]
    replacements = 0
    for number, line in enumerate(main_path.read_text(encoding="utf-8").splitlines(keepends=True), 1):
        include = re.fullmatch(r'\s*#\s*include\s*"([^"\n]+)"\s*(?://[^\n]*)?\s*', line)
        if include and (main_path.parent / include.group(1)).resolve() == shader_path:
            parts.extend([line_directive(1, shader_path), shader])
            if not shader.endswith("\n"):
                parts.append("\n")
            parts.append(line_directive(number + 1, main_path))
            replacements += 1
        else:
            parts.append(line)
    if replacements != 1:
        raise RuntimeError(f'Expected exactly one shader include in {main_path}')

    # Keep CMake's flags, object output and dependency-generation arguments.
    source_index = next(
        index for index, arg in enumerate(command[1:], 1)
        if not arg.startswith("-") and Path(arg).resolve() == main_path
    )
    command[source_index:source_index + 1] = ["-x", "c++", "-", "-x", "none"]
    # Prefer shader-local headers, then the extension's fallback headers.
    command.extend(["-iquote", str(shader_path.parent),
                    "-iquote", str(Path(__file__).resolve().parent),
                    "-iquote", str(main_path.parent)])
    return subprocess.run(command, input="".join(parts), encoding="utf-8").returncode

def find_shader(source: Path):
    """Resolve a shader from quoted includes relative to the C++ source."""
    shader_extensions = {'.vert', '.frag', '.geom', '.tesc', '.tese', '.comp', '.glsl'}
    shaders = []
    # Ignore commented-out includes, retaining newlines for line-based matching.
    context = re.sub(r'/\*.*?\*/|//[^\n]*',
                     lambda match: '\n' * match.group().count('\n'),
                     source.read_text(encoding='utf-8'), flags=re.DOTALL)
    for include in re.finditer(r'^\s*#\s*include\s*"([^"\n]+)"', context, re.MULTILINE):
        shader = (source.parent / include.group(1)).resolve()
        if shader.suffix.lower() in shader_extensions:
            if not shader.is_file():
                raise FileNotFoundError(f'Shader include does not exist: {shader}')
            shaders.append(shader)
    if len(shaders) > 1:
        raise RuntimeError(f'Expected at most one shader include in {source}')
    return shaders[0] if shaders else None


def build(source: Path, executable: Path):
    source = source.resolve()
    executable.parent.mkdir(parents=True, exist_ok=True)
    shader = find_shader(source)
    command = [
            *shlex.split(os.environ.get("CXX", "g++")),
            "-std=c++17", "-O0", "-g3", str(source),
            "-o", str(executable),
    ]
    if shader is not None:
        return compile_in_memory(source, shader, command)
    command.extend(['-iquote', str(Path(__file__).resolve().parent)])
    return subprocess.run(command).returncode


def debugcpp(source: Path, executable: Path):
    """Build the supplied C++ program and debug it interactively in GDB."""
    debugger = shutil.which("gdb")
    if debugger is None:
        print("GDB is required to run debugcpp.", file=sys.stderr)
        return 1
    result = build(source, executable)
    if result:
        return result
    return subprocess.run([
        debugger, "-q",
        "-ex", "set debuginfod enabled off",
        "-ex", "break shader",
        "-ex", "run",
        "--args", str(executable),
    ], cwd=source.parent).returncode


def main():
    import argparse
    parser = argparse.ArgumentParser(description='Build a C++ file using its shader includes.')
    parser.add_argument('source', type=Path)
    parser.add_argument('--output', type=Path)
    action = parser.add_mutually_exclusive_group()
    action.add_argument('--build', action='store_true', help='Build only (the default).')
    action.add_argument('--debugcpp', action='store_true', help='Build and launch GDB.')
    args = parser.parse_args()
    source = args.source.resolve()
    executable = (args.output or source.parent / 'build' / source.name.split('.')[0]).resolve()
    try:
        if args.debugcpp:
            return debugcpp(source, executable)
        return build(source, executable)
    except (OSError, RuntimeError, ValueError) as error:
        print(f'shader.py: {error}', file=sys.stderr)
        return 1

if __name__ == "__main__":
    sys.exit(main())
