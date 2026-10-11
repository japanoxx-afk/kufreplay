"""Launch KUF through its installed Steam protocol handler."""
import os

APP_ID = 2183600
LAUNCH_URI = f'steam://rungameid/{APP_ID}'

def launch_steam_game():
    try:
        os.startfile(LAUNCH_URI)
    except OSError as error:
        raise ValueError('Steam 실행 요청에 실패했습니다. Steam 설치 및 로그인을 확인하세요.') from error
    return LAUNCH_URI
