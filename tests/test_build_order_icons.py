import struct,sys,unittest,zlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'src'))
from build_order_icons import IconAtlas


def atlas(split=False):
    header=bytearray(107);struct.pack_into('<IIHH',header,0,1,1,2,1)
    if split:
        # RGB literal red/green, then alpha: fully opaque red, transparent green.
        data=b'\x05\x02'+struct.pack('<2H',0x7c00,0x03e0)+b'\x03\x02\xff\x00'
    else:
        data=struct.pack('<5H',5,0x02ff,0x7c00,0x03e0,0xffff)
    if len(data)%2:data+=b'\0'
    block=struct.pack('<2I',len(data)//2,0)+data
    signature=b'RLE Encode File'.ljust(32,b'\0')
    return signature+(struct.pack('<3I',2,1,1) if split else struct.pack('<2I',1,1))+header+struct.pack('<I',len(block))+block


def rgb(png):
    pos=8;raw=b''
    while pos<len(png):
        n=struct.unpack_from('>I',png,pos)[0];kind=png[pos+4:pos+8];data=png[pos+8:pos+8+n]
        if kind==b'IDAT':raw+=data
        pos+=12+n
    return zlib.decompress(raw)

class IconTests(unittest.TestCase):
    def test_legacy_color_and_fit(self):
        decoded=rgb(IconAtlas(atlas()).png(0,2))
        self.assertEqual(decoded[1:7],bytes((255,0,0,0,255,0)))

    def test_expansion_split_alpha(self):
        decoded=rgb(IconAtlas(atlas(True)).png(0,2))
        self.assertEqual(decoded[1:7],bytes((255,0,0,8,25,37)))

    def test_invalid_bounds_and_truncated_stream(self):
        for raw in (b'',atlas()[:-1],atlas(True)[:-3]):
            with self.subTest(length=len(raw)),self.assertRaises((ValueError,struct.error)):
                IconAtlas(raw)
        with self.assertRaises(ValueError):IconAtlas(atlas()).png(999)
        raw=bytearray(atlas());raw[-2:]=b'\0\0'
        with self.assertRaises(ValueError):IconAtlas(raw).png(0)
