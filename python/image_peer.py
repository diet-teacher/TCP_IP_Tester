"""Run: python python/image_peer.py. Network work stays off Tk's UI thread."""
import queue
import socket
import threading
import time
from pathlib import Path
import tkinter as tk
from tkinter import ttk, filedialog
from tkinter.scrolledtext import ScrolledText
from PIL import ImageTk
from protocol import Frame, Decoder, encode, load_image, to_image, describe

class App:
    def __init__(self, root):
        self.root = root
        root.title('Python ↔ C++ · TCP Image Tester')
        root.geometry('950x780')
        self.events = queue.Queue()
        self.sock = self.listener = None
        self.lock = threading.Lock()
        self.generation = 0
        self.sequence = 1
        self.pending = {}
        self.received = None
        self.photo = None
        self.busy = False
        self.active = False
        self.host = tk.StringVar(value='127.0.0.1')
        self.port = tk.StringVar(value='9001')
        self.path = tk.StringVar(value=str(Path(__file__).resolve().parents[1]/'000028.JPG'))
        self.state = tk.StringVar(value='연결 안 됨')
        top = ttk.Frame(root, padding=12); top.pack(fill='x')
        for label, var, width in [('IPv4',self.host,18),('Port',self.port,8)]:
            ttk.Label(top,text=label).pack(side='left',padx=4)
            ttk.Entry(top,textvariable=var,width=width).pack(side='left')
        self.listen_button=ttk.Button(top,text='서버 대기',command=lambda:self.start(True)); self.listen_button.pack(side='left',padx=4)
        self.connect_button=ttk.Button(top,text='클라이언트 연결',command=lambda:self.start(False)); self.connect_button.pack(side='left',padx=4)
        ttk.Button(top,text='중지',command=self.stop).pack(side='left',padx=4)
        ttk.Label(root,textvariable=self.state).pack(anchor='w',padx=16)
        row=ttk.Frame(root,padding=12); row.pack(fill='x')
        ttk.Entry(row,textvariable=self.path,state='readonly').pack(side='left',fill='x',expand=True)
        ttk.Button(row,text='사진 선택',command=self.choose).pack(side='left',padx=4)
        self.send_button=ttk.Button(row,text='사진 전송',command=self.send_image); self.send_button.pack(side='left')
        ttk.Button(row,text='수신 PNG 저장',command=self.save).pack(side='left',padx=4)
        self.preview=ttk.Label(root,anchor='center'); self.preview.pack(fill='x')
        self.info=ttk.Label(root,text='GRAY(1) / RGB(3) / RGBA(4) · 8 bit · raw pixels ≤ 64 MiB',wraplength=900)
        self.info.pack(fill='x',padx=16,pady=8)
        self.logs=ScrolledText(root,height=18,state='disabled'); self.logs.pack(fill='both',expand=True,padx=12,pady=12)
        root.protocol('WM_DELETE_WINDOW',self.close)
        self.poll()

    def log(self, message):
        self.logs.configure(state='normal')
        if int(self.logs.index('end-1c').split('.')[0]) > 2000:
            self.logs.delete('1.0','1000.0')
        self.logs.insert('end',time.strftime('[%H:%M:%S] ')+message+'\n')
        self.logs.see('end'); self.logs.configure(state='disabled')

    def event(self, generation, name, value=None):
        self.events.put((generation,name,value))

    def start(self, server):
        if self.active:
            return
        try:
            host,port=self.host.get(),int(self.port.get())
            socket.inet_pton(socket.AF_INET,host)
            if not 1<=port<=65535: raise ValueError('Port must be 1..65535')
        except (ValueError,OSError) as e:
            self.log(str(e)); return
        self.active=True
        self.generation+=1
        generation=self.generation
        self.state.set('서버 대기 중' if server else '연결 중...')
        threading.Thread(target=self.network,args=(generation,server,host,port),daemon=True).start()

    def network(self, generation, server, host, port):
        sock=listener=None
        parser=Decoder()
        try:
            if server:
                listener=socket.socket(socket.AF_INET,socket.SOCK_STREAM)
                if hasattr(socket,'SO_EXCLUSIVEADDRUSE'):
                    listener.setsockopt(socket.SOL_SOCKET,socket.SO_EXCLUSIVEADDRUSE,1)
                listener.settimeout(.5)
                with self.lock:
                    if generation!=self.generation: return
                    self.listener=listener
                listener.bind((host,port)); listener.listen(1)
                while generation==self.generation:
                    try: sock,_=listener.accept(); break
                    except socket.timeout: continue
                if sock is None: return
            else:
                sock=socket.create_connection((host,port),timeout=10)
            sock.settimeout(15)
            with self.lock:
                if generation!=self.generation: return
                self.sock=sock
            self.event(generation,'connected')
            while generation==self.generation:
                try: data=sock.recv(65536)
                except socket.timeout: continue
                if not data:
                    if parser.buffer: raise ValueError(f'truncated frame: {len(parser.buffer)} buffered bytes')
                    break
                for frame in parser.feed(data):
                    # Validate + reconstruct before ACK. Tk drawing occurs in the UI thread.
                    if frame.kind==5:
                        picture=to_image(frame.payload)
                        self.event(generation,'image',(frame,picture))
                    else:
                        self.event(generation,'frame',frame)
                    if frame.kind!=4:
                        with self.lock:
                            if generation!=self.generation: return
                            sock.sendall(encode(Frame(4,frame.sequence,b'')))
                        self.event(generation,'log',f'TX ACK seq={frame.sequence}')
        except (OSError,ValueError) as e:
            self.event(generation,'log',f'ERROR: {e}')
        finally:
            if sock: sock.close()
            if listener: listener.close()
            self.event(generation,'disconnected')

    def stop(self):
        # Invalidate old events before closing; a previous reader cannot reset a new session.
        self.generation+=1
        for s in (self.sock,self.listener):
            if s:
                try: s.shutdown(socket.SHUT_RDWR)
                except OSError: pass
                s.close()
        self.sock=self.listener=None
        if self.pending: self.log(f'ACK 미확인 {len(self.pending)}건')
        self.pending.clear(); self.active=False; self.busy=False
        self.state.set('연결 안 됨')

    def choose(self):
        name=filedialog.askopenfilename(filetypes=[('Images','*.jpg *.jpeg *.png *.bmp *.tif *.tiff'),('All','*.*')])
        if name: self.path.set(name)

    def show(self, image, text):
        preview=image.copy(); preview.thumbnail((850,300))
        self.photo=ImageTk.PhotoImage(preview)
        self.preview.configure(image=self.photo); self.info.configure(text=text)

    def send_image(self):
        if self.sock is None or self.busy: return
        self.busy=True
        seq=self.sequence; self.sequence=self.sequence%0xFFFFFFFF+1
        generation=self.generation; path=self.path.get()
        self.pending[seq]=time.monotonic()
        def work():
            try:
                frame=Frame(5,seq,load_image(path)); data=encode(frame)
                self.event(generation,'sent_image',(frame,to_image(frame.payload)))
                with self.lock:
                    if generation!=self.generation or self.sock is None: return
                    self.sock.sendall(data)
                self.event(generation,'log',f'TX socket write complete seq={seq}; waiting for ACK')
            except (OSError,ValueError) as e:
                self.event(generation,'log',f'SEND ERROR: {e}')
                # A failed sendall may have sent a partial frame: never reuse this stream.
                self.event(generation,'send_failed')
            finally:
                self.event(generation,'idle')
        threading.Thread(target=work,daemon=True).start()

    def save(self):
        if self.received is None: self.log('수신 이미지가 없습니다.'); return
        name=filedialog.asksaveasfilename(defaultextension='.png',filetypes=[('PNG','*.png')])
        if name:
            try: self.received.save(name); self.log(f'IMAGE SAVED {name}')
            except OSError as e: self.log(str(e))

    def poll(self):
        for _ in range(100):
            try: generation,name,value=self.events.get_nowait()
            except queue.Empty: break
            if generation!=self.generation: continue
            if name=='log': self.log(value)
            elif name=='connected': self.state.set('연결됨 · C++ / Python 양방향 이미지 전송')
            elif name in ('disconnected','send_failed'): self.stop()
            elif name=='idle': self.busy=False
            elif name in ('image','sent_image'):
                frame,picture=value; text=('RX ' if name=='image' else 'TX QUEUED ')+describe(frame)
                if name=='image': self.received=picture
                self.show(picture,text); self.log(text)
                self.log('META HEX '+frame.payload[:24].hex(' ').upper())
            elif name=='frame':
                self.log('RX '+describe(value))
                if value.kind==4:
                    started=self.pending.pop(value.sequence,None)
                    self.log(f'ACK 확인 seq={value.sequence}' + (f' elapsed={(time.monotonic()-started)*1000:.0f} ms' if started else ' (late/unmatched)'))
        now=time.monotonic()
        for seq,started in list(self.pending.items()):
            if now-started>120:
                self.log(f'ACK TIMEOUT seq={seq}'); self.pending.pop(seq,None)
        self.send_button.configure(state='normal' if self.sock and not self.busy else 'disabled')
        for button in (self.listen_button,self.connect_button): button.configure(state='disabled' if self.active else 'normal')
        self.root.after(50,self.poll)

    def close(self):
        self.stop(); self.root.destroy()

if __name__=='__main__':
    root=tk.Tk(); App(root); root.mainloop()
