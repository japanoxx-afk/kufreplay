"""Read-only probes; static VA values are translated using the loaded module base."""
import ctypes as c
from ctypes import wintypes as w
import json, sys

class MODULEENTRY32W(c.Structure):
    _fields_ = [('dwSize',w.DWORD),('th32ModuleID',w.DWORD),('th32ProcessID',w.DWORD),
                ('GlblcntUsage',w.DWORD),('ProccntUsage',w.DWORD),('modBaseAddr',c.POINTER(c.c_byte)),
                ('modBaseSize',w.DWORD),('hModule',w.HMODULE),('szModule',w.WCHAR*256),('szExePath',w.WCHAR*260)]

k=c.WinDLL('kernel32',use_last_error=True)
k.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD]
k.CreateToolhelp32Snapshot.restype=w.HANDLE
k.Module32FirstW.argtypes=[w.HANDLE,c.POINTER(MODULEENTRY32W)]
k.Module32NextW.argtypes=[w.HANDLE,c.POINTER(MODULEENTRY32W)]
k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD]
k.OpenProcess.restype=w.HANDLE
k.ReadProcessMemory.argtypes=[w.HANDLE,w.LPCVOID,w.LPVOID,c.c_size_t,c.POINTER(c.c_size_t)]
k.CloseHandle.argtypes=[w.HANDLE]

def probe(pid):
    snapshot=k.CreateToolhelp32Snapshot(0x18,pid)
    m=MODULEENTRY32W(); m.dwSize=c.sizeof(m)
    base=None
    ok=k.Module32FirstW(snapshot,c.byref(m))
    while ok:
        if m.szModule.lower()=='kingdomunderfire.exe':
            base=c.cast(m.modBaseAddr,c.c_void_p).value
            break
        ok=k.Module32NextW(snapshot,c.byref(m))
    k.CloseHandle(snapshot)
    if base is None:
        raise RuntimeError('Game module not found')
    h=k.OpenProcess(0x410,False,pid)
    if not h:
        raise c.WinError(c.get_last_error())
    def read(va,size=4):
        out=c.create_string_buffer(size); n=c.c_size_t()
        if not k.ReadProcessMemory(h,base+va-0x400000,out,size,c.byref(n)) or n.value!=size:
            raise c.WinError(c.get_last_error())
        return out.raw
    try:
        result={'module_base':hex(base)}
        for name,va,size in [('tick',0xb8a280,4),('seed',0xbed1e8,4),('rng_count',0xb827dc,4),
                             ('game_object',0xb8282c,4),('network_mode',0xb83ad0,1),
                             ('playback_flag',0xb83ad8,1),('record_flag',0xbea2a3,1),
                             ('map_path',0xb83ad9,256),('queue',0xb870c8,4)]:
            b=read(va,size)
            result[name]=b.split(b'\0')[0].decode('cp949','replace') if size==256 else int.from_bytes(b,'little')
        return result
    finally:
        k.CloseHandle(h)

if __name__=='__main__':
    print(json.dumps(probe(int(sys.argv[1])),ensure_ascii=False,indent=2))
