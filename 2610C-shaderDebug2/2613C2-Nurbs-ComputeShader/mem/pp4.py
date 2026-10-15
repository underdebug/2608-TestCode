import numpy as np
from pathlib import Path
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Line3DCollection

# Run in the debugger after the shader loop, while outputPositions is in scope.
# Read vec4 storage directly; GLM components are stored in anonymous unions.
count = int(mem('shaderProcessCount'))
P = np.asarray(MEM('outputPositions', 'f', [count, 4]), dtype=np.float32)
x = P[:, 0]
y = P[:, 1]
z = P[:, 2]

# The Vulkan pipeline uses LINE_LIST: each consecutive pair is one segment.
positions = np.column_stack((x, y, z))
segments = positions[:count // 2 * 2].reshape(-1, 2, 3)
segments = segments[np.isfinite(segments).all(axis=(1, 2))]
if not len(segments):
    raise ValueError('No finite output positions to plot; run past the shader loop first.')

fig = plt.figure(figsize=(10, 8))
ax = fig.add_subplot(111, projection='3d')
ax.add_collection3d(Line3DCollection(segments, colors='tab:blue', linewidths=0.6))
points = segments.reshape(-1, 3)
ax.auto_scale_xyz(points[:, 0], points[:, 1], points[:, 2])
ax.set_box_aspect(np.maximum(np.ptp(points, axis=0), 1e-6))
ax.set_xlabel('Clip X')
ax.set_ylabel('Clip Y')
ax.set_zlabel('Clip Z')
ax.set_title(f'Shader output positions ({count:,} vertices)')
fig.tight_layout()
figure_path = Path(mem.__code__.co_filename).resolve().parent / 'figure'
figure_path.mkdir(parents=True, exist_ok=True)
output_path = figure_path / 'outputPositions.png'
fig.savefig(output_path, dpi=200, bbox_inches='tight')
print(f'Saved plot: {output_path}')
plt.show()

