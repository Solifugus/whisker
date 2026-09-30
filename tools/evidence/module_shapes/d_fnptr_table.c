/* D: a dispatch table actually indexed by a runtime value -- the widget
   table the design documents warn about, without the constant folding. */
#include "modlib.h"

typedef int (*emit_fn)(const rv9_mod_env_t *env, int h);

static int emit_label(const rv9_mod_env_t *env, int h)  { return m_say(env, RV9_STDOUT, "<text/>") + h; }
static int emit_button(const rv9_mod_env_t *env, int h) { return m_say(env, RV9_STDOUT, "<rect/>") + h; }
static int emit_box(const rv9_mod_env_t *env, int h)    { return m_say(env, RV9_STDOUT, "<g/>") + h; }

static const emit_fn TAB[] = { emit_label, emit_button, emit_box };

__attribute__((section(".text.entry")))
int rv9_module_entry(const rv9_mod_env_t *env)
{
	if (env == NULL) return 1;
	int kind = (int)((uint32_t)env->time_ms() % 3u);      /* not knowable at compile time */
	return TAB[kind](env, 48) < 0 ? 2 : 0;
}
