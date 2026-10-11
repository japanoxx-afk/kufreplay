"""Bounded data-only containers; committed archives only enter the library."""
import hashlib
import json
import os
import re
import shutil
import tempfile
import uuid
import zipfile
from datetime import datetime
from pathlib import Path

FORMAT = 1
MAX_TOTAL = 256 * 1024 * 1024
MAX_MANIFEST = 64 * 1024
ALLOWED = {'manifest.json', 'initial.bin', 'commands.bin', 'rng.bin', 'checksums.bin', 'map.bin'}
REQUIRED = ALLOWED - {'manifest.json'}
SHA = re.compile(r'^[0-9a-f]{64}$')

def data_root():
    return Path(os.environ.get('LOCALAPPDATA', str(Path.home() / 'AppData' / 'Local'))) / 'KUFReplay'

def atomic_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + '.' + uuid.uuid4().hex + '.partial')
    try:
        with tmp.open('w', encoding='utf-8') as f:
            json.dump(value, f, ensure_ascii=False, indent=2)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)
    finally:
        tmp.unlink(missing_ok=True)

def validate(path):
    path = Path(path)
    if path.stat().st_size > MAX_TOTAL:
        raise ValueError('리플레이 파일 크기 제한 초과')
    with zipfile.ZipFile(path) as z:
        entries = z.infolist()
        names = [i.filename for i in entries]
        if len(names) != len(set(names)) or set(names) != ALLOWED:
            raise ValueError('필수 파일 누락 또는 허용되지 않는 파일/경로')
        if sum(i.file_size for i in entries) > MAX_TOTAL:
            raise ValueError('압축 해제 크기 제한 초과')
        for i in entries:
            if i.flag_bits & 1 or i.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
                raise ValueError('지원하지 않는 암호화 또는 압축 방식')
            if (i.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError('심볼릭 링크 금지')
            if i.file_size > max(1024 * 1024, i.compress_size * 200):
                raise ValueError('과도한 압축률')
        info = z.getinfo('manifest.json')
        if info.file_size > MAX_MANIFEST:
            raise ValueError('메타데이터 크기 제한 초과')
        m = json.loads(z.read(info).decode('utf-8'))
        if not isinstance(m, dict) or m.get('format_version') != FORMAT or m.get('complete') is not True:
            raise ValueError('미완성 또는 지원하지 않는 리플레이 형식')
        for key in ('game_sha256', 'rules_sha256', 'map_sha256'):
            if not isinstance(m.get(key), str) or not SHA.fullmatch(m[key]):
                raise ValueError(f'{key} 무결성 정보 오류')
        for key in ('game_version', 'rules_version', 'adapter_id', 'recorded_at', 'map_id'):
            if not isinstance(m.get(key), str) or not 0 < len(m[key]) <= 256:
                raise ValueError(f'{key} 메타데이터 오류')
        datetime.fromisoformat(m['recorded_at'])
        if type(m.get('ticks')) is not int or not 0 < m['ticks'] < 2**63:
            raise ValueError('틱 정보 오류')
        if type(m.get('tick_rate')) is not int or not 1 <= m['tick_rate'] <= 1000:
            raise ValueError('틱 주기 오류')
        if 'tick_interval_ms' in m and (type(m['tick_interval_ms']) is not int or not 1<=m['tick_interval_ms']<=1000):
            raise ValueError('시뮬레이션 시간 간격 오류')
        if set(m.get('hashes', {})) != REQUIRED:
            raise ValueError('데이터 무결성 정보 누락')
        for name in REQUIRED:
            digest = hashlib.sha256()
            with z.open(name) as f:
                while chunk := f.read(1024 * 1024):
                    digest.update(chunk)
            if digest.hexdigest() != m['hashes'][name]:
                raise ValueError(f'{name} 데이터 손상')
        if m['hashes']['map.bin'] != m['map_sha256']:
            raise ValueError('지도 무결성 불일치')
        return m

class Store:
    def __init__(self, root=None):
        self.root = Path(root) if root else data_root()
        self.replays = self.root / 'Replays'
        self.replays.mkdir(parents=True, exist_ok=True)
        self.config_file = self.root / 'settings.json'
        self.config = {'keep': 20, 'permanent': []}
        if self.config_file.exists():
            self.config.update(json.loads(self.config_file.read_text(encoding='utf-8')))
        self.set_keep(self.config['keep'], save=False)

    def save(self):
        atomic_json(self.config_file, self.config)

    def set_keep(self, count, save=True):
        count = int(count)
        if not 1 <= count <= 10000:
            raise ValueError('보관 개수는 1~10000입니다')
        self.config['keep'] = count
        if save:
            self.save()

    def listing(self):
        items = []
        for p in self.replays.glob('*.kufreplay'):
            try:
                m = validate(p)
                items.append((p, m, p.name in self.config['permanent']))
            except (ValueError, OSError, zipfile.BadZipFile, KeyError, TypeError, UnicodeError, RuntimeError):
                continue
        return sorted(items, key=lambda x: x[0].name, reverse=True)

    def pin(self, name, permanent):
        if not any(p.name == name for p, _, _ in self.listing()):
            raise ValueError('리플레이를 찾을 수 없습니다')
        pins = set(self.config['permanent'])
        pins.add(name) if permanent else pins.discard(name)
        self.config['permanent'] = sorted(pins)
        self.save()

    def prune(self):
        normal = [p for p, _, pin in self.listing() if not pin]
        for p in normal[self.config['keep']:]:
            p.unlink()

    def import_file(self, source, permanent=True):
        # Validate the staged bytes, preventing replacement between inspection and copying.
        with tempfile.NamedTemporaryFile(dir=self.replays, suffix='.partial', delete=False) as f:
            tmp = Path(f.name)
        try:
            if Path(source).stat().st_size > MAX_TOTAL:
                raise ValueError('리플레이 파일 크기 제한 초과')
            with open(source, 'rb') as src, tmp.open('wb') as dst:
                total = 0
                while chunk := src.read(1024 * 1024):
                    total += len(chunk)
                    if total > MAX_TOTAL:
                        raise ValueError('리플레이 파일 크기 제한 초과')
                    dst.write(chunk)
                dst.flush()
                os.fsync(dst.fileno())
            m = validate(tmp)
            when = datetime.fromisoformat(m['recorded_at']).astimezone()
            dest = self.replays / (when.strftime('%Y-%m-%d_%H-%M-%S') + '_' + uuid.uuid4().hex[:8] + '.kufreplay')
            # Save protection before commit: pruning never sees an unprotected import.
            if permanent:
                self.config['permanent'].append(dest.name)
                self.save()
            os.replace(tmp, dest)
            self.prune()
            return dest
        finally:
            tmp.unlink(missing_ok=True)

    def migrate(self, directory):
        # Copy only; preserve the previous library and its source files.
        copied = 0
        for source in Path(directory).glob('*.kufreplay'):
            self.import_file(source, permanent=True)
            copied += 1
        return copied
