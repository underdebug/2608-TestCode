
a = 1.12345
print(a)

b = round(a, 3)
print(b)

d = mem('d', None, None)
print(d)

x = L(d)
print(x)

def fmt_grid(grid):
    out = []
    for row in grid:
        out_row = []
        for p in row:
            out_row.append({
                'x': float(f"{p['x']:.3f}"),
                'y': float(f"{p['y']:.3f}"),
                'z': float(f"{p['z']:.3f}"),
            })
        out.append(out_row)
    return out

print(fmt_grid(d))
