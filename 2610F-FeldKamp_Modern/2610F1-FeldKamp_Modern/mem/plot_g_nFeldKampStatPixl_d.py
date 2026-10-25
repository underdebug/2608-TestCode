"""Plot the active slices' start pixels while the debugger is paused."""
np.set_printoptions(suppress=True)
# Six characters per number plus a space: at most 25 values per line.
np.set_printoptions(linewidth=176)
np.set_printoptions(threshold=np.inf)
np.set_printoptions(formatter={
    'float_kind': lambda x: f'{x:6.3f}',
    'int_kind': lambda x: f'{x:6d}',
})

from pathlib import Path

import matplotlib
matplotlib.use('Agg')
from matplotlib.pyplot import close, figure, plot, savefig, title, xlabel, ylabel

slices = mem('NZ')
if slices is None or int(slices) <= 0:
    raise ValueError('Pause in main with NZ available and the start-pixel array initialized.')

g_nFeldKampStatPixl_d = mem('g_nFeldKampStatPixl_d', 'int', [int(slices)])
if g_nFeldKampStatPixl_d is None:
    raise ValueError('Cannot read g_nFeldKampStatPixl_d from the paused program.')

figure(figsize=(7, 4))
plot(g_nFeldKampStatPixl_d, marker='o')
title('Start pixel per slice')
xlabel('Slice index')
ylabel('Start pixel')
directory = Path(mem.__globals__['PATH']) / 'figure'
directory.mkdir(parents=True, exist_ok=True)
filename = directory / 'g_nFeldKampStatPixl_d.png'
savefig(filename, dpi=160, bbox_inches='tight')
close()
print(f'Saved {filename}')
