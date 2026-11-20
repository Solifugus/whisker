#!/usr/bin/env python3
"""Remote polish (design.md §5as): a key for pages, saved places in the
client, and a window's title on every screen.

    python3 keys.py <whisker-code-web> <whisker-client>

Checked, each refusal beside what it refuses being served:

  - listening beyond this machine, the host makes a key and prints it; the
    page, the stream, a post and an image without it are 403, a wrong one
    or one with more after it too, and with it they are served; a duplex
    line without it is `E key`; on loopback with no key set, nothing is
    asked (every check before this one connects that way)
  - WHISKER_KEY on loopback demands it; whisker-client --key draws
    through it, and without it is refused
  - a program with a key that dials a display (WHISKER_DISPLAY) is shown
    there with no key: it chose where to go
  - a stream's joining starts `N 0 <title>`: the code editor's file name;
    Save As changes it (`N 0` again); the client titles its window so
  - saved places: --save, the name alone connects; the file 0600, one line
    a place, a second save replacing the first; --places lists; a word
    with a space refused; an unknown name refused, not dialled
"""
import os, re, signal, socket, subprocess, sys, tempfile, time, shutil

CODE, CLIENT = [os.path.abspath(a) for a in sys.argv[1:3]]
fails = 0
procs = []
tmp = tempfile.mkdtemp(prefix="whisker-keys-")


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


def editor(port, env, name="t.bas"):
    """the code editor on a file, its stderr kept"""
    with open(os.path.join(tmp, name), "w") as f:
        f.write('print "hello"\n')
    err = open(os.path.join(tmp, "ed%d.err" % port), "w")
    p = subprocess.Popen([CODE, name], cwd=tmp, stdout=subprocess.DEVNULL, stderr=err,
                         env=dict(os.environ, WHISKER_PORT=str(port), **env))
    procs.append(p)
    wait_port(port)
    return p, os.path.join(tmp, "ed%d.err" % port)


def ask(port, req, wait=0.6):
    """one request, what came back in `wait` seconds"""
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


def client(args, name, env=None):
    log = os.path.join(tmp, name + ".log")
    open(log, "w").close()
    err = os.path.join(tmp, name + ".err")
    e = dict(os.environ, SDL_VIDEODRIVER="dummy", WHISKER_CLIENT_SCRIPT="1", WHISKER_CLIENT_LOG=log,
             XDG_CONFIG_HOME=os.path.join(tmp, "cfg"))
    e.pop("WHISKER_KEY", None)
    e.update(env or {})
    p = subprocess.Popen([CLIENT] + args, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                         stderr=open(err, "w"), env=e)
    procs.append(p)
    return p, log, err


def text(path):
    try:
        with open(path, "rb") as f:
            return f.read().decode("utf-8", "replace")
    except OSError:
        return ""


