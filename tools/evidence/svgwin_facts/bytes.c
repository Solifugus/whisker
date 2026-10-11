/*
 * What does a panel of controls cost, in the only currency that binds?
 *
 * SRC_MAX does not scale with the screen: 1024x600 is 11.2 times the pixels
 * of 320x172 and the same 4 KB of description. So the question is how many
 * controls fit, and how much three ways of writing the same picture differ.
 *
 * Built twice, at both panel sizes; see run.sh. It also writes a PPM of the
 * panel, because a picture can be geometrically perfect and still be wrong
 * in a way only an eye catches.
 */
#include "drv_svgwin.c"

#include <stdio.h>
#include <string.h>

#ifndef PW
#define PW 320
#endif
#ifndef PH
#define PH 172
#endif

static uint16_t screen[PW * PH];
static int blits;

rv9_io_err_t rv9_panel_open(bool l, int *w, int *h)
{ (void)l; if (w) *w = PW; if (h) *h = PH; return RV9_IO_OK; }
void rv9_panel_size(int *w, int *h) { if (w) *w = PW; if (h) *h = PH; }
void rv9_panel_backlight(uint32_t p) { (void)p; }
uint32_t rv9_panel_backlight_get(void) { return 100; }
bool rv9_panel_take(const void *o) { (void)o; return false; }
/* RV-9 split the panel in two on 2026-10-11: the C5's SPI ST7789 wants
   its pixels byte-swapped and the P4's MIPI-DSI does not. These harnesses
   have always emulated the C5, and every swap in this file assumes it, so
   say so rather than inherit whichever the linker finds. */
bool rv9_panel_swaps_bytes(void) { return true; }

void rv9_panel_blit(int x0, int y0, int x1, int y1, const uint16_t *px)
{
	blits++;
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++)
			screen[y * PW + x] = px[(y - y0) * (x1 - x0) + (x - x0)];
}

static const char *LBL[20] = {
	"Pump", "Valve A", "Valve B", "Heater", "Fan", "Alarm", "Purge", "Drain",
	"Fill", "Vent", "Reset", "Start", "Stop", "Hold", "Step", "Auto",
	"Manual", "Zero", "Span", "Log"
};

/*
 * style 0  every presentation attribute on every element
 * style 1  the shared ones hoisted onto one <g>
 * style 2  hoisted, and no id attributes at all
 */
static int panel_doc(char *o, int cap, int n, int style)
{
	const int h = 40, gap = 4;
	int p = 0, y = 4;

	p += snprintf(o + p, cap - p, "<svg viewBox=\"0 0 %d %d\">", PW, PH);
	p += snprintf(o + p, cap - p,
	              "<rect width=\"%d\" height=\"%d\" fill=\"#10203a\"/>", PW, PH);
	if (style)
		p += snprintf(o + p, cap - p,
		              "<g font-size=\"16\" text-anchor=\"middle\" fill=\"#fff\">");

	for (int i = 0; i < n; i++) {
		if (style == 0)
			p += snprintf(o + p, cap - p,
				"<rect id=\"w%d\" x=\"8\" y=\"%d\" width=\"%d\" height=\"%d\" rx=\"4\" fill=\"#2a6b8f\"/>"
				"<text x=\"%d\" y=\"%d\" font-size=\"16\" text-anchor=\"middle\" fill=\"#fff\">%s</text>",
				i, y, PW - 16, h, PW / 2, y + h / 2 + 6, LBL[i % 20]);
		else if (style == 1)
			p += snprintf(o + p, cap - p,
				"<rect id=\"w%d\" x=\"8\" y=\"%d\" width=\"%d\" height=\"%d\" rx=\"4\" fill=\"#2a6b8f\"/>"
				"<text x=\"%d\" y=\"%d\">%s</text>",
				i, y, PW - 16, h, PW / 2, y + h / 2 + 6, LBL[i % 20]);
		else
			p += snprintf(o + p, cap - p,
				"<rect x=\"8\" y=\"%d\" width=\"%d\" height=\"%d\" rx=\"4\" fill=\"#2a6b8f\"/>"
				"<text x=\"%d\" y=\"%d\">%s</text>",
				y, PW - 16, h, PW / 2, y + h / 2 + 6, LBL[i % 20]);
		y += h + gap;
	}

	if (style) p += snprintf(o + p, cap - p, "</g>");
	p += snprintf(o + p, cap - p, "</svg>");
	return p;
}

int main(void)
{
	static char buf[16384];
	rv9_dev_t dev;
	char name[64];
	size_t done;
	int n, len;
	FILE *o;

	printf("panel %dx%d, SRC_MAX %d\n\n", PW, PH, SRC_MAX);
	printf("  %-9s %-14s %-14s %s\n", "controls", "per element", "shared on <g>", "<g>, no ids");
	for (int k = 4; k <= 20; k += 4)
		printf("  %-9d %-14d %-14d %d\n", k,
		       panel_doc(buf, sizeof buf, k, 0),
		       panel_doc(buf, sizeof buf, k, 1),
		       panel_doc(buf, sizeof buf, k, 2));

	printf("\n  fitting under SRC_MAX:");
	for (int style = 0; style <= 2; style++) {
		n = 0;
		while (n < 400 && panel_doc(buf, sizeof buf, n + 1, style) <= SRC_MAX) n++;
		printf(" %d", n);
	}
	puts("   (per element, shared, no ids)");

	memset(&dev, 0, sizeof dev);
	dev.opt[OPT_ROTATE] = 1;
	dev.opt[OPT_BG] = 0x0000;
	svgwin_init(&dev);
	svgwin_open(&dev, 3);

	n = PH / 44;
	if (n > 20) n = 20;
	len = panel_doc(buf, sizeof buf, n, 1);
	blits = 0;
	svgwin_write(&dev, buf, (size_t)len, &done);
	printf("\n  drew %d controls, %d bytes, %d bands\n", n, len, blits);

	snprintf(name, sizeof name, "panel_%dx%d.ppm", PW, PH);
	o = fopen(name, "wb");
	fprintf(o, "P6\n%d %d\n255\n", PW, PH);
	for (int i = 0; i < PW * PH; i++) {
		uint16_t v = (uint16_t)((screen[i] >> 8) | (screen[i] << 8));
		fputc(((v >> 11) & 0x1F) * 255 / 31, o);
		fputc(((v >> 5) & 0x3F) * 255 / 63, o);
		fputc((v & 0x1F) * 255 / 31, o);
	}
	fclose(o);
	printf("  wrote %s -- look at it\n", name);
	return 0;
}
