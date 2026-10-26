"""Run with F8 while CUDA-GDB is paused after the sine/cosine table is initialized."""
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
from matplotlib.pyplot import axis, close, figure, grid, plot, savefig, title, xlabel, ylabel

# F8 supplies the debugger extension's MEM() and mem() helpers.
g_fFeldKampSinTbl_d = MEM('g_fFeldKampSinTbl_d')
x = g_fFeldKampSinTbl_d['x']
y = g_fFeldKampSinTbl_d['y']

figure(figsize=(6, 6))
plot(x, y, '.')
axis('equal')
grid(True)
title('FeldKamp sine/cosine table')
xlabel('x (sine)')
ylabel('y (cosine)')

directory = Path(PATH) / 'figure'
directory.mkdir(parents=True, exist_ok=True)
filename = directory / 'g_fFeldKampSinTbl_d.png'
savefig(filename, dpi=160, bbox_inches='tight')
close()
print(f'Saved {filename}')
