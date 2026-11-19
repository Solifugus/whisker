#!/usr/bin/env python3
"""The code editor's files (design.md §5ae, and the second review,
2026-11-16): what it refuses to open, what it will not claim to have
saved, and a folder's hidden names not crowding out its files.

    python3 editorfiles.py <whisker-code-web binary> <file.bas> <scratch dir>
"""
import os, socket, subprocess, sys, threading, time

BIN, SRC, TMP = sys.argv[1], sys.argv[2], sys.argv[3]
PORT = 18099
fails = 0

def ok(c, what):
    global fails
    print(('ok    ' if c else 'FAIL  ') + what)
    if not c: fails += 1

app = subprocess.Popen([BIN, SRC], env={**os.environ, 'WHISKER_PORT': str(PORT)},
                       stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
said = []
threading.Thread(target=lambda: [said.append(l) for l in app.stdout], daemon=True).start()
docs = []
seat = []                              # the editor serves seats (§5al): posts name this one
def stream():
    s = socket.create_connection(('127.0.0.1', PORT))
    s.sendall(b'GET /events HTTP/1.1\r\n\r\n')
    buf = b''
    while True:
        try: d = s.recv(65536)
        except Exception: return
        if not d: return
        buf += d
        if buf.startswith(b'HTTP/') and b'\r\n\r\n' in buf:   # the response's headers, once
            buf = buf.split(b'\r\n\r\n', 1)[1]
        while b'\n\n' in buf:
            m, buf = buf.split(b'\n\n', 1)
            if m.startswith(b'data: S ') and not seat: seat.extend(m[8:].decode().split())
            docs.append(m)
def post(body):
    c = socket.create_connection(('127.0.0.1', PORT))
    b = body.encode()
    q = ('?s=%s&g=%s' % (seat[0], seat[1])).encode() if seat else b''
    c.sendall(b'POST /ev%s HTTP/1.1\r\nContent-Length: %d\r\n\r\n' % (q, len(b)) + b)
    try: c.recv(100)
    except Exception: pass
    c.close()
def key(k): post('K %d' % k); time.sleep(0.15)
def typed(t):
    for ch in t: post('T ' + ch)
    time.sleep(0.15)
def name_field():                      # the chooser 640 x 440, centred: its name 438..468
    post('D 500 453 0 1 0\nU 500 453 0'); time.sleep(0.15)
def choose(kind, path):
    key(623 if kind == 'o' else 883)    # Ctrl-O; Ctrl-Shift-S
    name_field()
    key(ord('a') | 0x200)               # Ctrl-A: the name offered, all of it
    typed(path)
    key(7)                              # Enter
    time.sleep(0.3)

time.sleep(0.5)
threading.Thread(target=stream, daemon=True).start()
time.sleep(0.5)
original = open(SRC).read()
try:
    big = os.path.join(TMP, 'big.bas')
    with open(big, 'w') as f: f.write(("print 1\n" * 70000))      # 560,000 bytes
    choose('o', big)
    ok(any('larger than this editor holds' in l for l in said), 'a 560 KB file: refused, larger than the editor holds')
    folder = os.path.join(TMP, 'afolder')
    os.makedirs(folder, exist_ok=True)
    choose('o', folder)
    ok(any('not a file' in l for l in said), 'a folder typed as the file to open: refused, not a file')
    out = os.path.join(TMP, 'kept.bas')
    if os.path.exists(out): os.remove(out)
    choose('s', out)
    ok(os.path.exists(out) and open(out).read() == original,
       'and the text was never touched: saved as, it is sample.bas exactly')
    choose('s', '/dev/full')
    ok(any('could not write /dev/full' in l for l in said) and not any('saved /dev/full' in l for l in said),
       'saved as /dev/full, which takes no bytes: said so -- not "saved"')
finally:
    app.kill()
print('\n%d editor-file check(s) failed' % fails if fails else '\nall editor-file checks passed')
sys.exit(1 if fails else 0)
