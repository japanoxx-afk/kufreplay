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
        app=int.from_bytes(read(0xbea7e8),'little')
        if app:
            def pointer_value(pointer,size=4):
                out=c.create_string_buffer(size); n=c.c_size_t()
                if not k.ReadProcessMemory(h,pointer,out,size,c.byref(n)) or n.value!=size:
                    raise c.WinError(c.get_last_error())
                return int.from_bytes(out.raw,'little')
            result['application']=hex(app)
            result['application_state']=pointer_value(app+0x128)
        result['scenario_mode']=int.from_bytes(read(0xb83ad2,2),'little')
        result['rts_mode']=int.from_bytes(read(0xb83ad4,1),'little')
        result['participant_count']=int.from_bytes(read(0xb83ad6,2),'little')
        result['custom_players']=[int.from_bytes(read(0xbecd78+i*4),'little') for i in range(8)]
        result['custom_resources']=[int.from_bytes(read(0xbecd98+i*4),'little') for i in range(8)]
        result['custom_game_type']=int.from_bytes(read(0xbecd74),'little')
        result['native_options']=list(read(0xbeda08,16))
        obj=int.from_bytes(read(0xb8282c),'little')
        if obj:
            result['match_options']=[pointer_value(obj+0xc7a+i,1) for i in range(10)]
            result['match_seed']=pointer_value(obj+0xc84)
        for name,va,size in [('tick',0xb8a280,4),('seed',0xbed1e8,4),('rng_count',0xb827dc,4),
                             ('game_object',0xb8282c,4),('network_mode',0xb83ad0,1),
                             ('playback_flag',0xb83ad8,1),('record_flag',0xbea2a3,1),
                             ('map_path',0xb83ad9,256),('map_name',0xb83bd9,256),('unit_manager',0xb870c0,4),('queue',0xb870c8,4)]:
            b=read(va,size)
            result[name]=b.split(b'\0')[0].decode('cp949','replace') if size==256 else int.from_bytes(b,'little')
        return result
    finally:
        k.CloseHandle(h)

if __name__=='__main__':
    print(json.dumps(probe(int(sys.argv[1])),ensure_ascii=False,indent=2))
