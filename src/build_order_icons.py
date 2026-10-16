"""Bounded, read-only KUF command icons from the user's installed game assets."""
import struct
import zlib
from pathlib import Path

MAX_RESOURCE=16*1024*1024


def resource(path, wanted):
    with Path(path).open('rb') as stream:
        size=stream.seek(0,2);stream.seek(0)
        if not stream.read(32).startswith(b'EFB KUF Resource File\0') or size<80:
            raise ValueError('게임 리소스 헤더 오류')
        stream.seek(72);count,index=struct.unpack('<2I',stream.read(8))
        if count>4096 or not 80<=index<=size or size-index>1024*1024:
            raise ValueError('게임 리소스 목차 오류')
        stream.seek(index);table=stream.read();pos=0;match=None
        for _ in range(count):
            fields=[]
            for _ in range(2):
                length=struct.unpack_from('<I',table,pos)[0];pos+=4
                if length>1024 or pos+length>len(table):raise ValueError('게임 리소스 이름 오류')
                fields.append(table[pos:pos+length].decode('cp949'));pos+=length
            _,offset,length,_=struct.unpack_from('<4I',table,pos);pos+=16
            if fields[0].casefold()==wanted.casefold():
                if match is not None or not 80<=offset<=index or length>MAX_RESOURCE or offset+length>index:
                    raise ValueError('게임 리소스 범위 오류')
                match=(offset,length)
        if match is None:raise ValueError('게임 아이콘 리소스 없음')
        stream.seek(match[0]);data=stream.read(match[1])
        if len(data)!=match[1]:raise ValueError('게임 리소스 읽기 오류')
        return data


