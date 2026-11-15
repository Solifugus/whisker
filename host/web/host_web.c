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
 *     in    the page POSTs events to /ev, a line each: "D x y [mods]",
 *           "M x y", "U x y", "W x y d", "K key", "T text", "R x y" (a
 *           context menu asked for, §5s), "P text" (pasted), "X" (draw it
 *           all again).
 *     clip  what the program copies goes to every page as "C text". Text is
 *           escaped both ways, \n for a line break and \\ for a backslash,
 *           so that a line is still one line. A page may read the clipboard
 *           only when it is pasted into, so a paste comes to the host as
 *           "P", which keeps it and turns it into Ctrl-V (§5s rule 7).
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
/* whisker's advance is half the font size; make the browser's monospace
   agree -- and keep runs of spaces, which an SVG collapses and /w0 does not
   (§5v: measured, xml:space on the root did nothing in Chrome; this does) */
"g.font='100px monospace';\n"
"const ls=(0.5-g.measureText('M').width/100).toFixed(4);\n"
"const STYLE='<style>text{font-family:monospace;letter-spacing:'+ls+'em;white-space:pre}</style>';\n"
/* an SVG drawn as an image may load nothing from outside itself, so each
   image is fetched once, kept as a data URL, and written into the SVG */
"const IMG={};\n"
"function inline(svg){\n"
"  const hrefs=[...svg.matchAll(/<image href=\"([^\"]+)\"/g)].map(m=>m[1]).filter(u=>!(u in IMG));\n"
"  return Promise.all(hrefs.map(u=>fetch(u).then(r=>r.blob()).then(b=>new Promise(ok=>{\n"
"    const f=new FileReader(); f.onload=()=>{IMG[u]=f.result;ok();}; f.onerror=()=>{IMG[u]='';ok();}; f.readAsDataURL(b);}))\n"
"    .catch(()=>{IMG[u]='';}))).then(()=>svg.replace(/<image href=\"([^\"]+)\"/g,(m,u)=>'<image href=\"'+(IMG[u]||u)+'\"'));\n"
"}\n"
"function draw(y0,y1,w,h,svg){\n"
"  svg=svg.replace('<svg ','<svg xmlns=\"http://www.w3.org/2000/svg\" ')\n"
"         .replace(/^(<svg[^>]*>)/,'$1'+STYLE);\n"
"  chain=chain.then(()=>inline(svg)).then(svg=>new Promise(done=>{\n"
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
/* what the program copied (§5s), escaped -- \n a line break, \\ a
   backslash -- onto the system clipboard where the browser lets a page
   write it: a secure origin, or failing that the old way, or a warning */
"const unesc=t=>t.replace(/\\\\(.)/g,(m,ch)=>ch==='n'?'\\n':ch);\n"
"const esc=t=>t.replace(/\\\\/g,'\\\\\\\\').replace(/\\r/g,'').replace(/\\n/g,'\\\\n');\n"
"let clip=null,copied=null;\n"
"function copy(t){\n"
"  clip=t;\n"
"  const old=()=>{const a=document.createElement('textarea');a.value=t;document.body.appendChild(a);\n"
"    a.select();let r=false;try{r=document.execCommand('copy');}catch(x){}a.remove();c.focus();\n"
"    copied=r?'execCommand':'refused';if(!r)console.warn('whisker: the browser would not take the copy');};\n"
"  if(navigator.clipboard&&navigator.clipboard.writeText)\n"
"    navigator.clipboard.writeText(t).then(()=>{copied='clipboard';},old);\n"
"  else old();\n"
"}\n"
"new EventSource('/events').onmessage=e=>{\n"
"  if(e.data.startsWith('C ')){copy(unesc(e.data.slice(2)));return;}\n"
"  const m=/^(\\d+) (\\d+) (\\d+) (\\d+) /.exec(e.data);\n"
"  if(m) draw(+m[1],+m[2],+m[3],+m[4],e.data.slice(m[0].length));\n"
"};\n"
/* events, in order: each POST waits for the one before */
"function send(line){sent=sent.then(()=>fetch('/ev',{method:'POST',body:line}).catch(()=>{}));}\n"
"function pt(e){const r=c.getBoundingClientRect();\n"
"  return Math.floor((e.clientX-r.left)*W/r.width)+' '+Math.floor((e.clientY-r.top)*H/r.height);}\n"
/* the primary button presses; the secondary asks for a context menu, which
   is the platform's contextmenu event -- a right-click, or a long press on a
   phone. A page may read the clipboard only when it is pasted into (§5s).
   A mouse's press goes from mousedown, the only event whose detail counts
   clicks -- a pointerdown's is 0, measured in Chrome 156 -- so a double-
   click is the platform's, timing and all (§5w); a finger's, from
   pointerdown, as a single. */
