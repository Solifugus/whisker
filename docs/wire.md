# The wire — how a whisker program and a screen talk

**Written 2026-11-18** (design §5al, §5am, D25), from `host/web/host_web.c` as it
is: the protocol the browser host has spoken since §5k, with seats (§5ab),
windows (§5af) and the clipboard (§5s), now named as a contract. Its two
readers are the page the browser host serves and `whisker-client`
(`host/linux/client.c`); any other screen reads the same. The browser host's
suites are its tests, and `host/web/remote.py` runs it through SSH.

A program never sees any of this: it calls the host contract
(`host/linux/host.h`) and the browser host speaks the wire for it.

---

## 1. The connection

Plain **HTTP/1.1 over TCP**. The program listens; screens connect.

- **Where**: `127.0.0.1` — this machine only — unless `WHISKER_LISTEN`
  names an address (`0.0.0.0` for every interface). Port `WHISKER_PORT`,
  else 8090.
- **A key** (§5as): listening beyond this machine, the program makes one —
  128 random bits, as hex — and prints the address with it,
  `http://host:8090/?k=…`; `WHISKER_KEY` sets one instead, loopback too.
  Every request then carries **`k=<key>`** in its query — the page, the
  stream, a post, an image — and a duplex screen says ` k=<key>` on its
  first line; without it a request is **`403`** and a duplex line gets
  `E key`, before anything else is read or sent. A display the program
  dialled is not asked. On loopback with no key set, nothing is asked.
  A key is not encryption. **Plain HTTP carries no secret**, so a screen on another
  machine comes through an **SSH tunnel** (`ssh -N -L 8090:localhost:8090
  you@there`), which encrypts and decides who may connect at all.
- **What a screen draws** it says itself (§5am): `caps=` in a duplex
  connection's first line (§8), `?c=` on a stream — the `WH_CAP_*` bits:
  1 UTF-8, 2 images, 4 clip paths, 8 opacity, 16 entities. A screen that
  says nothing is taken for a browser (1|2|4|16). The program asks
  `wh_seat_caps(seat)` for one screen and `wh_caps()` for what every
  screen here draws. `WHISKER_CAPS`, if set, is a ceiling over all of
  them.

| request | what it is |
|---|---|
| `GET /` | the page: a canvas and the script that reads the stream |
| `GET /static/<name>` | an image a document names (§5r): `.svg .png .jpg .gif`, a plain name, from `WHISKER_STATIC` or `./static` |
| `GET /events[?v=<viewer>][&c=<caps>]` | **the stream**: the program's messages to this screen, as Server-Sent Events, for as long as the screen stays |
| `WHISKER 1 …` | not HTTP at all: **the wire on one connection**, both ways (§8) |
| `POST /ev[?s=<seat>&g=<gen>][&w=<window>]` | **events**: this screen's input, lines in the body; answered `204`, connection closed |

A request, or a line on one stream (§8), is at most 64 KB — a longer line
is passed over to its end and the seat kept. A connection that is not yet
a screen (a request, a display dialled) and silent five seconds is
closed. Anything else is `404`; a stream when sixteen
are open is `503`.

## 2. The stream: program → screen

`Content-Type: text/event-stream`. Each message is one line,
`data: <message>`, then a blank line. A document's own line breaks are
sent as spaces, so a message is always one line.

| message | meaning |
|---|---|
| `S <seat> <gen>` | **you are seat `<seat>`**, this joining of it `<gen>` — sent first, and only when the program serves seats (`wh_seats(1)`). Every post must carry both. |
| `N <win> <title>` | **a window's title** (§5as), to the end of the line, at most 63 bytes — window 0's is sent on joining, after `S`; the page shows 0's on its tab |
| `F <win>` | **bring that window to the front** (§5as) |
| `O <win> <w> <h> <title>` | **a window opened** (§5af): number 1–7, `w`×`h` device pixels, its title to the end of the line |
| `Q <win>` | a window closed |
| `C <text>` | **copy this to the clipboard** (§5s), escaped (§4) |
| `<y0> <y1> <w> <h> <svg…>` | **a document for window 0**: repaint rows `y0`..`y1` (half-open) of a `w`×`h` picture with it — or all of it when `y1 <= y0`. A complete SVG every time, never a fragment: it carries the backing for the rows it repaints (§3 of the design) |
| `w<win> <y0> <y1> <w> <h> <svg…>` | the same, for window `win` |

