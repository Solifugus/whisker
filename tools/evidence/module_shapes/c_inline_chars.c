/* C: text inline in the struct as fixed char arrays. */
#include "modlib.h"

typedef struct { int kind; int h; char id[8]; char label[12]; } w_t;

static const w_t TREE[] = {
	{ 0, 20, "title",  "Settings" },
	{ 1, 48, "ok",     "OK" },
	{ 1, 48, "cancel", "Cancel" },
};

__attribute__((section(".text.entry")))
int rv9_module_entry(const rv9_mod_env_t *env)
{
	if (env == NULL) return 1;
	for (unsigned i = 0; i < sizeof(TREE)/sizeof(TREE[0]); i++) {
		m_say(env, RV9_STDOUT, TREE[i].id);
		m_say(env, RV9_STDOUT, TREE[i].label);
	}
	return 0;
}
