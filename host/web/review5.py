#!/usr/bin/env python3
"""A fifth independent read (design.md, after §5as): the hosts.

    python3 review5.py <whisker-code-web> <whisker-client>

Each check failed before its fix, and each stands beside a control:

  - a listening client with WHISKER_KEY set gave the key to anyone who
    dialled it; it sends none now -- and a client that dials still does
  - the page put the key on every image it fetched, another site's and a
    data: URL's too; only its own server's are keyed now
  - a program's title reached the client's terminal with its escapes; a
    control character is '?' now, and a plain title as it was
  - a file name cut mid-character made the title invalid UTF-8, and a long
    name cut " -- whisker source" off; both whole now
  - --save over a places.new already there kept its mode (0644, a key in
    it); made new now, 0600
  - a places line longer than 1,023 bytes came apart on the next save; it
    is carried whole, and a save that long is refused
  - a duplex hello ending in CRLF with the key last was refused
  - an image name over ~116 bytes lost the key past the cut: 403
"""
import os, re, signal, socket, subprocess, sys, tempfile, time, shutil, json

CODE, CLIENT = [os.path.abspath(a) for a in sys.argv[1:3]]
fails = 0
procs = []
tmp = tempfile.mkdtemp(prefix="whisker-review5-")


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


def text(path):
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError:
        return b""


def editor(port, env, name="t.bas", cwd=None):
    cwd = cwd or tmp
    with open(os.path.join(cwd, name), "w", encoding="utf-8") as f:
        f.write('print "hello"\n')
    err = os.path.join(tmp, "ed%d.err" % port)
    p = subprocess.Popen([CODE, name], cwd=cwd, stdout=subprocess.DEVNULL, stderr=open(err, "w"),
                         env=dict(os.environ, WHISKER_PORT=str(port), **env))
    procs.append(p)
    wait_port(port)
    return p, err


def ask(port, req, wait=0.6):
    s = socket.create_connection(("127.0.0.1", port))
    s.sendall(req)
    s.settimeout(0.2)
    got = b""
    end = time.time() + wait
    while time.time() < end:
        try:
            d = s.recv(65536)
        except socket.timeout:
            continue
        except OSError:
            break
        if not d:
            break
        got += d
    s.close()
    return got


def get(port, path, wait=0.6):
    return ask(port, b"GET %s HTTP/1.1\r\nHost: x\r\n\r\n" % path.encode(), wait)


def status(resp):
    m = re.match(rb"HTTP/1.1 (\d+)", resp)
    return int(m.group(1)) if m else 0


CFG = os.path.join(tmp, "cfg")


def client(args, name, env=None):
    log = os.path.join(tmp, name + ".log")
    open(log, "w").close()
    err = os.path.join(tmp, name + ".err")
    e = dict(os.environ, SDL_VIDEODRIVER="dummy", WHISKER_CLIENT_SCRIPT="1", WHISKER_CLIENT_LOG=log,
             XDG_CONFIG_HOME=CFG)
    e.pop("WHISKER_KEY", None)
    e.update(env or {})
    p = subprocess.Popen([CLIENT] + args, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                         stderr=open(err, "w"), env=e)
    procs.append(p)
    return p, log, err


def run_client(args):
    return subprocess.run([CLIENT] + args, capture_output=True, timeout=10,
                          env=dict(os.environ, XDG_CONFIG_HOME=CFG, SDL_VIDEODRIVER="dummy"))


def fake_program(port):
    """a listening socket playing a program: what the client says first"""
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", port))
    srv.listen(1)
    srv.settimeout(5)
    return srv


def first_line(conn):
    conn.settimeout(3)
    got = b""
    while b"\n" not in got:
        d = conn.recv(4096)
        if not d:
            break
        got += d
    return got.split(b"\n")[0]