"c.addEventListener('pointerdown',e=>{if(e.button!==0)return;c.focus();c.setPointerCapture(e.pointerId);\n"
"  if(e.pointerType!=='mouse')send('D '+pt(e)+' '+(e.shiftKey?256:0)+' 1');});\n"
"c.addEventListener('mousedown',e=>{if(e.button===0)send('D '+pt(e)+' '+(e.shiftKey?256:0)+' '+e.detail);});\n"
"c.addEventListener('pointermove',e=>{if(e.buttons&1)send('M '+pt(e));\n"
"  else if(e.buttons===0&&e.pointerType!=='touch')send('H '+pt(e));});\n"
"c.addEventListener('pointerleave',e=>{if(e.pointerType!=='touch')send('H -1 -1');});\n"
"c.addEventListener('pointerup',e=>{if(e.button===0)send('U '+pt(e));});\n"
"c.addEventListener('contextmenu',e=>{e.preventDefault();send('R '+pt(e));});\n"
"document.addEventListener('paste',e=>{const t=e.clipboardData&&e.clipboardData.getData('text');\n"
"  e.preventDefault();if(t)send('P '+esc(t));});\n"
"c.addEventListener('wheel',e=>{e.preventDefault();send('W '+pt(e)+' '+Math.round(e.deltaY));},{passive:false});\n"
/* keys with their modifiers (§5s): Shift only where it means something --
   a key that moves the caret, and a chord -- and a chord a printable key
   with Ctrl (or Command) or Alt, lower-case. Ctrl-V is left to the
   browser, whose paste event brings the text. */
"const KEYS={ArrowLeft:1,ArrowRight:2,Home:3,End:4,Backspace:5,Delete:6,Enter:7,Escape:10,ArrowUp:11,ArrowDown:12,ContextMenu:13};\n"
"const MOVES={1:1,2:1,3:1,4:1,11:1,12:1};\n"
"c.addEventListener('keydown',e=>{\n"
"  const ctl=e.ctrlKey||e.metaKey;\n"
"  let k=KEYS[e.key],m=(ctl?0x200:0)|(e.altKey?0x400:0);\n"
"  if(e.key==='Tab')k=e.shiftKey?9:8;\n"
"  if(e.key==='F10'&&e.shiftKey)k=13;\n"
"  if(!k&&m&&e.key.length===1&&e.key>' '&&e.key<='~'){\n"
"    if(ctl&&!e.altKey&&e.key.toLowerCase()==='v')return;\n"
"    k=e.key.toLowerCase().charCodeAt(0);\n"
"  }\n"
"  if(k){\n"
"    if(e.shiftKey&&(m||MOVES[k]))m|=0x100;\n"
"    if(k===13)m=0;\n"
"    e.preventDefault();send('K '+(k|m));return;\n"
"  }\n"
"  if([...e.key].length===1&&!m){e.preventDefault();send('T '+e.key);}\n"
"});\n"
"window.whisker={send,canvas:c,drawn:()=>drawn,idle:()=>chain,images:()=>Object.keys(IMG).filter(k=>IMG[k]).length,\n"
"  clip:()=>clip,copied:()=>copied};\n"
"c.focus();\n"
"</script></body></html>\n";

