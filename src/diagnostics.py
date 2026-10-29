"""Local, bounded diagnostic reports for user-submitted bug reports."""
import json
import shutil
import hashlib
import struct
import os
import re
import zipfile
from datetime import datetime
from pathlib import Path
from updater import VERSION

def game_identity(path):
    path=Path(path).resolve()
    result={'path':str(path)}
    try:
        result['bytes']=path.stat().st_size
        with path.open('rb') as f:
            result['sha256']=hashlib.file_digest(f,'sha256').hexdigest()
            f.seek(0);header=f.read(64)
            if header[:2]==b'MZ' and len(header)==64:
                pe=struct.unpack_from('<I',header,60)[0]
                if pe<=result['bytes']-24:
                    f.seek(pe);coff=f.read(24)
                    if coff[:4]==b'PE\0\0':
                        result['machine']=hex(struct.unpack_from('<H',coff,4)[0])
                        result['pe_timestamp']=struct.unpack_from('<I',coff,8)[0]
    except OSError as error:
        result['error']=str(error)
    return result

def record_game_identity(root,identity):
    try:
        folder=Path(root)/'Errors';folder.mkdir(parents=True,exist_ok=True)
        (folder/'game-build.json').write_text(json.dumps(identity,ensure_ascii=False,indent=2),encoding='utf-8')
    except OSError as error:
        log_error(root,'game-build-report',error)

def log_error(root, category, error):
    try:
        folder=Path(root)/'Errors';folder.mkdir(parents=True,exist_ok=True)
        with (folder/'launcher-errors.log').open('a',encoding='utf-8') as f:
            f.write(json.dumps({'time':datetime.now().isoformat(), 'version':VERSION,
                                'category':category,'error':str(error)},ensure_ascii=False)+'\n')
    except OSError:
        pass

def archive_engine_log(root):
    root=Path(root);source=root/'engine.log'
    try:
        if source.is_file():
            folder=root/'Errors'/'History';folder.mkdir(parents=True,exist_ok=True)
            name=datetime.fromtimestamp(source.stat().st_mtime).strftime('engine-%Y%m%d-%H%M%S-%f.log')
            target=folder/name
            if not target.exists(): shutil.copy2(source,target)
    except OSError as error:
        log_error(root,'log-archive',error)

def create_report(root,game=None):
    root=Path(root);errors=root/'Errors';errors.mkdir(parents=True,exist_ok=True)
    report=errors/datetime.now().strftime('Report-%Y%m%d-%H%M%S-%f');report.mkdir()
    if game: record_game_identity(root,game_identity(game))
    sources=[root/'engine.log',root/'session-cleanup.log',errors/'launcher-errors.log',errors/'game-build.json']
    sources+=sorted((errors/'History').glob('*.log'),key=lambda p:p.stat().st_mtime,reverse=True)[:5]
    sources+=sorted((root/'CaptureInbox').glob('*.rejected.json'),key=lambda p:p.stat().st_mtime,reverse=True)[:20]
    truncated_logs=[]
    for source in sources:
        if source.is_file() and not source.is_symlink() and source.resolve().is_relative_to(root.resolve()):
            with source.open('rb') as f:
                if source.suffix=='.log' and source.stat().st_size>1024*1024:
                    f.seek(-1024*1024,2);truncated_logs.append(source.name)
                data=f.read(1024*1024)
            (report/source.name).write_bytes(data)
    captures=[]
    for p in sorted((root/'CaptureInbox').glob('*'),key=lambda p:p.stat().st_mtime,reverse=True)[:30]:
        if p.is_file(): captures.append({'name':p.name,'bytes':p.stat().st_size})
    (report/'report.json').write_text(json.dumps({'launcher_version':VERSION,'created':datetime.now().isoformat(),
        'captures':captures,'logs_kept_last_1MiB':truncated_logs},ensure_ascii=False,indent=2),encoding='utf-8')
    (report/'제보안내.txt').write_text('이 폴더를 ZIP으로 압축해 제보해 주세요.\n경기 종료 시각, 지도, 싱글/멀티, 종료 방법을 함께 적어 주세요.\n'
        'engine.log: 엔진 상태 코드 및 기록 마감\n*.rejected.json: 저장 검증 거부 사유\nlauncher-errors.log: 런처 작업 오류\n'
        '기록 원본과 리플레이 파일은 이 보고서에 포함되지 않습니다.\n',encoding='utf-8')
    (report/'에러코드.txt').write_text('engine.log의 Engine status code 설명\n'
        '1 기록 스레드/크기 오류\n2 기록 버퍼 초과\n3 상태 읽기 실패\n4 명령 크기 오류\n'
        '5 미지원 네트워크 상태\n6 기록 구조 오류\n7 초기 상태 불일치\n8 틱/난수/상태 불일치\n'
        '9 관전 입력 분리 실패\n10 정상 재생 완료 (오류 아님)\n11 디스크 저장/마감 실패\n'
        '12 실행 코드 무결성 불일치\n13 지도/설정 복원 실패\n14 멀티 초기화 데이터 누락\n'
        'RP01 관전자 정보 패널의 알려진 빈 객체 읽기 차단; 기본 정보 표시 유지\n'
        'RP02 안개 표시용 객체를 실제 유닛 선택/추적에서 제외\n'
        'MS01 게임 자체의 멀티 동기화 불일치 감지: 발생 틱·난수·플레이어별 체크섬 기록\n'
        'PO02 빌드오더 인구수 읽기 실패: 해당 보급 정보만 생략하고 게임 명령은 계속 처리\n'
        '상태 코드가 없으면 로그의 준비·기록 시작·마감 메시지와 rejected.json을 확인해 주세요.\n',encoding='utf-8')
    return report


