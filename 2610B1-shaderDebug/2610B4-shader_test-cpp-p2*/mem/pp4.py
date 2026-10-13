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

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from runpy import run_path

_shader_helpers = run_path(str(ROOT / "nurbs.vert.py"))
line_directive = _shader_helpers["line_directive"]
translate_shader = _shader_helpers["translate_shader"]


def load_shader(shader_path: Path, library_path: Path) -> ctypes.CDLL:
    # This small native interface has no main(). Shader source locations are
    # preserved so GDB breakpoints refer to the original .vert file.
    wrapper = r'''
extern "C" void set_control(int index, float x, float y, float z, float weight) {
    surface.control[index] = vec4(x, y, z, weight);
}
extern "C" void run_vertex(int index, int mode, float* output) {
    view.mvp = mat4(1);
    view.mode = mode;
    gl_VertexIndex = index;
    shader();
    output[0] = gl_Position.x;
    output[1] = gl_Position.y;
    output[2] = gl_Position.z;
    output[3] = gl_Position.w;
}
'''
    source = ('#include "shader.h"\n'
              + line_directive(1, shader_path)
              + translate_shader(shader_path)
              + '\n#line 1 "pp5_native_wrapper"\n' + wrapper)
    compiler = shlex.split(os.environ.get('CXX', 'g++'))
    if not compiler:
        raise ValueError('CXX must specify a compiler')
    print(f'Building {shader_path.name} in memory', flush=True)
    subprocess.run([*compiler, '-std=c++17', '-O0', '-g3', '-shared', '-fPIC',
                    '-I', str(ROOT), '-I', str(Path(__file__).resolve().parent.parent / 'mem' / 'extension'), '-x', 'c++', '-', '-o', str(library_path)],
                   input=source, text=True, check=True)
    library = ctypes.CDLL(str(library_path))
    library.set_control.argtypes = [ctypes.c_int, *([ctypes.c_float] * 4)]
    library.set_control.restype = None
    library.run_vertex.argtypes = [ctypes.c_int, ctypes.c_int,
                                   ctypes.POINTER(ctypes.c_float)]
    library.run_vertex.restype = None
    return library


def run(library: ctypes.CDLL) -> None:
    # Write your Python input and test code here. Each call enters nurbs.vert.
    for index in range(100):
        library.set_control(index, 2, 3, 4, index % 7 + 1)

    position = (ctypes.c_float * 4)()
    print('Running nurbs.vert from Python', flush=True)
    for vertex in range(40000):
        library.run_vertex(vertex, 0, position)
        if any(abs(actual - expected) > 0.0001
               for actual, expected in zip(position, (2, 3, 4, 1))):
            raise RuntimeError(f'Invalid position at vertex {vertex}: {list(position)}')
    print(f'Passed: 40000 surface vertices; gl_Position = {list(position)}', flush=True)


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
