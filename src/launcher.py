import os
import queue
import threading
from datetime import datetime
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from pathlib import Path
from urllib.error import HTTPError
from replay_store import Store
from updater import VERSION, download_latest
from engine import ADAPTER, launch_engine, collect_completed, process_alive, replay_time

DEFAULT_GAME = Path(r'C:\Program Files (x86)\Steam\steamapps\common\Kingdom Under Fire War of Heroes\KingdomUnderFire.exe')

class Launcher:
    def __init__(self, root):
        self.root = root
        self.store = Store()
        self.events = queue.Queue()
        self.busy = False
        self.pending = []
        self.work_key = None
        self.rows = {}
        root.title(f'KUF 리플레이 런처 {VERSION}')
        root.geometry('980x620')
        root.minsize(840, 560)
        outer = ttk.Frame(root, padding=18)
        outer.pack(fill='both', expand=True)
        ttk.Label(outer, text='KINGDOM UNDER FIRE · REPLAY', font=('Segoe UI', 18, 'bold')).pack(anchor='w')
        ttk.Label(outer, text='싱글·멀티 자동 기록 · 관전자 자동 재생(실험용) · 멀티 실전 검증 중', foreground='#b45309').pack(anchor='w', pady=(6, 14))
        game = ttk.Frame(outer)
        game.pack(fill='x')
        self.game_path = tk.StringVar(value=self.store.config.get('game_path', str(DEFAULT_GAME)))
        ttk.Entry(game, textvariable=self.game_path).pack(side='left', fill='x', expand=True)
        ttk.Button(game, text='게임 경로', command=self.pick_game).pack(side='left', padx=5)
        ttk.Label(outer, text='Steam 실행 + 싱글·멀티 자동 기록 · 실행 중인 게임은 먼저 닫으세요 · 계정 안내창은 ‘아니요’').pack(anchor='w', pady=(8,0))
        controls = ttk.Frame(outer)
        controls.pack(fill='x', pady=14)
        for title, fn in [('폴더 열기', self.open_folder), ('리플레이 가져오기', self.import_replay), ('기존 폴더 이전', self.migrate), ('새로고침', self.refresh), ('런처 업데이트', self.update)]:
            ttk.Button(controls, text=title, command=fn).pack(side='left', padx=(0, 6))
        settings = ttk.Frame(outer)
        settings.pack(fill='x', pady=(0, 10))
        ttk.Label(settings, text='최근 자동 기록 보관 개수').pack(side='left')
        self.keep = tk.StringVar(value=str(self.store.config['keep']))
        ttk.Spinbox(settings, from_=1, to=10000, width=6, textvariable=self.keep).pack(side='left', padx=8)
        ttk.Button(settings, text='설정 저장', command=self.save_settings).pack(side='left')
        ttk.Label(settings, text='영구저장 경기는 자동 삭제에서 제외').pack(side='left', padx=16)
        frame = ttk.Frame(outer)
        frame.pack(fill='both', expand=True)
        self.tree = ttk.Treeview(frame, columns=('date', 'map', 'duration', 'pin', 'compat'), show='headings', selectmode='browse')
        for col, label, width in [('date','기록 시각',210), ('map','지도',170), ('duration','길이',70), ('pin','영구저장',80), ('compat','재생 상태',270)]:
            self.tree.heading(col, text=label)
            self.tree.column(col, width=width)
        self.tree.pack(side='left', fill='both', expand=True)
        scroll = ttk.Scrollbar(frame, orient='vertical', command=self.tree.yview)
        scroll.pack(side='right', fill='y')
        self.tree.configure(yscrollcommand=scroll.set)
        self.tree.bind('<Double-1>', lambda _: self.play())
        actions = ttk.Frame(outer)
        actions.pack(fill='x', pady=10)
        ttk.Button(actions, text='선택 리플레이 재생', command=self.play).pack(side='left')
        ttk.Button(actions, text='영구저장 / 해제', command=self.toggle_pin).pack(side='left', padx=8)
        self.game_button = ttk.Button(actions, text='게임 실행 (Steam)', command=self.start_game)
        self.game_button.pack(side='right')
        self.status = tk.StringVar(value=f'저장 폴더: {self.store.replays}')
        ttk.Label(outer, textvariable=self.status, wraplength=930).pack(anchor='w')
        self.work(self.store.listing, self.render)
        root.after(100, self.poll)
        root.after(2000, self.collect)

    def collect(self):
        inbox = self.store.root/'CaptureInbox'
        needs_collection = any(inbox.glob('*.partial.complete'))
        if not needs_collection and not self.busy:
            for capture in inbox.glob('*.partial'):
                pid = capture.name.split('_', 1)[0]
                if pid.isdecimal() and not process_alive(int(pid)):
                    needs_collection = True
                    break
        if not self.busy and not self.pending and needs_collection:
            game_path = self.game_path.get()
            def run():
                completed, errors=collect_completed(game_path,self.store)
                return self.store.listing(), completed, errors
            def done(result):
                items, completed, errors=result
                self.render(items)
                self.status.set(f'{len(completed)}개 자동 저장' if not errors else '기록 검증 실패: '+ '; '.join(errors))
            self.work(run,done,key='collect',background=True)
        self.root.after(2000,self.collect)

    def work(self, fn, done, key=None, background=False):
        if self.busy:
            if background:
                return
            if key and (key == self.work_key or any(job[2] == key for job in self.pending)):
                return
            self.pending.append((fn, done, key))
            self.status.set('요청을 대기열에 넣었습니다. 현재 작업이 끝나면 자동으로 처리합니다.')
            return
        self.busy = True
        self.work_key = key
        self.status.set('작업 중… 검증 / 디스크 작업을 별도 스레드에서 처리합니다.')
        def worker():
            try:
                self.events.put((done, fn(), None))
            except Exception as e:
                self.events.put((done, None, e))
        threading.Thread(target=worker, daemon=True).start()

    def poll(self):
        try:
            done, result, error = self.events.get_nowait()
            self.busy = False
            self.work_key = None
            self.status.set(f'저장 폴더: {self.store.replays}')
            if error:
                messagebox.showerror('작업 실패', str(error))
            else:
                done(result)
            if self.pending:
                fn, callback, key = self.pending.pop(0)
                self.work(fn, callback, key=key)
        except queue.Empty:
            pass
        self.root.after(100, self.poll)

    def render(self, items):
        chosen = self.tree.selection()
        selected_path = self.rows.get(chosen[0], (None,))[0] if chosen else None
        self.tree.delete(*self.tree.get_children())
        self.rows = {}
        for p, m, pin in items:
            time_text = replay_time(m)
            compat='싱글 자동 재생 · 실험용' if m['adapter_id']==ADAPTER else '호환 어댑터 없음'
            if m['adapter_id']==ADAPTER and m.get('recording_mode')=='multiplayer':
                compat='멀티 자동 재생 · 실험용' if m.get('multiplayer_supported') else '멀티 기록 · 초기화 데이터 없음'
            if m.get('recovery'): compat='종료 누락 복구 · 마지막 배치 제외 · '+compat
            date_text=datetime.fromisoformat(m['recorded_at']).astimezone().strftime('%Y-%m-%d %H:%M:%S')
            row = self.tree.insert('', 'end', values=(date_text, m['map_id'], time_text, '★' if pin else '', compat))
            self.rows[row] = (p, m, pin)
            if p == selected_path:
                self.tree.selection_set(row)
        self.status.set(f'{len(items)}개 · {self.store.replays}')

    def refresh(self):
        self.work(self.store.listing, self.render, key='refresh')

    def selected(self):
        chosen = self.tree.selection()
        if not chosen:
            messagebox.showinfo('리플레이 선택', '리플레이를 선택하세요.')
            return None
        return self.rows.get(chosen[0])

    def play(self):
        item = self.selected()
        if item:
            p,m,_=item
            game=self.game_path.get()
            def run():
                return launch_engine(game,self.store,mode='steam-play',replay=p)
            def done(pid):
                self.status.set(f"자동 경기 로딩 · PID {pid} · {m['map_id']} · 메뉴/지도 선택 없이 시작합니다.")
            self.work(run,done,key='launch')

    def toggle_pin(self):
        item = self.selected()
        if item:
            p, _, pin = item
            def run():
                self.store.pin(p.name, not pin)
                return self.store.listing()
            self.work(run, self.render)

    def open_folder(self):
        os.startfile(self.store.replays)

    def import_replay(self):
        path = filedialog.askopenfilename(filetypes=[('KUF 리플레이', '*.kufreplay')])
        if path:
            def run():
                self.store.import_file(path)
                return self.store.listing()
            self.work(run, self.render)

    def migrate(self):
        path = filedialog.askdirectory(title='이전 리플레이 폴더 선택 (원본 유지, 복사만 수행)')
        if path:
            def run():
                self.store.migrate(path)
                return self.store.listing()
            self.work(run, self.render)

    def save_settings(self):
        keep = self.keep.get()
        def done(_):
            messagebox.showinfo('저장 완료', '보관 개수를 저장했습니다. 정리는 다음 기록/가져오기 때 적용됩니다.')
        self.work(lambda:self.store.set_keep(keep), done, key='settings')

    def pick_game(self):
        path = filedialog.askopenfilename(filetypes=[('게임 실행 파일', '*.exe')])
        if path:
            self.game_path.set(path)
            def save():
                self.store.config['game_path'] = path
                self.store.save()
            self.work(save, lambda _:None, key='game-path')

    def start_game(self):
        path=Path(self.game_path.get())
        self.work(lambda:launch_engine(path,self.store,mode='steam-record'), lambda pid:self.status.set(f'Steam 기록 엔진 연결 완료 · PID {pid} · 싱글·멀티 경기 종료 시 자동 저장'), key='launch')

    def update(self):
        def run():
            try:
                return download_latest(self.store.root)
            except HTTPError as e:
                if e.code == 404:
                    raise ValueError('공개된 정식 릴리스가 아직 없습니다. 릴리스 게시 후 이 버튼으로 새 런처를 받을 수 있습니다.') from e
                raise
        def done(path):
            if path is None:
                messagebox.showinfo('업데이트', '현재 런처가 최신 버전입니다.')
            else:
                messagebox.showinfo('다운로드 완료', f'SHA256 검사 완료. 새 런처 위치:\n{path}\n\n현재 런처를 닫고 새 파일을 실행하세요. 저장 폴더는 유지됩니다.')
                os.startfile(path.parent)
        self.work(run, done)

if __name__ == '__main__':
    root = tk.Tk()
    Launcher(root)
    root.mainloop()
