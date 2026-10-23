
# import numpy as np

# np.set_printoptions(formatter={'float_kind': lambda x: f'{x:22.15g}'}, linewidth=120)

inverseMatrix = mem('view.inverseMatrix', 'f', [4, 4])
print(inverseMatrix, 'inverseMatrix')

A = inverseMatrix @ inverseMatrix.transpose()
print(A)
