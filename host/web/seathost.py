#!/usr/bin/env python3
"""The web host serving seats, over raw sockets (design.md §5ab, and the
review of 2026-11-16): what an independent read found, each checked so that
it fails on the host before its fix.

    python3 seathost.py <whisker-floor-web binary>
"""
import os, re, socket, subprocess, sys, threading, time

BIN = sys.argv[1]
PORT = 18096
fails = 0

def ok(c, what):
    global fails
    print(('ok    ' if c else 'FAIL  ') + what)
    if not c: fails += 1

class Stream:
    """A page's event stream: its seat and generation, and what it is sent."""
    def __init__(s):
        s.sock = socket.create_connection(('127.0.0.1', PORT))
        s.sock.sendall(b'GET /events HTTP/1.1\r\nHost: x\r\n\r\n')
        s.buf = b''; s.seat = None; s.gen = None; s.alive = True; s.docs = 0; s.head = b''
        s.closed = False
        live.append(s)
        threading.Thread(target=s.run, daemon=True).start()
    def run(s):
        while True:
            try: d = s.sock.recv(65536)
            except Exception: d = b''
            if not d: s.alive = False; return
            s.buf += d
            if not s.head and b'\r\n\r\n' in s.buf:
                s.head, s.buf = s.buf.split(b'\r\n\r\n', 1)
            while b'\n\n' in s.buf:
                m, s.buf = s.buf.split(b'\n\n', 1)
                for line in m.split(b'\n'):
                    if line.startswith(b'data: S '):
                        v = line[8:].split()
                        s.seat, s.gen = int(v[0]), (int(v[1]) if len(v) > 1 else 0)
                    elif line.startswith(b'data: '):
                        s.docs += 1
    def post(s, body):
        return post('/ev?s=%d&g=%d' % (s.seat, s.gen), body)
    def close(s):
        # shutdown first: a close alone does not end the stream while the
        # reading thread is still in recv on it
        s.closed = True
        try: s.sock.shutdown(socket.SHUT_RDWR)
        except Exception: pass
        s.sock.close()

def post(path, body, length=None):
    c = socket.create_connection(('127.0.0.1', PORT))
    b = body.encode()
    n = len(b) if length is None else length
    c.sendall(b'POST ' + path.encode() + b' HTTP/1.1\r\nContent-Length: ' + str(n).encode() + b'\r\n\r\n' + b)
    c.settimeout(2)
    try: r = c.recv(100)
    except Exception: r = b''
    c.close()
    return r

def wait(cond, secs):
    t = time.time() + secs
    while time.time() < t:
        if cond(): return True
        time.sleep(0.05)
    return cond()

live = []
def beat():
    # every page here says it is still there, each second, as a page does
    while True:
        for x in list(live):
            if x.alive and not x.closed and x.seat is not None:
                try: x.post('Y')
                except Exception: pass
        time.sleep(1)

app = subprocess.Popen([BIN], env={**os.environ, 'WHISKER_PORT': str(PORT)},
                       stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
said = []
threading.Thread(target=lambda: [said.append(l) for l in app.stdout], daemon=True).start()
time.sleep(0.5)
threading.Thread(target=beat, daemon=True).start()
try:
    # a lying length: negative, or past the buffer -- once a crash
    for n in (() if os.environ.get('SEATHOST_NO_CRASH') else (-100000, 2147483647)):
        try: post('/ev', 'Y', length=n)
        except Exception: pass
        time.sleep(0.2)
        ok(app.poll() is None, 'a POST with Content-Length %d: the host still runs' % n)

    # sixteen seats, each saying it is still there: none is dropped
    seats = [Stream() for _ in range(16)]
    ok(wait(lambda: all(x.seat is not None for x in seats), 5) and
       sorted(x.seat for x in seats) == list(range(16)), 'sixteen pages: seats 0..15')
    time.sleep(8)
    gone = sum(1 for l in said if l.startswith('gone'))
    ok(all(x.alive for x in seats) and gone == 0,
       'all sixteen, heard from each second for eight: none dropped (%d gone)' % gone)
    seventeen = Stream()
    time.sleep(0.5)
    ok(b'503' in seventeen.head and not seventeen.alive, 'a seventeenth page is told there is no room')

    # a stale page's post does not reach whoever has its number now
    a = seats[0]
    sa, ga = a.seat, a.gen
    a.close()
    ok(wait(lambda: any(l.startswith('gone   seat %d' % sa) for l in said), 3), 'a page closes: its seat goes')
    b = Stream()
    ok(wait(lambda: b.seat is not None, 3) and b.seat == sa and b.gen != ga,
       'the next page has the same seat, %d, a new generation' % sa)
    post('/ev?s=%d&g=%d' % (sa, ga), 'G')        # the old page's late beacon
    time.sleep(0.5)
    ok(b.alive, "the old page's late 'going' beacon does not drop the new one")
    b.post('G')
    ok(wait(lambda: not b.alive, 2), "CONTROL: the new page's own does")

    # the floor's frames follow the clock, not the host waking
    for x in seats[1:]: x.close()
    time.sleep(1)
    c = Stream()
    wait(lambda: c.seat is not None, 3)
    time.sleep(1)
    c.docs = 0
    t0 = time.time()
    while time.time() - t0 < 2:
        c.post('H 2 2\nY')
    rate = c.docs / 2
    ok(rate < 40, 'hover posts as fast as they go for two seconds: %.0f documents a second, under 40' % rate)
finally:
    app.kill()
    if fails: print('---- said:\n' + ''.join(said[-25:]))
print('\n%d seat-host check(s) failed' % fails if fails else '\nall seat-host checks passed')
sys.exit(1 if fails else 0)
