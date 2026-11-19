/*
 * whisker-client: a whisker program's window, from wherever the program runs
 * (design.md §5al, D25).
 *
 *     whisker-client [host:]port [viewer]
 *
 * The program serves its window with the browser host (host/web), which
 * speaks the wire (docs/wire.md); this is a screen for that wire that is not
 * a browser: an SDL window drawn by RV-9's rasteriser -- the desktop host's
 * own code, host_sdl.c -- so a program shown here draws exactly what it
 * draws in its own window. Across machines it goes through SSH:
 *
 *     ssh -N -L 8090:localhost:8090 you@server &     the tunnel
 *     whisker-client 8090 alice                      the window
 *
 * `viewer` is the name this screen gives the program (?v=, wh_who): a
 * viewer who comes back under the same name finds what the program kept for
 * them -- in the code editor, their caret and their view. A claim, not a
 * proof: SSH is what decides who may connect at all.
 *
 * Closing the window DETACHES: the program goes on running, and a client
 * started again later joins it as a new seat. The program is never closed
 * from here.
 *
 * For tests, WHISKER_CLIENT_SCRIPT=1 also reads wire lines from standard
 * input and sends them as this screen's own input ("T h", "K 15", "D 10 20
 * 0 1 0"...), and "!quit" detaches -- how a test types into a client whose
 * display (SDL's dummy driver) has no keyboard. WHISKER_SHOT is the desktop
 * host's: SIGUSR1 writes the window's pixels. WHISKER_CLIENT_LOG names a
 * file every message from the program is appended to, a line each.
 */
#include "host.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static char host[256] = "127.0.0.1", port[16] = "8090", viewer[64];
static int seat = -1, opened = 0, script = 0;
static unsigned gen = 0;
static int rwin[8];                     /* the program's window n, here: a local number or -1 */
static volatile sig_atomic_t stop;

static void on_term(int sig) { (void)sig; stop = 1; }

static int dial(void)
{
	struct addrinfo hints, *ai = NULL, *p;
	int fd = -1;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(host, port, &hints, &ai) != 0) return -1;
	for (p = ai; p != NULL && fd < 0; p = p->ai_next) {
		fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (fd >= 0 && connect(fd, p->ai_addr, p->ai_addrlen) != 0) { close(fd); fd = -1; }
	}
	freeaddrinfo(ai);
	return fd;
}

static int send_all(int fd, const char *b, int n)
{
	while (n > 0) {
		ssize_t k = send(fd, b, (size_t)n, MSG_NOSIGNAL);
		if (k <= 0) return -1;
		b += k; n -= (int)k;
	}
	return 0;
}

/* Events for one of the program's windows, gathered and sent as one POST. */
static char pend[8][16384];
static int plen[8];

static void queue(int w, const char *line)
{
	int n = (int)strlen(line);
	if (w < 0 || w >= 8 || plen[w] + n + 2 >= (int)sizeof pend[w]) return;
	memcpy(pend[w] + plen[w], line, (size_t)n);
	plen[w] += n;
	pend[w][plen[w]++] = '\n';
}

static void flush(void)
{
	for (int w = 0; w < 8; w++) {
		char head[256], resp[256];
		int fd, n;
		if (plen[w] == 0) continue;
		fd = dial();
		if (fd < 0) { plen[w] = 0; continue; }
		{
			/* the seat and its joining, when the program serves seats; the
			   window, when it is not the first (docs/wire.md) */
			char q[64] = "";
			int k = 0;
			if (seat >= 0) k += snprintf(q + k, sizeof q - (size_t)k, "s=%d&g=%u", seat, gen);
			if (w > 0) k += snprintf(q + k, sizeof q - (size_t)k, "%sw=%d", k ? "&" : "", w);
			n = snprintf(head, sizeof head, "POST /ev%s%s HTTP/1.1\r\nHost: %s\r\n"
			             "Content-Length: %d\r\nConnection: close\r\n\r\n",
			             k ? "?" : "", q, host, plen[w]);
		}
		if (send_all(fd, head, n) == 0) send_all(fd, pend[w], plen[w]);
		while (read(fd, resp, sizeof resp) > 0) ;   /* the 204, then the host closes */
		close(fd);
		plen[w] = 0;
	}
}

/* The program's window number for a local one. */
static int remote_of(int local)
{
	for (int n = 0; n < 8; n++) if (rwin[n] == local) return n;
	return -1;
}

static void unesc(char *t)
{
	char *o = t;
	for (; *t != '\0'; t++) {
		if (*t == '\\' && t[1] != '\0') { t++; *o++ = (*t == 'n') ? '\n' : *t; }
		else *o++ = *t;
	}
	*o = '\0';
}

