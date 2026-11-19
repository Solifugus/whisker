#!/usr/bin/env python3
"""The code editor, shown remotely through whisker-client over SSH (design.md §5al).

    python3 remote.py <whisker-code-web> <whisker-client> <whisker-code-sdl>

Everything goes through real OpenSSH: a private sshd on 127.0.0.1 with
throwaway keys (nothing of the person's touched), the editor served on this
machine only, a tunnel, and clients connecting through it as "alice" and
"bob". Checked:

  - the editor's first frame through the tunnel equals its own SDL window's
  - a viewer detaches and the editor goes on running
  - another viewer's edit while the first is away moves her caret and view
  - she comes back: caret and view FOLLOW her (moved by that edit), focus
    STARTS FRESH (the dialog she left open is gone, typing reaches the code),
    and the unsaved edits are all there
  - with two viewers, each one's input reaches only its own seat, and a post
    claiming another's seat without its generation is dropped
"""
import os, re, shutil, signal, socket, subprocess, sys, tempfile, time

WEB, CLIENT, SDL = [os.path.abspath(a) for a in sys.argv[1:4]]
EDIT_PORT, TUNNEL_PORT, SSH_PORT = 18099, 18199, 22917
fails = 0


def ok(cond, what):
    global fails
    print(("ok    " if cond else "FAIL  ") + what)
    if not cond:
        fails += 1


def wait_port(port, timeout=5):
    end = time.time() + timeout
    while time.time() < end:
        try:
            socket.create_connection(("127.0.0.1", port), timeout=0.5).close()
            return True
        except OSError:
            time.sleep(0.1)
    return False


def until(pred, timeout=5):
    end = time.time() + timeout
    while time.time() < end:
        v = pred()
        if v:
            return v
        time.sleep(0.05)
    return pred()


tmp = tempfile.mkdtemp(prefix="whisker-remote-")
procs = []


def start(args, **kw):
    p = subprocess.Popen(args, **kw)
    procs.append(p)
    return p


class Client:
    """whisker-client through the tunnel, typed into by its script input."""

    def __init__(self, viewer):
        self.viewer = viewer
        self.log = os.path.join(tmp, viewer + ".log")
        self.shot = os.path.join(tmp, viewer + ".ppm")
        open(self.log, "w").close()
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", WHISKER_CLIENT_SCRIPT="1",
                   WHISKER_CLIENT_LOG=self.log, WHISKER_SHOT=self.shot)
        self.p = start([CLIENT, "127.0.0.1:%d" % TUNNEL_PORT, viewer], stdin=subprocess.PIPE,
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, env=env, text=True)

    def send(self, *lines):
        for l in lines:
            self.p.stdin.write(l + "\n")
        self.p.stdin.flush()

    def lines(self):
        with open(self.log, encoding="utf-8", errors="replace") as f:
            return f.read().splitlines()

    def seat(self):
        for l in self.lines():
            if l.startswith("S "):
                return int(l.split()[1]), int(l.split()[2])
        return None

    def status(self):
        """the last status line drawn for this seat: 'file  line:col'"""
        last = None
        for l in self.lines():
            for m in re.finditer(r'>([^<>]*\.bas  \d+:\d+)</text>', l):
                last = m.group(1)
        return last

    def first_line_shown(self, since=0):
        """the smallest gutter number in the last document drawn from row 0"""
        best = None
        for l in self.lines()[since:]:
            if not re.match(r'^0 \d+ ', l):
                continue
            nums = [int(t) for t in re.findall(r'>(\d+)</text>', l)]
            if nums:
                best = min(nums)
        return best

    def snap(self):
        """asked again until the file comes: a client asked before its window
        opens ignores the signal"""
        before = os.path.getmtime(self.shot) if os.path.exists(self.shot) else 0
        end = time.time() + 6
        while time.time() < end and not (os.path.exists(self.shot) and os.path.getmtime(self.shot) != before):
            self.p.send_signal(signal.SIGUSR1)
            time.sleep(0.3)
        time.sleep(0.2)
        with open(self.shot, "rb") as f:
            return f.read()

    def quit(self):
        self.send("!quit")
        try:
            self.p.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.p.kill()
        return self.p.returncode


