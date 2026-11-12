/*
 * The substrate contract, in a browser (design.md §5k).
 *
 * host/linux/host.h is the seam: a window you write SVG to with a row range,
 * and events in. This implements exactly that over HTTP, so a program written
 * against it -- edit.c, demo.c, input.c -- runs in any browser by linking
 * this file instead of host_sdl.c, and neither it nor whisker knows.
 *
 *     out   every document is sent to every page subscribed to /events, as a
 *           Server-Sent Event: "y0 y1 w h <svg ...>". The page draws it into
 *           an image and copies ONLY rows y0..y1 onto a canvas the size of
 *           the device -- the board's banded repaint, kept by the page, so a
 *           clipped document paints its rows over what was there exactly as
 *           /w0 does.
 *     in    the page POSTs events to /ev, a line each: "D x y", "M x y",
 *           "U x y", "W x y d", "K key", "T text", "X" (draw it all again).
 *
 * Plain HTTP both ways: no WebSocket handshake, no library, no thread. One
 * poll() over the sockets, run whenever the program polls or waits. A page
 * that connects late is sent every document since the last full one, and the
 * program is asked to draw everything again (WH_EXPOSE), whichever it heeds.
 *
 * Text is the page's to get right: whisker lays out a character as half its
 * font size wide (§2.1), a browser's monospace is wider, and the page sets
 * letter-spacing so the two agree -- the host making its renderer match the
 * one whisker was written for, which is a host's job (§2.8).
 */
#include "host.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define MAXC 16

static const char PAGE[] =
"<!doctype html><html><head><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>whisker</title><style>"
"html,body{margin:0;height:100%;background:#000}"
"canvas{display:block;margin:auto;max-width:100vw;max-height:100vh;touch-action:none;outline:none}"
"</style></head><body><canvas id=c tabindex=0></canvas><script>\n"
"const c=document.getElementById('c'),g=c.getContext('2d');\n"
"let W=0,H=0,chain=Promise.resolve(),sent=Promise.resolve(),drawn=0;\n"
/* whisker's advance is half the font size; make the browser's monospace agree */
"g.font='100px monospace';\n"
"const ls=(0.5-g.measureText('M').width/100).toFixed(4);\n"
"const STYLE='<style>text{font-family:monospace;letter-spacing:'+ls+'em}</style>';\n"
"function draw(y0,y1,w,h,svg){\n"
"  svg=svg.replace('<svg ','<svg xmlns=\"http://www.w3.org/2000/svg\" ')\n"
"         .replace(/^(<svg[^>]*>)/,'$1'+STYLE);\n"
"  chain=chain.then(()=>new Promise(done=>{\n"
"    const img=new Image();\n"
"    img.onload=()=>{\n"
"      if(W!==w||H!==h){c.width=W=w;c.height=H=h;}\n"
"      let a=y0,b=y1; if(b<=a){a=0;b=h;}\n"
"      g.drawImage(img,0,a,w,b-a,0,a,w,b-a); drawn++; done();\n"
"    };\n"
"    img.onerror=()=>done();\n"
"    img.src='data:image/svg+xml;charset=utf-8,'+encodeURIComponent(svg);\n"
"  }));\n"
"}\n"
"new EventSource('/events').onmessage=e=>{\n"
"  const m=/^(\\d+) (\\d+) (\\d+) (\\d+) /.exec(e.data);\n"
"  if(m) draw(+m[1],+m[2],+m[3],+m[4],e.data.slice(m[0].length));\n"
"};\n"
/* events, in order: each POST waits for the one before */
"function send(line){sent=sent.then(()=>fetch('/ev',{method:'POST',body:line}).catch(()=>{}));}\n"
"function pt(e){const r=c.getBoundingClientRect();\n"
"  return Math.floor((e.clientX-r.left)*W/r.width)+' '+Math.floor((e.clientY-r.top)*H/r.height);}\n"
"c.addEventListener('pointerdown',e=>{c.focus();c.setPointerCapture(e.pointerId);send('D '+pt(e));});\n"
"c.addEventListener('pointermove',e=>{if(e.buttons)send('M '+pt(e));});\n"
"c.addEventListener('pointerup',e=>send('U '+pt(e)));\n"
"c.addEventListener('wheel',e=>{e.preventDefault();send('W '+pt(e)+' '+Math.round(e.deltaY));},{passive:false});\n"
"const KEYS={ArrowLeft:1,ArrowRight:2,Home:3,End:4,Backspace:5,Delete:6,Enter:7};\n"
"c.addEventListener('keydown',e=>{\n"
"  let k=KEYS[e.key]; if(e.key==='Tab')k=e.shiftKey?9:8;\n"
"  if(k){e.preventDefault();send('K '+k);return;}\n"
"  if(e.key.length===1&&!e.ctrlKey&&!e.metaKey&&!e.altKey){e.preventDefault();send('T '+e.key);}\n"
"});\n"
"window.whisker={send,canvas:c,drawn:()=>drawn,idle:()=>chain};\n"
"c.focus();\n"
"</script></body></html>\n";

