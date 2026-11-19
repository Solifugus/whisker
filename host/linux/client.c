/*
 * whisker-client: a whisker program's window, from wherever the program runs
 * (design.md §5al, §5am; D25).
 *
 *     whisker-client [host:]port [viewer]             dial a program
 *     whisker-client --ssh you@host [port] [viewer]   through SSH, nothing else
 *     whisker-client --listen [[host:]port]           wait for programs to dial
 *
 * The program serves its window with the browser host (host/web), which
 * speaks the wire (docs/wire.md); this is a screen for it that is not a
 * browser: an SDL window drawn by RV-9's rasteriser -- the desktop host's
 * own code, host_sdl.c -- so a program shown here draws exactly what it
 * draws in its own window. It speaks the wire on ONE two-way stream
 * (docs/wire.md §8): a socket it dialled, the pipes of an `ssh -W` it ran,
 * or a socket a program dialled to it.
 *
 *   --ssh     runs `ssh [WHISKER_SSH_OPTS] -W 127.0.0.1:<port> you@host` --
 *             OpenSSH's own stdio forwarding -- and speaks on its pipes: no
 *             tunnel, no port opened anywhere but the program's loopback.
 *   --listen  takes programs started with WHISKER_DISPLAY=<this>:<port>,
 *             several at once, each its own windows, and keeps a small
 *             window of its own saying where it listens. Across machines:
 *             ssh -R.
 *
 * `viewer` is the name this screen gives the program (wh_who): a viewer who
 * comes back under the same name finds what the program kept for them. A
 * claim, not a proof: SSH is what decides who may connect at all.
 *
 * Closing the window DETACHES: the program goes on running. A program
 * shown by --listen dials again when it is gone, so closing the client and
 * starting it again brings its programs back.
 *
 * For tests, WHISKER_CLIENT_SCRIPT=1 also reads wire lines from standard
 * input and sends them as this screen's input ("T h", "K 15"...), "!quit"
 * detaches; WHISKER_CLIENT_LOG names a file every message is appended to;
 * WHISKER_SHOT is the desktop host's (SIGUSR1 writes window 0's pixels).
 */
#include "host.h"
#include "whisker.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define CONNS 8
#define BUF (1 << 20)

/* One program shown here: the wire to it, and its windows. */
typedef struct {
	int   live;
	int   rfd, wfd;          /* a socket twice, or ssh's pipes */
	pid_t ssh;
	char *b;                 /* what has come and is not yet a whole line */
	int   len;
	int   rwin[8];           /* its window n, here: a local number, or -1 */
	int   seat;
} conn_t;

static conn_t conn[CONNS];
static char host[256] = "127.0.0.1", port[16] = "8090", viewer[64];
static int opened = 0, listening = 0, lfd = -1, script = 0;
static volatile sig_atomic_t stop;
static FILE *mlog;

static void on_term(int sig) { (void)sig; stop = 1; }

static int write_all(int fd, const char *b, int n)
{
	while (n > 0) {
		ssize_t k = write(fd, b, (size_t)n);
		if (k < 0 && errno == EINTR) continue;
		if (k <= 0) return -1;
		b += k; n -= (int)k;
	}
	return 0;
}

/* The first line a screen sends (docs/wire.md §8): who, and what it draws
   -- RV-9's rasteriser's: wh_caps, this desktop host's own answer. */
static void hello(conn_t *c)
{
	char h[160];
	int n = snprintf(h, sizeof h, "WHISKER 1%s%s caps=%d\n", viewer[0] ? " v=" : "", viewer, wh_caps());
	write_all(c->wfd, h, n);
}

