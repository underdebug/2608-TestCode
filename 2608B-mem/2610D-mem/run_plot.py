"""Run both plots while paused in main after the reconstruction host copy.

Open this file and press F8 in the mem extension, or use Debug Console:
    -exec source /absolute/path/to/mem/run_plot.py
"""
from pathlib import Path
import runpy

# F8 executes text without setting __file__; reuse the extension's mem directory.
if callable(globals().get('mem')):
    plot_directory = Path(mem.__globals__['PATH']).resolve()
elif '__file__' in globals():
    plot_directory = Path(__file__).resolve().parent
else:
    import gdb
    executable = gdb.current_progspace().filename
    if not executable:
        raise RuntimeError('Start CUDA-GDB and pause in main before running the plots.')
    plot_directory = Path(executable).resolve().parent.parent / 'mem'

if not callable(globals().get('mem')):
    # Load the project helper explicitly, without depending on Python's import path.
    helper = runpy.run_path(str(plot_directory / 'extension' / 'mem.py'))
    helper['mem'].__globals__['PATH'] = str(plot_directory)
    mem = helper['mem']


def source(filename):
    """Execute a plotting script with the live-memory helper available."""
    runpy.run_path(str(plot_directory / filename), init_globals={'mem': mem})


source('plot_input_data.py')
source('plot_output_data.py')
