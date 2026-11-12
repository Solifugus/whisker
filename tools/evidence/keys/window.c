/* RV-9's window at the P4's size, for drag.c. See the prologue there. */
#include "drv_svgwin.c"

#include <stdio.h>
#include <string.h>

#define PW 1024
#define PH 600

static uint16_t screen[PW * PH];
static rv9_dev_t dev;

rv9_io_err_t rv9_panel_open(bool l, int *w, int *h)
{ (void)l; if (w) *w = PW; if (h) *h = PH; return RV9_IO_OK; }
void rv9_panel_size(int *w, int *h) { if (w) *w = PW; if (h) *h = PH; }
void rv9_panel_backlight(uint32_t p) { (void)p; }
uint32_t rv9_panel_backlight_get(void) { return 100; }
bool rv9_panel_take(const void *o) { (void)o; return false; }
bool rv9_panel_swaps_bytes(void) { return false; }   /* the P4's MIPI-DSI */
void rv9_panel_blit(int x0, int y0, int x1, int y1, const uint16_t *px)
{
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++)
			screen[y * PW + x] = px[(y - y0) * (x1 - x0) + (x - x0)];
}

void win_init(void)
{
	memset(&dev, 0, sizeof dev);
	dev.opt[OPT_BG] = 0x0000;
	svgwin_init(&dev);
	svgwin_open(&dev, 3);
}

void win_write(const void *buf, uint32_t len)
{
	size_t done;
	svgwin_write(&dev, buf, len, &done);
}

int win_setstat(uint32_t code, void *arg)
{
	return svgwin_setstat(&dev, code, arg) == RV9_IO_OK ? 0 : -1;
}

uint16_t win_at(int x, int y) { return screen[y * PW + x]; }

void win_shot(const char *name)
{
	FILE *o = fopen(name, "wb");
	fprintf(o, "P6\n%d %d\n255\n", PW, PH);
	for (int i = 0; i < PW * PH; i++) {
		uint16_t v = screen[i];
		fputc(((v >> 11) & 0x1F) * 255 / 31, o);
		fputc(((v >> 5) & 0x3F) * 255 / 63, o);
		fputc((v & 0x1F) * 255 / 31, o);
	}
	fclose(o);
	printf("wrote %s\n", name);
}
