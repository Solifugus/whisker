#!/usr/bin/env python3
"""No document a program sends is over the 4 KB RV-9's window takes
(design.md §2.3): the code editor, its caret walked down past the bottom
of sample.bas. Before host/linux/fit.h the editor's documents passed 4 KB
there -- 5,718 bytes by the 40th line -- and the SDL window, which drops
them, looked locked (Matthew, 2026-11-16).

    python3 docsize.py <whisker-code-web binary> <file.bas>
"""
import os, socket, subprocess, sys, time

BIN, SRC = sys.argv[1], sys.argv[2]
PORT = 18098
fails = 0

def ok(c, what):
    global fails
    print(('ok    ' if c else 'FAIL  ') + what)
    if not c: fails += 1

app = subprocess.Popen([BIN, SRC], env={**os.environ, 'WHISKER_PORT': str(PORT)},
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(0.5)
try:
    s = socket.create_connection(('127.0.0.1', PORT))
    s.sendall(b'GET /events HTTP/1.1\r\n\r\n')
    s.settimeout(0.3)
    buf = b''
    def drain():
        global buf
        t = time.time()
        while time.time() - t < 0.3:
            try: d = s.recv(65536)
            except socket.timeout: break
            if not d: break
            buf += d
        out = []
        while b'\n\n' in buf:
            m, buf = buf.split(b'\n\n', 1)
            p = m[6:].split(b' ', 4)
            if len(p) == 5 and p[0].isdigit(): out.append(len(p[4]))
        return out
    def post(body):
        c = socket.create_connection(('127.0.0.1', PORT))
        b = body.encode()
        c.sendall(b'POST /ev HTTP/1.1\r\nContent-Length: %d\r\n\r\n' % len(b) + b)
        try: c.recv(100)
        except Exception: pass
        c.close()
    sizes = drain()
    for _ in range(80):
        post('K 12')                                   # Down
        sizes += drain()
    big = max(sizes) if sizes else 0
    print('      %d documents, the largest %d bytes' % (len(sizes), big))
    ok(len(sizes) > 80, 'the caret walked down 80 lines, past the end: every step drawn')
    ok(0 < big <= 4096, 'and no document over the 4,096 bytes RV-9\'s window takes')
finally:
    app.kill()
print('\n%d document-size check(s) failed' % fails if fails else '\nall document-size checks passed')
sys.exit(1 if fails else 0)
