# Reads a Windows minidump of FielDes: the exception, the modules, and the return addresses found on the crashing thread's stack, with
# the names of the functions (from the .pdb files that are known, through dbghelp).  No debugger needed.
# usage: python read_dump.py <dump> [pdb folder ...]
import ctypes
import ctypes.wintypes as wt
import struct
import sys

path = sys.argv[1]
pdb_dirs = sys.argv[2:] or [r'C:\dev\vcpkg\installed\x64-windows\bin']
data = open(path, 'rb').read()
sig, version, nstreams, dir_rva = struct.unpack_from('<IIII', data, 0)
assert sig == 0x504d444d, 'not a minidump'

streams = {}
for i in range(nstreams):
    t, size, rva = struct.unpack_from('<III', data, dir_rva + 12 * i)
    streams[t] = (size, rva)


def utf16(rva):
    n, = struct.unpack_from('<I', data, rva)
    return data[rva + 4:rva + 4 + n].decode('utf-16le')


# modules
modules = []
size, rva = streams[4]
count, = struct.unpack_from('<I', data, rva)
for i in range(count):
    off = rva + 4 + 108 * i
    base, msize, _, stamp, name_rva = struct.unpack_from('<QIIII', data, off)
    modules.append((base, msize, utf16(name_rva)))

# memory ranges
ranges = []
if 9 in streams:
    size, rva = streams[9]
    n, base_rva = struct.unpack_from('<QQ', data, rva)
    pos = base_rva
    for i in range(n):
        start, length = struct.unpack_from('<QQ', data, rva + 16 + 16 * i)
        ranges.append((start, length, pos))
        pos += length
if 5 in streams:
    size, rva = streams[5]
    n, = struct.unpack_from('<I', data, rva)
    for i in range(n):
        start, length, mrva = struct.unpack_from('<QII', data, rva + 4 + 16 * i)
        ranges.append((start, length, mrva))

# the exception
size, rva = streams[6]
tid, _ = struct.unpack_from('<II', data, rva)
code, flags, rec, addr, nparams = struct.unpack_from('<IIQQI', data, rva + 8)
params = struct.unpack_from('<15Q', data, rva + 8 + 4 + 4 + 8 + 8 + 4 + 4)
ctx_size, ctx_rva = struct.unpack_from('<II', data, rva + 8 + 152)
rsp, = struct.unpack_from('<Q', data, ctx_rva + 0x98)
rip, = struct.unpack_from('<Q', data, ctx_rva + 0xF8)
print('exception %#x at %#x  thread %d  parameters %s' % (code, addr, tid, [hex(p) for p in params[:nparams]]))
print('rsp %#x rip %#x' % (rsp, rip))


def module_of(a):
    for base, msize, name in modules:
        if base <= a < base + msize:
            return base, name
    return None


# symbols
dbghelp = ctypes.WinDLL('dbghelp.dll')
dbghelp.SymInitialize.argtypes = [wt.HANDLE, ctypes.c_char_p, wt.BOOL]
dbghelp.SymLoadModuleEx.argtypes = [wt.HANDLE, wt.HANDLE, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64, wt.DWORD, ctypes.c_void_p, wt.DWORD]
dbghelp.SymLoadModuleEx.restype = ctypes.c_uint64
dbghelp.SymFromAddr.argtypes = [wt.HANDLE, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64), ctypes.c_void_p]
proc = ctypes.windll.kernel32.GetCurrentProcess()
dbghelp.SymSetOptions(0x2 | 0x4 | 0x10)         # undecorate names, defer loads
dbghelp.SymInitialize(proc, ';'.join(pdb_dirs).encode(), False)
loaded = {}


class SYMBOL(ctypes.Structure):
    _fields_ = [('SizeOfStruct', wt.ULONG), ('TypeIndex', wt.ULONG), ('Reserved', ctypes.c_uint64 * 2), ('Index', wt.ULONG), ('Size', wt.ULONG),
                ('ModBase', ctypes.c_uint64), ('Flags', wt.ULONG), ('Value', ctypes.c_uint64), ('Address', ctypes.c_uint64),
                ('Register', wt.ULONG), ('Scope', wt.ULONG), ('Tag', wt.ULONG), ('NameLen', wt.ULONG), ('MaxNameLen', wt.ULONG),
                ('Name', ctypes.c_char * 512)]


def name_of(a):
    m = module_of(a)
    if not m:
        return '?'
    base, mname = m
    short = mname.split('\\')[-1]
    if base not in loaded:
        loaded[base] = dbghelp.SymLoadModuleEx(proc, None, mname.encode(), None, base, [x[1] for x in modules if x[0] == base][0], None, 0)
    sym = SYMBOL()
    sym.SizeOfStruct = 88
    sym.MaxNameLen = 512
    disp = ctypes.c_uint64(0)
    if dbghelp.SymFromAddr(proc, a, ctypes.byref(disp), ctypes.byref(sym)):
        return '%s!%s+%#x' % (short, sym.Name.decode(errors='replace'), disp.value)
    return '%s+%#x' % (short, a - base)


print('fault:', name_of(rip))
# the stack
mem = None
for start, length, pos in ranges:
    if start <= rsp < start + length:
        mem = (start, length, pos)
        break
assert mem, 'stack memory not in the dump'
start, length, pos = mem
frames = []
for off in range(rsp - start, min(length, rsp - start + 0x6000), 8):
    v, = struct.unpack_from('<Q', data, pos + off)
    m = module_of(v)
    if m and 'FielDes' in m[1] or (m and 'Qt5' in m[1]):
        frames.append((start + off, v))
print('return addresses on the stack (nearest first):')
for sp, v in frames[:60]:
    print('  %#x  %s' % (sp, name_of(v)))
