
X = mem('X')
print('X', X)

print(X, ' = X')


X2 = mem('X', None, [4, 2])
print('X2', X2)


a = mem('k')
print('a', a)


b = mem('b')
print('b', b)


c = mem('c')
print(c, '<= c')


s = 1
print(s, '<= s', len(s))

str = 'a'
d = mem(str)
print(d, '<= d', len(d))

print(d, '<= d', len(d))

str = 'X'
d = mem(str)
if hasattr(d, '__len__'):
    print(d, f'<= d[{len(d)}]')

print(d, '<= d', len(d))