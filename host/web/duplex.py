#!/usr/bin/env python3
"""The wire on one stream (design.md §5am), through real OpenSSH.

    python3 duplex.py <whisker-code-web> <whisker-client> <whisker-code-sdl>

A private sshd on 127.0.0.1 with throwaway keys (nothing of the person's
touched). Checked:

  - whisker-client --ssh: OpenSSH's own -W, no tunnel set up; its pixels the
    editor's own window's
  - screens on one document saying different things about what they draw:
    the browser-like one and the native one (§5at) sent the e-acute, one
    saying caps=16 a marker -- and never the character
  - a seat on its own connection takes input only from it: a POST naming it,
    with its very generation, is dropped (the control: its own line moves it)
  - WHISKER_DISPLAY: a program dials a --listen client through ssh -R; the
    client closed, the program runs on; the client back, the program back
"""
import os, re, shutil, signal, socket, subprocess, sys, tempfile, time

WEB, CLIENT, SDL = [os.path.abspath(a) for a in sys.argv[1:4]]
SSH_PORT, ED_PORT, ED2_PORT, LISTEN, REMOTE = 22918, 18101, 18102, 19100, 19200
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


tmp = tempfile.mkdtemp(prefix="whisker-duplex-")
procs = []


def start(args, **kw):
    p = subprocess.Popen(args, **kw)
    procs.append(p)
    return p


def text(path):
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError:
        return b""


sshd_dir = os.path.join(tmp, "sshd")
SSH_OPTS = None


class Client:
    def __init__(self, name, args):
        self.log = os.path.join(tmp, name + ".log")
        self.shot = os.path.join(tmp, name + ".ppm")
        open(self.log, "w").close()
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", WHISKER_CLIENT_SCRIPT="1",
                   WHISKER_CLIENT_LOG=self.log, WHISKER_SHOT=self.shot, WHISKER_SSH_OPTS=SSH_OPTS)
        self.err = open(os.path.join(tmp, name + ".err"), "w")
        self.p = start([CLIENT] + args, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                       stderr=self.err, env=env, text=True)

    def send(self, *lines):
        for l in lines:
            self.p.stdin.write(l + "\n")
        self.p.stdin.flush()

    def lines(self):
        return text(self.log).decode("utf-8", "replace").splitlines()

    def status(self):
        last = None
        for l in self.lines():
            for m in re.finditer(r'>([^<>]*\.bas  \d+:\d+)</text>', l):
                last = m.group(1)
        return last

    def seat(self):
        for l in self.lines():
            if l.startswith("S "):
                return int(l.split()[1]), int(l.split()[2])
        return None

    def snap(self):
        """asked again until the file comes: a client asked before its window
        opens ignores the signal"""
        before = os.path.getmtime(self.shot) if os.path.exists(self.shot) else 0
        end = time.time() + 6
        while time.time() < end and not (os.path.exists(self.shot) and os.path.getmtime(self.shot) != before):
            self.p.send_signal(signal.SIGUSR1)
            time.sleep(0.3)
        time.sleep(0.2)
        return text(self.shot)

    def stop(self):
        self.p.send_signal(signal.SIGTERM)
        try:
            self.p.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.p.kill()