/* One message off the stream (docs/wire.md): a seat, a window opening or
   closing, a copy, or a document to draw. */
static FILE *mlog;
static void message(char *m)
{
	if (mlog != NULL) { fputs(m, mlog); fputc('\n', mlog); fflush(mlog); }
	if (m[0] == 'S' && m[1] == ' ') {
		sscanf(m + 2, "%d %u", &seat, &gen);
		fprintf(stderr, "whisker-client: seat %d\n", seat);
		return;
	}
	if (m[0] == 'C' && m[1] == ' ') { unesc(m + 2); wh_copy(m + 2, (int)strlen(m + 2)); return; }
	if (m[0] == 'O' && m[1] == ' ') {
		int n, w, h, at = 0;
		if (sscanf(m + 2, "%d %d %d %n", &n, &w, &h, &at) == 3 && n > 0 && n < 8 && opened) {
			if (rwin[n] >= 0) wh_window_close(rwin[n]);
			rwin[n] = wh_window(w, h, m + 2 + at);
		}
		return;
	}
	if (m[0] == 'Q' && m[1] == ' ') {
		int n = atoi(m + 2);
		if (n > 0 && n < 8 && rwin[n] >= 0) { wh_window_close(rwin[n]); rwin[n] = -1; }
		return;
	}
	{
		int n = 0, y0, y1, w, h, at = 0;
		char *p = m;
		if (*p == 'w') { n = atoi(p + 1); p = strchr(p, ' '); if (p == NULL) return; p++; }
		if (sscanf(p, "%d %d %d %d %n", &y0, &y1, &w, &h, &at) != 4 || n < 0 || n >= 8) return;
		if (!opened && n == 0) {
			char title[300];
			snprintf(title, sizeof title, "whisker -- %s:%s", host, port);
			if (wh_open(w, h, 1, title) != 0) { stop = 1; return; }
			opened = 1;
			rwin[0] = 0;
		}
		if (rwin[n] < 0) return;
		wh_on(rwin[n]);
		wh_clip(y1 > y0 ? y0 : 0, y1 > y0 ? y1 : 0);
		wh_write(p + at, (int)strlen(p + at));
		wh_on(0);
	}
}

/* An event of this screen's as the wire says it, for the program. */
static void input(const wh_event_t *e)
{
	char l[96];
	int w = remote_of(e->win);
	if (w < 0) return;
	switch (e->kind) {
	case WH_PRESS:   snprintf(l, sizeof l, "D %d %d %d %d %d", e->x, e->y, e->key, e->d, e->finger); break;
	case WH_RELEASE: snprintf(l, sizeof l, "U %d %d %d", e->x, e->y, e->finger); break;
	case WH_MOVE:    snprintf(l, sizeof l, "M %d %d %d", e->x, e->y, e->finger); break;
	case WH_HOVER:   snprintf(l, sizeof l, "H %d %d", e->x, e->y); break;
	case WH_WHEEL:   snprintf(l, sizeof l, "W %d %d %d", e->x, e->y, e->d); break;
	case WH_MENU:    snprintf(l, sizeof l, "R %d %d", e->x, e->y); break;
	case WH_TEXT:    snprintf(l, sizeof l, "T %s", e->text); break;
	case WH_KEY:
		if (e->key == ('v' | WH_MOD_CTRL)) {
			/* a paste: this machine's clipboard, carried to the program as
			   the page carries its own (P, escaped) */
			static char t[6000], q[12000];
			int n = wh_paste(t, sizeof t), k = 0;
			if (n <= 0) return;
			q[k++] = 'P'; q[k++] = ' ';
			for (int i = 0; i < n && k < (int)sizeof q - 3; i++) {
				if (t[i] == '\n') { q[k++] = '\\'; q[k++] = 'n'; }
				else if (t[i] == '\\') { q[k++] = '\\'; q[k++] = '\\'; }
				else if (t[i] != '\r') q[k++] = t[i];
			}
			q[k] = '\0';
			queue(w, q);
			return;
		}
		snprintf(l, sizeof l, "K %d", e->key);
		break;
	case WH_CLOSE:
		if (w == 0) { stop = 1; return; }       /* the program's own window: detach */
		snprintf(l, sizeof l, "C");
		break;
	default: return;
	}
	queue(w, l);
}

