from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

position = mem('points.position', None, [43984])
if position is None or len(position) == 0:
    raise ValueError('Cannot read points.position; pause after the shader loop.')

xyz = np.array([[p['x'], p['y'], p['z']] for p in position], dtype=float)
xyz = xyz[np.isfinite(xyz).all(axis=1)]
if not len(xyz):
    raise ValueError('No finite positions to plot.')

fig = plt.figure(figsize=(10, 8))
ax = fig.add_subplot(111, projection='3d')
ax.scatter(xyz[:, 0], xyz[:, 1], xyz[:, 2], s=2, marker='.', color='tab:blue')
ax.set_xlabel('X')
ax.set_ylabel('Y')
ax.set_zlabel('Z')
ax.set_title(f'Position ({len(xyz):,} points)')
ax.set_box_aspect(np.maximum(np.ptp(xyz, axis=0), 1e-6))
fig.tight_layout()

figure_dir = Path(mem.__code__.co_filename).resolve().parent / 'figure'
figure_dir.mkdir(parents=True, exist_ok=True)
fig.savefig(figure_dir / 'position.png', dpi=200, bbox_inches='tight')
plt.show()
