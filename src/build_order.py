"""Read-only opening build orders from validated native command traces."""
import hashlib
import json
import struct
import zipfile
from dataclasses import dataclass
from engine import ADAPTER, BOOTSTRAP, MAX_TRACE, RULES, parse_trace, supported_game_hash
from replay_store import validate, ALLOWED, MAX_TOTAL, MAX_MANIFEST
from build_order_catalog import UNIT_NAMES, COMMAND_NAMES

WINDOW_MS = 300_000
MAX_EVENTS = 20_000

@dataclass(frozen=True)
class BuildEvent:
    elapsed_ms: int
    player: int
    category: str
    name: str
    detail: str
    opcode: int
    population: int | None = None
    capacity: int | None = None
    ident: int | None = None

    @property
    def supply(self):
        return "—" if self.population is None else f"{self.population}/{self.capacity}"

    @property
    def time(self):
        seconds, milliseconds = divmod(self.elapsed_ms, 1000)
        return f'{seconds//60}:{seconds%60:02d}.{milliseconds:03d}'

@dataclass(frozen=True)
class BuildOrder:
    map_name: str
    players: dict
    events: tuple
    duration_ms: int


def decode_build_order(trace, interval_ms=30, population=None):
    if type(interval_ms) is not int or not 1 <= interval_ms <= 1000:
        raise ValueError('빌드오더 시간 간격 오류')
    start = struct.unpack_from('<I', trace.initial)[0]
    samples = {}
    if population is not None:
        if not isinstance(population, list) or len(population)>1500:
            raise ValueError('보급 기록 크기 오류')
        for row in population:
            if (not isinstance(row,list) or len(row)!=3 or any(type(v) is not int for v in row) or
                not 0<=row[0]<10_000_000 or any(not 0<=v<=1_000_000 for v in row[1:]) or row[0] in samples):
                raise ValueError('보급 기록 형식 오류')
            samples[row[0]]=row[1:]
    ordinal=-1
    events = []
    for before, commands, after in trace.frames:
        if (before[0]-start)*interval_ms > WINDOW_MS:
            break
        for tick, seed, random_count, data in commands:
            ordinal+=1
            op = struct.unpack_from('<H', data)[0]
            if op not in (170, 5170, 400, 401, 500):
                continue
            expected = 12 if op in (170, 5170) else 8
            if len(data) != expected:
                raise ValueError(f'빌드오더 명령 {op}의 크기가 올바르지 않습니다.')
            player = struct.unpack_from('<H', data, 2)[0]
            if player >= 8:
                raise ValueError('빌드오더 참가자 번호 오류')
            elapsed = (tick-start)*interval_ms
            if not 0 <= elapsed <= WINDOW_MS:
                continue
            if op in (170, 5170):
                ident = struct.unpack_from('<i', data, 8)[0]
                if not 0 <= ident < 116:
                    raise ValueError('빌드오더 건물 번호 오류')
                x, y = struct.unpack_from('<2h', data, 4)
                name = UNIT_NAMES.get(ident, COMMAND_NAMES.get((300, ident), f'건물 #{ident}'))
                category = '건설'
                detail = f'{"예약 건설 지시" if op==5170 else "건설 지시"} · 위치 ({x}, {y})'
            else:
                ident = struct.unpack_from('<i', data, 4)[0]
                if not 0 <= ident < (148 if op==500 else 116):
                    raise ValueError('빌드오더 생산·연구 번호 오류')
                if op == 500:
                    name = COMMAND_NAMES.get((500, ident), f'연구 #{ident}')
                    category = '건물 업그레이드' if name.startswith('Upgrade to ') else '연구·업그레이드'
                    detail = '연구·업그레이드 지시'
                else:
                    name = UNIT_NAMES.get(ident, COMMAND_NAMES.get((400, ident), f'유닛 #{ident}'))
                    category, detail = '생산', '생산 지시'
            events.append(BuildEvent(elapsed, player, category, name, detail, op, *samples.pop(ordinal, (None,None)), ident))
            if len(events) > MAX_EVENTS:
                raise ValueError('첫 5분의 빌드오더 항목 수 제한을 초과했습니다.')
    if samples:
        raise ValueError("보급 기록과 빌드오더 명령이 일치하지 않습니다.")
    players = {}
    # Empty/observer slots also have default race values in older bootstraps.
    # Only attribute orders to the slot explicitly recorded in each command.
    for event in events:
        players.setdefault(event.player, None)
    names = trace.player_names or ('',)*8
    players = {i: names[i] or f'P{i+1}' for i in sorted(players)}
    return BuildOrder(trace.map_name, players, tuple(events), min(WINDOW_MS, max(0, (trace.final[0]-start)*interval_ms)))


def load_build_order(path):
    metadata = validate(path)
    if metadata['adapter_id'] != ADAPTER or not supported_game_hash(metadata['game_sha256']):
        raise ValueError('이 게임 빌드의 빌드오더 분석은 아직 지원하지 않습니다.')
    if metadata['rules_sha256'] != RULES:
        raise ValueError('이 기록의 명령 규칙은 빌드오더 분석과 호환되지 않습니다.')
    with zipfile.ZipFile(path) as archive:
        entries = archive.infolist()
        if (len(entries) != len(ALLOWED) or {entry.filename for entry in entries} != ALLOWED or
            sum(entry.file_size for entry in entries) > MAX_TOTAL or
            archive.getinfo('manifest.json').file_size > MAX_MANIFEST or
            archive.getinfo('initial.bin').file_size != 664):
            raise ValueError('분석 중 리플레이 파일 구조가 변경되었습니다.')
        if archive.getinfo('commands.bin').file_size > MAX_TRACE:
            raise ValueError('빌드오더 기록 크기 제한 초과')
        raw = archive.read('commands.bin')
        if json.loads(archive.read('manifest.json')) != metadata or hashlib.sha256(raw).hexdigest() != metadata['hashes']['commands.bin']:
            raise ValueError('분석 중 리플레이 파일이 변경되었습니다.')
        initial = archive.read('initial.bin')
    trace = parse_trace(raw)
    if initial != trace.initial or metadata['map_id'] != trace.map_name:
        raise ValueError('빌드오더 초기 상태 또는 지도 정보 불일치')
    interval = metadata.get('tick_interval_ms', BOOTSTRAP.unpack(trace.bootstrap)[7] if trace.bootstrap else 30)
    return decode_build_order(trace, interval, metadata.get("build_order_population"))