try:
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
    start(["/usr/sbin/sshd", "-D", "-f", os.path.join(sshd_dir, "config"), "-E", os.path.join(sshd_dir, "log")])
    ok(wait_port(SSH_PORT), "a private sshd on 127.0.0.1, its keys made for this test")
    SSH_OPTS = ("-p %d -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=%s -o BatchMode=yes"
                % (SSH_PORT, os.path.join(sshd_dir, "userkey"), os.path.join(sshd_dir, "known")))

    # ---- the editor, a file with an e-acute in it ---------------------------
    src = 'print "café"\n' + "".join('print "line %02d"\n' % i for i in range(2, 41))
    for d in ("", "own"):
        os.makedirs(os.path.join(tmp, d), exist_ok=True)
        with open(os.path.join(tmp, d, "t.bas"), "w", encoding="utf-8") as f:
            f.write(src)
    out = open(os.path.join(tmp, "editor.out"), "w")
    editor = start([WEB, "t.bas"], cwd=tmp, stdout=out, stderr=subprocess.STDOUT,
                   env=dict(os.environ, WHISKER_PORT=str(ED_PORT)))
    ok(wait_port(ED_PORT), "the editor serves, on this machine only")

    # ---- --ssh: nothing but OpenSSH's -W -------------------------------------
    own = start([SDL, "t.bas"], cwd=os.path.join(tmp, "own"), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                env=dict(os.environ, SDL_VIDEODRIVER="dummy", WHISKER_SHOT=os.path.join(tmp, "own.ppm")))
    time.sleep(1.5)
    own.send_signal(signal.SIGUSR1)
    until(lambda: os.path.exists(os.path.join(tmp, "own.ppm")), 3)
    time.sleep(0.2)
    own.kill()
    a = Client("alice", ["--ssh", "127.0.0.1", str(ED_PORT), "alice"])
    ok(until(lambda: a.status() is not None, 8), "whisker-client --ssh 127.0.0.1: the editor, through ssh -W -- no tunnel set up")
    a_px = a.snap()
    own_px = text(os.path.join(tmp, "own.ppm"))
    ok(len(a_px) > 0 and a_px == own_px, "its pixels the editor's own window's, every one (%d bytes)" % len(a_px))

    # ---- two screens, two things drawn ----------------------------------------
    b = socket.create_connection(("127.0.0.1", ED_PORT))
    b.sendall(b"GET /events?v=pagebob HTTP/1.1\r\nHost: x\r\n\r\n")
    b.settimeout(0.3)
    bbuf = b""
    end = time.time() + 4
    while time.time() < end and b"</svg>" not in bbuf:
        try:
            bbuf += b.recv(65536)
        except socket.timeout:
            pass
    eacute = "é".encode()
    marker = re.compile(rb'<rect x="\d+" y="\d+" width="\d+" height="9" fill="#')
    alog = text(a.log)
    ok(eacute in bbuf, "a browser-like screen, saying nothing of what it draws: sent the e-acute itself")
    ok(eacute in alog,
       "the native client, which draws past ASCII since §5at (caps=17): sent the e-acute too")
    c = socket.create_connection(("127.0.0.1", ED_PORT))
    c.sendall(b"WHISKER 1 v=plain caps=16\n")
    c.settimeout(0.3)
    cbuf = b""
    end = time.time() + 4
    while time.time() < end and b"</svg>" not in cbuf:
        try:
            cbuf += c.recv(65536)
        except socket.timeout:
            pass
    ok(eacute not in cbuf and marker.search(cbuf) is not None,
       "a screen on the wire saying caps=16, entities only: sent a marker for it, and never the character")
    c.close()
    ok(b"WHISKER" not in bbuf and alog.count(b"\n") > 0, "each its own seat's documents, at once, from one editor")
    b.close()

    # ---- a seat on its own line: only that line speaks for it ----------------
    sa = a.seat()
    st0 = a.status()
    with socket.create_connection(("127.0.0.1", ED_PORT)) as s:
        body = b"K 12\nK 12\n"
        s.sendall(b"POST /ev?s=%d&g=%d HTTP/1.1\r\nHost: x\r\nContent-Length: %d\r\n\r\n%s"
                  % (sa[0], sa[1], len(body), body))
        s.recv(256)
    time.sleep(0.8)
    ok(sa is not None and a.status() == st0,
       "a POST naming alice's seat AND its generation (%s): dropped -- her caret still %s" % (sa, st0))
    a.send("K 12")
    ok(until(lambda: a.status() != st0), "CONTROL: a Down on her own line moves it (%s -> %s)" % (st0, a.status()))
    a.stop()

    # ---- WHISKER_DISPLAY: the program dials, through ssh -R ------------------
    tun = start(["ssh"] + SSH_OPTS.split() + ["-N", "-o", "ExitOnForwardFailure=yes",
                 "-R", "%d:127.0.0.1:%d" % (REMOTE, LISTEN), "127.0.0.1"], stderr=subprocess.DEVNULL)
    disp = Client("display", ["--listen", "127.0.0.1:%d" % LISTEN])
    ok(wait_port(LISTEN) and wait_port(REMOTE), "a --listen client, and ssh -R carrying port %d to it" % REMOTE)
    with open(os.path.join(tmp, "p.bas"), "w") as f:
        f.write('print "dialled"\n')
    pout = open(os.path.join(tmp, "p.out"), "w")
    prog = start([WEB, "p.bas"], cwd=tmp, stdout=pout, stderr=subprocess.STDOUT,
                 env=dict(os.environ, WHISKER_PORT=str(ED2_PORT), WHISKER_DISPLAY="127.0.0.1:%d" % REMOTE))
    ok(until(lambda: any("p.bas" in l for l in disp.lines()), 8),
       "a program started with WHISKER_DISPLAY=127.0.0.1:%d: shown in the listening client" % REMOTE)
    disp.stop()
    ok(until(lambda: b"left -- caret" in text(os.path.join(tmp, "p.out")), 6) and prog.poll() is None,
       "the listening client closed: the program noted it and runs on")
    n_joined = text(os.path.join(tmp, "p.out")).count(b"joined")
    disp = Client("display2", ["--listen", "127.0.0.1:%d" % LISTEN])
    ok(until(lambda: any("p.bas" in l for l in disp.lines()), 10)
       and text(os.path.join(tmp, "p.out")).count(b"joined") > n_joined,
       "the client back: the program dialled again and is shown again")
    disp.stop()
    ok(prog.poll() is None and editor.poll() is None, "and every program still running")
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
    if os.environ.get("KEEP"):
        print("kept", tmp)
    else:
        shutil.rmtree(tmp, ignore_errors=True)
print(f"\n{fails} duplex check(s) failed" if fails else "\nall duplex checks passed")
sys.exit(1 if fails else 0)