typedef struct {
	int  fd;
	int  sub;                /* subscribed to /events */
	char req[8192];          /* a paste comes in one: room for a few KB */
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
	char     *clip;          /* the clipboard: the last copied or pasted */
	int       clen;
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

/* The clipboard's text, kept: the host's copy of what the page has. */
static void keep(const char *t, int n)
{
	free(S.clip);
	S.clip = (char *)malloc((size_t)n + 1);
	S.clen = 0;
	if (S.clip == 0) return;
	memcpy(S.clip, t, (size_t)n);
	S.clip[n] = '\0';
	S.clen = n;
}

/* One line of /ev: a pointer, a key, text, a paste, or "draw it all again". */
static void event_line(const char *l)
{
	wh_event_t e;
	memset(&e, 0, sizeof e);
	switch (l[0]) {
	case 'D': e.kind = WH_PRESS;   sscanf(l + 1, "%d %d %d %d", &e.x, &e.y, &e.key, &e.d); break;
	case 'R': e.kind = WH_MENU;    sscanf(l + 1, "%d %d", &e.x, &e.y); break;
	case 'P': {
		/* pasted: unescaped into the host's clipboard, then Ctrl-V */
		const char *p = l + ((l[1] == ' ') ? 2 : 1);
		char *t = (char *)malloc(strlen(p) + 1);
		int n = 0;
		if (t == 0) return;
		for (; *p != '\0' && *p != '\r'; p++) {
			if (*p == '\\' && p[1] != '\0') { p++; t[n++] = (*p == 'n') ? '\n' : *p; }
			else t[n++] = *p;
		}
		keep(t, n);
		free(t);
		e.kind = WH_KEY;
		e.key = 'v' | WH_MOD_CTRL;
		break;
	}
	case 'M': e.kind = WH_MOVE;    sscanf(l + 1, "%d %d", &e.x, &e.y); break;
	case 'H': e.kind = WH_HOVER;   sscanf(l + 1, "%d %d", &e.x, &e.y); break;   /* §5aa */
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
	} else if (strncmp(path, "/static/", 8) == 0) {
		/* Files a page's images name (§5r): from WHISKER_STATIC, or ./static,
		   by a plain name -- no '/', no "..", nothing above the folder. */
		const char *name = path + 8, *dir = getenv("WHISKER_STATIC");
		const char *type = strstr(name, ".svg") ? "image/svg+xml"
		                 : strstr(name, ".png") ? "image/png"
		                 : (strstr(name, ".jpg") || strstr(name, ".jpeg")) ? "image/jpeg"
		                 : strstr(name, ".gif") ? "image/gif" : 0;
		char file[512], h[160];
		FILE *f = 0;
		long len = 0;
		if (type != 0 && name[0] != '\0' && strchr(name, '/') == 0 && strstr(name, "..") == 0) {
			snprintf(file, sizeof file, "%s/%s", dir ? dir : "static", name);
			f = fopen(file, "rb");
		}
		if (f == 0) {
			send_str(c->fd, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
		} else {
			char buf[4096];
			size_t k;
			fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
			snprintf(h, sizeof h, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n"
			         "Content-Length: %ld\r\nConnection: close\r\n\r\n", type, len);
			send_str(c->fd, h);
			while ((k = fread(buf, 1, sizeof buf, f)) > 0) send_all(c->fd, buf, (int)k);
			fclose(f);
		}
		drop(i);
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

/* A browser draws UTF-8, images and clip paths (§5r), and decodes entities,
   so text may hold < > & (§5v). Not opacity yet: the page composites by
   rows onto a canvas, and nothing whisker draws uses it. */
int wh_caps(void) { return WH_CAP_UTF8 | WH_CAP_IMAGE | WH_CAP_CLIP | WH_CAP_ENTITY; }

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

/* Copy: kept here, and sent to every page to put on the system clipboard,
   escaped so that it is one line of an event. Not added to the log a late
   page is sent: a copy is something that happened, not part of the picture. */
int wh_copy(const char *s, int n)
{
	char *m = (char *)malloc((size_t)n * 2 + 16);
	int k;
	if (m == 0) return -1;
	keep(s, n);
	k = snprintf(m, 16, "data: C ");
	for (int i = 0; i < n; i++) {
		if (s[i] == '\n') { m[k++] = '\\'; m[k++] = 'n'; }
		else if (s[i] == '\\') { m[k++] = '\\'; m[k++] = '\\'; }
		else if (s[i] != '\r') m[k++] = s[i];
	}
	m[k++] = '\n'; m[k++] = '\n';
	for (int i = 0; i < MAXC; i++)
		if (S.c[i].fd >= 0 && S.c[i].sub && send_all(S.c[i].fd, m, k) < 0) drop(i);
	free(m);
	return 0;
}

/* Paste: what the page last pasted, or this program last copied -- whole,
   or -1. */
int wh_paste(char *b, int cap)
{
	if (S.clip == 0 || S.clen == 0 || S.clen >= cap) return -1;
	memcpy(b, S.clip, (size_t)S.clen + 1);
	return S.clen;
}

void wh_close(void)
{
	if (!S.open) return;
	for (int i = 0; i < MAXC; i++) if (S.c[i].fd >= 0) drop(i);
	close(S.lfd);
	free(S.doc); free(S.log); free(S.clip);
	S.open = 0;
}