static conn_t *adopt(int rfd, int wfd, pid_t ssh)
{
	for (int k = 0; k < CONNS; k++)
		if (!conn[k].live) {
			conn_t *c = &conn[k];
			if (c->b == NULL) c->b = (char *)malloc(BUF);
			if (c->b == NULL) return NULL;
			c->live = 1; c->rfd = rfd; c->wfd = wfd; c->ssh = ssh; c->len = 0; c->seat = -1;
			for (int n = 0; n < 8; n++) c->rwin[n] = -1;
			fcntl(rfd, F_SETFL, fcntl(rfd, F_GETFL) | O_NONBLOCK);
			hello(c);
			return c;
		}
	return NULL;
}

static void lobby(void);

/* A program gone: its windows closed here; the program itself goes on. */
static void lose(conn_t *c)
{
	for (int n = 0; n < 8; n++)
		if (c->rwin[n] >= 0) {
			if (c->rwin[n] != 0 || listening) wh_window_close(c->rwin[n]);
			c->rwin[n] = -1;
		}
	close(c->rfd);
	if (c->wfd != c->rfd) close(c->wfd);
	if (c->ssh > 0) { kill(c->ssh, SIGTERM); waitpid(c->ssh, NULL, 0); }
	c->live = 0;
	if (listening) lobby();
	else stop = 1;                          /* the one program: nothing left to show */
}

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

/* ssh -W: the program's loopback port, on this process's pipes. */
static pid_t ssh_to(const char *target, int *rfd, int *wfd)
{
	int in[2], out[2];
	pid_t pid;
	if (pipe(in) != 0 || pipe(out) != 0) return -1;
	pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) {
		char *argv[64], fw[64], *opts = getenv("WHISKER_SSH_OPTS"), *copy = opts ? strdup(opts) : NULL;
		int a = 0;
		dup2(in[0], 0); dup2(out[1], 1);
		close(in[0]); close(in[1]); close(out[0]); close(out[1]);
		argv[a++] = (char *)"ssh";
		for (char *t = copy ? strtok(copy, " ") : NULL; t != NULL && a < 56; t = strtok(NULL, " ")) argv[a++] = t;
		snprintf(fw, sizeof fw, "127.0.0.1:%s", port);
		argv[a++] = (char *)"-W"; argv[a++] = fw;
		argv[a++] = (char *)target;
		argv[a] = NULL;
		execvp("ssh", argv);
		_exit(127);
	}
	close(in[0]); close(out[1]);
	*wfd = in[1]; *rfd = out[0];
	return pid;
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

/* A program's window 0, here: in a client of one program the client's own
   window, opened at its size; listening, a window of its own. */
static int main_window(int w, int h)
{
	char title[300];
	snprintf(title, sizeof title, "whisker -- %s", listening ? "a program" : host);
	if (!listening) {
		if (!opened) {
			if (wh_open(w, h, 1, title) != 0) { stop = 1; return -1; }
			opened = 1;
		}
		return 0;
	}
	return wh_window(w, h, title);
}

/* One message from a program (docs/wire.md §2): a seat, a window, a copy,
   or a document to draw. */
static void message(conn_t *c, char *m)
{
	if (mlog != NULL) { fputs(m, mlog); fputc('\n', mlog); fflush(mlog); }
	if (m[0] == 'S' && m[1] == ' ') {
		c->seat = atoi(m + 2);
		fprintf(stderr, "whisker-client: seat %d\n", c->seat);
		return;
	}
	if (m[0] == 'E' && m[1] == ' ') { fprintf(stderr, "whisker-client: refused: %s\n", m + 2); return; }
	if (m[0] == 'C' && m[1] == ' ') { unesc(m + 2); wh_copy(m + 2, (int)strlen(m + 2)); return; }
	if (m[0] == 'O' && m[1] == ' ') {
		int n, w, h, at = 0;
		if (sscanf(m + 2, "%d %d %d %n", &n, &w, &h, &at) == 3 && n > 0 && n < 8 && opened) {
			if (c->rwin[n] >= 0) wh_window_close(c->rwin[n]);
			c->rwin[n] = wh_window(w, h, m + 2 + at);
		}
		return;
	}
	if (m[0] == 'Q' && m[1] == ' ') {
		int n = atoi(m + 2);
		if (n > 0 && n < 8 && c->rwin[n] >= 0) { wh_window_close(c->rwin[n]); c->rwin[n] = -1; }
		return;
	}
	{
		int n = 0, y0, y1, w, h, at = 0;
		char *p = m;
		if (*p == 'w') { n = atoi(p + 1); p = strchr(p, ' '); if (p == NULL) return; p++; }
		if (sscanf(p, "%d %d %d %d %n", &y0, &y1, &w, &h, &at) != 4 || n < 0 || n >= 8) return;
		if (n == 0 && c->rwin[0] < 0) {
			c->rwin[0] = main_window(w, h);
			if (listening) lobby();
		}
		if (c->rwin[n] < 0) return;
		wh_on(c->rwin[n]);
		wh_clip(y1 > y0 ? y0 : 0, y1 > y0 ? y1 : 0);
		wh_write(p + at, (int)strlen(p + at));
		wh_on(0);
	}
}

