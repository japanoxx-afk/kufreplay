"""KUF native adapter controller. Gameplay bytes are never treated as code."""
import hashlib
import hmac
import json
import os
import struct
import subprocess
import sys
import uuid
import zipfile
import ctypes
import re
import msvcrt
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from replay_store import Store, validate, REQUIRED, ALLOWED, MAX_TOTAL, MAX_MANIFEST, atomic_json
from updater import VERSION
from diagnostics import archive_engine_log, game_identity, record_game_identity

ADAPTER = 'kuf-steam-1.416-experimental-v1'
GAME_HASH = '69f2575ad14c78d3c6f7c15d0187234be005f06abf095d3f1bd602f1c3722868'
UPDATED_GAME_HASH = 'd72bc8814c4e3c5614b893c79fde5ac13bf17fcaa7f830dff8f4e240126e5cfc'

def supported_game_hash(digest):
    return digest in (GAME_HASH, UPDATED_GAME_HASH)

RULES = hashlib.sha256(b'KUFReplay-native-command-adapter-v1-singleplayer-same-initial-state').hexdigest()
MAGIC = b'KUFTRC01'
MAX_TRACE = 128 * 1024 * 1024
STATE = struct.Struct('<4I')
INITIAL_SIZE = 664
SETUP = struct.Struct('<21I')
BOOTSTRAP = struct.Struct('<52I16s')
COMMAND = struct.Struct('<4I')

def replay_time(metadata):
    """Supported KUF builds advance one simulation tick every 30 milliseconds."""
    if metadata.get('adapter_id') == ADAPTER and supported_game_hash(metadata.get('game_sha256')):
        seconds = metadata['ticks'] * metadata.get('tick_interval_ms',30) // 1000
    else:
        seconds = metadata['ticks'] // metadata['tick_rate']
    return f'{seconds // 60}:{seconds % 60:02d}'

@dataclass(frozen=True)
class Trace:
    initial: bytes
    frames: tuple
    final: tuple
    commands: int
    raw: bytes
    setup: bytes | None = None
    bootstrap: bytes | None = None
    player_names: tuple | None = None

    @property
    def map_name(self):
        return self.initial[276:532].split(b'\0')[0].decode('cp949')

    @property
    def map_directory(self):
        return self.initial[20:276].split(b'\0')[0].decode('cp949')

    @property
    def network_mode(self):
        return struct.unpack_from('<I',self.initial,16)[0]