try:
    # ---- 1. a listening client keeps its key to itself ----------------------
    L = 19151
    c1, _, _ = client(["--listen", "127.0.0.1:%d" % L], "listen", {"WHISKER_KEY": "MySecretKey123"})
    wait_port(L)
    s = socket.create_connection(("127.0.0.1", L))
    hello = first_line(s)
    s.close()
    ok(hello.startswith(b"WHISKER 1") and b"MySecretKey123" not in hello,
       "a stranger dialling a listening client with WHISKER_KEY set: its hello, and no key (%r)" % hello)
    srv = fake_program(19152)
    c2, _, _ = client(["127.0.0.1:19152", "--key", "MySecretKey123"], "dialler")
    conn, _ = srv.accept()
    hello = first_line(conn)
    ok(b" k=MySecretKey123" in hello, "CONTROL: a client that dials a program still carries its key")

    # ---- 3. a title's escapes never reach the terminal -----------------------
    conn.sendall(b"N 0 \x1b]0;OWNED\x07\x1b[31mred\n0 0 64 32 <svg width=\"64\" height=\"32\"></svg>\n")
    err2 = os.path.join(tmp, "dialler.err")
    ok(until(lambda: b"titled" in text(err2), 4) and b"\x1b" not in text(err2) and b"\x07" not in text(err2)
       and b'titled "whisker -- ?]0;OWNED??[31mred"' in text(err2),
       "a program's title with escapes in it: each control character '?' in the client's log")
    conn.sendall("N 0 plain café\n".encode())
    ok(until(lambda: 'titled "whisker -- plain café"'.encode() in text(err2), 3),
       "CONTROL: a plain title, é and all, as it was sent")
    conn.close()
    srv.close()

    # ---- 2. the page keys only its own server's images ---------------------
    P = 18131
    ed, _ = editor(P, {"WHISKER_KEY": "Sekrit"})
    page = get(P, "/?k=Sekrit").decode("utf-8", "replace")
    m = re.search(r"^const keyed=.*$", page, re.M)
    kq = re.search(r"^const KQ=.*$", page, re.M) or re.search(r"KQ=[^;]*;", page)
    if m and shutil.which("node"):
        js = "const KQ='k=Sekrit';\n" + m.group(0) + "\nconsole.log(JSON.stringify(%s.map(keyed)))" % json.dumps(
            ["/static/logo.png", "https://cdn.example.com/pic.png", "//cdn.example.com/p.png",
             "data:image/png;base64,iVBORw0KGgo="])
        out = subprocess.run(["node", "-e", js], capture_output=True, text=True)
        got = json.loads(out.stdout) if out.returncode == 0 else []
        ok(len(got) == 4 and got[1] == "https://cdn.example.com/pic.png" and got[2] == "//cdn.example.com/p.png"
           and got[3] == "data:image/png;base64,iVBORw0KGgo=",
           "the page's keyed(): another site's image, a scheme-relative one, a data: URL -- no key on them")
        ok(len(got) == 4 and got[0] == "/static/logo.png?k=Sekrit", "CONTROL: its own server's image keyed")
    else:
        ok(False, "the page's keyed() found, and node to run it (%s)" % bool(m))

    # ---- 7. a hello in CRLF -------------------------------------------------
    d = ask(P, b"WHISKER 1 v=crlf caps=0 k=Sekrit\r\n", 1.0)
    ok(d.startswith(b"S ") or b"\nN 0 " in d or d.startswith(b"N 0"),
       "a duplex hello ending in CRLF, the key last: a seat (%r)" % d[:20])
    d = ask(P, b"WHISKER 1 v=crlf caps=0 k=SekritX\r\n", 1.0)
    ok(d.startswith(b"E key"), "CONTROL: a key with more after it, in CRLF: E key")

    # ---- 8. a long image name keeps its key ------------------------------------
    st = os.path.join(tmp, "static")
    os.makedirs(st, exist_ok=True)
    long = "a" * 140 + ".png"
    with open(os.path.join(st, long), "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
    ok(status(get(P, "/static/%s?k=Sekrit" % long)) == 200, "an image named in 144 bytes, with the key: served")
    ok(status(get(P, "/static/%s" % long)) == 403, "CONTROL: without it, 403")

    # ---- 4. a long name beyond ASCII titles whole ---------------------------
    P2 = 18132
    name = "a" * 59 + "é.bas"
    ed2, _ = editor(P2, {}, name)
    d = ask(P2, b"WHISKER 1 v=t caps=16\n", 1.0)
    m = re.search(rb"^N 0 (.*)$", d, re.M)
    t = m.group(1) if m else b""
    try:
        t.decode("utf-8")
        valid = True
    except UnicodeDecodeError:
        valid = False
    ok(m is not None and valid and t.endswith(b" -- whisker source") and len(t) <= 63,
       "a 65-byte file name with é at the cut: its title valid UTF-8, \" -- whisker source\" whole (%r)" % t)
    ed3, _ = editor(18133, {}, "short.bas")
    d = ask(18133, b"WHISKER 1 v=t caps=16\n", 1.0)
    ok(b"N 0 short.bas -- whisker source" in d, "CONTROL: a short name titled as before")

    # ---- 5. a places.new already there ----------------------------------------
    wd = os.path.join(CFG, "whisker")
    os.makedirs(wd, mode=0o700, exist_ok=True)
    pl = os.path.join(wd, "places")
    with open(pl + ".new", "w") as f:
        f.write("old\n")
    os.chmod(pl + ".new", 0o644)
    c4, _, _ = client(["--save", "office", "127.0.0.1:1", "--key", "TopSecretKey"], "save1")
    until(lambda: os.path.exists(pl), 4)
    time.sleep(0.3)
    c4.send_signal(signal.SIGTERM)
    ok(os.path.exists(pl) and (os.stat(pl).st_mode & 0o777) == 0o600,
       "--save over a places.new left at 0644: the places file 0600 (%o)" %
       (os.stat(pl).st_mode & 0o777 if os.path.exists(pl) else 0))
    ok(text(pl) == b"office 127.0.0.1:1 --key TopSecretKey\n", "CONTROL: and the place saved in it")

    # ---- 6. a line longer than the reader's buffer ---------------------------
    big = "wrap " + "a" * 1019 + "wrap 127.0.0.1:1"
    with open(pl, "w") as f:
        f.write(big + "\nother 127.0.0.1:3\n")
    os.chmod(pl, 0o600)
    c5, _, _ = client(["--save", "wrap", "127.0.0.1:2"], "save2")
    until(lambda: b"wrap 127.0.0.1:2" in text(pl), 4)
    time.sleep(0.3)
    c5.send_signal(signal.SIGTERM)
    lines = text(pl).decode().splitlines()
    ok(lines == ["other 127.0.0.1:3", "wrap 127.0.0.1:2"],
       "saved over a place whose line is 1,040 bytes: it replaced whole, no piece left behind (%s)" %
       [l[:20] for l in lines])
    with open(pl, "w") as f:
        f.write("keep " + "b" * 1100 + "\nother 127.0.0.1:3\n")
    c6, _, _ = client(["--save", "new", "127.0.0.1:4"], "save3")
    until(lambda: b"new 127.0.0.1:4" in text(pl), 4)
    time.sleep(0.3)
    c6.send_signal(signal.SIGTERM)
    lines = text(pl).decode().splitlines()
    ok(len(lines) == 3 and lines[0] == "keep " + "b" * 1100,
       "CONTROL: another place's long line carried through a save whole")
    out = run_client(["--save", "huge", "127.0.0.1:5"] + ["w" * 50] * 25)
    ok(out.returncode != 0 and b"huge" not in text(pl), "a save of 1,300 bytes: refused, nothing written")
    out = run_client(["keep"])
    ok(out.returncode == 2 and b"no saved place" in out.stderr,
       "a name whose line is too long to read: no such place, never a piece of it dialled")
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
print("\n%d review5 check(s) failed" % fails if fails else "\nall review5 checks passed")
sys.exit(1 if fails else 0)
