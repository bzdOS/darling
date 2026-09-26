int sandbox_check(void) { return 0; }
int SANDBOX_CHECK_NO_REPORT = 0;

// FreeBSD raw-clang dyld build (the "dyld rebuild" task): sigexc_setup() normally comes
// from Darling's libsystem_platform (empty submodule here) and installs the
// task's SIGEXC exception ports on macOS. No-op is sufficient to unblock the
// sNotifyObjCMapped diagnosis runs — guest crashes are still caught by mldr's
// host-side signal handler. Revisit if bootstrap ever hangs in exception setup.
void __attribute__((weak)) sigexc_setup(void) {}