def parse_trace(raw):
    if len(raw) > MAX_TRACE or not raw.startswith(MAGIC):
        raise ValueError('기록 크기 또는 형식 오류')
    offset, initial, pending, final = 8, None, None, None
    frames, commands, last_tick = [], 0, -1
    setup, bootstrap, player_names = None, None, None
    while offset < len(raw):
        if offset + 8 > len(raw):
            raise ValueError('잘린 기록 헤더')
        kind, length = struct.unpack_from('<2I', raw, offset)
        offset += 8
        if length > 4096 or offset + length > len(raw):
            raise ValueError('잘린 또는 과도한 기록 데이터')
        payload = raw[offset:offset+length]
        offset += length
        if final is not None:
            raise ValueError('종료 뒤 추가 데이터')
        if kind == 1:
            if initial is not None or length != INITIAL_SIZE:
                raise ValueError('초기 상태 오류')
            initial = payload
            if struct.unpack_from('<I', initial, 16)[0] not in (0,1):
                raise ValueError('지원하지 않는 네트워크 경기 모드')
            # Game filenames must be bounded, terminated strings.
            for segment in (initial[20:276], initial[276:532]):
                if b'\0' not in segment:
                    raise ValueError('지도 문자열 종료 누락')
                segment.split(b'\0')[0].decode('cp949')
        elif kind == 9:
            if initial is None or player_names is not None or setup is not None or bootstrap is not None or pending is not None or frames or length!=516 or struct.unpack_from('<I',payload)[0]!=1:
                raise ValueError('플레이어 아이디 기록 순서/크기 오류')
            names=[]
            for i in range(8):
                value=payload[4+i*64:4+(i+1)*64]
                if b'\0' not in value: raise ValueError('플레이어 아이디 종료 누락')
                name=value.split(b'\0')[0].decode('cp949')
                if any(ord(c)<32 or ord(c)==127 for c in name): raise ValueError('플레이어 아이디 제어 문자 오류')
                names.append(name)
            player_names=tuple(names)
        elif kind == 7:
            if initial is None or setup is not None or pending is not None or frames or length not in (SETUP.size, SETUP.size+16):
                raise ValueError('자동 경기 설정 순서/크기 오류')
            values=SETUP.unpack(payload[:SETUP.size])
            if values[0] not in (1,2) or values[1:3] != (0,1) or values[3]>5 or values[4]>=8 or any(v>1 for v in values[5:13]) or any(v>3 for v in values[13:]) or values[5+values[4]]!=0 or (values[0]==2 and length!=100) or any(v>2 for v in payload[84:93]):
                raise ValueError('지원하지 않는 자동 경기 설정')
            setup=payload
        elif kind == 8:
            if initial is None or struct.unpack_from('<I',initial,16)[0]!=1 or bootstrap is not None or length!=BOOTSTRAP.size:
                raise ValueError('멀티 초기화 데이터 순서/크기 오류')
            if pending is None and frames:
                raise ValueError('멀티 초기화 데이터가 경기 배치 밖에 있습니다')
            if pending is not None and (not pending[1] or len(pending[1][-1][3])!=96 or struct.unpack_from('<H',pending[1][-1][3])[0]!=1007):
                raise ValueError('멀티 초기화 데이터가 지도 초기화 명령 뒤에 있지 않습니다')
            values=BOOTSTRAP.unpack(payload)
            if (values[0]!=1 or values[1:3]!=(0,1) or not 1<=values[3]<=11 or values[4]>=8 or
                not 1<=values[7]<=1000 or any(v>2 for v in values[-1][:9])):
                raise ValueError('지원하지 않는 멀티 초기화 설정')
            for i in range(11):
                controller,race,descriptor,ready=values[8+i*4:12+i*4]
                # Native auxiliary slot 10 is assigned a controller by map initialization.
                if (i<10 and controller>3) or race not in (1000,2000,3000,4000) or descriptor>65535 or ready>1:
                    raise ValueError('손상된 멀티 참가자 설정')
            bootstrap=payload
        elif kind == 2:
            if initial is None or pending is not None or length != STATE.size:
                raise ValueError('틱 시작 순서 오류')
            before = STATE.unpack(payload)
            if before[0] < last_tick or (not frames and before != STATE.unpack_from(initial)):
                raise ValueError('틱 순서 또는 초기 상태 불일치')
            pending = [before, [], None]
            last_tick = before[0]
        elif kind == 3:
            if pending is None or length < COMMAND.size:
                raise ValueError('명령 처리 순서 오류')
            tick, seed, count, size = COMMAND.unpack_from(payload)
            if not 2 <= size <= 2048 or length != COMMAND.size+size or tick != pending[0][0]:
                raise ValueError('명령 길이 또는 틱 오류')
            if not pending[1] and (seed,count) != pending[0][1:3]:
                raise ValueError('첫 명령 난수 상태 불일치')
            pending[1].append((tick, seed, count, payload[16:]))
            commands += 1
        elif kind == 4:
            if pending is None or length != STATE.size:
                raise ValueError('틱 종료 순서 오류')
            after = STATE.unpack(payload)
            if after[0] != pending[0][0]:
                raise ValueError('명령 처리 중 틱 변경')
            frames.append((pending[0], tuple(pending[1]), after))
            pending = None
        elif kind == 5:
            if pending is not None or not frames or length != STATE.size:
                raise ValueError('미완성 경기 기록')
            final = STATE.unpack(payload)
            if final[0] < last_tick:
                raise ValueError('경기 종료 틱 오류')
        else:
            raise ValueError('알 수 없는 기록 종류')
    if initial is None or final is None:
        raise ValueError('미완성 경기 기록')
    return Trace(initial, tuple(frames), final, commands, raw, setup, bootstrap, player_names)

def native_dir():
    if getattr(sys, 'frozen', False):
        return Path(sys._MEIPASS) / 'native'
    return Path(__file__).resolve().parents[1] / 'dist' / 'native'

def _key(root, create=False):
    path = Path(root) / 'engine-local.key'
    if not path.exists() and create:
        path.parent.mkdir(parents=True, exist_ok=True)
        try:
            with path.open('xb') as f:
                f.write(os.urandom(32))
                f.flush()
                os.fsync(f.fileno())
        except FileExistsError:
            pass
    if not path.exists() or path.stat().st_size != 32:
        raise ValueError('이 PC에서 기록한 파일임을 확인할 수 없습니다. 외부 리플레이 재생은 패킷 의미 검증 구현 전까지 제한됩니다.')
    return path.read_bytes()

