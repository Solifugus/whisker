#!/usr/bin/env python3
"""A fourth independent read (design.md), the hosts: each defect it found, as
a check that failed before its fix.

    python3 review4.py <whisker-code-web> <whisker-client>

Screens are duplex connections (docs/wire.md §8) made here, so each viewer is
a socket this script drives; the editor's stdout says where each caret was
left.
"""
import os, re, shutil, signal, socket, subprocess, sys, tempfile, time

WEB, CLIENT = [os.path.abspath(a) for a in sys.argv[1:3]]
fails = 0
procs = []
tmp = tempfile.mkdtemp(prefix="whisker-review4-")


def ok(cond, what):
    global fails
    print(("ok    " if cond else "FAIL  ") + what)
    if not cond:
        fails += 1


def wait_port(port, t=5):
    end = time.time() + t
    while time.time() < end:
        try:
            socket.create_connection(("127.0.0.1", port), timeout=.5).close()
            return True
        except OSError:
            time.sleep(.1)
    return False


def until(pred, t=5):
    end = time.time() + t
    while time.time() < end:
        if pred():
            return True
        time.sleep(.05)
    return pred()


class Duplex:
    def __init__(s, port, v="", caps=31):
        s.s = socket.create_connection(("127.0.0.1", port))
        s.buf = b""
        s.lines = []
        s.s.sendall(("WHISKER 1" + (" v=" + v if v else "") + " caps=%d\n" % caps).encode())
        s.s.settimeout(0.05)

    def send(s, *ls):
        try:
            s.s.sendall("".join(l + "\n" for l in ls).encode("utf-8"))
        except OSError:
            pass                    # dropped: what the check after says

    def pump(s, t=0.4):
        end = time.time() + t
        while time.time() < end:
            try:
                d = s.s.recv(1 << 20)
                if not d:
                    break
                s.buf += d
            except socket.timeout:
                pass
            except OSError:
                break
        *ls, s.buf = s.buf.split(b"\n")
        s.lines += [l.decode("utf-8", "replace") for l in ls]
        return s.lines

    def docs(s):
        return sum(1 for l in s.lines if l[:1].isdigit())

    def close(s):
        s.s.close()


class Editor:
    def __init__(s, name, text, port, env=None):
        s.dir = os.path.join(tmp, "e%d" % port)
        os.makedirs(s.dir)
        with open(os.path.join(s.dir, name), "wb") as f:
            f.write(text.encode("utf-8"))
        s.out = os.path.join(s.dir, "out")
        s.p = subprocess.Popen([WEB, name], cwd=s.dir, stdout=open(s.out, "w"), stderr=subprocess.STDOUT,
                               env=dict(os.environ, WHISKER_PORT=str(port), **(env or {})))
        procs.append(s.p)
        s.port = port

    def said(s):
        with open(s.out, "rb") as f:
            return f.read().decode("utf-8", "replace")

    def left(s, who):
        m = re.findall(r"%s left -- caret (\d+)" % who, s.said())
        return int(m[-1]) if m else None


