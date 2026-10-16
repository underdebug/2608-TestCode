
import numpy as np
from pathlib import Path

data_path = Path(mem.__code__.co_filename).resolve().parent / 'data'
data_path.mkdir(parents=True, exist_ok=True)

#LINE 635 (after bc.size is assigned)
size = mem('bc.size')
surface = mem('values', 'uc', int(size))
np.asarray(values, dtype=np.uint8).tofile(data_path / 'surface.dat')

size = mem('sizeof(push)')
push_values = mem('&push', 'uc', int(size))
np.asarray(push_values, dtype=np.uint8).tofile(data_path / 'view.dat')

