#!/usr/bin/env python3
"""
The web host, checked with no browser (design.md §5k, criterion 2).

Starts edit.c linked against host_web.c, then speaks to it as a page would:
fetch the page, subscribe to /events, POST events, and read back the
documents the program sends. Standard library only.
"""
import http.client, os, re, socket, subprocess, sys, time

BIN = sys.argv[1]
PORT = 18090
fails = 0


def ok(cond, what):
    global fails
    print(("ok    " if cond else "FAIL  ") + what)
    if not cond:
        fails += 1


def box(doc, wid):
    """Where a widget was drawn: its rect's x, y, width, height, from the SVG --
    a widget's own rect, or a nested stack's first, inside its <g>."""
    m = re.search(r'(?:<rect id="%s"|<g id="%s"><rect)(?: x="(\d+)")? y="(\d+)" width="(\d+)" height="(\d+)"'
                  % (wid, wid), doc or "")
    return None if m is None else (int(m.group(1) or 0), int(m.group(2)), int(m.group(3)), int(m.group(4)))


def middle(b):
    return "%d %d" % (b[0] + b[2] // 2, b[1] + b[3] // 2)


def post(body):
    c = http.client.HTTPConnection("127.0.0.1", PORT, timeout=5)
    c.request("POST", "/ev", body=body.encode())
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
        return msg.decode("utf-8", "replace")

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
# from this folder, whose static/ holds the image a document names -- run from
# anywhere else, the host looked for ./static there and the check failed
p = subprocess.Popen([BIN], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                     cwd=os.path.dirname(os.path.abspath(__file__)))
time.sleep(0.3)
try:
    c = http.client.HTTPConnection("127.0.0.1", PORT, timeout=5)
    c.request("GET", "/")
    r = c.getresponse()
    page = r.read().decode()
    ok(r.status == 200 and "<canvas" in page and "EventSource(keyed('/events'" in page,
       "the page is served: a canvas, and a subscription to /events")
    # §5ak: whisker's icon, an inline SVG that parses
    import re as _re, urllib.parse as _up, xml.etree.ElementTree as _et
    _m = _re.search(r'<link rel=icon href="data:image/svg\+xml,([^"]*)"', page)
    try:
        _svg = _et.fromstring(_up.unquote(_m.group(1))) if _m else None
    except _et.ParseError:
        _svg = None
    ok(_svg is not None and _svg.tag.endswith("svg") and len(list(_svg.iter())) >= 4,
       "and whisker's icon as its favicon: an inline SVG that parses -- a square, the whiskers, the nose")
    ok("PageUp:14,PageDown:15" in page and "14:1,15:1" in page,
       "and the page passes Page Up and Page Down as whisker's keys 14 and 15, Shift kept with them (§5ak)")
    c.close()

    ev = Events()
    ok(b"text/event-stream" in ev.head, "/events is a Server-Sent Event stream")
    title = ev.next()
    ok(title is not None and title.startswith("data: N 0 "),
       "on joining, window 0's title first (§5as): %s" % title)
    first = ev.next()
    ok(first is not None and first.startswith("data: 0 0 1024 600 <svg"),
       "a page that subscribes is sent the whole picture first: rows 0..0 (all), 1024x600")
    ok(first is not None and ">whisker -- type into a field</text>" in first,
       "and it is edit.c's window, drawn by whisker, unchanged")
    # §5r: this host said it draws UTF-8, images and clip paths, and the
    # program used all three
    ok(first is not None and "fields \u2014 Enter" in first,
       "a host that draws UTF-8 is sent it: the help line's real dash")
    ok(first is not None and ">src</text>" in first and first.count("<path d=\"M") >= 4,
       "and the tree's arrows are marks (\u00a75t), drawn as paths as on every host -- not characters")
    ok(first is not None and '<image href="/static/logo.svg"' in first,
       "and an image, for a host that draws images")
    ok(first is not None and 'clip-path="url(#wsk-c' in first,
       "and the scrolling tree clipped, for a host that clips")
    c = http.client.HTTPConnection("127.0.0.1", PORT, timeout=5)
    c.request("GET", "/static/logo.svg")
    r = c.getresponse()
    body = r.read()
    ok(r.status == 200 and r.getheader("Content-Type") == "image/svg+xml" and b"<svg" in body,
       "the host serves the image from its static folder")
    c.request("GET", "/static/../host_web.c")
    r = c.getresponse(); r.read()
    ok(r.status == 404, "CONTROL: and nothing above that folder")
    c.close()

    ok(post("T hi") == 204, "a typed event is accepted")
    m = ev.until(">hi</text>")
    ok(m is not None, "and the document that comes back has \"hi\" in the focused field")
    ok(m is not None and not m.startswith("data: 0 0 "),
       "as a clipped repaint -- the field's rows, not the whole screen")

    # §5v step 3: a character beyond ASCII typed, and Backspace takes all
    # of it -- the field's text is "hi" again, not "hi" and half an é
    post("T \u00e9")
    m = ev.until(">hi\u00e9</text>")
    ok(m is not None, "an \u00e9 typed (two bytes in UTF-8) into a host that draws UTF-8")
    post("K 5")                       # Backspace
    m = ev.until("</svg>")
    ok(m is not None and ">hi</text>" in m and "\ufffd" not in m,
       "and one Backspace takes it back whole: \"hi\"")

    post("K 8")                       # Tab
    post("T Paris")
    m = ev.until(">Paris</text>")
    ok(m is not None, "Tab, then typing, reaches the second field")

    # a folder clicked twice, the second time while it is the chosen row:
    # it closes, then opens -- Matthew found the second click did nothing
    src = middle(box(first, "it0"))
    post("D %s\nU %s" % (src, src))
    post("D %s\nU %s" % (src, src))
    post("D 900 200\nU 900 200")      # a click in the list, two events in one POST
    post("K 7")                       # Enter
    # The Note memo (§5o): 278..374. A tap there, two lines, Enter between.
    post("D 100 300\nU 100 300")
    post("T one")
    post("K 7")
    post("T two")
    m = ev.until(">two</text>")
    ok(m is not None and ">one</text>" in m,
       "a memo: two lines typed with Enter between come back as two lines")

    # §5s: a right-click on the name field opens the context menu there; its
    # items are keys fed to the toolkit, and copies go to the page.
    name = box(first, "name")
    post("R %d %d" % (name[0] + 20, name[1] + 10))
    m = ev.until(">Cut</text>")
    ok(m is not None and ">Select all</text>" in m,
       "a right-click on a field (R) opens the program's context menu: Cut, Copy, Paste, Select all")
    post("D %s\nU %s" % (middle(box(m, "cx-all")), middle(box(m, "cx-all"))))
    post("R %d %d" % (name[0] + 20, name[1] + 10))
    m = ev.until(">Copy</text>")
    post("D %s\nU %s" % (middle(box(m, "cx-copy")), middle(box(m, "cx-copy"))))
    c = ev.until("data: C ")
    ok(c == 'data: C hi', "Select all, then Copy, from the menu: \"hi\" sent to the page as C, for the clipboard")
    post("K 4")                       # End: the selection collapses
    post("P  there")                  # what the page's paste event sends
    m = ev.until(">hi there</text>")
    ok(m is not None, "a paste (P) arrives as Ctrl-V, and the program pastes the host's copy: \"hi there\"")
    post("P a\\nb")                  # a line break, escaped, into a one-line field
    post("K %d" % (ord("s") | 0x200))  # Ctrl-S
    m = ev.until(">Save now</text>")
    ok(m is not None, "Ctrl-S, the program's own shortcut, opens the Save menu")
    post("K 10")                      # Escape closes it
    post("K %d" % (ord("q") | 0x200))  # a chord nobody uses (Ctrl-Z undoes, §5v)
    time.sleep(0.3)
    second = Events()
    second.next()                     # its title (§5as)
    replay = second.next()
    ok(replay is not None and replay.startswith("data: 0 0 "),
       "a page that subscribes late is sent the picture from the last full document")
    full = second.until(">hi there</text>", 3)
    ok(full is not None, "and the late page sees what was typed before it came")
    ok(full is not None and "data: C " not in replay + full,
       "CONTROL: but not the copy -- a copy is something that happened, not part of the picture")
finally:
    p.terminate()
    out, err = p.communicate(timeout=5)

ok("enter  city = \"Paris\"" in out, "Enter on the second field printed it: city = \"Paris\"")
ok("select it" in out, "the click in the table reached the program: a cell clicked, its row chosen")
ok("close  src" in out and out.find("open   src", out.find("close  src")) > 0,
   "a folder clicked twice closes and opens again -- the second click on the chosen row counts")
ok('menu   name' in out and 'click  cx-all' in out and 'copy   name "hi"' in out,
   "the program was told: MENU on name, Select all and Copy chosen, \"hi\" copied")
ok('paste  name "hi there"' in out, "and the paste went into name")
ok("paste  refused (-1)" in out, "a line break pasted into a one-line field is refused, not trimmed")
ok("shortcut save" in out and out.find("close  the menu", out.find("shortcut save")) > 0,
   "Ctrl-S was the program's; Escape, after it, closed what it opened")
ok("key    0x271, not ours" in out, "and an unused chord, Ctrl-Q, reached the program as a KEY notice")
print(("\n%d web check(s) failed" % fails) if fails else "\nall web checks passed")
sys.exit(1 if fails else 0)
