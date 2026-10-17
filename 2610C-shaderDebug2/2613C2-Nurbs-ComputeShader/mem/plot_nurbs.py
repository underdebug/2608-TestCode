from pathlib import Path

import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Line3DCollection
import numpy as np

# Pause in shader/nurbs.comp.cpp after all three calculation phases, before
# return 0, then run this file with F8. The debugger provides mem().
# if 'mem' not in globals():
#     raise RuntimeError('Run this file with F8 in the debugger after the compute loops.')


def read_scalar(expression):
    value = mem(expression)
    if value is None:
        raise ValueError(f'{expression} is unavailable; select the compute main() frame.')
    return int(value)


def read_points(expression, count):
    # Read packed vec4 floats directly to avoid GLM anonymous-union fields.
    value = mem(expression, 'f', [count, 4])
    if value is None:
        raise ValueError(f'Cannot read {expression}; pause after the compute loops.')
    value = np.asarray(value, dtype=np.float32).reshape(count, 4)
    if not np.isfinite(value).all() or not np.allclose(value[:, 3], 1):
        raise ValueError(f'{expression} contains invalid or unfinished points.')
    return value


rows = read_scalar('calculation.rows')
columns = read_scalar('calculation.columns')
if not (4 <= rows <= 16 and 4 <= columns <= 16):
    raise ValueError(f'Invalid cubic interpolation grid: {rows} x {columns}.')
if read_scalar('calculation.phase') < 3:
    raise ValueError('Finish all three calculation phases before plotting.')

# Match the storage offsets and LINE_LIST topology in shader/nurbs.comp.
surface = read_points('points.position', 40400)
input_points = read_points('surface.inputPoint', rows * columns)
control_points = read_points('points.position + 43472', rows * columns)
segments = surface[:, :3].reshape(-1, 2, 3)
control_grid = control_points[:, :3].reshape(rows, columns, 3)
control_segments = np.concatenate((
    np.stack((control_grid[:-1], control_grid[1:]), axis=-2).reshape(-1, 2, 3),
    np.stack((control_grid[:, :-1], control_grid[:, 1:]), axis=-2).reshape(-1, 2, 3),
))

fig = plt.figure(figsize=(11, 9))
ax = fig.add_subplot(111, projection='3d')
ax.add_collection3d(Line3DCollection(
    segments, colors='tab:blue', linewidths=0.4, alpha=0.65, label='NURBS surface'))
ax.add_collection3d(Line3DCollection(
    control_segments, colors='tab:green', linewidths=0.7, alpha=0.7, label='Control net'))
ax.scatter(*input_points[:, :3].T, color='tab:red', s=18, label='Input points', depthshade=False)
ax.scatter(*control_points[:, :3].T, color='tab:green', s=10, label='Control points', depthshade=False)
all_points = np.concatenate((surface[:, :3], input_points[:, :3], control_points[:, :3]))
ax.auto_scale_xyz(*all_points.T)
ax.set_box_aspect(np.maximum(np.ptp(all_points, axis=0), 1e-6))
ax.set_xlabel('World X')
ax.set_ylabel('World Y')
ax.set_zlabel('World Z')
ax.set_title(f'NURBS interpolation ({rows} x {columns} input points)')
ax.legend()
fig.tight_layout()

output_root = Path(mem.__code__.co_filename).resolve().parent
figure_path = output_root / 'figure'
figure_path.mkdir(parents=True, exist_ok=True)
output_path = figure_path / 'nurbs.png'
fig.savefig(output_path, dpi=200, bbox_inches='tight')
# Keep named arrays and grid dimensions together for later inspection.
data_path = output_root / 'data'
data_path.mkdir(parents=True, exist_ok=True)
np.savez(data_path / 'nurbs.npz', surface=surface, input_points=input_points,
         control_points=control_points, rows=rows, columns=columns)
print(f'Saved plot: {output_path}')
print(f'Saved data: {data_path / "nurbs.npz"}')
plt.show()