try:
    # ---- a private sshd: this machine only, throwaway keys -----------------
    sshd_dir = os.path.join(tmp, "sshd")
    os.makedirs(sshd_dir)
    for k in ("hostkey", "userkey"):
        subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", os.path.join(sshd_dir, k)], check=True)
    shutil.copy(os.path.join(sshd_dir, "userkey.pub"), os.path.join(sshd_dir, "authorized"))
    with open(os.path.join(sshd_dir, "config"), "w") as f:
        f.write(f"""Port {SSH_PORT}
ListenAddress 127.0.0.1
HostKey {sshd_dir}/hostkey
PidFile {sshd_dir}/sshd.pid
AuthorizedKeysFile {sshd_dir}/authorized
PasswordAuthentication no
KbdInteractiveAuthentication no
UsePAM no
StrictModes no
AllowTcpForwarding yes
PermitTTY no
""")
    sshd = start(["/usr/sbin/sshd", "-D", "-f", os.path.join(sshd_dir, "config"), "-E", os.path.join(sshd_dir, "log")])
    ok(wait_port(SSH_PORT), "a private sshd on 127.0.0.1, its keys made for this test")

    # ---- the editor, served on this machine only ---------------------------
    doc = os.path.join(tmp, "t.bas")
    with open(doc, "w") as f:
        for i in range(1, 81):
            f.write('print "line %02d"\n' % i)
    os.makedirs(os.path.join(tmp, "own"))
    shutil.copy(doc, os.path.join(tmp, "own", "t.bas"))      # the same name: the status line shows it
    out = open(os.path.join(tmp, "editor.out"), "w")
    # "t.bas" as the SDL one is given it: Output says the path given
    editor = start([WEB, "t.bas"], cwd=tmp, stdout=out, stderr=subprocess.STDOUT,
                   env=dict(os.environ, WHISKER_PORT=str(EDIT_PORT), WHISKER_CAPS="16"))
    ok(wait_port(EDIT_PORT), "the editor serves its window, on 127.0.0.1")

    def said():
        with open(os.path.join(tmp, "editor.out")) as f:
            return f.read()

    lan = None
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("192.0.2.1", 9))          # no packet sent: only picks this machine's address
        lan = s.getsockname()[0]
        s.close()
    except OSError:
        pass
    if lan and not lan.startswith("127."):
        try:
            socket.create_connection((lan, EDIT_PORT), timeout=1).close()
            reached = True
        except OSError:
            reached = False
        ok(not reached, "and not from the network: %s:%d refuses (WHISKER_LISTEN not set)" % (lan, EDIT_PORT))

    # ---- the tunnel ----------------------------------------------------------
    tunnel = start(["ssh", "-N", "-i", os.path.join(sshd_dir, "userkey"), "-p", str(SSH_PORT),
                    "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=" + os.path.join(sshd_dir, "known"),
                    "-o", "BatchMode=yes", "-o", "ExitOnForwardFailure=yes",
                    "-L", "%d:127.0.0.1:%d" % (TUNNEL_PORT, EDIT_PORT), "127.0.0.1"],
                   stderr=subprocess.DEVNULL)
    ok(wait_port(TUNNEL_PORT), "an SSH tunnel to it, by key")

    # ---- alice: the first frame, the same pixels as the editor's own window --
    own = start([SDL, "t.bas"], cwd=os.path.join(tmp, "own"), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                env=dict(os.environ, SDL_VIDEODRIVER="dummy", WHISKER_SHOT=os.path.join(tmp, "own.ppm")))
    time.sleep(1.5)
    own.send_signal(signal.SIGUSR1)
    until(lambda: os.path.exists(os.path.join(tmp, "own.ppm")), 3)
    time.sleep(0.2)
    own.kill()
    with open(os.path.join(tmp, "own.ppm"), "rb") as f:
        own_px = f.read()
    a = Client("alice")
    ok(until(lambda: a.status() is not None), "alice's client draws the editor through the tunnel")
    a_px = a.snap()
    ok(len(a_px) == len(own_px) and a_px == own_px,
       "and its pixels are the editor's own window's, every one (%d bytes)" % len(a_px))

    # ---- alice edits, scrolls, opens a dialog, and detaches ----------------
    a.send("T Q", "T Q", "T Q", "K 7")            # QQQ, Enter: an unsaved edit at the top
    ok(until(lambda: a.status() and a.status().endswith("2:1")), "alice types QQQ and Enter: her caret at 2:1")
    a.send("K 15")                                 # Page Down
    ok(until(lambda: a.status() and not a.status().endswith("2:1")), "Page Down: her caret and view a page on")
    a.send("K 623")                                # Ctrl-O: the file dialog
    ok(until(lambda: "choose a file to open" in said()), "Ctrl-O: a file dialog open in her seat")
    ok(a.quit() == 0, "alice closes her client: detached")
    m = until(lambda: re.search(r"alice left -- caret (\d+), line (\d+) at the top; the program goes on", said()))
    ok(m is not None and editor.poll() is None,
       "the editor goes on running, keeping her caret and view (%s)" % (m.group(0) if m else said()[-200:]))
    caret0, top0 = (int(m.group(1)), int(m.group(2))) if m else (0, 0)

    # ---- bob, while she is away, puts a line at the top ---------------------
    b = Client("bob")
    ok(until(lambda: b.status() is not None), "bob's client joins through the same tunnel")
    b.send("K 515", "T B", "T B", "K 7")           # Ctrl-Home, BB, Enter: a line above hers
    ok(until(lambda: b.status() and b.status().endswith("2:1")), "bob inserts a line \"BB\" at the very top")

    # ---- alice comes back ----------------------------------------------------
    n_a = 0
    a = Client("alice")
    m = until(lambda: re.search(r"alice is back -- caret (\d+), line (\d+) at the top", said()))
    ok(m is not None and int(m.group(1)) == caret0 + 3 and int(m.group(2)) == top0 + 1,
       "CARET and SCROLL FOLLOW her: caret %d -> %s (+3, bob's \"BB\\n\"), top line %d -> %s (+1)"
       % (caret0, m.group(1) if m else "?", top0, m.group(2) if m else "?"))
    ok(until(lambda: a.first_line_shown() == top0 + 1),
       "drawn so: her window opens with line %d at the top (%s)" % (top0 + 1, a.first_line_shown()))
    ok(until(lambda: a.status() is not None) and not any("Open a file" in l for l in a.lines()),
       "FOCUS STARTS FRESH: the dialog she left open is not there")
    st0 = a.status()
    a.send("T R")
    ok(until(lambda: a.status() and a.status() != st0 and a.status().endswith(":2")),
       "and what she types goes into the code, at her caret (%s -> %s)" % (st0, a.status()))
    a.send("K 515")                                # Ctrl-Home
    ok(until(lambda: any(">QQQ</text>" in l for l in a.lines()) and any(">BB</text>" in l for l in a.lines())),
       "the unsaved edits all there: her QQQ, and bob's BB above it")

    # ---- two viewers: each one's input reaches only its own seat -----------
    sa, sb = a.seat(), b.seat()
    ok(sa is not None and sb is not None and sa[0] != sb[0], "alice and bob in two seats (%s, %s)" % (sa, sb))
    time.sleep(0.5)
    a.send("K 12", "K 12")                         # alice: Down, Down
    ok(until(lambda: a.status() and a.status().endswith("3:1")), "alice presses Down twice: her caret at 3:1")
    nb = len(b.lines())
    time.sleep(0.8)
    ok(len(b.lines()) == nb and b.status().endswith("2:1"),
       "bob's window is sent nothing for it, and his caret stays at 2:1")
    na = len(a.lines())
    b.send("K 12")                                 # bob: Down
    ok(until(lambda: b.status() and b.status().endswith("3:1")), "CONTROL: bob presses Down: his caret moves, to 3:1")
    time.sleep(0.8)
    ok(len(a.lines()) == na and a.status().endswith("3:1"), "and alice's window is sent nothing for it")
    b.send("T Z")                                  # bob types: the document is both of theirs
    ok(until(lambda: b.status() and b.status().endswith("3:2")), "bob types Z: his caret at 3:2")
    ok(until(lambda: len(a.lines()) > na) and a.status().endswith("3:1"),
       "alice's window is drawn again -- the document changed -- but her caret stays at 3:1")
    # a post that names bob's seat without his joining's number is not his
    nb = len(b.lines())
    with socket.create_connection(("127.0.0.1", TUNNEL_PORT)) as s:
        body = b"K 12\n"
        s.sendall(b"POST /ev?s=%d&g=%d HTTP/1.1\r\nHost: x\r\nContent-Length: %d\r\n\r\n%s"
                  % (sb[0], sb[1] + 1000, len(body), body))
        s.recv(256)
    time.sleep(0.8)
    ok(len(b.lines()) == nb and b.status().endswith("3:2"),
       "a post claiming bob's seat with the wrong generation: dropped, his caret unmoved")
    a.quit()
    b.quit()
    ok(editor.poll() is None, "both detached: the editor still running")
finally:
    for p in procs:
        try:
            p.kill()
        except OSError:
            pass
    for p in procs:
        try:
            p.wait(timeout=3)
        except Exception:
            pass
    if os.environ.get("KEEP"): print("kept", tmp)
    else: shutil.rmtree(tmp, ignore_errors=True)
print(f"\n{fails} remote check(s) failed" if fails else "\nall remote checks passed")
sys.exit(1 if fails else 0)
