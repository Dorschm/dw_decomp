#include <stdio.h>
#include <string.h>

#include <emscripten/console.h>

#include <web/host/log.h>

#define MAX_SEEN 512

static const char *seen[MAX_SEEN];
static int seen_count;

/*
 * Print "unimplemented: NAME" once per unique name and return 0, so stubs
 * can `return web_unimplemented("Func")`.
 */
long web_unimplemented(const char *name)
{
	int i;
	for (i = 0; i < seen_count; i++) {
		if (strcmp(seen[i], name) == 0) {
			return 0;
		}
	}
	if (seen_count < MAX_SEEN) {
		seen[seen_count++] = name;
	}
	emscripten_console_logf("unimplemented: %s", name);
	return 0;
}
