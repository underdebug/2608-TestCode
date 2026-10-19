"""Export shader inputs every two seconds while the GDB session renders."""
from pathlib import Path
import runpy
import time
import gdb

CAPTURE_DELAY_SECONDS = 2.0
_directory = Path(__file__).resolve().parent
_source = _directory.parent / 'VolumeRendering.cpp'
_line = next(i for i, line in enumerate(_source.read_text().splitlines(), 1)
             if 'vkCmdBindPipeline(command,' in line)


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
            runpy.run_path(str(_directory / 'capture_volume.py'))
            gdb.write(f'Volume export took {time.monotonic() - started:.3f} seconds.\n')
        except Exception as error:
            gdb.write(f'Automatic volume capture failed: {error}\n', gdb.STDERR)
            return True
        return False


_CaptureVolumePeriodic(f'{_source}:{_line}', internal=True)
gdb.write(f'Volume capture armed at {_source.name}:{_line}\n')
