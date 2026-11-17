#!/usr/bin/env python3
"""The code editor opens a file indented with tabs, in the browser host (§5ai).

    python3 tabfile.py <whisker-code-web binary> <a file indented with tabs>

Subscribes to /events and reads the first document: each line's first word
must start 8 columns in per tab from where an unindented line starts. Before
§5ai the editor refused the file and exited.
"""
import os, re, socket, subprocess, sys, time

BIN, FILE = sys.argv[1], sys.argv[2]
PORT = 18096
fails = 0


def ok(cond, what):
    global fails
    print(("ok    " if cond else "FAIL  ") + what)
    if not cond:
        fails += 1


env = dict(os.environ, WHISKER_PORT=str(PORT))
p = subprocess.Popen([BIN, FILE], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
time.sleep(0.4)
try:
    alive = p.poll() is None
    ok(alive, "the editor opened a file indented with tabs and is running")
    first = ""
    if alive:
        s = socket.create_connection(("127.0.0.1", PORT), timeout=5)
        s.sendall(b"GET /events HTTP/1.1\r\nHost: x\r\n\r\n")
        buf, end = b"", time.time() + 5
        while b"</svg>" not in buf and time.time() < end:
            got = s.recv(65536)
            if not got:
                break
            buf += got
        s.close()
        first = buf.decode("utf-8", "replace")
    # every <text> at an x, its content -- the editor's own font is 18, its advance 9
    texts = [(int(x), y, t) for x, y, t in re.findall(r'<text[^>]* x="(\d+)" y="(\d+)"[^>]*>([^<]*)</text>', first)]
    def x_of(word, nth=0):
        xs = [x for x, _, t in texts if t == word]
        return xs[nth] if len(xs) > nth else None
    start, p1, p2, adv = x_of("program"), x_of("print", 0), x_of("print", 1), 9
    print(f"      'program' at {start}; the first 'print' at {p1}, the second at {p2}")
    ok(start is not None and p1 == start + 8 * adv, "one tab in: the line's first word at column 8")
    ok(start is not None and p2 == start + 16 * adv, "two tabs in: at column 16")
    ok("\t" not in first, "and no tab byte in the document: a tab draws nothing")
finally:
    p.kill()
    p.wait()
print(f"\n{fails} tab file check(s) failed" if fails else "\nall tab file checks passed")
sys.exit(1 if fails else 0)
