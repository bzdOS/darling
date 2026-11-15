/* C++ half of unwind-probe.m: a throw that crosses a frame with a cleanup. */
struct Guard { int *p; ~Guard() { *p = 1; } };
static void thrower(int *flag) { Guard g{flag}; throw 42; }
extern "C" int uw_cxx_throw_catch(void) {
    int flag = 0;
    try { thrower(&flag); } catch (int v) { return flag ? v : 2; }
    return 0;
}
