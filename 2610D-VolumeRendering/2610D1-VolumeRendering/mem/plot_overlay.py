from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.collections import LineCollection
import numpy as np

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

output = Path(__file__).resolve().parent / 'figure' if '__file__' in globals() else Path('mem/figure')
output.mkdir(parents=True, exist_ok=True)
filename = output / f'overlay-mode-{mode}.png'
fig.savefig(filename, dpi=160, facecolor=fig.get_facecolor())
print(f'Saved plot: {filename}')
plt.show()
