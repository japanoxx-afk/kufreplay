# kufreplay

Kingdom Under Fire: War of Heroes용 리플레이 런처 개발 저장소.

**현재 버전 0.2.2는 Steam 게임 실행·싱글플레이 자동 기록과 실험용 재생 엔진·보관함입니다. 실제 경기 기록과 정상 파일 저장은 확인했으나 같은 경기 재현은 아직 검증하지 못했습니다. 재생에는 동일한 경기 설정으로 직접 게임을 시작해야 하며, 멀티플레이와 관전자 기능은 미지원입니다. 원본 게임 파일을 수정하지 않고 지원 해시의 실행 중 메모리에만 등록된 후킹을 적용합니다.**

## 실행

빌드된 `dist/0.2.2/KUFReplayLauncher.exe`를 실행하거나 Python 3.10 이상에서 `Launch.cmd`를 실행합니다. 빌드된 실행 파일에는 엔진 DLL/주입기가 포함되며 Python 설치가 필요 없습니다. 소스 실행은 먼저 `native/build.cmd`를 빌드해야 합니다.

0.2.2의 하단 오른쪽 **게임 실행 (Steam)** 버튼은 설치된 App ID 2183600을 `steam://rungameid/2183600`으로 실행하고 새 게임 프로세스에 기록 엔진을 연결합니다. 기존 게임을 먼저 닫고 런처의 **기록 엔진 연결 완료** 안내를 확인하세요. 초기 네트워크 계정 안내창에서는 **아니요**를 누르세요. 싱글플레이 경기 후 게임 창을 정상 종료하면 자동 저장합니다. 실제 경기 기록·정상 파일 포장과 목록 등록을 확인했습니다. Steam 재생 실행 및 같은 경기 재현은 아직 미확인입니다.

- 게임 경로 지정 및 원본 게임 실행
- 실험용 싱글플레이 자동 기록과 정상 완료 기록 포장
- 기본 최근 20개, 1~10000개 보관 설정: 정상 기록 및 일반 항목 가져오기에 적용
- 날짜·시간 파일명, 영구저장/해제, 외부 리플레이 가져오기
- 외부 가져오기는 기본 영구저장으로 원치 않는 자동 삭제 방지
- `%LOCALAPPDATA%\KUFReplay\Replays`에 저장: 런처 이동·업데이트와 독립
- 기존 폴더 이전은 검증한 `.kufreplay`를 복사하고 원본 유지
- 미완성 `.partial`, 손상 파일, 지원하지 않는 파일은 목록 제외
- 더블클릭과 재생 버튼: 로컬 인증 기록의 재생 엔진 연결 후 동일 경기 설정에서 시작 (설정 자동 복원 미지원)

## 업데이트

버튼은 `japanoxx-afk/kufreplay`의 최신 정식 GitHub Release를 확인합니다. 현재보다 높은 `vMAJOR.MINOR.PATCH` 버전에 `KUFReplayLauncher.exe`와 `KUFReplayLauncher.exe.sha256`이 있어야 다운로드합니다. 크기 제한과 SHA256 검사 후 공통 데이터 폴더의 `Updates`에 저장하고 해당 폴더를 엽니다. 사용자가 새 실행 파일을 실행합니다. SHA256은 전송 무결성 검사이며 코드 서명이나 독립된 게시자 인증을 대신하지 않습니다.

이 작업에서는 릴리스 배포를 수행하지 않습니다. 릴리스가 없으면 버튼에 해당 이유가 표시됩니다.

## 개발 및 검증

```powershell
py -3 -m unittest discover -s tests -v
cmd /c native\test.cmd
powershell -ExecutionPolicy Bypass -File build.ps1
```

빌드 의존성: `py -3 -m pip install pyinstaller`. 정적 분석 의존성: `pefile`, `capstone`.
게임 리소스·실행 파일·사용자 정보는 저장소에 포함하지 않습니다.

현재 테스트 방법과 구현 경계는 [docs/ENGINE_NATIVE.md](docs/ENGINE_NATIVE.md), 초기 분석은 [docs/ENGINE.md](docs/ENGINE.md), 분석 원자료는 [docs/game-analysis.json](docs/game-analysis.json)을 참고하세요.