class IconAtlas:
    def __init__(self, raw):
        if raw[:15]!=b'RLE Encode File' or len(raw)>MAX_RESOURCE:
            raise ValueError('게임 아이콘 형식 오류')
        version,count=struct.unpack_from('<2I',raw,32)
        if version==2:
            count=struct.unpack_from('<I',raw,40)[0]
        if version not in (1,2) or not 1<=count<=1024:raise ValueError('게임 아이콘 버전 오류')
        pos=44 if version==2 else 40;headers=[]
        for _ in range(count):
            _,frames,width,height=struct.unpack_from('<IIHH',raw,pos)
            if not 1<=frames<=256 or not 1<=width<=2048 or not 1<=height<=2048:
                raise ValueError('게임 아이콘 크기 오류')
            headers.append((frames,width,height));pos+=100+7*frames
            if pos>len(raw):raise ValueError('게임 아이콘 헤더 잘림')
        block_size=struct.unpack_from('<I',raw,pos)[0];pos+=4
        block_end=pos+block_size
        if block_end>len(raw):raise ValueError('게임 아이콘 블록 잘림')
        self.entries=[];self.raw=raw
        self.split_alpha=version==2 and bool(struct.unpack_from("<I",raw,36)[0]&1)
        # First stored block is RGB555, followed by RGB565. Only the first is needed.
        for frames,width,height in headers:
            words=struct.unpack_from('<I',raw,pos)[0];pos+=4
            first=struct.unpack_from('<I',raw,pos)[0];pos+=4*frames
            if first>=words or pos+words*2>block_end:raise ValueError('게임 아이콘 프레임 오류')
            self.entries.append((width,height,pos+first*2,pos+words*2));pos+=words*2
        if pos!=block_end:raise ValueError('게임 아이콘 블록 길이 오류')

    def png(self, index, size=26):
        if not 0<=index<len(self.entries) or not 1<=size<=64:raise ValueError('게임 아이콘 번호 오류')
        width,height,pos,end=self.entries[index];raw=self.raw
        if width*height>256*256:raise ValueError('아이콘으로 사용할 수 없는 이미지')
        background=(8,25,37);pixels=[background]*(width*height)
        if self.split_alpha:
            # URE v2 flag 1: separate length-prefixed RGB and alpha byte-run streams.
            # Native 639f90 advances each block; 6398b0/639750 decode the runs.
            def block():
                nonlocal pos
                length=0
                while True:
                    if pos>=end:raise ValueError('아이콘 블록 잘림')
                    value=raw[pos];pos+=1;length+=value
                    if value!=255:break
                stop=pos+length
                if stop>end:raise ValueError('아이콘 블록 길이 오류')
                start=pos;pos=stop
                return start,stop
            def runs(start,stop,word):
                result=[]
                while start<stop:
                    command=raw[start];start+=1;count=command&127
                    values=1 if command&128 else count
                    unit=2 if word else 1
                    if count==0 or len(result)+count>width or start+values*unit>stop:
                        raise ValueError('아이콘 분리 스트림 오류')
                    data=[struct.unpack_from('<H',raw,start+k*2)[0] if word else raw[start+k] for k in range(values)]
                    result.extend(data*count if values==1 else data);start+=values*unit
                if len(result)!=width:raise ValueError('아이콘 분리 스트림 너비 오류')
                return result
            for y in range(height):
                colors=runs(*block(),True);alphas=runs(*block(),False)
                for x,(color,alpha) in enumerate(zip(colors,alphas)):
                    rgb=(((color>>10)&31)*255//31,((color>>5)&31)*255//31,(color&31)*255//31)
                    pixels[y*width+x]=tuple((c*alpha+bg*(255-alpha))//255 for c,bg in zip(rgb,background))
        else:
            for y in range(height):
                length=struct.unpack_from('<H',raw,pos)[0];row_end=pos+length*2;pos+=2;x=0;terminated=False
                if length<2 or row_end>end:raise ValueError('게임 아이콘 행 오류')
                while pos<row_end:
                    command=struct.unpack_from('<H',raw,pos)[0];pos+=2
                    if command==65535:terminated=True;break
                    count=(command>>8)&127;alpha=command&255
                    values=1 if not alpha or command&32768 else count
                    if count==0 or x+count>width or pos+values*2>row_end:raise ValueError('게임 아이콘 픽셀 오류')
                    if alpha:
                        for k in range(count):
                            color=struct.unpack_from('<H',raw,pos+(0 if values==1 else k*2))[0]
                            rgb=(((color>>10)&31)*255//31,((color>>5)&31)*255//31,(color&31)*255//31)
                            pixels[y*width+x+k]=tuple((c*alpha+bg*(255-alpha))//255 for c,bg in zip(rgb,background))
                    pos+=values*2;x+=count
                if not terminated:raise ValueError('게임 아이콘 행 종료 오류')
                pos=row_end
        out=bytearray(bytes(background)*(size*size));scale=min(size/width,size/height)
        w=max(1,int(width*scale));h=max(1,int(height*scale));left=(size-w)//2;top=(size-h)//2
        for y in range(h):
            for x in range(w):
                color=pixels[min(height-1,int(y/scale))*width+min(width-1,int(x/scale))]
                offset=((y+top)*size+x+left)*3;out[offset:offset+3]=bytes(color)
        def chunk(kind,data):
            return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
        scan=b''.join(b'\0'+out[y*size*3:(y+1)*size*3] for y in range(size))
        return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',size,size,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(scan))+chunk(b'IEND',b'')


def load_icons(game, events):
    """Return PNGs keyed by (opcode, entity ID); assets are never redistributed."""
    root=Path(game).parent
    atlas=IconAtlas(resource(root/'res6.res','MenuIcon8.URE'))
    extra=None
    extra_path=root/'res'/'misc'/'MenuIcon8_2.URE'
    if extra_path.exists():
        if extra_path.stat().st_size>MAX_RESOURCE:raise ValueError('추가 아이콘 크기 제한 초과')
        extra=IconAtlas(extra_path.read_bytes())
    menu=resource(root/'res11.res','KufButton.kmf')
    if menu[:13]!=b'Kuf menu file' or len(menu)<56:raise ValueError('명령 아이콘 목록 오류')
    count=struct.unpack_from('<I',menu,36)[0]
    if count>1024 or len(menu)!=56+512*count:raise ValueError('명령 아이콘 목록 크기 오류')
    mapping={}
    for offset in range(56,len(menu),512):
        icon,opcode,ident=struct.unpack_from('<3i',menu,offset+4)
        if opcode in (300,400,500) and (0<=icon<len(atlas.entries) or extra is not None and 1000<=icon<1000+len(extra.entries)):
            mapping.setdefault((opcode,ident),icon)
    images={};cache={}
    for event in events:
        opcode=300 if event.opcode in (170,5170) else 400 if event.opcode in (400,401) else event.opcode
        key=(opcode,event.ident)
        if key in mapping and key not in images:
            index=mapping[key]
            if index not in cache:cache[index]=extra.png(index-1000) if index>=1000 else atlas.png(index)
            images[key]=cache[index]
    return images

