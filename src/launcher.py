import hashlib
import os
import queue
import subprocess
import threading
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from pathlib import Path
from urllib.error import HTTPError
from replay_store import Store
from updater import VERSION, download_latest
from engine import ADAPTER, launch_engine, collect_completed

DEFAULT_GAME = Path(r'C:\Program Files (x86)\Steam\steamapps\common\Kingdom Under Fire War of Heroes\KingdomUnderFire.exe')
ANALYZED_HASH = '69f2575ad14c78d3c6f7c15d0187234be005f06abf095d3f1bd602f1c3722868'

class Launcher:
    def __init__(self, root):
        self.root = root
        self.store = Store()
        self.events = queue.Queue()
        self.busy = False
        self.rows = {}
        root.title(f'KUF 리플레이 런처 {VERSION}')
        root.geometry('980x620')
        root.minsize(840, 560)
        outer = ttk.Frame(root, padding=18)
        outer.pack(fill='both', expand=True)
        ttk.Label(outer, text='KINGDOM UNDER FIRE · REPLAY', font=('Segoe UI', 18, 'bold')).pack(anchor='w')
        ttk.Label(outer, text='실험용 엔진: 싱글플레이 · 재생 시 같은 지도/설정으로 경기 시작 필요 · 실경기 재현 검증 전', foreground='#b45309').pack(anchor='w', pady=(6, 14))
        game = ttk.Frame(outer)
        game.pack(fill='x')
        self.game_path = tk.StringVar(value=self.store.config.get('game_path', str(DEFAULT_GAME)))
        ttk.Entry(game, textvariable=self.game_path).pack(side='left', fill='x', expand=True)
        ttk.Button(game, text='게임 경로', command=self.pick_game).pack(side='left', padx=5)
        ttk.Button(game, text='게임 실행', command=self.start_game).pack(side='left')
        self.record = tk.BooleanVar(value=True)
        ttk.Checkbutton(outer, text='싱글플레이 경기 자동 기록 (실험용, 현재 실행 중인 게임은 먼저 닫아야 합니다)', variable=self.record).pack(anchor='w', pady=(8,0))
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
        self.status = tk.StringVar(value=f'저장 폴더: {self.store.replays}')
        ttk.Label(outer, textvariable=self.status, wraplength=930).pack(anchor='w')
        self.work(self.store.listing, self.render)
        root.after(100, self.poll)
        root.after(2000, self.collect)

    def collect(self):
        if not self.busy and any((self.store.root/'CaptureInbox').glob('*.partial.complete')):
            game_path = self.game_path.get()
            def run():
                completed, errors=collect_completed(game_path,self.store)
                return self.store.listing(), completed, errors
            def done(result):
                items, completed, errors=result
                self.render(items)
                self.status.set(f'{len(completed)}개 자동 저장' if not errors else '기록 검증 실패: '+ '; '.join(errors))
            self.work(run,done)
        self.root.after(2000,self.collect)

    def work(self, fn, done):
        if self.busy:
            messagebox.showinfo('작업 중', '현재 작업이 끝난 뒤 다시 시도하세요.')
            return
        self.busy = True
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
            self.status.set(f'저장 폴더: {self.store.replays}')
            if error:
                messagebox.showerror('작업 실패', str(error))
            else:
                done(result)
        except queue.Empty:
            pass
        self.root.after(100, self.poll)

    def render(self, items):
        self.tree.delete(*self.tree.get_children())
        self.rows = {}
        for p, m, pin in items:
            duration = m['ticks'] // m['tick_rate']
            time_text=f'{duration//60}:{duration%60:02d}' if m.get('duration_verified',True) else f"{m['ticks']}틱"
            compat='실험용 · 같은 경기 설정 필요' if m['adapter_id']==ADAPTER else '호환 어댑터 없음'
            row = self.tree.insert('', 'end', values=(m['recorded_at'], m['map_id'], time_text, '★' if pin else '', compat))
            self.rows[row] = (p, m, pin)
        self.status.set(f'{len(items)}개 · {self.store.replays}')

    def refresh(self):
        self.work(self.store.listing, self.render)

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
                return launch_engine(game,self.store,mode='play',replay=p)
            def done(pid):
                self.status.set(f'재생 엔진 연결 · PID {pid} · 게임에서 같은 지도/진영/슬롯/규칙으로 시작하세요.')
                messagebox.showinfo('실험용 재생 준비', f"지도: {m['map_id']}\n\n게임에서 기록 당시와 같은 경기 설정으로 싱글플레이를 시작하세요.\n초기 상태·틱·난수·체크섬이 다르면 재생을 중단합니다.\n현재는 경기 설정 자동 복원과 외부 PC 재생이 미지원입니다.")
            self.work(run,done)

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
        if self.busy:
            messagebox.showinfo('작업 중', '현재 작업이 끝난 뒤 설정을 저장하세요.')
            return
        try:
            self.store.set_keep(self.keep.get())
            messagebox.showinfo('저장 완료', '보관 개수를 저장했습니다. 정리는 다음 기록/가져오기 때 적용됩니다.')
        except Exception as e:
            messagebox.showerror('설정 오류', str(e))

    def pick_game(self):
        if self.busy:
            messagebox.showinfo('작업 중', '현재 작업이 끝난 뒤 경로를 변경하세요.')
            return
        path = filedialog.askopenfilename(filetypes=[('게임 실행 파일', '*.exe')])
        if path:
            self.game_path.set(path)
            self.store.config['game_path'] = path
            self.store.save()

    def start_game(self):
        path = Path(self.game_path.get())
        if self.record.get():
            self.work(lambda:launch_engine(path,self.store),lambda pid:self.status.set(f'기록 엔진 연결 · PID {pid} · 싱글플레이 경기 종료 후 자동 저장'))
            return
        def inspect():
            if path.name.lower() != 'kingdomunderfire.exe':
                raise ValueError('KingdomUnderFire.exe를 선택하세요')
            with path.open('rb') as f:
                return hashlib.file_digest(f, 'sha256').hexdigest()
        def launch(digest):
            if digest != ANALYZED_HASH:
                messagebox.showwarning('다른 게임 빌드', '분석한 실행 파일과 해시가 다릅니다. 게임은 원본 상태로 실행됩니다. 기록·재생은 미지원입니다.')
            try:
                subprocess.Popen([str(path)], cwd=path.parent)
            except OSError as e:
                messagebox.showerror('게임 실행 실패', str(e))
        self.work(inspect, launch)

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
