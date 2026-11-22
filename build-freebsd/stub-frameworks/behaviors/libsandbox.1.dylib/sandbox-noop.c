/* libsandbox.1.dylib behaviour: the sandbox profile API the Chrome framework (lib ordinal 64) and its helper
 * processes bind from THIS image. There is no sandbox on this system, so every call succeeds as a no-op:
 * profile compilation and parameter blocks return non-NULL dummy objects, apply and init return 0 (success,
 * no error string), free calls accept anything. Without sandbox_apply the helper aborted at bind time. */
static char dummy_params[16], dummy_profile[16];

void *sandbox_create_params(void) { return dummy_params; }
int sandbox_set_param(void *p, const char *k, const char *v) { (void)p; (void)k; (void)v; return 0; }
void sandbox_free_params(void *p) { (void)p; }
void *sandbox_compile_string(const char *s, void *params, char **err) { (void)s; (void)params; if (err) *err = 0; return dummy_profile; }
void *sandbox_compile_file(const char *s, void *params, char **err) { (void)s; (void)params; if (err) *err = 0; return dummy_profile; }
void *sandbox_compile_entitlements(const char *s, void *params, char **err) { (void)s; (void)params; if (err) *err = 0; return dummy_profile; }
void sandbox_free_profile(void *p) { (void)p; }
int sandbox_apply(void *profile) { (void)profile; return 0; }
int sandbox_init(const char *profile, unsigned long long flags, char **err) { (void)profile; (void)flags; if (err) *err = 0; return 0; }
int sandbox_init_with_parameters(const char *profile, unsigned long long flags, const char *const params[], char **err) { (void)profile; (void)flags; (void)params; if (err) *err = 0; return 0; }
int sandbox_init_with_extensions(const char *profile, unsigned long long flags, const char *const ext[], char **err) { (void)profile; (void)flags; (void)ext; if (err) *err = 0; return 0; }
void sandbox_free_error(char *e) { (void)e; }
