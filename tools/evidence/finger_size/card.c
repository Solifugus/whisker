/*
 * Is a 64-row button a finger?
 *
 * The default button height came from arithmetic -- 9 mm at the P4's ~170 dpi
 * -- and arithmetic cannot say whether a target is comfortable. This draws
 * three candidate heights at matched PHYSICAL size on a screen you do have,
 * so a fingertip can answer it before the panel arrives.
 *
 * The screen it was written for is 1920x1080 over 477 mm, which is 4.025
 * px/mm against the P4's 6.675, so a P4 pixel is 0.603 of one here and a
 * 64-row button is 39 pixels. Change MON_* for another screen; the numbers in
 * the labels are computed, not typed.
 *
 * It needs no touch at all. The question is millimetres and a finger, not
 * events -- which is how it got answered on a monitor whose touch frame was
 * enumerating and reporting nothing (2026-10-01).
 *
 *     ./run.sh          writes the card
 *     gwenview -f card.png      full screen, 1:1, on the screen you measured
 */
#include "drv_svgwin.c"
#include "whisker.h"
#include <stdio.h>
#include <string.h>
/* The screen this will be LOOKED at on. */
#define MON_W     1920
#define MON_H     1080
#define MON_MM_W  477          /* from the EDID: kscreen-doctor, or xrandr */

/* The screen being designed FOR. */
#define TGT_W     1024
#define TGT_H     600
#define TGT_INCH  7

#define PW MON_W
#define PH MON_H
static uint16_t screen[PW*PH];
rv9_io_err_t rv9_panel_open(bool l,int *w,int *h){(void)l;if(w)*w=PW;if(h)*h=PH;return RV9_IO_OK;}
void rv9_panel_size(int *w,int *h){if(w)*w=PW;if(h)*h=PH;}
void rv9_panel_backlight(uint32_t p){(void)p;}
uint32_t rv9_panel_backlight_get(void){return 100;}
bool rv9_panel_take(const void *o){(void)o;return false;}
/* RV-9 split the panel in two on 2026-10-11: the C5's SPI ST7789 wants
   its pixels byte-swapped and the P4's MIPI-DSI does not. These harnesses
   have always emulated the C5, and every swap in this file assumes it, so
   say so rather than inherit whichever the linker finds. */
bool rv9_panel_swaps_bytes(void) { return true; }
void rv9_panel_blit(int x0,int y0,int x1,int y1,const uint16_t *px){
	for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++)screen[y*PW+x]=px[(y-y0)*(x1-x0)+(x-x0)];
}
static void to_window(wsk_sink_t *s,const char *b,int n){
	size_t done; svgwin_write((rv9_dev_t*)s->ctx,b,(size_t)n,&done);
}
/* 48, 64 and 80 P4 rows, at this monitor's scale: 29, 39 and 48 px. */
/* Target rows to try. The middle one is the default in design.md §5. */
static const int ROWS[3] = { 48, 64, 80 };

static double mon_px_per_mm(void) { return (double)MON_W / (double)MON_MM_W; }
static double tgt_px_per_mm(void)
{
	double diag_px = 0.0;
	diag_px = (double)TGT_W * TGT_W + (double)TGT_H * TGT_H;
	/* integer sqrt is enough here and this is host code, so use the float */
	return __builtin_sqrt(diag_px) / ((double)TGT_INCH * 25.4);
}
static int at_monitor_scale(int target_rows)
{
	double mm = target_rows / tgt_px_per_mm();
	return (int)(mm * mon_px_per_mm() + 0.5);
}
int main(void){
	rv9_dev_t dev; memset(&dev,0,sizeof dev);
	dev.opt[OPT_ROTATE]=1; dev.opt[OPT_BG]=0x0000;
	svgwin_init(&dev); svgwin_open(&dev,3);
	wsk_widget_t live[4]; wsk_tree_t t; wsk_sink_t s;

	memset(live, 0, sizeof live);
	live[0].kind = WSK_LABEL;
	live[0].size = 24;
	wsk__scpy(live[0].id, "t", WSK_ID_MAX);
	wsk__scpy(live[0].text, "put a finger on each", WSK_TEXT_MAX);
	for (int i = 0; i < 3; i++) {
		char lab[WSK_TEXT_MAX];
		double mm = ROWS[i] / tgt_px_per_mm();
		snprintf(lab, sizeof lab, "%d rows  %.1f mm", ROWS[i], mm);
		live[i + 1].kind = WSK_BUTTON;
		live[i + 1].size = (short)at_monitor_scale(ROWS[i]);
		live[i + 1].id[0] = (char)('a' + i);
		wsk__scpy(live[i + 1].text, lab, WSK_TEXT_MAX);
		printf("  %3d target rows = %5.2f mm = %3d px here\n",
		       ROWS[i], mm, live[i + 1].size);
	}
	live[2].mark = WSK_MARK_CORNERS;     /* the pressed look, at true size */
	live[2].fill = 0x8fd0ffu;
	live[2].ink  = 0x112233u;
	wsk_init(&t, WSK_COL, PW, PH);
	t.pad = 10;
	t.gap = at_monitor_scale(4) < 3 ? 3 : at_monitor_scale(4);
	t.font = at_monitor_scale(16);       /* the target's 16 px, in millimetres */
	t.bg = 0x10203au;
	t.cross_max = (short)at_monitor_scale(480);
	wsk_area(&t, 0, 300, PW, 400);
	t.item = live; t.n = 4;
	s.put = to_window; s.ctx = &dev; s.len = 0;
	printf("card: %d bytes\n", wsk_render(&t,&s,4096));
	FILE *o=fopen("card.ppm","wb");
	fprintf(o,"P6\n%d %d\n255\n",PW,PH);
	for(int i=0;i<PW*PH;i++){uint16_t v=(uint16_t)((screen[i]>>8)|(screen[i]<<8));
		fputc(((v>>11)&0x1F)*255/31,o);fputc(((v>>5)&0x3F)*255/63,o);fputc((v&0x1F)*255/31,o);}
	fclose(o); puts("wrote card.ppm -- view it full screen, 1:1, and use a finger");
	return 0;
}
