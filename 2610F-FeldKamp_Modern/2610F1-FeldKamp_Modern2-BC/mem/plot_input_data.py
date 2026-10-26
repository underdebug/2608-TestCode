"""Plot live input arrays with the debugger extension's mem() helper.

Run while paused in main after raw and filtered have been filled.
"""
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np


def plot_input_data():
    channels, rows, views = [mem(name) for name in ('C', 'ROWS', 'V')]
    
    if any(value is None for value in (channels, rows, views)):
        raise ValueError('Pause in main after the projection loop, with debug symbols enabled.')
    channels, rows, views = int(channels), int(rows), int(views)
    if min(channels, rows, views) <= 0:
        raise ValueError('Invalid projection dimensions.')
    shape = (views, rows, channels)
    arrays = []
    for name in ('raw', 'filtered'):
        values = mem(name, 'float', list(shape))
        if values is None:
            raise ValueError(f'Cannot read {name}; pause in main after both input arrays are filled.')
        array = np.asarray(values, dtype=np.float32)
        if array.size != views * rows * channels or not np.isfinite(array).all():
            raise ValueError(f'{name}: incomplete or nonfinite projection data.')
        arrays.append(array.reshape(shape))
    raw, filtered = arrays
    fig, axes = plt.subplots(2, 2, figsize=(12, 8), constrained_layout=True)
    for ax, data, title, xlabel, ylabel in (
        (axes[0, 0], raw[0], 'Raw detector projection, view 0', 'Detector channel', 'Detector row'),
        (axes[0, 1], filtered[0], 'Ramp-filtered projection, view 0', 'Detector channel', 'Detector row'),
        (axes[1, 0], raw[:, rows // 2], 'Raw central-row sinogram', 'Detector channel', 'View index'),
        (axes[1, 1], filtered[:, rows // 2], 'Filtered central-row sinogram', 'Detector channel', 'View index'),
    ):
        signed = 'Filtered' in title or 'filtered' in title
        limit = float(np.abs(data).max()) or 1.0
        options = dict(cmap='RdBu_r', vmin=-limit, vmax=limit) if signed else dict(cmap='gray')
        artist = ax.imshow(data, aspect='auto', origin='lower', **options)
        ax.set(title=title, xlabel=xlabel, ylabel=ylabel)
        fig.colorbar(artist, ax=ax, label='Projection value')
    directory = Path(mem.__globals__['PATH']) / 'figure'
    directory.mkdir(parents=True, exist_ok=True)
    filename = directory / 'input_data.png'
    fig.savefig(filename, dpi=160)
    plt.close(fig)
    print(f'Saved {filename} ({views} views, {rows} rows, {channels} channels).')


plot_input_data()
