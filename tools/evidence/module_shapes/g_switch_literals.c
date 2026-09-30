/* G: a switch over an int returning string literals. */
#include "modlib.h"

static const char *tag_of(int kind)
{
	switch (kind) {
	case 0: return "text";
	case 1: return "rect";
	case 2: return "circle";
	case 3: return "line";
	case 4: return "polyline";
	case 5: return "polygon";
	case 6: return "ellipse";
	case 7: return "path";
	default: return "g";
	}
}

__attribute__((section(".text.entry")))
int rv9_module_entry(const rv9_mod_env_t *env)
{
	if (env == NULL) return 1;
	for (int k = 0; k < 9; k++) m_say(env, RV9_STDOUT, tag_of(k));
	return 0;
}