/* An event of this screen's, as a wire line, to the program it belongs to. */
static void input(const wh_event_t *e)
{
	char l[12064];
	conn_t *c = NULL;
	int w = -1, k = 0;
	for (int i = 0; i < CONNS && c == NULL; i++)
		for (int n = 0; conn[i].live && n < 8; n++)
			if (conn[i].rwin[n] == e->win) { c = &conn[i]; w = n; break; }
	if (c == NULL) return;
	if (w > 0) k = snprintf(l, sizeof l, "w%d ", w);
	switch (e->kind) {
	case WH_PRESS:   k += snprintf(l + k, sizeof l - (size_t)k, "D %d %d %d %d %d", e->x, e->y, e->key, e->d, e->finger); break;
	case WH_RELEASE: k += snprintf(l + k, sizeof l - (size_t)k, "U %d %d %d", e->x, e->y, e->finger); break;
	case WH_MOVE:    k += snprintf(l + k, sizeof l - (size_t)k, "M %d %d %d", e->x, e->y, e->finger); break;
	case WH_HOVER:   k += snprintf(l + k, sizeof l - (size_t)k, "H %d %d", e->x, e->y); break;
	case WH_WHEEL:   k += snprintf(l + k, sizeof l - (size_t)k, "W %d %d %d", e->x, e->y, e->d); break;
	case WH_MENU:    k += snprintf(l + k, sizeof l - (size_t)k, "R %d %d", e->x, e->y); break;
	case WH_TEXT:    k += snprintf(l + k, sizeof l - (size_t)k, "T %s", e->text); break;
	case WH_KEY:
		if (e->key == ('v' | WH_MOD_CTRL)) {
			/* a paste: this machine's clipboard, carried as the page carries
			   its own (P, escaped) */
			char t[6000];
			int n = wh_paste(t, sizeof t);
			if (n <= 0) return;
			l[k++] = 'P'; l[k++] = ' ';
			for (int i = 0; i < n && k < (int)sizeof l - 3; i++) {
				if (t[i] == '\n') { l[k++] = '\\'; l[k++] = 'n'; }
				else if (t[i] == '\\') { l[k++] = '\\'; l[k++] = '\\'; }
				else if (t[i] != '\r') l[k++] = t[i];
			}
			l[k] = '\0';
		} else k += snprintf(l + k, sizeof l - (size_t)k, "K %d", e->key);
		break;
	case WH_CLOSE:
		if (w == 0) {                           /* a program's own window */
			if (!listening) stop = 1;           /* the one program: detach */
			return;                             /* listening: the program closes its own */
		}
		k += snprintf(l + k, sizeof l - (size_t)k, "C");
		break;
	default: return;
	}
	if (k < (int)sizeof l - 1) { l[k++] = '\n'; l[k] = '\0'; }
	if (write_all(c->wfd, l, k) != 0) lose(c);
}