**A screen ignores a message it does not know**: that is how the wire
grows.

**On joining**, in order: `S` (seats only); `N 0` and window 0's title; an `O` for every window already
open; then, for a program **without** seats, every document since the last
whole one, so a late screen sees what the others see. A program **with**
seats is told the screen joined (`WH_JOIN`) and draws it whole itself.

**Repainting rows** is the screen's job, exactly as RV-9's `/w0` does it: draw
the document, and copy only rows `y0`..`y1` onto what is already there.
Documents arrive in order and must be applied in order.

## 3. Events: screen → program

`POST /ev`, a body of lines — any number, applied in order. The query names
the screen: `s` and `g` from its `S` message (a program serving seats drops
a post that names no seat, or a seat with another joining's `g`), and `w` for
a window other than 0.

| line | event (`host.h`) |
|---|---|
| `D <x> <y> <mods> <clicks> <finger>` | **press** (`WH_PRESS`). `mods`: 256 Shift, 2048 a finger not a mouse; `clicks` the platform's count (2 a double-click); `finger` 0 the mouse, else 1–255 |
| `M <x> <y> <finger>` | moved while pressed |
| `U <x> <y> <finger>` | released |
| `H <x> <y>` | **hover** (§5aa): moved with nothing pressed; `-1 -1` left the window |
| `R <x> <y>` | asked for a context menu: a right-click, a long press |
| `W <x> <y> <d>` | wheel: `d` pixels, positive toward the end |
| `K <key>` | **a key**: one of `WH_KEY_*` — 1 Left, 2 Right, 3 Home, 4 End, 5 Backspace, 6 Delete, 7 Enter, 8 Tab, 9 Shift-Tab, 10 Escape, 11 Up, 12 Down, 13 Menu, 14 Page Up, 15 Page Down — or a **chord**, a printable character lower-case; either with modifiers added: 256 Shift, 512 Ctrl, 1024 Alt. Ctrl-S is `K 627` |
| `T <text>` | typed text, UTF-8, up to 7 bytes, to the end of the line |
| `P <text>` | **pasted** text, escaped (§4): kept as **this seat's** clipboard, then Ctrl-V — each seat has its own |
| `X` | draw everything again (`WH_EXPOSE`) |
| `C` | a window's close button (`WH_CLOSE`), with `w` — never window 0 here |
| `Y` | still here (seats): every 2 s; **6 s of silence and the seat is gone** |
| `G` | gone for good: the page is closing (seats) |
| `Z <w> <h>` | this screen's size in pixels, 160..4096 (seats) |

A point outside the screen is dropped (a hover's `-1 -1` aside). Motion
(`M`, `H`) the program has not yet taken is merged, the newest standing,
so one screen's pointer cannot crowd out another's keys; and while the
program is behind, a stream is not read — TCP holds the screen back. A finger's
number is the screen's, so it is folded into 1–255 and never becomes 0.

## 4. Text in messages

`C` and `P` carry text that may hold line breaks: `\n` stands for a line
break and `\\` for a backslash; a carriage return is dropped. `T` and titles
end at the line's end.

## 5. Seats, and who is who

A program that calls `wh_seats(1)` serves **seats**: each stream is one, up
to 16 at once, and the program hears `WH_JOIN` and `WH_GONE` with the seat's
number and sends a document to one seat with `wh_to(seat)`. A seat's number
is a slot and is reused; its **generation** (`g`) is not, so a post from a
screen that lost its seat cannot speak for the next one to hold the number.

**`?v=<viewer>`** on the stream is the name the screen gives itself —
letters, digits, `-`, `_` and `.`, at most 63 — and the program reads it
with **`wh_who(seat)`**. It is a **claim, not a proof**: a program uses it
to give a viewer back what was theirs (the code editor keeps their caret
and view), never to decide what they may do. Who may connect at all is the
transport's — SSH — and who a person is, is the program's (D24, D25; the
architecture's "who owns what"). The page sends a name kept for its tab
(`sessionStorage`): a reload returns as the same viewer, two tabs are two.
`whisker-client` sends the name it is given.

A program that does **not** serve seats sees every screen as one: every
document goes to every screen, every screen's input to seat 0.

## 6. What a screen must do, in short

(On an HTTP stream; on one stream, §8, it is simpler: first line, then
lines.)

1. Open `GET /events`, with `?v=` if it has a name; read messages.
2. On `S`, keep the seat and generation; post with them.
3. Apply documents in order, repainting only their rows, per window.
4. Open and close windows on `O` and `Q`; put `C`'s text on its clipboard.
5. Post its input as lines; batch what it has; `Y` every 2 s if seated.
6. To detach, close the stream. The program goes on.

## 7. Not yet

- **A slow screen slows every screen**: a document is written to each in
  turn, waiting up to a second on one whose link is full (and dropping it
  after). Queues per screen would end that; named by the fourth review,
  not built.

- **No encryption, no secret in the protocol itself**: SSH provides both
  for a remote screen; a browser on the network has only
  `WHISKER_LISTEN`'s choice to open it, and the key that then comes with
  it (§1): who may connect, not who can read. TLS is to come.
- **No compression**; a document is at most what the program sends —
  RV-9's own window takes 4,096 bytes, and programs that can exceed it draw
  in bands (`host/linux/fit.h`).
- **An SSH server without forwarding** (RV-9's) cannot carry `ssh -W`; a
  relay run as the session's command would — named, not built.
- **Saved places** are the client's own (§5as): `whisker-client --save
  NAME …` keeps the words, `whisker-client NAME` uses them,
  `--places` lists them — in `~/.config/whisker/places`, 0600.

## 8. The wire on one stream (§5am)

The same messages, on **one two-way byte stream** — a TCP connection to
the program's port, or anything that carries bytes both ways: **OpenSSH's
`ssh -W 127.0.0.1:<port> you@host`**, which joins a session's own input and
output to a port on the far side, so SSH carries the wire with no tunnel
and nothing installed.

- **The screen speaks first**, one line:
  `WHISKER 1[ v=<viewer>][ caps=<n>][ k=<key>]` — who it says it is (§5), the key if the program has one (§1), and what it
  draws (§1).
- **Then both send lines**, `
` ends each. The program: every message of
  §2, as it is — no `data: `, no blank line. The screen: every event line
  of §3, for window 0 as it is, for window `n` prefixed `w<n> `.
- **The connection is the seat**: no `s`, no `g`; the program serving
  seats sends `S <seat> <gen>` as on a stream, for the screen's own use.
  **Nothing else can speak for it**: a POST naming a duplex seat is
  dropped, its generation right or not. `Y` is not needed — the
  connection closing is the seat going.
- A program refusing a screen (sixteen already) says one line, `E <why>`,
  and closes.

**Either side may dial.** A screen dials a program's port — `whisker-client
[host:]port`, or `--ssh you@host [port]`. Or **a program dials a screen**:
`WHISKER_DISPLAY=<host>:<port>` makes it connect out, as well as listen, to
a screen waiting with `whisker-client --listen [<host>:]<port>` (9100 by
default), which answers with its `WHISKER` line; from there it is a seat
like any other. Across machines: `ssh -R 9100:127.0.0.1:9100 you@server`,
then on the server `WHISKER_DISPLAY=127.0.0.1:9100 program`. **The display
going away leaves the program running**, and it dials again every three
seconds while it is away — a display that comes back gets it back. The
dial never waits: a display that does not answer (a laptop asleep) costs
the program's other screens nothing.
