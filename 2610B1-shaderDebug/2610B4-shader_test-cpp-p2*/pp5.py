"""Run nurbs.vert from Python, without a C++ main file.

Run: python3 mem/pp5.py
Debug shader: select 'pp5: Python -> nurbs.vert (GDB)' in VS Code and press F5.
The shader is translated to CPU C++, compiled through stdin, and loaded with ctypes.
"""

import argparse
import ctypes
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
from runpy import run_path

_shader_helpers = run_path(str(ROOT / "nurbs.vert.py"))
line_directive = _shader_helpers["line_directive"]
translate_shader = _shader_helpers["translate_shader"]


def load_shader(shader_path: Path, library_path: Path) -> ctypes.CDLL:
    # Preserve original shader locations for GDB. No native wrapper functions.
    source = ('#include "shader.h"\n'
              + line_directive(1, shader_path)
              + translate_shader(shader_path))
    compiler = shlex.split(os.environ.get('CXX', 'g++'))
    if not compiler:
        raise ValueError('CXX must specify a compiler')
    print(f'Building {shader_path.name} in memory', flush=True)
    subprocess.run([*compiler, '-std=c++17', '-O0', '-g3', '-shared', '-fPIC',
                    '-I', str(ROOT), '-I', str(Path(__file__).resolve().parent / 'mem' / 'extension'), '-x', 'c++', '-', '-o', str(library_path)],
                   input=source, text=True, check=True)
    library = ctypes.CDLL(str(library_path))
    # Linux GCC/Clang C++ symbol for void shader(). No extern "C" needed.
    library.shader = getattr(library, '_Z6shaderv')
    library.shader.argtypes = []
    library.shader.restype = None
    return library


class Vec4(ctypes.Structure):
    _fields_ = [(name, ctypes.c_float) for name in ('x', 'y', 'z', 'w')]


class Surface(ctypes.Structure):
    _fields_ = [('control', Vec4 * 100), ('inputPoint', Vec4 * 100)]


class View(ctypes.Structure):
    # Match shader.h's column-major matrix and translated View struct.
    _fields_ = [('mvp', ctypes.c_float * 16), ('mode', ctypes.c_int)]


def run(library: ctypes.CDLL) -> None:
    # These objects refer directly to the compiled shader's global memory.
    surface = Surface.in_dll(library, 'surface')
    view = View.in_dll(library, 'view')
    gl_VertexIndex = ctypes.c_int.in_dll(library, 'gl_VertexIndex')
    gl_Position = Vec4.in_dll(library, 'gl_Position')
    shader = library.shader

    # Write your inputs and shader calls entirely in Python.
    for index in range(100):
        surface.control[index] = Vec4(2, 3, 4, index % 7 + 1)
    for index in range(16):
        view.mvp[index] = 1 if index % 5 == 0 else 0
    view.mode = 0

    print('Running nurbs.vert from Python', flush=True)
    for vertex in range(40000):
        gl_VertexIndex.value = vertex
        shader()
        position = (gl_Position.x, gl_Position.y, gl_Position.z, gl_Position.w)
        if any(abs(actual - expected) > 0.0001
               for actual, expected in zip(position, (2, 3, 4, 1))):
            raise RuntimeError(f'Invalid position at vertex {vertex}: {position}')
    print(f'Passed: 40000 surface vertices; gl_Position = {position}', flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('shader', type=Path, nargs='?', default=ROOT / 'nurbs.vert',
                        help='NURBS shader path (default: project nurbs.vert)')
    args = parser.parse_args()
    try:
        with tempfile.TemporaryDirectory(prefix='pp5-shader-') as directory:
            library = load_shader(args.shader.resolve(), Path(directory) / 'nurbs.so')
            run(library)
        return 0
    except subprocess.CalledProcessError as error:
        return error.returncode
    except (OSError, ValueError, RuntimeError) as error:
        print(f'pp5.py: {error}', file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130


if __name__ == '__main__':
    sys.exit(main())
