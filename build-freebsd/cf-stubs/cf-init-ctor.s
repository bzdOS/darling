/* ld64.lld does not implement "-init ___CFInitialize" (no LC_ROUTINES is written), so the initializer that real
 * macOS CoreFoundation runs from LC_ROUTINES would be lost. Register it as a __mod_init_func entry instead.
 * This object must be the FIRST object on the link line: entries run in link order and LC_ROUTINES runs before every
 * other initializer of the image (NSBlockInitialize, NSExceptionInitializer, ...). __CFInitialize guards itself with
 * __CFInitialized/__CFInitializing, so a repeat call is a no-op. */
.section __DATA,__mod_init_func,mod_init_funcs
.p2align 3
.quad ___CFInitialize
