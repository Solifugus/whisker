/* E: the same dispatch as a switch over the kind, calling directly. */
#include "modlib.h"

static int emit_label(const rv9_mod_env_t *env, int h)  { return m_say(env, RV9_STDOUT, "<text/>") + h; }
static int emit_button(const rv9_mod_env_t *env, int h) { return m_say(env, RV9_STDOUT, "<rect/>") + h; }
static int emit_box(const rv9_mod_env_t *env, int h)    { return m_say(env, RV9_STDOUT, "<g/>") + h; }

__attribute__((section(".text.entry")))
int rv9_module_entry(const rv9_mod_env_t *env)
{
	if (env == NULL) return 1;
	int kind = (int)((uint32_t)env->time_ms() % 3u);
	int r;
	switch (kind) {
	case 0:  r = emit_label(env, 48);  break;
	case 1:  r = emit_button(env, 48); break;
	default: r = emit_box(env, 48);    break;
	}
	return r < 0 ? 2 : 0;
}