/* The listening client's own window: where it listens, what it shows --
   drawn by whisker, as any program's is. */
static wsk_widget_t lob[2];
static wsk_tree_t lobt;
static void to_window(wsk_sink_t *s, const char *b, int n) { (void)s; wh_write(b, n); }
static void lobby(void)
{
	wsk_sink_t s;
	int shown = 0;
	if (!listening || !opened) return;
	for (int i = 0; i < CONNS; i++) shown += conn[i].live;
	snprintf(lob[0].text, WSK_TEXT_MAX, "on %.15s:%.6s", host, port);
	snprintf(lob[1].text, WSK_TEXT_MAX, "%d program%s shown", shown, shown == 1 ? "" : "s");
	s.put = to_window; s.ctx = NULL; s.len = 0;
	wh_on(0);
	wh_clip(0, 0);
	wsk_render(&lobt, &s, 4096);
}

int main(int argc, char **argv)
{
	const char *target = NULL;
	int a = 1;

	if (argc > 1 && strcmp(argv[1], "--listen") == 0) { listening = 1; snprintf(port, sizeof port, "9100"); a = 2; }
	else if (argc > 2 && strcmp(argv[1], "--ssh") == 0) { target = argv[2]; a = 3; }
	if (!listening && target == NULL && argc < 2) {
		fprintf(stderr, "usage: whisker-client [host:]port [viewer]\n"
		                "       whisker-client --ssh you@host [port] [viewer]\n"
		                "       whisker-client --listen [[host:]port]\n");
		return 2;
	}
	if (argc > a) {
		const char *c = strrchr(argv[a], ':');
		if (c != NULL) {
			snprintf(host, sizeof host, "%.*s", (int)(c - argv[a]), argv[a]);
			snprintf(port, sizeof port, "%s", c + 1);
		} else snprintf(port, sizeof port, "%s", argv[a]);
		a++;
	}
	if (argc > a) {
		int k = 0;
		for (const char *v = argv[a]; *v != '\0' && k < (int)sizeof viewer - 1; v++)
			if ((*v >= 'a' && *v <= 'z') || (*v >= 'A' && *v <= 'Z') || (*v >= '0' && *v <= '9') ||
			    *v == '-' || *v == '_' || *v == '.') viewer[k++] = *v;
		viewer[k] = '\0';
	}
	script = getenv("WHISKER_CLIENT_SCRIPT") != NULL;
	if (getenv("WHISKER_CLIENT_LOG") != NULL) mlog = fopen(getenv("WHISKER_CLIENT_LOG"), "a");
	signal(SIGTERM, on_term);
	signal(SIGINT, on_term);
	signal(SIGPIPE, SIG_IGN);
	if (script) fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

	if (listening) {
		struct sockaddr_in sa;
		int one = 1;
		lfd = socket(AF_INET, SOCK_STREAM, 0);
		setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
		memset(&sa, 0, sizeof sa);
		sa.sin_family = AF_INET;
		sa.sin_port = htons((uint16_t)atoi(port));
		if (inet_pton(AF_INET, host, &sa.sin_addr) != 1 ||
		    bind(lfd, (struct sockaddr *)&sa, sizeof sa) != 0 || listen(lfd, 8) != 0) {
			fprintf(stderr, "whisker-client: cannot listen on %s:%s\n", host, port);
			return 1;
		}
		if (wh_open(460, 120, 1, "whisker -- display") != 0) return 1;
		opened = 1;
		memset(lob, 0, sizeof lob);
		lob[0].kind = WSK_LABEL; lob[0].size = 40;
		lob[1].kind = WSK_LABEL; lob[1].size = 40;
		wsk_init(&lobt, WSK_COL, 460, 120);
		lobt.item = lob; lobt.n = 2; lobt.font = 18; lobt.talign = WSK_ALIGN_START;
		lobby();
		fprintf(stderr, "whisker-client: listening on %s:%s (WHISKER_DISPLAY=%s:%s)\n", host, port, host, port);
	} else if (target != NULL) {
		int rfd, wfd;
		pid_t pid = ssh_to(target, &rfd, &wfd);
		if (pid < 0 || adopt(rfd, wfd, pid) == NULL) { fprintf(stderr, "whisker-client: ssh would not start\n"); return 1; }
	} else {
		int fd = dial();
		if (fd < 0) { fprintf(stderr, "whisker-client: nothing answers at %s:%s\n", host, port); return 1; }
		adopt(fd, fd, 0);
	}

	while (!stop) {
		struct pollfd pf[CONNS + 2];
		int np = 0, which[CONNS + 2];
		for (int i = 0; i < CONNS; i++)
			if (conn[i].live) { pf[np].fd = conn[i].rfd; pf[np].events = POLLIN; which[np++] = i; }
		if (lfd >= 0) { pf[np].fd = lfd; pf[np].events = POLLIN; which[np++] = -1; }
		if (script) { pf[np].fd = 0; pf[np].events = POLLIN; which[np++] = -2; }
		poll(pf, (nfds_t)np, opened ? 10 : 100);

		for (int k = 0; k < np; k++) {
			if (!(pf[k].revents & (POLLIN | POLLHUP | POLLERR))) continue;
			if (which[k] == -1) {                   /* a program dialling this display */
				int fd = accept(lfd, NULL, NULL);
				if (fd >= 0 && adopt(fd, fd, 0) == NULL) close(fd);
				if (fd >= 0) lobby();
			} else if (which[k] >= 0) {             /* a program's lines */
				conn_t *c = &conn[which[k]];
				for (;;) {
					ssize_t r = read(c->rfd, c->b + c->len, (size_t)(BUF - 1 - c->len));
					char *nl, *p;
					if (r == 0 || (r < 0 && errno != EAGAIN && errno != EINTR)) {
						fprintf(stderr, "whisker-client: a program went away\n");
						lose(c);
						break;
					}
					if (r < 0) break;
					c->len += (int)r;
					c->b[c->len] = '\0';
					for (p = c->b; (nl = strchr(p, '\n')) != NULL; p = nl + 1) {
						*nl = '\0';
						if (*p != '\0') message(c, p);
						if (!c->live) break;
					}
					if (!c->live) break;
					c->len -= (int)(p - c->b);
					memmove(c->b, p, (size_t)c->len + 1);
					if (c->len >= BUF - 1) { fprintf(stderr, "whisker-client: a line too long\n"); lose(c); break; }
				}
			}
		}

		if (opened) {                                /* this screen's input */
			wh_event_t e;
			int k;
			while ((k = wh_poll(&e)) != WH_NOTHING) {
				if (k == WH_QUIT) { stop = 1; break; }
				if (listening && e.win == 0) {      /* the display's own window */
					if (k == WH_CLOSE) { stop = 1; break; }
					if (k == WH_EXPOSE) lobby();
					continue;
				}
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
				else if (in[0] != '\0')
					for (int i = 0; i < CONNS; i++)
						if (conn[i].live) {
							char l[4100];
							int n = snprintf(l, sizeof l, "%s\n", in);
							if (write_all(conn[i].wfd, l, n) != 0) lose(&conn[i]);
							break;
						}
				memmove(in, nl + 1, strlen(nl + 1) + 1);
				il = (int)strlen(in);
			}
		}
	}
	for (int i = 0; i < CONNS; i++)                 /* detached: the programs go on */
		if (conn[i].live) {
			close(conn[i].rfd);
			if (conn[i].wfd != conn[i].rfd) close(conn[i].wfd);
			if (conn[i].ssh > 0) { kill(conn[i].ssh, SIGTERM); waitpid(conn[i].ssh, NULL, 0); }
			conn[i].live = 0;
		}
	if (opened) wh_close();
	fprintf(stderr, "whisker-client: detached\n");
	return 0;
}
