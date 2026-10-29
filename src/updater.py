import hashlib
import json
import os
import re
import urllib.request
from pathlib import Path

VERSION = '0.2.63'
REPO = 'japanoxx-afk/kufreplay'
ASSET = 'KUFReplayLauncher.exe'
LIMIT = 100 * 1024 * 1024

def version_tuple(text):
    if not re.fullmatch(r'v?\d+\.\d+\.\d+', text):
        raise ValueError('지원하지 않는 릴리스 버전 표기')
    return tuple(map(int, text.lstrip('v').split('.')))

def read_url(url, limit):
    allowed = ('https://api.github.com/', 'https://github.com/' + REPO + '/releases/download/')
    if not url.startswith(allowed):
        raise ValueError('허용되지 않는 업데이트 주소')
    req = urllib.request.Request(url, headers={'User-Agent': 'KUFReplayLauncher', 'Accept': 'application/vnd.github+json'})
    with urllib.request.urlopen(req, timeout=25) as response:
        data = response.read(limit + 1)
    if len(data) > limit:
        raise ValueError('업데이트 크기 제한 초과')
    return data

def download_latest(root):
    release = json.loads(read_url(f'https://api.github.com/repos/{REPO}/releases/latest', 1024 * 1024))
    tag = release['tag_name']
    if version_tuple(tag) <= version_tuple(VERSION):
        return None
    if release.get('draft') or release.get('prerelease'):
        raise ValueError('정식 릴리스가 아닙니다')
    assets = {a['name']: a for a in release['assets']}
    if ASSET not in assets or ASSET + '.sha256' not in assets:
        raise ValueError('릴리스에 실행 파일 또는 SHA256 검증 파일이 없습니다')
    check = read_url(assets[ASSET + '.sha256']['browser_download_url'], 1024).decode('ascii').split()[0].lower()
    if not re.fullmatch('[0-9a-f]{64}', check):
        raise ValueError('업데이트 검증 파일 오류')
    body = read_url(assets[ASSET]['browser_download_url'], LIMIT)
    if hashlib.sha256(body).hexdigest() != check or not body.startswith(b'MZ'):
        raise ValueError('업데이트 무결성 검사 실패')
    folder = Path(root) / 'Updates' / tag.lstrip('v')
    folder.mkdir(parents=True, exist_ok=True)
    target = folder / ASSET
    temp = target.with_suffix('.partial')
    try:
        with temp.open('wb') as f:
            f.write(body)
            f.flush()
            os.fsync(f.fileno())
        os.replace(temp, target)
    finally:
        temp.unlink(missing_ok=True)
    return target
