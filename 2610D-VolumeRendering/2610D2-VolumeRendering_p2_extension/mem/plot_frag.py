from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.collections import LineCollection
import numpy as np

import gdb


def plot_volume():
    width = int(mem('viewportWidth'))
    height = int(mem('viewportHeight'))
    completed = int(mem('renderedPixels'))
    if width <= 0 or height <= 0:
        raise ValueError('Invalid viewport dimensions.')
    start = gdb.parse_and_eval('framebuffer._M_impl._M_start')
    finish = gdb.parse_and_eval('framebuffer._M_impl._M_finish')
    if int(finish - start) != width * height:
        raise ValueError('Pause after framebuffer.assign(), or at frameReady().')

    rgba = np.asarray(mem('framebuffer', 'float', [height, width, 4]))
    rgb = np.clip(rgba[..., :3], 0, 1)
    dpi = 100
    fig = plt.figure(figsize=(width / dpi, height / dpi), dpi=dpi)
    ax = fig.add_axes([0, 0, 1, 1])
    ax.imshow(rgb, origin='upper', interpolation='nearest')
    ax.set_axis_off()
    output = Path(mem.__globals__['PATH']) / 'figure'
    output.mkdir(parents=True, exist_ok=True)
    filename = output / 'volume-framebuffer.png'
    # Save the actual framebuffer, without figure margins or resampling.
    plt.imsave(filename, rgb, origin='upper')
    print(f'Saved plot: {filename} ({width} x {height}; '
          f'{completed}/{width * height} pixels completed)')
    if completed < width * height:
        print('Partial frame: the current shader pixel is stored only after shader() returns. '
              'Pause at frameReady() for the complete image.')
    plt.show()


def plot_overlay():
    # Pause at overlay.vert.cpp line 64 (after the vertex loop), then run with F8.
    vertices = mem('outputVertices')
    if vertices is None or len(vertices) == 0:
        raise ValueError('Pause after the vertex loop while outputVertices is in scope.')

    positions = np.array([[v['position'][c] for c in ('x', 'y', 'z', 'w')]
                          for v in vertices], dtype=float)
    colors = np.array([[v['color'][c] for c in ('x', 'y', 'z')]
                       for v in vertices], dtype=float)
    width = int(mem('view.viewportWidth'))
    height = int(mem('view.viewportHeight'))
    mode = int(mem('view.mode'))

    # Vulkan LINE_LIST: every consecutive pair forms a segment.
    pairs = positions.reshape(-1, 2, 4)
    valid = np.isfinite(pairs).all(axis=(1, 2))
    valid &= (np.abs(pairs[:, :, 3]) > 1e-8).all(axis=1)
    valid &= ~np.isclose(pairs, [2, 2, 0, 1]).all(axis=2).any(axis=1)
    ndc = pairs[valid, :, :2] / pairs[valid, :, 3:4]
    pixels = (ndc + 1) * np.array([width, height]) / 2
    line_colors = np.clip(colors.reshape(-1, 2, 3)[valid].mean(axis=1), 0, 1)

    fig, ax = plt.subplots(figsize=(12.8, 7.2), facecolor='#17191f')
    ax.set_facecolor('#17191f')
    ax.add_collection(LineCollection(pixels, colors=line_colors, linewidths=1))
    ax.set_xlim(0, width)
    ax.set_ylim(height, 0)  # Match the Vulkan viewport's downward screen Y.
    ax.set_aspect('equal')
    ax.set_axis_off()
    ax.set_title(f'Overlay mode {mode}: {len(pixels)} line segments', color='white')
    fig.tight_layout()

    output = Path(mem.__globals__['PATH']) / 'figure'
    output.mkdir(parents=True, exist_ok=True)
    filename = output / f'overlay-mode-{mode}.png'
    fig.savefig(filename, dpi=160, facecolor=fig.get_facecolor())
    print(f'Saved plot: {filename}')
    plt.show()


# Read only the paused debug process; no cached image files.
if gdb.lookup_global_symbol('framebuffer') is not None:
    plot_volume()
else:
    plot_overlay()
