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
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from replay_store import Store, validate, REQUIRED, ALLOWED, MAX_TOTAL, MAX_MANIFEST, atomic_json

ADAPTER = 'kuf-steam-1.416-experimental-v1'
GAME_HASH = '69f2575ad14c78d3c6f7c15d0187234be005f06abf095d3f1bd602f1c3722868'
RULES = hashlib.sha256(b'KUFReplay-native-command-adapter-v1-singleplayer-same-initial-state').hexdigest()
MAGIC = b'KUFTRC01'
MAX_TRACE = 128 * 1024 * 1024
STATE = struct.Struct('<4I')
INITIAL_SIZE = 664
COMMAND = struct.Struct('<4I')

@dataclass(frozen=True)
class Trace:
    initial: bytes
    frames: tuple
    final: tuple
    commands: int
    raw: bytes

    @property
    def map_name(self):
        return self.initial[276:532].split(b'\0')[0].decode('cp949')

    @property
    def map_directory(self):
        return self.initial[20:276].split(b'\0')[0].decode('cp949')

def parse_trace(raw):
    if len(raw) > MAX_TRACE or not raw.startswith(MAGIC):
        raise ValueError('기록 크기 또는 형식 오류')
    offset, initial, pending, final = 8, None, None, None
    frames, commands, last_tick = [], 0, -1
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
            if struct.unpack_from('<I', initial, 16)[0] != 0:
                raise ValueError('현재 어댑터는 싱글플레이만 지원합니다')
            # Game filenames must be bounded, terminated strings.
            for segment in (initial[20:276], initial[276:532]):
                if b'\0' not in segment:
                    raise ValueError('지도 문자열 종료 누락')
                segment.split(b'\0')[0].decode('cp949')
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
    return Trace(initial, tuple(frames), final, commands, raw)

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

def package_capture(source, game, store):
    source = Path(source)
    if source.stat().st_size > MAX_TRACE:
        raise ValueError('기록 크기 제한 초과')
    trace = parse_trace(source.read_bytes())
    with Path(game).open('rb') as f:
        game_hash = hashlib.file_digest(f,'sha256').hexdigest()
    if game_hash != GAME_HASH:
        raise ValueError('기록 대상 게임 빌드 불일치')
    map_path = resolve_map(game, trace)
    if map_path.stat().st_size > 64*1024*1024:
        raise ValueError('지도 크기 제한 초과')
    contents = {'initial.bin':trace.initial, 'commands.bin':trace.raw, 'map.bin':map_path.read_bytes()}
    contents['rng.bin'] = b''.join(struct.pack('<3I',*frame[0][:3]) for frame in trace.frames)
    contents['checksums.bin'] = b''.join(struct.pack('<2I',frame[0][0],frame[0][3]) for frame in trace.frames)
    hashes = {name:hashlib.sha256(data).hexdigest() for name,data in contents.items()}
    stamp = datetime.fromtimestamp(source.stat().st_mtime,timezone.utc).isoformat()
    metadata = {'format_version':1, 'complete':True, 'game_sha256':GAME_HASH,
                'rules_sha256':RULES, 'map_sha256':hashes['map.bin'], 'game_version':'Steam 1.416',
                'rules_version':'native-v1-experimental', 'adapter_id':ADAPTER, 'recorded_at':stamp,
                'map_id':trace.map_name, 'ticks':max(1,trace.final[0]-trace.frames[0][0][0]+1),
                'tick_rate':25, 'duration_verified':False, 'hashes':hashes,
                'frame_count':len(trace.frames), 'command_count':trace.commands,
                'initial_restore':'manual-identical-native-setup', 'multiplayer_supported':False}
    # Tick rate remains an estimate pending runtime measurement. UI does not show a false time.
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

def stage_playback(path, game, store):
    m = validate(path)
    if m['adapter_id'] != ADAPTER or m['game_sha256'] != GAME_HASH or m['rules_sha256'] != RULES:
        raise ValueError('게임 빌드 또는 기록 규칙이 이 재생 어댑터와 호환되지 않습니다')
    if not hmac.compare_digest(m.get('local_signature',''),_signature(_key(store.root),m)):
        raise ValueError('외부/변경된 명령 데이터는 아직 재생할 수 없습니다. 이 PC에서 직접 기록한 원본만 지원합니다.')
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
        if z.read('initial.bin') != trace.initial:
            raise ValueError('초기 상태와 명령 기록 불일치')
        expected_rng=b''.join(struct.pack('<3I',*frame[0][:3]) for frame in trace.frames)
        expected_checksums=b''.join(struct.pack('<2I',frame[0][0],frame[0][3]) for frame in trace.frames)
        if z.read('rng.bin')!=expected_rng or z.read('checksums.bin')!=expected_checksums:
            raise ValueError('틱 난수/검사 기록 불일치')
    # Do not overwrite installed maps. The experimental bootstrap requires the existing identical map.
    map_path=resolve_map(game,trace)
    with map_path.open('rb') as f:
        if hashlib.file_digest(f,'sha256').hexdigest()!=m['map_sha256']:
            raise ValueError('설치된 지도와 기록 당시 지도 데이터가 다릅니다')
    staging=store.root/'Playback';staging.mkdir(parents=True,exist_ok=True)
    target=staging/(uuid.uuid4().hex+'.trace')
    with target.open('xb') as f:
        f.write(raw);f.flush();os.fsync(f.fileno())
    return target, trace

def launch_engine(game, store, mode='record', replay=None):
    game=Path(game).resolve()
    with game.open('rb') as f:
        if hashlib.file_digest(f,'sha256').hexdigest()!=GAME_HASH:
            raise ValueError('지원하지 않는 게임 SHA256입니다. 후킹하지 않았습니다.')
    directory=native_dir()
    helper,dll=directory/'KUFReplayInjector.exe',directory/'KUFReplayEngine.dll'
    if not helper.exists() or not dll.exists():
        raise ValueError('네이티브 엔진 파일이 없습니다. native/build.cmd를 빌드하세요.')
    staged=None
    if mode=='play':
        staged,_=stage_playback(replay,game,store)
    try:
        result=subprocess.run([str(helper),str(game),str(dll),str(store.root),mode,str(staged or '-')],
                              capture_output=True,text=True,timeout=100 if mode=='steam-record' else 40,creationflags=subprocess.CREATE_NO_WINDOW)
        if result.returncode:
            raise ValueError(result.stderr.strip() or '엔진 초기화 실패: engine.log 확인')
        return int(result.stdout.strip())
    finally:
        # The DLL loads the complete input before the helper reports success.
        if staged is not None:
            staged.unlink(missing_ok=True)

def collect_completed(game, store):
    result, errors=[], []
    for source in (store.root/'CaptureInbox').glob('*.partial.complete'):
        try:
            result.append(package_capture(source,game,store))
        except (ValueError,OSError,UnicodeError,zipfile.BadZipFile) as error:
            rejected=source.with_suffix(source.suffix+'.rejected')
            source.rename(rejected)
            atomic_json(rejected.with_suffix(rejected.suffix+'.json'),{'error':str(error)})
            errors.append(str(error))
    return result, errors
