/* B: one string blob plus integer offsets. No addresses in the data. */
#include "modlib.h"

typedef struct { unsigned char kind, h, id, label; } w_t;   /* id/label = offsets */

static const char TEXT[] = "title\0Settings\0ok\0OK\0cancel\0Cancel\0";
static const w_t TREE[] = {
	{ 0, 20,  0,  6 },
	{ 1, 48, 15, 18 },
	{ 1, 48, 21, 28 },
};

__attribute__((section(".text.entry")))
int rv9_module_entry(const rv9_mod_env_t *env)
{
	if (env == NULL) return 1;
	for (unsigned i = 0; i < sizeof(TREE)/sizeof(TREE[0]); i++) {
		m_say(env, RV9_STDOUT, &TEXT[TREE[i].id]);
		m_say(env, RV9_STDOUT, &TEXT[TREE[i].label]);
	}
	return 0;
}