REPORT_FILE_LIMIT = 128 * 1024 * 1024
REPORT_TOTAL_LIMIT = 256 * 1024 * 1024
CAPTURE_NAME = re.compile(r'(\d+_\d+\.partial)(?:\.(?:complete|processed|recovered|rejected|population|json))*')

def create_report_zip(root, game=None, replay=None):
    """Bundle bounded diagnostic evidence locally, including the unfinished tail."""
    root=Path(root).resolve()
    report=create_report(root,game)
    target=report.with_suffix('.zip')
    temporary=Path(str(target)+'.partial')
    included=[];skipped=[];total=0;related=None
    if replay is None:
        replays=sorted((root/'Replays').glob('*.kufreplay'),key=lambda p:p.stat().st_mtime,reverse=True)
        replay=replays[0] if replays else None
    if replay is not None:
        replay=Path(replay)
        # Only the small manifest is needed to find the recovered source family.
        # Treat it as untrusted: never join a manifest path to the filesystem.
        try:
            if replay.suffix!='.kufreplay' or replay.is_symlink(): raise ValueError('리플레이 형식/링크 오류')
            with zipfile.ZipFile(replay) as z:
                info=z.getinfo('manifest.json')
                if info.file_size>256*1024: raise ValueError('메타데이터 크기 제한')
                metadata=json.loads(z.read(info))
            source=metadata.get('recovery',{}).get('source','')
            if isinstance(source,str):
                match=CAPTURE_NAME.fullmatch(source)
                if match: related=match[1]
        except (OSError,ValueError,KeyError,TypeError,AttributeError,zipfile.BadZipFile) as error:
            skipped.append({'name':replay.name,'reason':'원본 기록 연결 정보 확인 실패: '+str(error)})
    families={}
    for source in (root/'CaptureInbox').glob('*'):
        match=CAPTURE_NAME.fullmatch(source.name)
        if match:
            try: families.setdefault(match[1],[]).append((source,source.stat().st_mtime))
            except OSError: pass
    newest=sorted(families,key=lambda key:max(t for _,t in families[key]),reverse=True)[:3]
    chosen=([related] if related else [])+[key for key in newest if key!=related]
    if related and not any('.population' not in p.name and p.suffix!='.json' for p,_ in families.get(related,[])):
        skipped.append({'name':'CaptureInbox/'+related,'reason':'이 PC에 해당 원본 기록 없음'})
    candidates=[]
    # Put the selected capture's raw tail ahead of older incidental captures.
    for key in chosen:
        files=sorted(families.get(key,[]),key=lambda item:('.population' in item[0].name or item[0].suffix=='.json',item[0].name))
        candidates.extend((p,'CaptureInbox/'+p.name) for p,_ in files)
    if replay is not None and replay.suffix=='.kufreplay':
        candidates.insert(0,(replay,'Replays/'+replay.name))
    try:
        with zipfile.ZipFile(temporary,'x',zipfile.ZIP_DEFLATED,compresslevel=1) as archive:
            for source in sorted(report.iterdir()):
                if source.is_file():
                    archive.write(source,'Logs/'+source.name);total+=source.stat().st_size
            for source,name in candidates:
                try:
                    if source.is_symlink() or not source.is_file(): raise ValueError('일반 파일이 아니거나 링크임')
                    if name.startswith('CaptureInbox/') and not source.resolve().is_relative_to((root/'CaptureInbox').resolve()):
                        raise ValueError('기록 폴더 외부 경로')
                    # Opening first keeps a renamed capture readable while the
                    # launcher collector finalizes it. Copy a bounded snapshot.
                    with source.open('rb') as stream:
                        before=os.fstat(stream.fileno());size=before.st_size
                        if size>REPORT_FILE_LIMIT or total+size>REPORT_TOTAL_LIMIT: raise ValueError('보고서 용량 제한 초과')
                        digest=hashlib.sha256();remaining=size;copied=0
                        with archive.open(name,'w') as out:
                            while remaining:
                                block=stream.read(min(1024*1024,remaining))
                                if not block: break
                                out.write(block);digest.update(block);copied+=len(block);remaining-=len(block)
                        after=os.fstat(stream.fileno())
                    total+=copied
                    included.append({'name':name,'bytes':copied,'sha256':digest.hexdigest(),
                                     'changed_during_copy':remaining!=0 or (before.st_size,before.st_mtime_ns)!=(after.st_size,after.st_mtime_ns)})
                except (OSError,ValueError) as error:
                    skipped.append({'name':name,'reason':str(error)})
            archive.writestr('bundle.json',json.dumps({'launcher_version':VERSION,'included':included,'skipped':skipped,
                'capture_families':chosen,'file_limit':REPORT_FILE_LIMIT,'total_limit':REPORT_TOTAL_LIMIT},ensure_ascii=False,indent=2))
            archive.writestr('제보안내.txt','이 ZIP 파일을 제보해 주세요. 자동 전송되지 않습니다.\n'
                'Logs: 엔진·런처 로그와 게임 버전 정보\nCaptureInbox: 최근 기록 원본, 복구 전 데이터, 인구수 파일\n'
                'Replays: 선택한 리플레이(미선택 시 가장 최근 파일)\nbundle.json: 포함/누락 파일과 사유, 복사 중 변경 여부\n'
                '플레이어 이름·게임 명령 및 로그의 경로가 포함될 수 있습니다. 인증 키와 게임 실행 파일은 포함하지 않습니다.\n'
                '가능하면 게임 종료 후 생성하고, 발생 시각·방장/참가자 여부를 함께 알려주세요.\n')
        os.replace(temporary,target)
    except Exception:
        temporary.unlink(missing_ok=True)
        raise
    return target
