p = r'C:\Users\tinmi\OneDrive\FielDes\FielDes\app\src\tutorial.cpp'
s = open(p, newline='', encoding='utf-8').read()
escape = chr(92) + 'u25B8'
n = s.count(escape)
s = s.replace(escape, chr(0x25B8))
open(p, 'w', newline='', encoding='utf-8').write(s)
print('replaced', n)
