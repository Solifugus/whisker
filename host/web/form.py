#!/usr/bin/env python3
"""A form in cells (design.md §5aq), in the browser host, over HTTP: the
hints drawn while their fields are empty, a password sent as stars and
never as itself, Enter in a field pressing the default button, and a
whole form submitted -- form.c, as a page would drive it.

    python3 form.py <whisker-form-web binary>
"""
import os, re, socket, subprocess, sys, time

BIN = sys.argv[1]
PORT = 18097
fails = 0


def ok(c, what):
    global fails
    print(('ok    ' if c else 'FAIL  ') + what)
    if not c:
        fails += 1


app = subprocess.Popen([BIN], env={**os.environ, 'WHISKER_PORT': str(PORT)},
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(0.5)
try:
    s = socket.create_connection(('127.0.0.1', PORT))
    s.sendall(b'GET /events HTTP/1.1\r\n\r\n')
    s.settimeout(0.3)
    raw = b''

    def drain():
        """every document the program sent since the last call, joined"""
        global raw
        t = time.time()
        while time.time() - t < 0.4:
            try:
                d = s.recv(65536)
            except socket.timeout:
                break
            if not d:
                break
            raw += d
        got, raw = raw, b''
        return got.decode('utf-8', 'replace')

    def post(*lines):
        c = socket.create_connection(('127.0.0.1', PORT))
        b = ('\n'.join(lines) + '\n').encode()
        c.sendall(b'POST /ev HTTP/1.1\r\nContent-Length: %d\r\n\r\n' % len(b) + b)
        try:
            c.recv(100)
        except Exception:
            pass
        c.close()
        return drain()

    first = drain()
    ok('your name' in first and 'e.g. editor' in first and 'anything we should know' in first,
       'the first frame: three hints, in their empty fields')
    ok('Tab between the parts' in first and 'Recent' in first, 'and the rest of the form with them')

    d = post('K 8', 'T Ada')
    last = d.split('\n\n')[-2] if d.count('\n\n') >= 1 else d     # the last frame sent
    ok('Ada' in last and 'your name' not in last and 'your name' in d,
       'Tab, then "Ada": the hint while the focused field was empty, then the name and no hint')

    d = post('K 8', 'K 8', 'T p', 'T w', 'T 1')
    ok('***' in d and 'pw1' not in d and not re.search(r'>[^<]*pw[^<]*<', d),
       'Tab twice to the password, "pw1": three stars sent, and the characters never')

    d = post('K 7')
    ok('tick I agree first' in d, 'Enter in the password: the default button pressed -- "tick I agree first"')

    d = post('D 60 419 0 1 0', 'U 60 419 0')
    ok('agreed' in d, 'I agree clicked: "agreed"')

    d = post('D 230 23 0 1 0', 'U 230 23 0', 'K 7')
    ok('thank you, Ada -- a password of 3 characters' in d,
       'back in the name, Enter: "thank you, Ada -- a password of 3 characters"')
    s.close()
finally:
    app.kill()
    app.wait()
print('\n%d form check(s) failed' % fails if fails else '\nall form checks passed')
sys.exit(1 if fails else 0)
