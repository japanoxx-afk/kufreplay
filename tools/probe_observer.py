"""Bounded, read-only observer data probe for the supported KUF executable."""
import ctypes as c
import json
import struct
import sys
from probe_game import k, probe

pid = int(sys.argv[1])
base = int(probe(pid)['module_base'], 16)
handle = k.OpenProcess(0x410, False, pid)
def read(pointer, size=4):
    data = c.create_string_buffer(size)
    count = c.c_size_t()
    if not k.ReadProcessMemory(handle, pointer, data, size, c.byref(count)) or count.value != size:
        raise c.WinError(c.get_last_error())
    return data.raw
def word(pointer):
    return int.from_bytes(read(pointer), 'little')
def va(address):
    return base + address - 0x400000

def layers(world, depth=0):
    start,end=word(world+0x30),word(world+0x34)
    result=[]
    for i in range(start,min(end,start+128),4):
        obj=word(i);draw=word(word(obj)+8)-base+0x400000
        row={'pointer':hex(obj),'draw':hex(draw)}
        if draw==0x49e470 and depth<2: row['children']=layers(word(obj+8),depth+1)
        result.append(row)
    return result
try:
    game = word(va(0xb8282c))
    players = []
    for slot in range(8):
        player = word(game+0x48+slot*4)
        players.append({'slot':slot,'pointer':hex(player),'vtable':hex(word(player)),
                        'draw':hex(word(word(player)+4)-base+0x400000),
                        'identity':list(read(player+8,8)),
                        'resources':struct.unpack('<4I',read(player+0xa04,16)),
                        'population':struct.unpack('<2I',read(player+0xe28,8)),
                        'counts':struct.unpack('<5I',read(player+0xa04+0x420,20)),
                        'ui':hex(word(player+0x5e0)),
                        'ui_methods':[hex(word(word(word(player+0x5e0))+offset)-base+0x400000) for offset in (0x10,0x14,0x18)] if word(player+0x5e0) else []})
    manager = word(va(0xb870c0))
    count = min(word(manager+0xdc38),11264)
    index = word(manager+0xdc3c)
    units=[]
    for _ in range(count):
        if index>=11264: break
        unit=word(manager+0x2c38+index*4)
        if unit and len(units)<64:
            units.append({'index':index,'type':struct.unpack('<h',read(unit+4,2))[0],
                          'class':struct.unpack('<h',read(unit+6,2))[0],
                          'owner':struct.unpack('<h',read(unit+8,2))[0],
                          'position':struct.unpack('<2i',read(unit+0x18,8)),
                          'display_methods':[hex(word(word(unit)+offset)-base+0x400000) for offset in (0x60,0x64,0x68)],
                          'flags':list(read(unit+0x68,8)),'hp':word(unit+0x14)})
        index=word(manager+0xdc4c+index*12)
    map_draw = word(word(va(0xb82800))+8)
    world=word(va(0xb82808));ui=word(word(game+0x48)+0x5e0)
    layer_start,layer_end=word(world+0x30),word(world+0x34)
    print(json.dumps({'players':players,'unit_count':count,'units':units,
                      'map_pointer':hex(map_draw),'ui_scenes':{hex(o):hex(word(ui+o)) for o in (0x10,0x14,0x1888)},
                      'layers_recursive':layers(world),
                      'render_contexts':{hex(o):{'pointer':hex(word(game+o)),'clip':struct.unpack('<4i',read(word(game+o)+0x96c,16))} for o in (0x9b4,0x9b8,0x9bc) if word(game+o)},
                      'world_layers':[{'pointer':hex(word(i)),'vtable':hex(word(word(i))),
                                       'draw':hex(word(word(word(i))+8)-base+0x400000),
                                       'child':hex(word(word(i)+8)),
                                       'child_draw':hex(word(word(word(word(i)+8))+0xc)-base+0x400000) if word(word(word(i))+8)-base+0x400000==0x49e470 else None}
                                      for i in range(layer_start,min(layer_end,layer_start+128),4)],
                      'map_draw':{hex(o):hex(word(map_draw+o)) for o in (0x50,0x54,0x58,0x5c,0x68,0x6c,0x70,0x74,0x7c,0x96c,0x970,0x974,0x978)},
                      'ui_vtable':[hex(word(word(word(word(game+0x48)+0x5e0))+o)-base+0x400000) for o in range(0,0x80,4)],
                      'game_event':hex(word(word(game)+0x64)-base+0x400000),
                      'world_render':hex(word(word(word(va(0xb82808)))+12)-base+0x400000),
                      'definitions':{str(t):read(word(va(0xb870f4))+t*0xaf8,64).hex() for t in (0,15,46,62)},
                      'viewport':struct.unpack('<6i',read(va(0xbf2578),24)),
                      'camera':struct.unpack('<2i',read(game+0x9c4,8))},indent=2))
finally:
    k.CloseHandle(handle)