def _signature(key, manifest):
    unsigned = {k:v for k,v in manifest.items() if k != 'local_signature'}
    return hmac.new(key, json.dumps(unsigned,sort_keys=True,separators=(',',':'),ensure_ascii=False).encode(),hashlib.sha256).hexdigest()

def resolve_map(game, trace):
    root = Path(game).resolve().parent
    directory, name = trace.map_directory.replace('\\','/'), trace.map_name.replace('\\','/')
    directory_path = Path(directory)
    directory_tail = directory[len(directory_path.drive):]
    if not name or '\x00' in name or '\x00' in directory or ':' in name or ':' in directory_tail:
        raise ValueError('지도 경로 오류')
    candidate = (root / directory / name).resolve()
    if not candidate.is_relative_to(root) or candidate.suffix.lower() != '.kfm' or not candidate.is_file():
        raise ValueError('설치 폴더 안에서 기록한 .kfm 지도를 찾지 못했습니다')
    return candidate

def read_population(source, trace):
    source=Path(source)
    if source.name.endswith('.complete'):
        source=source.with_suffix('')
    sidecar=Path(str(source)+'.population')
    if not sidecar.exists():
        return []
    if sidecar.stat().st_size>30008:
        raise ValueError('보급 메타데이터 크기 제한 초과')
    raw=sidecar.read_bytes()
    if raw[:8]!=b'KUFPOP01':
        raise ValueError('보급 메타데이터 헤더 오류')
    commands=[command for _,batch,_ in trace.frames for command in batch]
    start=struct.unpack_from('<I',trace.initial)[0]
    interval=BOOTSTRAP.unpack(trace.bootstrap)[7] if trace.bootstrap else 30
    result=[];seen=set()
    for offset in range(8,len(raw)-19,20):
        ordinal,tick,player,used,capacity=struct.unpack_from('<5I',raw,offset)
        if ordinal>=len(commands):
            continue # Recovery may intentionally discard the final batch.
        command=commands[ordinal];data=command[3]
        opcode=struct.unpack_from('<H',data)[0]
        if (ordinal in seen or command[0]!=tick or len(data)<4 or
            struct.unpack_from('<H',data,2)[0]!=player or player>=8 or
            opcode not in (170,5170,400,401,500) or used>1000000 or capacity>1000000 or
            not 0<=(tick-start)*interval<=300000):
            raise ValueError('보급 메타데이터 명령 불일치')
        seen.add(ordinal);result.append([ordinal,used,capacity])
    return result


