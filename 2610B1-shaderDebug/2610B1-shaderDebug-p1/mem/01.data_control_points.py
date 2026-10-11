
import os
import numpy as np

import matplotlib
matplotlib.use('Qt5Agg')
import matplotlib.pyplot as plt

d = MEM('controls', None, None)

dx = d['x']
dy = d['y']
dz = d['z']

c = MEM('surfacePoints', None, None)

cx = c['x']
cy = c['y']
cz = c['z']


fig = plt.figure(figsize=(10, 8), dpi=100)
ax = fig.add_subplot(111, projection='3d')
ax.plot(dx.flatten(), dy.flatten(), dz.flatten(), 'b.')
ax.plot_wireframe(dx, dy, dz)

ax.plot(cx.flatten(), cy.flatten(), cz.flatten(), 'r.')

ax.set_box_aspect([np.ptp(a) for a in (dx, dy, dz)])

file = f'mem/figure/01.data_control_points.png';
plt.savefig(file, dpi=300, bbox_inches='tight')
print(os.path.abspath(file))
plt.show()