try:
    # ---- a key made, listening beyond this machine ----------------------------
    P1 = 18121
    ed, errf = editor(P1, {"WHISKER_LISTEN": "0.0.0.0"})
    m = until(lambda: re.search(r"\?k=([0-9a-f]{32})", text(errf)), 4)
    ok(m is not None, "listening on 0.0.0.0: the address printed with a key of 128 random bits")
    key = m.group(1) if m else "x" * 32
    ok(status(get(P1, "/")) == 403 and status(get(P1, "/events")) == 403 and
       status(get(P1, "/static/a.png")) == 403,
       "the page, the stream and an image without it: 403")
    body = b"K 12\n"
    ok(status(ask(P1, b"POST /ev?s=0&g=1 HTTP/1.1\r\nContent-Length: %d\r\n\r\n%s" % (len(body), body))) == 403,
       "a post without it: 403")
    wrong = ("0" if key[0] != "0" else "1") + key[1:]
    ok(status(get(P1, "/?k=" + wrong)) == 403 and status(get(P1, "/?k=" + key + "0")) == 403 and
       status(get(P1, "/?vk=" + key)) == 403,
       "a key one character wrong, one with more after it, one under another name: 403")
    page = get(P1, "/?k=" + key)
    ev = get(P1, "/events?v=keyed&k=" + key, 1.0)
    ok(status(page) == 200 and b"<canvas" in page and status(ev) == 200 and b"data: " in ev,
       "CONTROL: with it, the page and its stream served")
    ok(status(get(P1, "/static/a.png?k=" + key)) == 404,
       "and an image asked for with it gets past the key (404: there is no such file)")
    ok(status(ask(P1, b"POST /ev?k=%s HTTP/1.1\r\nContent-Length: %d\r\n\r\n%s" % (key.encode(), len(body), body))) == 204,
       "and a post with it: 204")
    d = ask(P1, b"WHISKER 1 v=noone caps=16\n", 1.0)
    ok(d.startswith(b"E key"), "a duplex line without it: E key, and nothing else")
    d = ask(P1, b"WHISKER 1 v=someone caps=16 k=" + key.encode() + b"\n", 1.0)
    ok(b"N 0 t.bas -- whisker source" in d, "with it: a seat, and the window's title first")

    # ---- loopback, as before ----------------------------------------------------
    P2 = 18122
    ed2, _ = editor(P2, {})
    ev = get(P2, "/events", 1.0)
    ok(status(get(P2, "/")) == 200 and status(ev) == 200,
       "CONTROL: on loopback with no key set, nothing is asked -- as before")
    first = [l for l in ev.decode("utf-8", "replace").split("\n") if l.startswith("data: ")]
    ok(len(first) >= 2 and first[1] == "data: N 0 t.bas -- whisker source",
       "a stream's joining: S, then N 0 and the editor's title -- its file's name")

    # ---- WHISKER_KEY on loopback, and the client with and without it ---------
    P3 = 18123
    ed3, _ = editor(P3, {"WHISKER_KEY": "sesame-42"})
    ok(status(get(P3, "/")) == 403 and status(get(P3, "/?k=sesame-42")) == 200,
       "WHISKER_KEY on loopback: asked for, and served with it")
    c1, log1, err1 = client(["127.0.0.1:%d" % P3, "nokey"], "nokey")
    until(lambda: "refused" in text(err1), 4)
    ok("refused: key" in text(err1) and "t.bas" not in text(log1), "whisker-client without it: refused, shown nothing")
    c2, log2, err2 = client(["127.0.0.1:%d" % P3, "haskey", "--key", "sesame-42"], "haskey")
    ok(until(lambda: re.search(r"^\d+ \d+ \d+ \d+ <svg", text(log2), re.M), 5) is not None,
       "whisker-client --key: the editor drawn through it")
    ok(until(lambda: 'window 0 titled "whisker -- t.bas -- whisker source"' in text(err2), 3),
       "and its window titled from the program's: whisker -- t.bas -- whisker source")
    c2.send_signal(signal.SIGTERM)

    # ---- a dialled display needs no key -------------------------------------
    LISTEN = 19131
    c3, log3, err3 = client(["--listen", "127.0.0.1:%d" % LISTEN], "display")
    wait_port(LISTEN)
    P4 = 18124
    ed4, _ = editor(P4, {"WHISKER_KEY": "sesame-42", "WHISKER_DISPLAY": "127.0.0.1:%d" % LISTEN}, "d.bas")
    ok(until(lambda: "N 0 d.bas -- whisker source" in text(log3), 6),
       "a program with a key, dialling a --listen client that knows none: shown there all the same")
    c3.send_signal(signal.SIGTERM)

    # ---- the title changes: Save As -------------------------------------------
    s = socket.create_connection(("127.0.0.1", P2))
    s.sendall(b"WHISKER 1 v=saver caps=16\n")
    s.settimeout(0.3)
    seen = b""
    end = time.time() + 1
    while time.time() < end:
        try:
            seen += s.recv(65536)
        except socket.timeout:
            pass
    s.sendall(b"K 883\n")                 # Ctrl-Shift-S: save as
    time.sleep(0.5)
    s.sendall(b"T x\nK 7\n")              # the name's field starts with the file's name; type, Enter
    seen = b""
    end = time.time() + 2
    while time.time() < end and b"N 0 " not in seen:
        try:
            seen += s.recv(65536)
        except socket.timeout:
            pass
    m = re.search(rb"N 0 (\S+) -- whisker source", seen)
    ok(m is not None and m.group(1) != b"t.bas",
       "Save As to another name: N 0 again, the new name (%s)" % (m.group(1).decode() if m else None))
    s.close()

    # ---- to the front: Ctrl-N with the scratch pad open ---------------------
    s = socket.create_connection(("127.0.0.1", P2))
    s.sendall(b"WHISKER 1 v=raiser caps=16\n")
    s.settimeout(0.3)
    seen = b""
    s.sendall(b"K 622\n")                 # Ctrl-N: the scratch pad, window 1
    end = time.time() + 1.5
    while time.time() < end:
        try:
            seen += s.recv(65536)
        except socket.timeout:
            pass
    opened = re.search(rb"^O (\d) ", seen, re.M)
    ok(opened is not None and b"\nF " not in seen, "Ctrl-N: the scratch pad opens, and nothing is raised")
    seen = b""
    s.sendall(b"K 622\n")                 # again, open already
    end = time.time() + 1.5
    while time.time() < end and b"F " not in seen:
        try:
            seen += s.recv(65536)
        except socket.timeout:
            pass
    ok(opened is not None and re.search(rb"^F %s$" % opened.group(1), seen, re.M) is not None,
       "Ctrl-N again: F -- the pad brought to the front, not a second one")
    s.close()

    # ---- saved places ----------------------------------------------------------
    c4, log4, err4 = client(["--save", "work", "127.0.0.1:%d" % P3, "alice", "--key", "sesame-42"], "save1")
    ok(until(lambda: re.search(r"^\d+ \d+ \d+ \d+ <svg", text(log4), re.M), 5) is not None,
       "--save work, the address, a viewer and the key: saved, and connected")
    c4.send_signal(signal.SIGTERM)
    pl = os.path.join(tmp, "cfg", "whisker", "places")
    ok(os.path.exists(pl) and (os.stat(pl).st_mode & 0o777) == 0o600 and
       text(pl) == "work 127.0.0.1:%d alice --key sesame-42\n" % P3,
       "the places file: mode 0600, one line, the words as typed")
    c5, log5, err5 = client(["work"], "byname")
    ok(until(lambda: re.search(r"^\d+ \d+ \d+ \d+ <svg", text(log5), re.M), 5) is not None,
       "whisker-client work: the same program, key and all")
    c5.send_signal(signal.SIGTERM)
    c6, _, _ = client(["--save", "work", "127.0.0.1:%d" % P2, "bob"], "save2")
    time.sleep(1.0)
    c6.send_signal(signal.SIGTERM)
    c7, _, _ = client(["--save", "home", "127.0.0.1:%d" % P2], "save3")
    time.sleep(1.0)
    c7.send_signal(signal.SIGTERM)
    ok(text(pl) == "work 127.0.0.1:%d bob\nhome 127.0.0.1:%d\n" % (P2, P2),
       "saved again as work: replaced, not added; home beside it")
    out = subprocess.run([CLIENT, "--places"], capture_output=True, text=True,
                         env=dict(os.environ, XDG_CONFIG_HOME=os.path.join(tmp, "cfg")))
    ok(out.returncode == 0 and out.stdout == text(pl), "--places lists them")
    out = subprocess.run([CLIENT, "--save", "odd", "127.0.0.1:%d" % P2, "two words"], capture_output=True,
                         text=True, env=dict(os.environ, XDG_CONFIG_HOME=os.path.join(tmp, "cfg"), SDL_VIDEODRIVER="dummy"))
    ok(out.returncode != 0 and "space" in out.stderr and "odd" not in text(pl), "a word with a space: refused, nothing saved")
    out = subprocess.run([CLIENT, "nowhere"], capture_output=True, text=True, timeout=5,
                         env=dict(os.environ, XDG_CONFIG_HOME=os.path.join(tmp, "cfg"), SDL_VIDEODRIVER="dummy"))
    ok(out.returncode == 2 and "no saved place" in out.stderr, "an unknown name: refused, not dialled as a host")
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
print("\n%d key check(s) failed" % fails if fails else "\nall key checks passed")
sys.exit(1 if fails else 0)
