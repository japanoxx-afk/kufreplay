"""Read-only PE inspection. Never patches the executable."""
import hashlib, json, re, struct, sys
from pathlib import Path
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

p = Path(sys.argv[1])
data = p.read_bytes()
pe = pefile.PE(data=data)
base = pe.OPTIONAL_HEADER.ImageBase
needles = [b'Initialize random seed', b'rndSeed:%d', b'g_RandSeed:',
           b'g_RandomCount:', b'EventTCPIPCmdQueue', b'MyChecksum',
           b'Unrecoverable Sync', b'.kfm', b'SaveGame', b'LoadGame']
evidence = []
for needle in needles:
    for m in re.finditer(re.escape(needle), data):
        start = data.rfind(b'\x00', 0, m.start()) + 1
        end = data.find(b'\x00', m.end())
        if end - start > 512:
            start = m.start()
        try:
            va = base + pe.get_rva_from_offset(start)
        except pefile.PEFormatError:
            continue
        refs = []
        packed = struct.pack('<I', va)
        for section in pe.sections:
            if section.Characteristics & 0x20000000:
                code = section.get_data()
                for ref in re.finditer(re.escape(packed), code):
                    lo = max(0, ref.start() - 12)
                    address = base + section.VirtualAddress + lo
                    dis = Cs(CS_ARCH_X86, CS_MODE_32)
                    refs.append({'address': hex(base + section.VirtualAddress + ref.start()),
                                 'context': [f'{i.address:08x} {i.mnemonic} {i.op_str}'
                                             for i in dis.disasm(code[lo:ref.start()+24], address)]})
        evidence.append({'string': data[start:end].decode('ascii', 'replace'),
                         'va': hex(va), 'candidate_immediate_references': refs[:12]})
result = {'file': p.name, 'sha256': hashlib.sha256(data).hexdigest(),
          'machine': hex(pe.FILE_HEADER.Machine), 'image_base': hex(base),
          'sections': [{'name': s.Name.rstrip(b'\0').decode(), 'rva': hex(s.VirtualAddress),
                        'size': s.Misc_VirtualSize} for s in pe.sections],
          'imports': {e.dll.decode(): [i.name.decode() if i.name else f'ordinal:{i.ordinal}'
                                      for i in e.imports] for e in pe.DIRECTORY_ENTRY_IMPORT},
          'evidence': evidence,
          'limitations': 'Immediate matches are candidate references, not proven function boundaries. No runtime validation.'}
Path('docs/game-analysis.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps({'sha256':result['sha256'], 'machine':result['machine'],
                  'evidence': [{'string':e['string'], 'va':e['va'], 'references':len(e['candidate_immediate_references'])} for e in evidence]}, indent=2))