try:
    # 1. a deletion moved another seat's caret twice
    e = Editor("f1.bas", "0123456789\nabcdefghij\n", 19601)
    wait_port(19601)
    A = Duplex(19601, "alice"); A.pump()
    B = Duplex(19601, "bob"); B.pump()
    B.send("K %d" % (4 | 512)); B.pump()                       # Ctrl-End: caret 22
    A.send("K 3", *["K %d" % (2 | 256)] * 5); A.pump()       # five selected from the start
    A.send("K 6"); A.pump(0.6)                                # deleted
    B.close()
    ok(until(lambda: e.left("bob") is not None) and e.left("bob") == 17,
       "five bytes deleted before bob's caret at the end (22): it moves once, to 17 (was 12)")
    A.close()

    # 2. a caret moved inside a character, and every screen stopped
    e = Editor("f2.bas", "éa\nline two\n", 19602)
    wait_port(19602)
    A = Duplex(19602, "alice"); A.pump()
    B = Duplex(19602, "bob"); B.pump()
    B.send("K 2"); B.pump()                                   # bob after the e-acute: byte 2
    A.send("K 3", "K %d" % (2 | 256), "K %d" % (2 | 256)); A.pump()
    A.send("T è"); A.pump(0.6); B.pump()
    na, nb = A.docs(), B.docs()
    B.send("T x"); B.pump(0.6); A.pump(0.6)
    ok(A.docs() > na and B.docs() > nb,
       "alice replaces éa with è -- sharing é's first byte -- then bob types: both still drawn")
    with open(os.path.join(e.dir, "f2.bas"), "rb") as f:
        pass
    A.close(); B.close()

    # 3. one clipboard for every seat
    e = Editor("f3.bas", "x\n" * 400, 19603)
    wait_port(19603)
    A = Duplex(19603, "alice"); A.pump()
    B = Duplex(19603, "bob"); B.pump()
    C = Duplex(19603, "carol"); C.pump()
    C.send(*["T q"] * 20); time.sleep(0.05)                   # the program kept busy
    A.send("P AAAAAAAAAA"); B.send("P b")
    A.pump(3); B.pump(1); C.pump(1)
    A.close(); B.close(); C.close()
    ok(until(lambda: e.left("alice") is not None and e.left("bob") is not None) and
       e.left("alice") == 11 and e.left("bob") == 1,
       "alice and bob paste at the start at once: her ten A's are hers, his b his -- he at 1, she at 11, "
       "moved on by his b (with one clipboard she pasted his: 1 or 2)")

    # 4. a display away held every screen
    e = Editor("f4.bas", "x\n", 19604, {"WHISKER_DISPLAY": "10.255.255.1:9100"})
    wait_port(19604, 3)
    A = Duplex(19604, "alice")
    t = time.time()
    while time.time() - t < 6 and not A.docs():
        A.pump(0.2)
    ok(A.docs() > 0 and time.time() - t < 3,
       "WHISKER_DISPLAY a machine that does not answer: a local screen drawn at once (%.1f s)" % (time.time() - t))
    A.close()

    # 5. sixteen idle sockets held every slot
    e = Editor("f5.bas", "x\n", 19605)
    wait_port(19605)

    def get_root():
        try:
            s = socket.create_connection(("127.0.0.1", 19605), timeout=2)
            s.sendall(b"GET / HTTP/1.1\r\nHost: x\r\n\r\n")
            d = s.recv(64)
            s.close()
            return d
        except OSError:
            return b""
    idle = [socket.create_connection(("127.0.0.1", 19605)) for _ in range(16)]
    time.sleep(0.5)
    first = get_root()
    time.sleep(6)
    ok(get_root().startswith(b"HTTP/1.1 200"), "sixteen sockets open and silent: dropped after five seconds, and a page loads")
    ok(not first.startswith(b"HTTP/1.1 200"), "CONTROL: while they were new, they did hold every slot")
    for s in idle:
        s.close()

    # 6. a large paste dropped its viewer; a line past the buffer is passed over
    e = Editor("f6.bas", "x\n", 19606)
    wait_port(19606)
    A = Duplex(19606, "alice"); A.pump()
    A.send("P " + "ab\\n" * 2100); A.pump(1)                  # 8,402 bytes, as client.c escapes 4,200
    A.send("K %d" % (4 | 512)); A.pump(0.5)
    ok("alice left" not in e.said(), "a paste of 8,402 bytes on the line: alice is still there")
    A.send("P " + "z" * 70000); A.pump(1)
    A.send("T k"); A.pump(0.6)
    A.close()
    ok(until(lambda: e.left("alice") is not None) and e.left("alice") == 6303,
       "and it went in whole (caret 6,303); a 70,000-byte line passed over, the seat kept, the next key heard")

    # 7. a pointer's flood dropped another seat's key
    e = Editor("f7.bas", "0123456789\nabcdefghij\n", 19607)
    wait_port(19607)
    A = Duplex(19607, "alice"); A.pump()
    B = Duplex(19607, "bob"); B.pump()
    A.send(*["H %d 300" % x for x in range(100, 400)]); B.send("T z")
    A.pump(1.5); B.pump(0.5)
    B.close()
    ok(until(lambda: e.left("bob") is not None) and e.left("bob") == 1,
       "alice's pointer sends 300 moves while bob types: bob's letter arrives (caret 1)")
    A.close()

    # 8. a file whose name is not ASCII, on a screen that draws only ASCII
    e = Editor("aéééééééé.bas", "x\n", 19608)
    up = wait_port(19608, 3)
    ok(up, "a file named with nine e-acutes opens (it was refused as \"a NUL byte\")")
    if up:
        A = Duplex(19608, "alice", caps=31); A.pump(0.8)
        B = Duplex(19608, "bob", caps=16); B.pump(0.8)
        drawn = A.docs() > 0 and B.docs() > 0 and any("a????????.bas" in l for l in B.lines)
        A.close(); B.close()
    else:
        drawn = False
    ok(drawn, "drawn for a page and for an ASCII-only screen -- whose status line says a????????.bas")

    # 9. the client never showed a window open before it came
    e = Editor("f9.bas", "x\n", 19609)
    wait_port(19609)
    A = Duplex(19609, "alice"); A.pump()
    A.send("K %d" % (ord('n') | 512)); A.pump(0.6)           # Ctrl-N: the scratch pad
    err = os.path.join(tmp, "client.err")
    cl = subprocess.Popen([CLIENT, "127.0.0.1:19609", "carol"], stdout=subprocess.DEVNULL, stderr=open(err, "w"),
                          env=dict(os.environ, SDL_VIDEODRIVER="dummy"))
    procs.append(cl)
    ok(until(lambda: "open already" in open(err).read(), 6),
       "a client arriving after the scratch pad opened: it opens it too")
    A.close()
finally:
    for p in procs:
        try:
            p.kill()
            p.wait(3)
        except Exception:
            pass
    shutil.rmtree(tmp, ignore_errors=True)
print(f"\n{fails} review check(s) failed" if fails else "\nall review checks passed")
sys.exit(1 if fails else 0)