int main(int argc, char **argv)
{
	static char sb[1 << 20];
	int sfd, slen = 0;
	long last_y = 0;

	if (argc < 2) {
		fprintf(stderr, "usage: whisker-client [host:]port [viewer]\n");
		return 2;
	}
	{
		const char *c = strrchr(argv[1], ':');
		if (c != NULL) {
			snprintf(host, sizeof host, "%.*s", (int)(c - argv[1]), argv[1]);
			snprintf(port, sizeof port, "%s", c + 1);
		} else snprintf(port, sizeof port, "%s", argv[1]);
	}
	if (argc > 2) {
		int k = 0;
		for (const char *v = argv[2]; *v != '\0' && k < (int)sizeof viewer - 1; v++)
			if ((*v >= 'a' && *v <= 'z') || (*v >= 'A' && *v <= 'Z') || (*v >= '0' && *v <= '9') ||
			    *v == '-' || *v == '_' || *v == '.') viewer[k++] = *v;
		viewer[k] = '\0';
	}
	script = getenv("WHISKER_CLIENT_SCRIPT") != NULL;
	if (getenv("WHISKER_CLIENT_LOG") != NULL) mlog = fopen(getenv("WHISKER_CLIENT_LOG"), "a");
	for (int n = 0; n < 8; n++) rwin[n] = -1;
	signal(SIGTERM, on_term);
	signal(SIGINT, on_term);
	if (script) fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

	sfd = dial();
	if (sfd < 0) { fprintf(stderr, "whisker-client: nothing answers at %s:%s\n", host, port); return 1; }
	{
		char req[300];
		int n = snprintf(req, sizeof req, "GET /events%s%s HTTP/1.1\r\nHost: %s\r\nAccept: text/event-stream\r\n\r\n",
		                 viewer[0] ? "?v=" : "", viewer, host);
		if (send_all(sfd, req, n) != 0) return 1;
	}
	fcntl(sfd, F_SETFL, fcntl(sfd, F_GETFL) | O_NONBLOCK);

	while (!stop) {
		struct pollfd pf[2];
		int np = 1;
		pf[0].fd = sfd; pf[0].events = POLLIN;
		if (script) { pf[1].fd = 0; pf[1].events = POLLIN; np = 2; }
		poll(pf, (nfds_t)np, opened ? 10 : 100);

		/* the stream: whole messages, "data: ...\n\n", after the headers */
		for (;;) {
			ssize_t r = read(sfd, sb + slen, sizeof sb - 1 - (size_t)slen);
			if (r == 0) { fprintf(stderr, "whisker-client: the program went away\n"); stop = 1; break; }
			if (r < 0) break;
			slen += (int)r;
			sb[slen] = '\0';
			if (slen >= (int)sizeof sb - 1) { fprintf(stderr, "whisker-client: a message too long\n"); stop = 1; break; }
		}
		for (;;) {
			static int past_head = 0;
			char *end;
			int used;
			if (!past_head) {                   /* the response's headers, once */
				char *body = strstr(sb, "\r\n\r\n");
				if (body == NULL) break;
				if (strncmp(sb, "HTTP/1.1 200", 12) != 0) {
					fprintf(stderr, "whisker-client: refused: %.40s\n", sb);
					stop = 1;
					break;
				}
				past_head = 1;
				used = (int)(body + 4 - sb);
				memmove(sb, sb + used, (size_t)(slen - used) + 1);
				slen -= used;
			}
			end = strstr(sb, "\n\n");
			if (end == NULL) break;
			*end = '\0';
			if (strncmp(sb, "data: ", 6) == 0) message(sb + 6);
			used = (int)(end + 2 - sb);
			memmove(sb, sb + used, (size_t)(slen - used) + 1);
			slen -= used;
		}

		/* this screen's input */
		if (opened) {
			wh_event_t e;
			int k;
			while ((k = wh_poll(&e)) != WH_NOTHING) {
				if (k == WH_QUIT) { stop = 1; break; }
				input(&e);
			}
		}
		if (script) {
			static char in[4096];
			static int il = 0;
			ssize_t r = read(0, in + il, sizeof in - 1 - (size_t)il);
			if (r > 0) il += (int)r;
			in[il] = '\0';
			for (char *nl; (nl = strchr(in, '\n')) != NULL; ) {
				*nl = '\0';
				if (strcmp(in, "!quit") == 0) stop = 1;
				else if (in[0] != '\0') queue(0, in);
				memmove(in, nl + 1, strlen(nl + 1) + 1);
				il = (int)strlen(in);
			}
		}
		/* still here, every two seconds: a seat silent for six is gone */
		if (seat >= 0 && wh_ms() - last_y >= 2000) { queue(0, "Y"); last_y = wh_ms(); }
		if (opened) flush();
	}
	close(sfd);                                 /* detached: the program goes on */
	if (opened) wh_close();
	fprintf(stderr, "whisker-client: detached\n");
	return 0;
}