def package_capture(source, game, store, recovery=None, population_source=None):
    source = Path(source)
    if source.stat().st_size > MAX_TRACE:
        raise ValueError('기록 크기 제한 초과')
    trace = parse_trace(source.read_bytes())
    with Path(game).open('rb') as f:
        game_hash = hashlib.file_digest(f,'sha256').hexdigest()
    if not supported_game_hash(game_hash):
        raise ValueError('기록 대상 게임 빌드 불일치')
    map_path = resolve_map(game, trace)
    if map_path.stat().st_size > 64*1024*1024:
        raise ValueError('지도 크기 제한 초과')
    contents = {'initial.bin':trace.initial, 'commands.bin':trace.raw, 'map.bin':map_path.read_bytes()}
    contents['rng.bin'] = b''.join(struct.pack('<3I',*frame[0][:3]) for frame in trace.frames)
    contents['checksums.bin'] = b''.join(struct.pack('<2I',frame[0][0],frame[0][3]) for frame in trace.frames)
    hashes = {name:hashlib.sha256(data).hexdigest() for name,data in contents.items()}
    stamp = datetime.fromtimestamp(source.stat().st_mtime,timezone.utc).isoformat()
    interval=BOOTSTRAP.unpack(trace.bootstrap)[7] if trace.bootstrap else 30
    playable=not trace.network_mode or trace.bootstrap is not None
    metadata = {'format_version':1, 'complete':True, 'game_sha256':game_hash,
                'rules_sha256':RULES, 'map_sha256':hashes['map.bin'], 'game_version':'Steam 1.416',
                'rules_version':'native-v1-experimental', 'adapter_id':ADAPTER, 'recorded_at':stamp,
                'recorder_version':VERSION,
                'map_id':trace.map_name, 'ticks':max(1,trace.final[0]-trace.frames[0][0][0]+1),
                'tick_rate':max(1,1000//interval), 'tick_interval_ms':interval, 'duration_verified':True, 'hashes':hashes,
                'frame_count':len(trace.frames), 'command_count':trace.commands,
                'initial_restore':('automatic-network-bootstrap' if trace.bootstrap else
                                   'multiplayer-lobby-restore-required' if trace.network_mode else
                                   'automatic-native-setup' if trace.setup else 'verified-legacy-defaults'),
                'setup_version':struct.unpack_from('<I',trace.setup)[0] if trace.setup else 0,
                'network_mode':trace.network_mode,
                'recording_mode':'multiplayer' if trace.network_mode else 'singleplayer',
                'playback_supported':playable,
                'multiplayer_supported':bool(trace.bootstrap)}
    try:
        population=read_population(population_source or source,trace)
    except (OSError,ValueError):
        population=[] # Optional display metadata must never prevent replay saving.
    if population:
        metadata['build_order_population']=population
    if recovery:
        metadata['recovery']=recovery
    # Exact time uses tick_interval_ms; tick_rate is retained for format-v1 readers.
    metadata['local_signature'] = _signature(_key(store.root,create=True),metadata)
    temp = store.root / (uuid.uuid4().hex+'.partial')
    try:
        # Data is stored uncompressed to avoid rejecting legitimately repetitive tick/RNG streams.
        with zipfile.ZipFile(temp,'w',zipfile.ZIP_STORED) as z:
            z.writestr('manifest.json',json.dumps(metadata,ensure_ascii=False))
            for name,data in contents.items():
                z.writestr(name,data)
        result = store.import_file(temp,permanent=False)
        source.rename(source.with_suffix(source.suffix+'.processed'))
        return result
    finally:
        temp.unlink(missing_ok=True)

def recover_closed_capture(source, game, store):
    """Recover a structurally complete prefix; preserve the original unfinished trace."""
    source=Path(source)
    if source.stat().st_size>MAX_TRACE:
        raise ValueError('기록 크기 제한 초과')
    raw=source.read_bytes()
    offset=8;last_before=None;last_after=None;complete_end=0;complete_before=None
    while offset<len(raw):
        if offset+8>len(raw): break
        kind,length=struct.unpack_from('<2I',raw,offset)
        if length>4096: raise ValueError('기록 크기 오류')
        if offset+8+length>len(raw): break
        if kind==5: raise ValueError('이미 마감된 기록입니다')
        if kind==2 and length==STATE.size:
            last_before=(offset,raw[offset+8:offset+8+length])
        if kind==4 and length==STATE.size:
            last_after=raw[offset+8:offset+8+length]
            complete_end=offset+8+length;complete_before=last_before
        offset+=8+length
    if complete_before is None or last_after is None: raise ValueError('복구 가능한 경기 경계 없음')
    # Ignore only the torn final write; validate every complete record in the prefix.
    original=parse_trace(raw[:complete_end]+struct.pack('<2I',5,STATE.size)+last_after)
    if len(original.frames)<2: raise ValueError('기록이 너무 짧아 복구할 수 없습니다')
    recovered=raw[:complete_before[0]]+struct.pack('<2I',5,STATE.size)+complete_before[1]
    trace=parse_trace(recovered)
    staging=store.root/'Recovery';staging.mkdir(parents=True,exist_ok=True)
    temporary=staging/(uuid.uuid4().hex+'.complete')
    temporary.write_bytes(recovered)
    os.utime(temporary,(source.stat().st_atime,source.stat().st_mtime))
    try:
        return package_capture(temporary,game,store,recovery={
            'reason':'missing-end-record', 'source':source.name,
            'discarded_batches':1, 'discarded_commands':original.commands-trace.commands,
            'truncated_tail_bytes':len(raw)-complete_end,
            'final_tick':trace.final[0]},population_source=source)
    finally:
        temporary.unlink(missing_ok=True)

def stage_playback(path, game, store):
    m = validate(path)
    if m['adapter_id'] != ADAPTER or not supported_game_hash(m['game_sha256']) or m['rules_sha256'] != RULES:
        raise ValueError('게임 빌드 또는 기록 규칙이 이 재생 어댑터와 호환되지 않습니다')
    with zipfile.ZipFile(path) as z:
        # Recheck the reopened archive: replacement after validate must not reach the engine.
        entries = z.infolist()
        if len(entries) != len(ALLOWED) or {i.filename for i in entries} != ALLOWED:
            raise ValueError('검증 중 리플레이 파일 구조가 변경되었습니다')
        if sum(i.file_size for i in entries) > MAX_TOTAL or z.getinfo('manifest.json').file_size > MAX_MANIFEST:
            raise ValueError('리플레이 데이터 크기 제한 초과')
        if json.loads(z.read('manifest.json')) != m:
            raise ValueError('검증 중 리플레이 파일이 변경되었습니다')
        for entry, digest in m['hashes'].items():
            if hashlib.sha256(z.read(entry)).hexdigest() != digest:
                raise ValueError('검증 중 리플레이 데이터가 변경되었습니다')
        raw = z.read('commands.bin')
        trace = parse_trace(raw)
        if (m.get('map_id')!=trace.map_name or m.get('frame_count')!=len(trace.frames) or
            m.get('command_count')!=trace.commands or m.get('network_mode')!=trace.network_mode or
            m.get('ticks')!=max(1,trace.final[0]-trace.frames[0][0][0]+1)):
            raise ValueError('메타데이터와 실제 경기 기록 불일치')
        if z.read('initial.bin') != trace.initial:
            raise ValueError('초기 상태와 명령 기록 불일치')
        if trace.network_mode and trace.bootstrap is None:
            raise ValueError('이 기록에는 지도 초기화 설정이 누락되어 있습니다. 0.2.11의 수집 시점 오류로 생성된 기록도 해당합니다. 0.2.12 이상 런처로 새 경기를 기록해 주세요.')
        expected_rng=b''.join(struct.pack('<3I',*frame[0][:3]) for frame in trace.frames)
        expected_checksums=b''.join(struct.pack('<2I',frame[0][0],frame[0][3]) for frame in trace.frames)
        if z.read('rng.bin')!=expected_rng or z.read('checksums.bin')!=expected_checksums:
            raise ValueError('틱 난수/검사 기록 불일치')
        embedded_map=z.read('map.bin')
    # Use the included map in a content-addressed user cache; preserve installed maps.
    try:
        map_path=resolve_map(game,trace)
        identical=hashlib.sha256(map_path.read_bytes()).hexdigest()==m['map_sha256']
    except ValueError:
        identical=False
    if not identical:
        name=trace.map_name
        if Path(name).name!=name or any(c in name for c in '/\\:') or not name.lower().endswith('.kfm'):
            raise ValueError('포함된 지도 이름/경로 오류')
        if (len(embedded_map)<108 or len(embedded_map)>64*1024*1024 or
            not embedded_map.startswith(b'KUF Map File v0.67\0') or not 1<=struct.unpack_from('<I',embedded_map,0x2c)[0]<=8):
            raise ValueError('포함된 지도 형식 오류')
        folder=store.root/'Playback'/'Maps'/m['map_sha256'];folder.mkdir(parents=True,exist_ok=True)
        if not folder.resolve().is_relative_to((store.root/'Playback'/'Maps').resolve()):
            raise ValueError('지도 캐시 경로 탈출')
        map_path=folder/name
        if not map_path.resolve().is_relative_to(folder.resolve()): raise ValueError('지도 캐시 파일 경로 탈출')
        if map_path.exists() and hashlib.sha256(map_path.read_bytes()).hexdigest()!=m['map_sha256']:
            raise ValueError('지도 캐시 무결성 오류')
        if not map_path.exists():
            with map_path.open('xb') as f: f.write(embedded_map)
        directory=(str(folder.resolve())+'\\').encode('cp949')
        if len(directory)>255: raise ValueError('지도 캐시 경로가 너무 깁니다')
        # Relocate only the loader path; preserve simulation state and command bytes.
        relocated=bytearray(raw);relocated[36:292]=directory+bytes(256-len(directory))
        raw=bytes(relocated);trace=parse_trace(raw)
    staging=store.root/'Playback';staging.mkdir(parents=True,exist_ok=True)
    target=staging/(uuid.uuid4().hex+'.trace')
    with target.open('xb') as f:
        f.write(raw);f.flush();os.fsync(f.fileno())
    return target, trace

def launch_engine(game, store, mode='record', replay=None):
    archive_engine_log(store.root)
    game=Path(game).resolve()
    identity=game_identity(game)
    record_game_identity(store.root,identity)
    if not supported_game_hash(identity.get('sha256')):
        raise ValueError('GB01: 지원하지 않는 게임 빌드입니다. 후킹하지 않았습니다.\n'
                         f"파일: {game}\nSHA256: {identity.get('sha256', '읽기 실패')}\n"
                         'Steam 파일 무결성 확인 후 다시 시도하거나 에러코드 폴더의 game-build.json을 제보해 주세요.')
    directory=native_dir()
    helper,dll=directory/'KUFReplayInjector.exe',directory/'KUFReplayEngine.dll'
    if not helper.exists() or not dll.exists():
        raise ValueError('네이티브 엔진 파일이 없습니다. native/build.cmd를 빌드하세요.')
    staged=None
    if mode in ('play','steam-play'):
        staged,_=stage_playback(replay,game,store)
    try:
        result=subprocess.run([str(helper),str(game),str(dll),str(store.root),mode,str(staged or '-')],
                              capture_output=True,text=True,timeout=100 if mode.startswith('steam-') else 40,creationflags=subprocess.CREATE_NO_WINDOW)
        if result.returncode:
            if result.returncode==4:
                raise ValueError('게임이 아직 실행 중입니다. 기존 게임 창을 정상적으로 닫고 다시 실행/재생하세요. 진행 중인 경기는 자동으로 종료하지 않습니다.')
            raise ValueError(result.stderr.strip() or '엔진 초기화 실패: engine.log 확인')
        return int(result.stdout.strip())
    finally:
        # The DLL loads the complete input before the helper reports success.
        if staged is not None:
            staged.unlink(missing_ok=True)

def collect_completed(game, store):
    # Multiple launcher windows must not package the same capture concurrently.
    with (store.root/'.capture-collect.lock').open('a+b') as lock:
        if lock.tell()==0: lock.write(b'0');lock.flush()
        lock.seek(0)
        try:
            msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
        except OSError:
            return [],[]
        try:
            return _collect_completed(game,store)
        finally:
            lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_UNLCK,1)

