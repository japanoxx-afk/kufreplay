"""Bounded, read-only disassembly and MSVC RTTI lookup for this installed build."""
import argparse, json, re, struct
from pathlib import Path
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

GAME = Path(r'C:\Program Files (x86)\Steam\steamapps\common\Kingdom Under Fire War of Heroes\KingdomUnderFire.exe')
p = pefile.PE(str(GAME))
b = GAME.read_bytes()
base = p.OPTIONAL_HEADER.ImageBase
md = Cs(CS_ARCH_X86, CS_MODE_32)
md.detail = True

def va(off):
    return base + p.get_rva_from_offset(off)

def refs(address, executable=False):
    result = []
    for s in p.sections:
        if executable and not s.Characteristics & 0x20000000:
            continue
        for m in re.finditer(re.escape(struct.pack('<I', address)), s.get_data()):
            result.append(base + s.VirtualAddress + m.start())
    return result

def dump(address, size):
    for i in md.disasm(p.get_data(address-base,size), address):
        print(f'{i.address:08x}: {i.bytes.hex():24s} {i.mnemonic} {i.op_str}')

def rtti(name):
    target = ('.?AV'+name+'@@').encode()
    off = b.find(target)
    if off == -1:
        return
    td = va(off) - 8
    print('TYPE', name, hex(td))
    for ref in refs(td):
        # x86 CompleteObjectLocator has type descriptor at +12.
        col = ref - 12
        raw = p.get_data(col-base,20)
        if len(raw)!=20:
            continue
        sig,offset,cd,typ,hierarchy = struct.unpack('<5I',raw)
        if sig != 0 or offset > 0x10000 or typ != td:
            continue
        for vf in refs(col):
            table = vf + 4
            words = struct.unpack('<24I',p.get_data(table-base,96))
            if all(base <= x < base+0x700000 for x in words[:3]):
                print('VTABLE', hex(table),'OFFSET',offset,'ENTRIES',','.join(hex(x) for x in words))

arg = argparse.ArgumentParser()
arg.add_argument('--rtti')
arg.add_argument('--string')
arg.add_argument('--address',type=lambda x:int(x,0))
arg.add_argument('--size',type=lambda x:int(x,0),default=256)
arg.add_argument('--refs',type=lambda x:int(x,0))
arg.add_argument('--calls',type=lambda x:int(x,0))
a=arg.parse_args()
if a.rtti:
    rtti(a.rtti)
if a.string:
    for match in re.finditer(re.escape(a.string.encode()),b):
        start=b.rfind(b'\0',0,match.start())+1
        if match.start()-start>512:
            start=match.start()
        address=va(start)
        print('STRING',hex(address),'REFS',','.join(hex(x) for x in refs(address,True)))
if a.address:
    dump(a.address,min(a.size,8192))
if a.refs:
    for address in refs(a.refs,True):
        print('REF',hex(address),'BYTES',p.get_data(address-base-8,24).hex())
if a.calls:
    for s in p.sections:
        if not s.Characteristics & 0x20000000:
            continue
        code=s.get_data()
        for m in re.finditer(b'\xe8',code):
            pos=m.start()
            if pos+5<=len(code):
                at=base+s.VirtualAddress+pos
                if at+5+struct.unpack_from('<i',code,pos+1)[0]==a.calls:
                    print('CALL',hex(at))
