/* F: dispatch as an if/else chain, literals returned from code. */
#include "modlib.h"

static const char *tag_of(int kind)
{
	if (kind == 0) return "text";
	if (kind == 1) return "rect";
	return "g";
}

__attribute__((section(".text.entry")))
int rv9_module_entry(const rv9_mod_env_t *env)
{
	if (env == NULL) return 1;
	for (int k = 0; k < 3; k++) m_say(env, RV9_STDOUT, tag_of(k));
	return 0;
}
