#!/usr/bin/env python3
"""
The web host, checked with no browser (design.md §5k, criterion 2).

Starts edit.c linked against host_web.c, then speaks to it as a page would:
fetch the page, subscribe to /events, POST events, and read back the
documents the program sends. Standard library only.
"""
import http.client, os, socket, subprocess, sys, time

BIN = sys.argv[1]
PORT = 18090
fails = 0


def ok(cond, what):
    global fails
    print(("ok    " if cond else "FAIL  ") + what)
    if not cond:
        fails += 1


def post(body):
    c = http.client.HTTPConnection("127.0.0.1", PORT, timeout=5)
    c.request("POST", "/ev", body=body)
    r = c.getresponse()
    r.read()
    c.close()
    return r.status


class Events:
    """A raw subscription to /events: read whole Server-Sent Events."""

    def __init__(self):
        self.s = socket.create_connection(("127.0.0.1", PORT), timeout=5)
        self.s.sendall(b"GET /events HTTP/1.1\r\nHost: x\r\n\r\n")
        self.buf = b""
        while b"\r\n\r\n" not in self.buf:
            self.buf += self.s.recv(65536)
        self.head, self.buf = self.buf.split(b"\r\n\r\n", 1)

    def next(self, timeout=5):
        end = time.time() + timeout
        while b"\n\n" not in self.buf:
            self.s.settimeout(max(0.05, end - time.time()))
            try:
                got = self.s.recv(65536)
            except socket.timeout:
                return None
            if not got:
                return None
            self.buf += got
        msg, self.buf = self.buf.split(b"\n\n", 1)
        return msg.decode()

    def until(self, needle, timeout=5):
        end = time.time() + timeout
        while time.time() < end:
            m = self.next(end - time.time())
            if m is None:
                return None
            if needle in m:
                return m
        return None


env = dict(os.environ, WHISKER_PORT=str(PORT))
p = subprocess.Popen([BIN], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
time.sleep(0.3)
try:
    c = http.client.HTTPConnection("127.0.0.1", PORT, timeout=5)
    c.request("GET", "/")
    r = c.getresponse()
    page = r.read().decode()
    ok(r.status == 200 and "<canvas" in page and "EventSource('/events')" in page,
       "the page is served: a canvas, and a subscription to /events")
    c.close()

    ev = Events()
    ok(b"text/event-stream" in ev.head, "/events is a Server-Sent Event stream")
    first = ev.next()
    ok(first is not None and first.startswith("data: 0 0 1024 600 <svg"),
       "a page that subscribes is sent the whole picture first: rows 0..0 (all), 1024x600")
    ok(first is not None and ">whisker -- type into a field</text>" in first,
       "and it is edit.c's window, drawn by whisker, unchanged")

    ok(post("T hi") == 204, "a typed event is accepted")
    m = ev.until(">hi</text>")
    ok(m is not None, "and the document that comes back has \"hi\" in the focused field")
    ok(m is not None and not m.startswith("data: 0 0 "),
       "as a clipped repaint -- the field's rows, not the whole screen")

    post("K 8")                       # Tab
    post("T Paris")
    m = ev.until(">Paris</text>")
    ok(m is not None, "Tab, then typing, reaches the second field")

    post("D 900 200\nU 900 200")      # a click in the list, two events in one POST
    post("K 7")                       # Enter
    second = Events()
    replay = second.next()
    ok(replay is not None and replay.startswith("data: 0 0 "),
       "a page that subscribes late is sent the picture from the last full document")
    full = second.until(">hi</text>", 3)
    ok(full is not None, "and the late page sees what was typed before it came")
finally:
    p.terminate()
    out, err = p.communicate(timeout=5)

ok("enter  city = \"Paris\"" in out, "Enter on the second field printed it: city = \"Paris\"")
ok("select it" in out, "the click in the table reached the program: a cell clicked, its row chosen")
print(("\n%d web check(s) failed" % fails) if fails else "\nall web checks passed")
sys.exit(1 if fails else 0)
