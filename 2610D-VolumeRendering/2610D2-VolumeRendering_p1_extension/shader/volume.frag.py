"""Load in GDB to capture volume.frag inputs every two seconds.

Run with: python import runpy; runpy.run_path('shader/volume.frag.py')
The first rendered frame is saved immediately. Then the latest rendered inputs
are saved to mem/data every two seconds, including while the window is idle.
The two fixed filenames are overwritten for replay by volume.frag.cpp.
"""
from pathlib import Path
import struct
import threading
import time
import gdb

CAPTURE_DELAY_SECONDS = 2.0
_root = Path(__file__).resolve().parent.parent
_source = _root / 'VolumeRendering.cpp'
_line = next(i for i, line in enumerate(_source.read_text().splitlines(), 1)
             if 'vkCmdBindPipeline(command,' in line)


def _capture_volume(cached_volume=None):
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
    # The loaded voxel data is immutable; avoid reading the entire volume on
    # every frame. Camera/transfer bytes are refreshed at every render.
    key = (inferior.num, int(start), int(finish), view_bytes[64:96])
    if cached_volume is None or cached_volume[0] != key:
        volume_bytes = (view_bytes[80:96] + view_bytes[64:80]
                        + bytes(inferior.read_memory(start, ((count + 1) // 2) * 4)))
        cached_volume = (key, volume_bytes)
    return cached_volume, view_bytes + struct.pack('=2I', width, height)


class _CaptureVolumePeriodic(gdb.Breakpoint):
    def __init__(self):
        super().__init__(f'{_source}:{_line}', internal=True)
        self._snapshot = None
        self._cancel = threading.Event()
        self._worker = None
        gdb.events.exited.connect(self._on_exit)

    def _write_snapshot(self):
        volume, view = self._snapshot
        output = _root / 'mem' / 'data'
        output.mkdir(parents=True, exist_ok=True)
        # Replace complete files so the CPU replay cannot read a partial write.
        for name, data in (('cpu-volume.bin', volume[1]), ('cpu-view.bin', view)):
            target = output / name
            temporary = output / (name + '.tmp')
            temporary.write_bytes(data)
            temporary.replace(target)

    def _write_periodically(self):
        # No GDB API calls from this thread: only write the cached Python bytes.
        deadline = time.monotonic() + CAPTURE_DELAY_SECONDS
        while not self._cancel.wait(max(0, deadline - time.monotonic())):
            try:
                self._write_snapshot()
            except Exception as error:
                print(f'Automatic volume capture failed: {error}', flush=True)
            deadline += CAPTURE_DELAY_SECONDS
            if deadline < time.monotonic():
                deadline = time.monotonic() + CAPTURE_DELAY_SECONDS

    def _on_exit(self, event):
        self._cancel.set()
        if self._worker is not None:
            self._worker.join()
        self._worker = None
        self._snapshot = None

    def close(self):
        self._on_exit(None)
        gdb.events.exited.disconnect(self._on_exit)
        if self.is_valid():
            self.delete()

    def stop(self):
        try:
            cached = self._snapshot[0] if self._snapshot else None
            self._snapshot = _capture_volume(cached)
            if self._worker is None:
                self._write_snapshot()
                self._cancel.clear()
                self._worker = threading.Thread(target=self._write_periodically,
                                                daemon=True)
                self._worker.start()
                gdb.write(f'Volume captured in {_root / "mem" / "data"}; '
                          f'updating every {CAPTURE_DELAY_SECONDS:g} seconds.\n')
        except Exception as error:
            gdb.write(f'Automatic volume capture failed: {error}\n', gdb.STDERR)
            return True
        return False


# runpy uses a fresh namespace each time. Keep the controller on gdb so loading
# the script again replaces the old breakpoint and stops its writer thread.
_previous = getattr(gdb, '_volume_capture', None)
if _previous is not None:
    _previous.close()
gdb._volume_capture = _CaptureVolumePeriodic()
gdb.write(f'Volume capture armed at {_source.name}:{_line}; waiting for a rendered frame.\n')