typedef struct {
	int  fd;
	int  sub;                /* subscribed to /events */
	char req[2048];
	int  got;
} client_t;

static struct {
	int       lfd, w, h, y0, y1, open;
	client_t  c[MAXC];
	char     *doc;           /* the document being written */
	int       dlen, dcap;
	char     *log;           /* every message since the last full document */
	int       llen, lcap;
	wh_event_t q[64];
	int       qh, qt;
} S;

static void grow(char **b, int *cap, int need)
{
	if (need <= *cap) return;
	while (*cap < need) *cap = *cap ? *cap * 2 : 8192;
	*b = realloc(*b, (size_t)*cap);
}

static void push(const wh_event_t *e)
{
	int nt = (S.qt + 1) % 64;
	if (nt == S.qh) return;                 /* full: drop, as a busy panel would */
	S.q[S.qt] = *e;
	S.qt = nt;
}

static int send_all(int fd, const char *b, int n);
static int send_str(int fd, const char *t) { return send_all(fd, t, (int)strlen(t)); }

static int send_all(int fd, const char *b, int n)
{
	while (n > 0) {
		ssize_t k = send(fd, b, (size_t)n, MSG_NOSIGNAL);
		if (k <= 0) return -1;
		b += k; n -= (int)k;
	}
	return 0;
}

static void drop(int i)
{
	close(S.c[i].fd);
	S.c[i].fd = -1;
	S.c[i].sub = 0;
	S.c[i].got = 0;
}

/* One line of /ev: a pointer, a key, text, or "draw it all again". */
static void event_line(const char *l)
{
	wh_event_t e;
	memset(&e, 0, sizeof e);
	switch (l[0]) {
	case 'D': e.kind = WH_PRESS;   sscanf(l + 1, "%d %d", &e.x, &e.y); break;
	case 'M': e.kind = WH_MOVE;    sscanf(l + 1, "%d %d", &e.x, &e.y); break;
	case 'U': e.kind = WH_RELEASE; sscanf(l + 1, "%d %d", &e.x, &e.y); break;
	case 'W': e.kind = WH_WHEEL;   sscanf(l + 1, "%d %d %d", &e.x, &e.y, &e.d); break;
	case 'K': e.kind = WH_KEY;     sscanf(l + 1, "%d", &e.key); break;
	case 'X': e.kind = WH_EXPOSE;  break;
	case 'T': {
		int i = 0;
		e.kind = WH_TEXT;
		l += (l[1] == ' ') ? 2 : 1;
		while (l[i] != '\0' && l[i] != '\n' && l[i] != '\r' && i < (int)sizeof e.text - 1) {
			e.text[i] = l[i];
			i++;
		}
		e.text[i] = '\0';
		break;
	}
	default: return;
	}
	if (e.kind != WH_EXPOSE && e.kind != WH_KEY && e.kind != WH_TEXT &&
	    (e.x < 0 || e.y < 0 || e.x >= S.w || e.y >= S.h)) return;
	push(&e);
}

