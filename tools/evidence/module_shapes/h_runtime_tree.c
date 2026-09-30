/* H: the tree built at runtime on the stack, labels assigned from literals.
   Pointers exist only in registers and on the stack, never in rodata. */
#include "modlib.h"

typedef struct { int kind; int h; const char *id; const char *label; } w_t;

__attribute__((section(".text.entry")))
int rv9_module_entry(const rv9_mod_env_t *env)
{
	if (env == NULL) return 1;

	w_t tree[3];
	tree[0].kind = 0; tree[0].h = 20; tree[0].id = "title";  tree[0].label = "Settings";
	tree[1].kind = 1; tree[1].h = 48; tree[1].id = "ok";     tree[1].label = "OK";
	tree[2].kind = 1; tree[2].h = 48; tree[2].id = "cancel"; tree[2].label = "Cancel";

	for (int i = 0; i < 3; i++) {
		m_say(env, RV9_STDOUT, tree[i].id);
		m_say(env, RV9_STDOUT, tree[i].label);
	}
	return 0;
}
