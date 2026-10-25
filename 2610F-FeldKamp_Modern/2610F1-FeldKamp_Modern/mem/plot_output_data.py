"""Plot live reconstruction with mem() while paused in main after the host copy."""
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np


def plot_output_data():
    n, slices = [mem(name) for name in ('N', 'NZ')]
    
    if n is None or slices is None:
        raise ValueError('Pause in main after cudaMemcpy(image.data(), ...), with debug symbols enabled.')
    n, slices = int(n), int(slices)
    if min(n, slices) <= 0:
        raise ValueError('Invalid reconstruction dimensions.')
    values = mem('image', 'float', [slices, n, n])
    positions = mem('zpos', 'float', [slices])
    if values is None or positions is None:
        raise ValueError('Cannot read image or zpos; pause in main after the reconstruction host copy.')
    image = np.asarray(values, dtype=np.float32)
    zpos = np.asarray(positions, dtype=np.float32)
    if image.size != slices * n * n or zpos.size != slices:
        raise ValueError('Incomplete reconstruction or slice positions.')
    image = image.reshape(slices, n, n)
    zpos = zpos.reshape(slices)
    if not np.isfinite(image).all() or not np.isfinite(zpos).all():
        raise ValueError('Reconstruction contains nonfinite values.')
    fig, axes = plt.subplots(1, slices, figsize=(4 * slices, 4.5),
                             squeeze=False, constrained_layout=True)
    lo, hi = float(image.min()), float(image.max())
    for index, ax in enumerate(axes[0]):
        artist = ax.imshow(image[index], origin='lower', cmap='gray', vmin=lo, vmax=hi,
                           extent=(-n / 2, n / 2, -n / 2, n / 2))
        ax.set(title=f'Reconstruction z = {float(zpos[index]):g}', xlabel='x (voxel units)',
               ylabel='y (voxel units)')
    fig.colorbar(artist, ax=list(axes[0]), label='Reconstructed value')
    directory = Path(mem.__globals__['PATH']) / 'figure'
    directory.mkdir(parents=True, exist_ok=True)
    filename = directory / 'output_data.png'
    fig.savefig(filename, dpi=160)
    plt.close(fig)
    print(f'Saved {filename} ({slices} reconstructed {n} x {n} slices).')


plot_output_data()
