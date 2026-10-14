
import numpy as np
from pathlib import Path

data_path = Path(mem.__code__.co_filename).resolve().parent / 'data'
data_path.mkdir(parents=True, exist_ok=True)

#LINE 635
size = mem('sizeof(values)')
surface = mem('values', 'uc', int(size))
np.asarray(surface, dtype=np.uint8).tofile(data_path / 'surface.dat')
_surface = surface.view(np.float32)
print('_surface', _surface)

#LINE 902
size = mem('sizeof(push)')
push = mem('push', 'uc', int(size))
np.asarray(push, dtype=np.uint8).tofile(data_path / 'push.dat')
_push = push.view(np.float32)
print('_push', _push)