static void respond(int i)
{
	client_t *c = &S.c[i];
	char *end = strstr(c->req, "\r\n\r\n");
	char method[8], path[64];
	int len = 0;
	const char *cl;

	if (end == 0) return;                   /* headers not all here yet */
	if (sscanf(c->req, "%7s %63s", method, path) != 2) { drop(i); return; }
	if ((cl = strstr(c->req, "Content-Length:")) != 0 || (cl = strstr(c->req, "content-length:")) != 0)
		len = atoi(cl + 15);
	if (c->got < (int)(end + 4 - c->req) + len) return;   /* body not all here */

	if (strcmp(method, "POST") == 0 && strcmp(path, "/ev") == 0) {
		char *p = end + 4, *stop = p + len;
		*stop = '\0';
		while (p < stop) {
			char *nl = strchr(p, '\n');
			if (nl) *nl = '\0';
			event_line(p);
			if (!nl) break;
			p = nl + 1;
		}
		send_str(c->fd, "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n");
		drop(i);
	} else if (strcmp(path, "/events") == 0) {
		static const char H[] = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
		                        "Cache-Control: no-cache\r\nConnection: keep-alive\r\n\r\n";
		wh_event_t e;
		send_all(c->fd, H, (int)sizeof H - 1);
		if (S.llen > 0) send_all(c->fd, S.log, S.llen);
		c->sub = 1;
		c->got = 0;
		memset(&e, 0, sizeof e);
		e.kind = WH_EXPOSE;
		push(&e);
	} else if (strcmp(path, "/") == 0) {
		char h[160];
		int n = snprintf(h, sizeof h, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n"
		                 "Content-Length: %d\r\nConnection: close\r\n\r\n", (int)sizeof PAGE - 1);
		send_all(c->fd, h, n);
		send_all(c->fd, PAGE, (int)sizeof PAGE - 1);
		drop(i);
	} else {
		send_str(c->fd, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
		drop(i);
	}
}

/* Accept, read, answer: everything the network has for us, waiting up to ms. */
static void service(int ms)
{
	struct pollfd p[MAXC + 1];
	int n = 0, idx[MAXC + 1];

	p[n].fd = S.lfd; p[n].events = POLLIN; idx[n] = -1; n++;
	for (int i = 0; i < MAXC; i++)
		if (S.c[i].fd >= 0) { p[n].fd = S.c[i].fd; p[n].events = POLLIN; idx[n] = i; n++; }
	if (poll(p, (nfds_t)n, ms) <= 0) return;

	if (p[0].revents & POLLIN) {
		int fd = accept(S.lfd, 0, 0);
		if (fd >= 0) {
			struct timeval tv = { 1, 0 };
			int i;
			setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
			for (i = 0; i < MAXC && S.c[i].fd >= 0; i++) ;
			if (i == MAXC) close(fd);
			else { S.c[i].fd = fd; S.c[i].sub = 0; S.c[i].got = 0; }
		}
	}
	for (int k = 1; k < n; k++) {
		int i = idx[k];
		client_t *c = &S.c[i];
		ssize_t r;
		if (!(p[k].revents & (POLLIN | POLLHUP | POLLERR))) continue;
		r = recv(c->fd, c->req + c->got, sizeof c->req - 1 - (size_t)c->got, 0);
		if (r <= 0) { drop(i); continue; }
		if (c->sub) continue;                /* a subscriber says nothing more */
		c->got += (int)r;
		c->req[c->got] = '\0';
		respond(i);
		if (S.c[i].fd >= 0 && S.c[i].got >= (int)sizeof c->req - 1) drop(i);
	}
}

int wh_open(int w, int h, int scale, const char *title)
{
	struct sockaddr_in a;
	const char *port = getenv("WHISKER_PORT");
	int one = 1;

	(void)scale;
	memset(&S, 0, sizeof S);
	for (int i = 0; i < MAXC; i++) S.c[i].fd = -1;
	S.w = w; S.h = h;
	S.lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (S.lfd < 0) { perror("whisker-web: socket"); return -1; }
	setsockopt(S.lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
	memset(&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_ANY);
	a.sin_port = htons((uint16_t)(port ? atoi(port) : 8090));
	if (bind(S.lfd, (struct sockaddr *)&a, sizeof a) < 0 || listen(S.lfd, 8) < 0) {
		perror("whisker-web: bind");
		return -1;
	}
	fprintf(stderr, "whisker-web: %s, %dx%d, at http://<this machine>:%d/\n",
	        title ? title : "whisker", w, h, port ? atoi(port) : 8090);
	S.open = 1;
	return 0;
}

void wh_clip(int y0, int y1) { S.y0 = y0; S.y1 = y1; }

/* Collect a document; at "</svg>" send it, with its rows, to every page. */
void wh_write(const void *bytes, int n)
{
	grow(&S.doc, &S.dcap, S.dlen + n + 1);
	memcpy(S.doc + S.dlen, bytes, (size_t)n);
	S.dlen += n;
	S.doc[S.dlen] = '\0';
	if (S.dlen < 6 || memcmp(S.doc + S.dlen - 6, "</svg>", 6) != 0) return;

	{
		char head[64];
		int full = (S.y1 <= S.y0), hn, mn;
		for (int i = 0; i < S.dlen; i++) if (S.doc[i] == '\n' || S.doc[i] == '\r') S.doc[i] = ' ';
		hn = snprintf(head, sizeof head, "data: %d %d %d %d ", S.y0, S.y1, S.w, S.h);
		mn = hn + S.dlen + 2;
		if (full) S.llen = 0;               /* a full document: the log starts again */
		grow(&S.log, &S.lcap, S.llen + mn);
		memcpy(S.log + S.llen, head, (size_t)hn);
		memcpy(S.log + S.llen + hn, S.doc, (size_t)S.dlen);
		memcpy(S.log + S.llen + hn + S.dlen, "\n\n", 2);
		for (int i = 0; i < MAXC; i++)
			if (S.c[i].fd >= 0 && S.c[i].sub &&
			    send_all(S.c[i].fd, S.log + S.llen, mn) < 0) drop(i);
		S.llen += mn;
	}
	S.dlen = 0;
	S.y0 = S.y1 = 0;                        /* one-shot, as RV9_SVG_SS_ROWS is */
}

int wh_poll(wh_event_t *out)
{
	service(0);
	if (S.qh == S.qt) { out->kind = WH_NOTHING; return WH_NOTHING; }
	*out = S.q[S.qh];
	S.qh = (S.qh + 1) % 64;
	return out->kind;
}

void wh_wait(int ms) { if (S.qh == S.qt) service(ms); }

void wh_close(void)
{
	if (!S.open) return;
	for (int i = 0; i < MAXC; i++) if (S.c[i].fd >= 0) drop(i);
	close(S.lfd);
	free(S.doc); free(S.log);
	S.open = 0;
}
