"""Monitor overlay draw inputs inside GDB; save to project mem/data.

Run: python import runpy; runpy.run_path('shader/overlay.frag.py')
Automatically breaks at both overlay draws and immediately resumes the app.
Saves cpu-overlay.json and black-background cpu-overlay.png on the first complete
frame, then every two seconds,
including while idle. Matrices are column-major, matching overlay.vert.
These are CPU draw inputs, not GPU fragment/pixel readbacks.
"""
from pathlib import Path
import json
import struct
import shutil
import subprocess
import threading
import time
import gdb

CAPTURE_DELAY_SECONDS = 2.0
_root = Path(__file__).resolve().parent.parent
_source = _root / 'VulkanWindow.cpp'


class _OverlayDrawBreakpoint(gdb.Breakpoint):
    def __init__(self, monitor, line, mode, count):
        super().__init__(f'{_source}:{line}', internal=True)
        self.monitor, self.mode, self.count = monitor, mode, count

    def stop(self):
        try:
            self.monitor.capture(self.mode, self.count)
        except Exception as error:
            gdb.write(f'Automatic overlay capture failed: {error}\n', gdb.STDERR)
            return True
        return False


class _OverlayMonitor:
    def __init__(self):
        self.breakpoints = []
        self.pending = {}
        self.snapshot = None
        self.cancel = threading.Event()
        self.worker = None
        self.renderer = None
        # Match draw statements rather than hard-coding source line numbers.
        draws = [('vkCmdDraw(cb, 30, 1, 0, 0);', 4, 30),
                 ('vkCmdDraw(cb, 2 * (2 + 2 * 16 * 57), 1, 0, 0);',
                  3, 2 * (2 + 2 * 16 * 57))]
        lines = _source.read_text().splitlines()
        try:
            for statement, mode, count in draws:
                matches = [i for i, text in enumerate(lines, 1)
                           if text.strip() == statement]
                if len(matches) != 1:
                    raise ValueError(f'Expected one overlay draw: {statement}')
                self.breakpoints.append(_OverlayDrawBreakpoint(
                    self, matches[0], mode, count))
            gdb.events.exited.connect(self.on_exit)
        except Exception:
            for breakpoint in self.breakpoints:
                breakpoint.delete()
            raise

    def capture(self, mode, count):
        push = gdb.parse_and_eval('push')
        if int(push.type.sizeof) != 80:
            raise ValueError('Unexpected overlay ViewConstants layout (expected 80 bytes).')
        inferior = gdb.selected_inferior()
        raw = bytes(inferior.read_memory(push.address, 80))
        values = struct.unpack('=16f4i', raw)
        actual_mode, width, height, _ = values[16:]
        if actual_mode != mode or width <= 0 or height <= 0:
            raise ValueError('Invalid overlay mode or viewport.')
        # Pair axes and ruler from the same renderer invocation/thread.
        key = (inferior.num, gdb.selected_thread().global_num,
               int(gdb.parse_and_eval('this')))
        draw = {'name': 'axes' if mode == 4 else 'rulers',
                'mode': mode, 'vertex_count': count, 'instance_count': 1,
                'first_vertex': 0, 'first_instance': 0,
                'matrix': list(values[:16]),
                'viewport_width': width, 'viewport_height': height}
        if mode == 4:
            self.pending[key] = draw
            return
        axes = self.pending.pop(key, None)
        if axes is None:
            return
        if (axes['viewport_width'], axes['viewport_height']) != (width, height):
            raise ValueError('Overlay draw viewport mismatch.')
        self.snapshot = json.dumps({
            'format_version': 1,
            'captured_at_unix_seconds': time.time(),
            'vertex_shader': 'shader/overlay.vert',
            'fragment_shader': 'shader/overlay.frag',
            'topology': 'line_list', 'matrix_order': 'column_major',
            'draws': [axes, draw]}, indent=2, allow_nan=False) + '\n'
        if self.worker is None:
            self.write_snapshot()
            self.cancel.clear()
            self.worker = threading.Thread(target=self.write_periodically, daemon=True)
            self.worker.start()
            gdb.write(f'Overlay captured in {_root / "mem/data/cpu-overlay.json"} '
                      f'and cpu-overlay.png; updating every {CAPTURE_DELAY_SECONDS:g} seconds.\n')

    def write_snapshot(self):
        output = _root / 'mem' / 'data'
        output.mkdir(parents=True, exist_ok=True)
        temporary = output / 'cpu-overlay.json.tmp'
        snapshot = self.snapshot
        temporary.write_text(snapshot, encoding='utf-8')
        temporary.replace(output / 'cpu-overlay.json')
        if self.renderer is None or self.renderer.poll() is not None:
            if self.renderer is not None:
                self.renderer.stdin.close()
            python = shutil.which('python3')
            if python is None:
                raise RuntimeError('python3 is required for overlay image export')
            self.renderer = subprocess.Popen(
                [python, str(_root / 'mem/render_overlay.py'),
                 str(output / 'cpu-overlay.json'), '--watch'],
                stdin=subprocess.PIPE, text=True, start_new_session=True)
        self.renderer.stdin.write(json.dumps(json.loads(snapshot)) + '\n')
        self.renderer.stdin.flush()

    def write_periodically(self):
        # GDB access stays in breakpoint callbacks. This thread writes only
        # immutable cached JSON, so idle rendering does not stop the timer.
        deadline = time.monotonic() + CAPTURE_DELAY_SECONDS
        while not self.cancel.wait(max(0, deadline - time.monotonic())):
            try:
                self.write_snapshot()
            except Exception as error:
                print(f'Automatic overlay save failed: {error}', flush=True)
            deadline += CAPTURE_DELAY_SECONDS
            if deadline < time.monotonic():
                deadline = time.monotonic() + CAPTURE_DELAY_SECONDS

    def on_exit(self, event):
        self.cancel.set()
        if self.worker is not None:
            self.worker.join()
        self.worker = None
        if self.renderer is not None:
            self.renderer.stdin.close()
            try:
                self.renderer.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.renderer.kill()
                self.renderer.wait()
            self.renderer = None
        self.pending.clear()
        self.snapshot = None

    def close(self):
        self.on_exit(None)
        gdb.events.exited.disconnect(self.on_exit)
        for breakpoint in self.breakpoints:
            if breakpoint.is_valid():
                breakpoint.delete()


_previous = getattr(gdb, '_overlay_capture', None)
if _previous is not None:
    _previous.close()
gdb._overlay_capture = _OverlayMonitor()
gdb.write('Overlay monitor armed at axes and ruler draws; waiting for a rendered frame.\n')
