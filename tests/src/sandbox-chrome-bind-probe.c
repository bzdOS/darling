/* sandbox-chrome-bind-probe: the sandbox profile API is imported two-level and bound at link time to
 * /usr/lib/libsandbox.1.dylib, where the Chrome framework and helper bind it (no dlsym). Prints SBXBIND <name> ok|FAIL. */
#include <stdio.h>
extern void *sandbox_create_params(void);
extern int sandbox_set_param(void *, const char *, const char *);
extern void *sandbox_compile_string(const char *, void *, char **);
extern int sandbox_apply(void *);
extern int sandbox_init_with_parameters(const char *, unsigned long long, const char *const[], char **);
extern void sandbox_free_error(char *);
extern void sandbox_free_profile(void *);
extern void sandbox_free_params(void *);
static int bad;
static void res(const char *n, int ok) { printf("SBXBIND %s %s\n", n, ok ? "ok" : "FAIL"); fflush(stdout); if (!ok) bad++; }
int main(void)
{
	char *err = (char *)1;
	void *p = sandbox_create_params();
	res("sandbox_create_params", p != 0);
	res("sandbox_set_param", sandbox_set_param(p, "k", "v") == 0);
	void *pr = sandbox_compile_string("(version 1)", p, &err);
	res("sandbox_compile_string", pr != 0);
	res("sandbox_apply", sandbox_apply(pr) == 0);
	err = (char *)1;
	res("sandbox_init_with_parameters", sandbox_init_with_parameters("(version 1)", 0, 0, &err) == 0 && err == 0);
	sandbox_free_error(err); sandbox_free_profile(pr); sandbox_free_params(p);
	printf("SBXBIND DONE fail=%d\n", bad); fflush(stdout);
	return bad != 0;
}
