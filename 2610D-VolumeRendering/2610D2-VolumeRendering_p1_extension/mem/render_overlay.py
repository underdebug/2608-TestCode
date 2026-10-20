"""Render captured overlay draw inputs on a black PNG using the GLSL source."""
from pathlib import Path
import importlib.util
import json
import math
import re
import struct
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent


def build_replay(directory):
    spec = importlib.util.spec_from_file_location('overlay_shader_translation',
                                                ROOT / 'mem/extension/shader.py')
    translator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(translator)
    vertex = translator.translate_shader(ROOT / 'shader/overlay.vert')
    vertex = re.sub(r'\.xy\b(?!\s*\()', '.xy()', vertex)
    fragment = translator.translate_shader(ROOT / 'shader/overlay.frag')
    source = ('#include "shader.h"\n#include <cstdio>\n#include <cstring>\n' + vertex
              + '\nnamespace fragment {\n' + fragment + '\n}\n' + r'''
int main() {
    int count;
    while (std::fread(&count, sizeof(count), 1, stdin) == 1) {
        unsigned char constants[80];
        if (std::fread(constants, sizeof(constants), 1, stdin) != 1) return 1;
        static_assert(sizeof(view) == 76);
        std::memcpy(&view, constants, sizeof(view));
        for (gl_VertexIndex = 0; gl_VertexIndex < count; ++gl_VertexIndex) {
            shader();
            fragment::color = color;
            fragment::shader();
            const float result[] = {gl_Position.x, gl_Position.y,
                gl_Position.z, gl_Position.w, fragment::fragColor.r,
                fragment::fragColor.g, fragment::fragColor.b, fragment::fragColor.a};
            if (std::fwrite(result, sizeof(result), 1, stdout) != 1) return 2;
        }
    }
}
''')
    executable = Path(directory) / 'overlay-replay'
    subprocess.run(['g++', '-std=c++17', '-O2', '-x', 'c++', '-',
                    '-I', str(ROOT / 'shader'), '-o', str(executable)],
                   input=source, text=True, check=True, capture_output=True)
    return executable


def clip_line(a, b):
    # Clip homogeneous coordinates to Vulkan's x/y +/-w and z [0,w].
    low, high = 0.0, 1.0
    for plane in (lambda v: v[3] + v[0], lambda v: v[3] - v[0],
                  lambda v: v[3] + v[1], lambda v: v[3] - v[1],
                  lambda v: v[2], lambda v: v[3] - v[2]):
        fa, fb = plane(a), plane(b)
        if fa < 0 and fb < 0:
            return None
        if fa < 0:
            low = max(low, fa / (fa - fb))
        elif fb < 0:
            high = min(high, fa / (fa - fb))
    if low > high:
        return None
    return ([x + low * (y - x) for x, y in zip(a, b)],
            [x + high * (y - x) for x, y in zip(a, b)])


def render(snapshot, executable, output):
    draws = snapshot['draws']
    width, height = draws[0]['viewport_width'], draws[0]['viewport_height']
    if not (0 < width <= 16384 and 0 < height <= 16384):
        raise ValueError('Invalid overlay image dimensions')
    payload = b''.join(struct.pack('=i16f4i', draw['vertex_count'],
                                  *draw['matrix'], draw['mode'], width, height, 0)
                       for draw in draws)
    result = subprocess.run([str(executable)], input=payload,
                            capture_output=True, check=True)
    expected = sum(draw['vertex_count'] for draw in draws) * 32
    if len(result.stdout) != expected:
        raise ValueError('Incomplete overlay shader output')
    vertices = list(struct.iter_unpack('=8f', result.stdout))
    image = Image.new('RGBA', (width, height), (0, 0, 0, 255))
    painter = ImageDraw.Draw(image)
    for a, b in zip(vertices[::2], vertices[1::2]):
        if not all(math.isfinite(x) for x in (*a, *b)):
            continue
        clipped = clip_line(a, b)
        if clipped is None or any(v[3] <= 1e-8 for v in clipped):
            continue
        points = [((v[0] / v[3] + 1) * width / 2,
                   (v[1] / v[3] + 1) * height / 2) for v in clipped]
        rgba = tuple(round(max(0, min(1, (a[i] + b[i]) / 2)) * 255)
                     for i in range(4, 8))
        painter.line(points, fill=rgba, width=1)
    output = Path(output)
    temporary = output.with_suffix('.png.tmp')
    image.save(temporary, format='PNG')
    temporary.replace(output)


def main():
    source = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'mem/data/cpu-overlay.json'
    output = source.with_suffix('.png')
    with tempfile.TemporaryDirectory(prefix='overlay-replay-') as directory:
        executable = build_replay(directory)
        if '--watch' in sys.argv:
            # Parent GDB monitor sends complete snapshots, one JSON per line.
            for line in sys.stdin:
                render(json.loads(line), executable, output)
        else:
            render(json.loads(source.read_text()), executable, output)
            print(f'Saved overlay image: {output}')


if __name__ == '__main__':
    main()
