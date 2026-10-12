"""Local, bounded diagnostic reports for user-submitted bug reports."""
import json
import shutil
from datetime import datetime
from pathlib import Path
from updater import VERSION

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

def create_report(root):
    root=Path(root);errors=root/'Errors';errors.mkdir(parents=True,exist_ok=True)
    report=errors/datetime.now().strftime('Report-%Y%m%d-%H%M%S-%f');report.mkdir()
    sources=[root/'engine.log',root/'session-cleanup.log',errors/'launcher-errors.log']
    sources+=sorted((errors/'History').glob('*.log'),key=lambda p:p.stat().st_mtime,reverse=True)[:5]
    sources+=sorted((root/'CaptureInbox').glob('*.rejected.json'),key=lambda p:p.stat().st_mtime,reverse=True)[:20]
    for source in sources:
        if source.is_file():
            with source.open('rb') as f: data=f.read(1024*1024)
            (report/source.name).write_bytes(data)
    captures=[]
    for p in sorted((root/'CaptureInbox').glob('*'),key=lambda p:p.stat().st_mtime,reverse=True)[:30]:
        if p.is_file(): captures.append({'name':p.name,'bytes':p.stat().st_size})
    (report/'report.json').write_text(json.dumps({'launcher_version':VERSION,'created':datetime.now().isoformat(),
        'captures':captures},ensure_ascii=False,indent=2),encoding='utf-8')
    (report/'제보안내.txt').write_text('이 폴더를 ZIP으로 압축해 제보해 주세요.\n경기 종료 시각, 지도, 싱글/멀티, 종료 방법을 함께 적어 주세요.\n'
        'engine.log: 엔진 상태 코드 및 기록 마감\n*.rejected.json: 저장 검증 거부 사유\nlauncher-errors.log: 런처 작업 오류\n'
        '기록 원본과 리플레이 파일은 이 보고서에 포함되지 않습니다.\n',encoding='utf-8')
    (report/'에러코드.txt').write_text('engine.log의 Engine status code 설명\n'
        '1 기록 스레드/크기 오류\n2 기록 버퍼 초과\n3 상태 읽기 실패\n4 명령 크기 오류\n'
        '5 미지원 네트워크 상태\n6 기록 구조 오류\n7 초기 상태 불일치\n8 틱/난수/상태 불일치\n'
        '9 관전 입력 분리 실패\n10 정상 재생 완료 (오류 아님)\n11 디스크 저장/마감 실패\n'
        '12 실행 코드 무결성 불일치\n13 지도/설정 복원 실패\n14 멀티 초기화 데이터 누락\n'
        '상태 코드가 없으면 로그의 준비·기록 시작·마감 메시지와 rejected.json을 확인해 주세요.\n',encoding='utf-8')
    return report
