"""Load in GDB to capture volume.frag inputs every two seconds.

Run with: python import runpy; runpy.run_path('shader/volume.frag.py')
Captures are written to mem/data for replay by volume.frag.cpp.
"""
from pathlib import Path
import struct
import time
import gdb

CAPTURE_DELAY_SECONDS = 2.0
_root = Path(__file__).resolve().parent.parent
_source = _root / 'VolumeRendering.cpp'
_line = next(i for i, line in enumerate(_source.read_text().splitlines(), 1)
             if 'vkCmdBindPipeline(command,' in line)


def _capture_volume():
    # Verify scope before writing anything. push has been fully initialized here.
    push = gdb.parse_and_eval('push')
    extent = gdb.parse_and_eval('extent')
    if int(push.type.sizeof) != 112:
        raise ValueError('Unexpected ViewConstants layout (expected 112 bytes).')
    width, height = int(extent['width']), int(extent['height'])
    if not (0 < width <= 4096 and 0 < height <= 4096):
        raise ValueError('Invalid viewport extent.')
    inferior = gdb.selected_inferior()
    # Preserve matrix float precision: mem() normally rounds float arrays.
    view_bytes = bytes(inferior.read_memory(push.address, 112))
    dimensions = struct.unpack('=4i', view_bytes[80:96])
    count = int(dimensions[0]) * int(dimensions[1]) * int(dimensions[2])
    if any(d <= 0 for d in dimensions[:3]):
        raise ValueError('Invalid volume dimensions.')
    storage = gdb.parse_and_eval('this->voxels')['_M_impl']
    start, finish = storage['_M_start'], storage['_M_finish']
    if int(finish - start) != (count + 1) // 2:
        raise ValueError('Incomplete volume input.')
    output = _root / 'mem' / 'data'
    output.mkdir(parents=True, exist_ok=True)
    # Same header as VolumeRendering::exportCpu: dimensions, halfSize, voxels.
    with (output / 'cpu-volume.bin').open('wb') as file:
        file.write(view_bytes[80:96])
        file.write(view_bytes[64:80])
        file.write(inferior.read_memory(start, ((count + 1) // 2) * 4))
    (output / 'cpu-view.bin').write_bytes(view_bytes + struct.pack('=2I', width, height))
    print(f'Captured volume {tuple(dimensions[:3])}, viewport {width} x {height}, camera and transfer parameters.')
    print(f'Capture saved to: {output}')
    print('Launch GDB: CPU volume shader to replay these inputs; plot_frag.py at frameReady().')


class _CaptureVolumePeriodic(gdb.Breakpoint):
    _next_capture = None

    def stop(self):
        now = time.monotonic()
        if self._next_capture is None:
            self._next_capture = now + CAPTURE_DELAY_SECONDS
        # Allow small render scheduling jitter around the two-second timer.
        if now + 0.05 < self._next_capture:
            return False
        self._next_capture = now + CAPTURE_DELAY_SECONDS
        try:
            started = time.monotonic()
            _capture_volume()
            gdb.write(f'Volume export took {time.monotonic() - started:.3f} seconds.\n')
        except Exception as error:
            gdb.write(f'Automatic volume capture failed: {error}\n', gdb.STDERR)
            return True
        return False


_CaptureVolumePeriodic(f'{_source}:{_line}', internal=True)
gdb.write(f'Volume capture armed at {_source.name}:{_line}\n')
