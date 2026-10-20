"""F8 while paused at vkCmdBindPipeline in VolumeRendering::recordScene."""
from pathlib import Path
import gdb
import struct

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
output = Path(__file__).resolve().parent / 'data'
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
