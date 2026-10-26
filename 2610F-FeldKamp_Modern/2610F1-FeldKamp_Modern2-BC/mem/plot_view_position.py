from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

W = mem('g_fFeldKampPixlX_d[0]')
H = mem('g_fFeldKampPixlX_d[1]')

frame = [[-W, -H], [W, -H], [W, H], [-W, H]]

x = mem('fX')
y = mem('fY')

xr = mem('fChannelPos')
yr = mem('fV1')

x2 = mem('fChannelPos')
y2 = mem('fV')

theta = mem('theta')
theta_label = f'{theta:.2f}°' if theta is not None else 'unavailable'

line1 = [[0, 0], [x, y]]

line2 = [[0, 0], [xr, yr]]

line3 = [[xr, yr], [x2, y2]]

fig, ax = plt.subplots(figsize=(7, 7))
ax.plot(*zip(*(frame + [frame[0]])), 'k-', label='Frame')
ax.plot(*zip(*line1), 'o-', label='(fX, fY)')
ax.plot(*zip(*line2), 'o-', label='(fChannelPos, fV1)')
ax.plot(*zip(*line3), '.-.', label='(fChannelPos, fV1) → (fChannelPos, fV)')
ax.set_aspect('equal', adjustable='box')
ax.set(xlabel='X', ylabel='Y', title=f'View position — θ = {theta_label}')
ax.grid(True)
ax.legend()

filename = Path(__file__).resolve().parent / 'figure' / 'view_position.png'
filename.parent.mkdir(parents=True, exist_ok=True)

fig.savefig(filename, dpi=160, bbox_inches='tight')
plt.close(fig)
print(f'Saved {filename}')

import subprocess
subprocess.run(['xdg-open', str(filename)], stderr=subprocess.DEVNULL)
