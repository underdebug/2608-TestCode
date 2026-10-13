
import numpy as np

from pathlib import Path

# mem() is loaded from the active project's mem/mem.py by the debugger extension.
project_path = Path(mem.__code__.co_filename).resolve().parent.parent
kernel_path = project_path / 'kernel'
kernel_path.mkdir(parents=True, exist_ok=True)

print(kernel_path)

N = mem('N')
A = mem('A', 'f', N)
B = mem('B', 'f', N)
C = mem('C', 'f', N)

np.save(kernel_path / 'A.npy', A)
np.save(kernel_path / 'B.npy', B)
np.save(kernel_path / 'C.npy', C)
np.save(kernel_path / 'N.npy', N)

print(type(A), A.shape)
