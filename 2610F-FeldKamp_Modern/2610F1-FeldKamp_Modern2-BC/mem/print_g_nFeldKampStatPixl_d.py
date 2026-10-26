"""Print NumPy arrays with up to 10 values per line in the debug console."""
import numpy as np
from numpy.random import rand

# g_nFeldKampStatPixl_d = mem('g_nFeldKampStatPixl_d')
# printf(g_nFeldKampStatPixl_d)


np.set_printoptions(suppress=True)  # FORMAT LONG G
np.set_printoptions(linewidth=250) # MAX WIDTH NODELETE
np.set_printoptions(threshold=1000) # MAX NUMBER NODELETE
np.set_printoptions(edgeitems=10)   # FIRST LAST NUMBER NODELETE
np.set_printoptions(formatter={
    'float_kind': lambda x: f'{x:8.3f}',
    'int_kind': lambda x: f'{x:5d}',
})


A = rand(10240) * 10000
B = rand(1024, 1024) * 100
C = rand(10, 10) * 100

print(A)
# print(B)
# print(C)
