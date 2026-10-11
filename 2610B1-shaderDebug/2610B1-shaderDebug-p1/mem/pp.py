"""Compute and plot cubic B-spline basis values for t = 0 : 0.001 : 1."""
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

np.set_printoptions(linewidth=200)
np.set_printoptions(suppress=True)
np.set_printoptions(threshold=np.inf)


k = np.array([0., 0., 0., 0., 0.222, 0.333, 0.444,
              0.556, 0.667, 0.778, 1., 1., 1., 1.])
# t = np.linspace(0., 1., 20)
t = 0.48

def basis(parameters):
    """Return shape (number of parameters, 10), one basis per column."""
    parameters = np.asarray(parameters, dtype=float).reshape(-1)
    # 零次基函数：位于半开节点区间内为 1，否则为 0。
    b = ((parameters[:, None] >= k[:-1]) &
         (parameters[:, None] < k[1:])).astype(float)
    # 从左向右原地更新，右侧 b[:, i]、b[:, i+1] 仍是上一阶的值。
    # 重复节点对应的零分母项取零。
    print('k', k)

    for d in range(1, 4):
        for i in range(13 - d):
            # print('k', k)
            # print(i, i+d, i+1, i+d+1, parameters)

            a = k[i + d] - k[i]
            c = k[i + d + 1] - k[i + 1]
            b[:, i] = (
                ((parameters - k[i]) * b[:, i] / a if a else 0)
                + ((k[i + d + 1] - parameters) * b[:, i + 1] / c if c else 0)
            )

            b[:, i] = np.round(b[:, i], 3)
        print('b', b)

    b = b[:, :10]

    print('b', b)

    # 夹持节点向量的右端点取左极限，最后一个基函数为 1。
    at_end = parameters == k[-1]
    b[at_end, :] = 0.
    b[at_end, -1] = 1.
    return b


if __name__ == "__main__":
# bb = basis(0.5)
# print('bb', bb)

    b = basis(t)  # b[j, i] = N_{i,3}(t[j]), shape (1001, 10)
    output = Path("mem/figure")
    output.mkdir(exist_ok=True)
    data_output = Path("mem/data")
    data_output.mkdir(exist_ok=True)
    np.savetxt(data_output / "bspline_basis.csv", np.column_stack((t, b)),
                delimiter=",", comments="", fmt="%.10g",
                header="t," + ",".join(f"N{i}_3" for i in range(10)))
    fig, ax = plt.subplots(figsize=(10, 5))
    for i in range(10):
        ax.plot(t, b[:, i], '.-', label=fr"$N_{{{i},3}}$")
        
    ax.set(xlabel="t", ylabel="Basis value", title="Cubic B-spline basis functions",
            xlim=(0, 1), ylim=(0, 1.05))
    ax.set_xticks(np.unique(k))
    ax.grid(alpha=0.3)
    ax.legend(ncol=5)
    fig.tight_layout()
    fig.savefig(output / "bspline_basis.png", dpi=180)
    print(f"Computed b.shape = {b.shape}; saved CSV in {data_output}, plot in {output}")
    plt.show()
