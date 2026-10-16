"""Dark, side-by-side opening-order comparison without changing launcher styles."""
import tkinter as tk
import struct
from tkinter import font

BG='#050e17'
PANEL='#081925'
TEXT='#bed4df'
CYAN='#77c4df'


def show_window(root, path, report, game=None):
    window=tk.Toplevel(root,background=BG)
    window.title('빌드오더 · '+path.name)
    icons={}
    if game:
        try:
            from build_order_icons import load_icons
            icons={key:tk.PhotoImage(master=window,data=data) for key,data in load_icons(game,report.events).items()}
        except (OSError,ValueError,UnicodeError,IndexError,struct.error,tk.TclError):
            pass
    window.build_order_icons=icons # Keep Tk images alive for both independently scrolled panels.
    window.geometry('1080x680');window.minsize(780,440)
    body=tk.Frame(window,bg=BG,padx=18,pady=16);body.pack(fill='both',expand=True)
    tk.Label(body,text='BUILD ORDER  /  초반 5분',font=('Segoe UI',17,'bold'),fg='#eef8ff',bg=BG).pack(anchor='w')
    end=report.duration_ms//1000
    tk.Label(body,text=f'{report.map_name}    ·    0:00 – {end//60}:{end%60:02d}',fg=CYAN,bg=BG,font=('맑은 고딕',10)).pack(anchor='w',pady=(4,14))
    panels=tk.Frame(body,bg=BG);panels.pack(fill='both',expand=True)
    panels.columnconfigure((0,1),weight=1,uniform='players');panels.rowconfigure(0,weight=1)
    players=list(report.players)
    labels=[f'{report.players[p]}  ·  P{p+1}' for p in players]
    textfont=font.Font(window,family='맑은 고딕',size=10)
    timefont=font.Font(window,family='Segoe UI',size=10)

    def panel(column):
        frame=tk.Frame(panels,bg=PANEL,highlightbackground='#25485d',highlightthickness=1)
        frame.grid(row=0,column=column,sticky='nsew',padx=(0,7) if column==0 else (7,0))
        bar=tk.Frame(frame,bg='#102e42',padx=12,pady=10);bar.pack(fill='x')
        options=labels or ['참가자 없음']
        chosen=tk.StringVar(value=options[min(column,len(options)-1)])
        choice=tk.OptionMenu(bar,chosen,*options,command=lambda *_:select())
        choice.configure(bg='#0b2333',fg='#e1f2fb',activebackground='#1d435b',activeforeground='white',
                         font=('맑은 고딕',10),highlightbackground='#39617a',highlightthickness=1,relief='flat',anchor='w')
        choice['menu'].configure(bg='#0b2333',fg='#e1f2fb',activebackground='#25566e',activeforeground='white',font=('맑은 고딕',10))
        choice.pack(fill='x')
        count=tk.Label(bar,bg='#102e42',fg='#82aabd',font=('맑은 고딕',9));count.pack(anchor='w',pady=(5,0))
        header=tk.Canvas(frame,height=32,bg='#050e17',highlightthickness=0);header.pack(fill='x')
        area=tk.Frame(frame,bg=PANEL);area.pack(fill='both',expand=True)
        canvas=tk.Canvas(area,bg=PANEL,highlightthickness=0,yscrollincrement=34)
        scrollbar=tk.Scrollbar(area,command=canvas.yview,bg='#14364b',troughcolor=BG,activebackground=CYAN)
        scrollbar.pack(side='right',fill='y');canvas.pack(side='left',fill='both',expand=True)
        canvas.configure(yscrollcommand=scrollbar.set)
        rows=[]
        def redraw(event=None):
            width=max(240,canvas.winfo_width());height=canvas.winfo_height()
            header.delete('all')
            header.create_text(17,16,text='시간',fill=TEXT,anchor='w',font=textfont)
            header.create_text(112,16,text='활동',fill=TEXT,anchor='w',font=textfont)
            header.create_text(width-20,16,text='보급',fill=TEXT,anchor='e',font=textfont)
            header.create_line(0,31,width+20,31,fill='#315569')
            canvas.configure(scrollregion=(0,0,width,max(height,len(rows)*34)))
            canvas.delete('all')
            top=max(0,int(canvas.canvasy(0)//34)-1)
            bottom=min(len(rows),top+height//34+4)
            for index in range(top,bottom):
                item=rows[index];y=index*34
                canvas.create_rectangle(0,y,width,y+34,fill='#091c29' if index%2==0 else '#06131e',outline='')
                canvas.create_line(0,y+33,width,y+33,fill='#183140')
                seconds=item.elapsed_ms//1000
                canvas.create_text(17,y+17,text=f'{seconds//60:02d}:{seconds%60:02d}',anchor='w',fill=CYAN,font=timefont)
                opcode=300 if item.opcode in (170,5170) else 400 if item.opcode in (400,401) else item.opcode
                icon=icons.get((opcode,item.ident))
                if icon:
                    canvas.create_image(88,y+17,image=icon)
                title=item.name
                limit=max(20,width-222)
                while title and textfont.measure(title)>limit:
                    title=title[:-1]
                if title!=item.name:title=title[:-1]+'…'
                canvas.create_text(112,y+17,text=title,anchor='w',fill=TEXT,font=textfont)
                canvas.create_text(width-20,y+17,text=item.supply,anchor='e',fill=CYAN,font=timefont)
            if not rows:
                canvas.create_text(width/2,45,text='이 구간에 기록된 지시가 없습니다.',fill=TEXT,font=textfont)
        def scroll(*args):
            canvas.yview(*args);redraw()
        scrollbar.configure(command=scroll)
        def wheel(event):
            canvas.yview_scroll(-int(event.delta/120),'units');redraw();return 'break'
        canvas.bind('<MouseWheel>',wheel)
        canvas.bind('<Configure>',redraw);header.bind('<Configure>',redraw)
        def select(event=None):
            rows[:]=[e for e in report.events if players and e.player==players[labels.index(chosen.get())]]
            count.configure(text=f'{len(rows)}개 지시   ·   생산 / 건설 / 업그레이드')
            canvas.yview_moveto(0);redraw()
        select()
    panel(0);panel(1)
    tk.Label(body,text='시간은 지시 시각이며 완료를 뜻하지 않습니다.  보급: 당시 사용 인구수 / 한도 · 기록이 없으면 —',fg='#819aaa',bg=BG,font=('맑은 고딕',9),anchor='w',wraplength=1000).pack(fill='x',pady=(12,0))
    tk.Button(body,text='닫기',command=window.destroy,bg='#143348',fg='#e1f3ff',activebackground='#214c65',activeforeground='white',relief='flat',padx=24,pady=5).pack(anchor='e',pady=(8,0))
    window.bind('<Escape>',lambda _:window.destroy())
    return window