def _collect_completed(game, store):
    result, errors=[], []
    recovered_sources={m.get('recovery',{}).get('source') for _,m,_ in store.listing()}
    inbox=store.root/'CaptureInbox'
    sources=list(inbox.glob('*.partial.complete'))
    # Revalidate captures rejected by 0.2.12's auxiliary-slot enum check.
    # Other rejected files stay quarantined; package_capture validates all data again.
    for source in inbox.glob('*.partial.complete.rejected'):
        try:
            reason=json.loads(source.with_suffix(source.suffix+'.json').read_text(encoding='utf-8'))
            if reason.get('error') in ('손상된 멀티 참가자 설정','지원하지 않는 멀티 초기화 설정'):
                parse_trace(source.read_bytes())
                sources.append(source)
        except (ValueError,OSError,UnicodeError):
            continue
    for source in sources:
        try:
            result.append(package_capture(source,game,store))
        except (ValueError,OSError,UnicodeError,zipfile.BadZipFile) as error:
            rejected=source.with_suffix(source.suffix+'.rejected')
            source.rename(rejected)
            atomic_json(rejected.with_suffix(rejected.suffix+'.json'),{'error':str(error)})
            errors.append(str(error))
    for source in (store.root/'CaptureInbox').glob('*.partial'):
        match=re.fullmatch(r'(\d+)_\d+\.partial',source.name)
        if not match or process_alive(int(match[1])):
            continue
        if source.name in recovered_sources:
            source.rename(source.with_suffix('.partial.recovered'))
            continue
        try:
            if source.stat().st_size>MAX_TRACE: raise ValueError('기록 크기 제한 초과')
            try:
                parse_trace(source.read_bytes())
            except ValueError:
                result.append(recover_closed_capture(source,game,store))
                source.rename(source.with_suffix('.partial.recovered'))
            else:
                result.append(package_capture(source,game,store))
        except (ValueError,OSError,UnicodeError,zipfile.BadZipFile) as error:
            rejected=source.with_suffix('.partial.rejected')
            source.rename(rejected)
            atomic_json(rejected.with_suffix(rejected.suffix+'.json'),{'error':str(error)})
            errors.append(str(error))
    return result, errors

def process_alive(pid):
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.OpenProcess.argtypes=[ctypes.c_uint32,ctypes.c_int,ctypes.c_uint32]
    kernel.OpenProcess.restype=ctypes.c_void_p
    kernel.WaitForSingleObject.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
    kernel.CloseHandle.argtypes=[ctypes.c_void_p]
    handle=kernel.OpenProcess(0x100000,False,pid)
    if not handle:
        return ctypes.get_last_error()!=87  # Access denied/unknown is not evidence of exit.
    try:
        return kernel.WaitForSingleObject(handle,0)!=0
    finally:
        kernel.CloseHandle(handle)
