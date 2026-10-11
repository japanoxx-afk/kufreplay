"""Diagnostic only. Briefly suspends one game thread and always resumes it."""
import ctypes as c
from ctypes import wintypes as w
import struct, sys
from probe_game import k

class Context(c.Structure):
    _fields_=[('words',w.DWORD*51),('extended',c.c_byte*512)]

k.OpenThread.argtypes=[w.DWORD,w.BOOL,w.DWORD]; k.OpenThread.restype=w.HANDLE
k.SuspendThread.argtypes=[w.HANDLE]; k.ResumeThread.argtypes=[w.HANDLE]
k.Wow64GetThreadContext.argtypes=[w.HANDLE,c.POINTER(Context)]
pid,tid=map(int,sys.argv[1:3])
h=k.OpenProcess(0x410,False,pid)
t=k.OpenThread(0xA,False,tid)
if not h or not t:
    raise c.WinError(c.get_last_error())
try:
    if k.SuspendThread(t)==0xffffffff:
        raise c.WinError(c.get_last_error())
    try:
        context=Context(); context.words[0]=0x10007
        if not k.Wow64GetThreadContext(t,c.byref(context)):
            raise c.WinError(c.get_last_error())
        esp=context.words[49]; eip=context.words[46]
        out=c.create_string_buffer(1024); n=c.c_size_t()
        if not k.ReadProcessMemory(h,esp,out,1024,c.byref(n)):
            raise c.WinError(c.get_last_error())
        print('EIP',hex(eip),'ESP',hex(esp))
        print('STACK_CODE_CANDIDATES',[(hex(esp+i*4),hex(v)) for i,v in enumerate(struct.unpack('<256I',out.raw)) if 0x500000<v<0xb30000][:40])
        fp=context.words[45]
        for _ in range(12):
            pair=c.create_string_buffer(8)
            if not k.ReadProcessMemory(h,fp,pair,8,c.byref(n)):
                break
            nxt,ret=struct.unpack('<2I',pair.raw)
            print('FRAME',hex(fp),hex(ret))
            if nxt<=fp or nxt-fp>0x100000:
                break
            fp=nxt
    finally:
        k.ResumeThread(t)
finally:
    k.CloseHandle(t); k.CloseHandle(h)
