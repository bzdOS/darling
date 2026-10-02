#!/usr/bin/env python3
"""Instrument wayland-client.c (1.25.0) with stderr body markers.

Every marker is a fprintf to stderr — no behaviour change: the roundtrip's
own body becomes speakable, which is what run №9-4's park needs. Anchors are
asserted unique: a missing or duplicated anchor aborts with the count, so a
drifted source cannot be silently half-instrumented.
"""
import re, sys

PATH = "src/wayland-client.c"
src = open(PATH).read()

def sub_once(old, new, what):
    global src
    n = src.count(old)
    if n != 1:
        print(f"FATAL: anchor for {what}: count={n}")
        sys.exit(1)
    src = src.replace(old, new, 1)
    print(f"ok: {what}")

MACRO = (
    '\n/* wlbody: diagnostic stderr markers for the roundtrip park; no\n'
    ' * behaviour change — see build/wl-debug-copy/README notes. */\n'
    'static unsigned long wlbody_seq;\n'
    '#define WLB(...) do { fprintf(stderr, "[wlbody] #%lu ", ++wlbody_seq); \\\n'
    '\tfprintf(stderr, __VA_ARGS__); fprintf(stderr, "\\n"); } while (0)\n'
    '/* wrap: entry markers that do NOT consume the #N sequence, so existing\n'
    ' * number-to-site mapping stays comparable across instrumentation rounds. */\n'
    '#define WLBW(...) do { fprintf(stderr, "[wlbody] wrap "); \\\n'
    '\tfprintf(stderr, __VA_ARGS__); fprintf(stderr, "\\n"); } while (0)\n'
)

# 0) macro + counter right after the FIRST include line
lines = src.split("\n")
for i, ln in enumerate(lines):
    if ln.startswith("#include"):
        lines.insert(i + 1, MACRO)
        break
else:
    print("FATAL: no #include line found")
    sys.exit(1)
src = "\n".join(lines)
print("ok: macro after first include line")

# 1) roundtrip_queue: entry
sub_once("\tint done, ret = 0;\n",
         "\tint done, ret = 0;\n"
         "\tWLB(\"roundtrip_queue ENTER tid=%lu display=%p queue=%p\",\n"
         "\t    (unsigned long)pthread_self(), (void *)display, (void *)queue);\n",
         "roundtrip entry")

# 1b) wl_display_roundtrip: the one-line wrapper's entry — the first
# reachable point of the native library after the guest-side bind and the
# shim's forwarding. Its absence on a parked lane places the park ABOVE
# libwayland (guest bind / shim export / thread bookkeeping).
sub_once("wl_display_roundtrip(struct wl_display *display)\n"
         "{\n"
         "\treturn wl_display_roundtrip_queue(display, &display->default_queue);\n",
         "wl_display_roundtrip(struct wl_display *display)\n"
         "{\n"
         "\tWLBW(\"ENTER tid=%lu display=%p\",\n"
         "\t    (unsigned long)pthread_self(), (void *)display);\n"
         "\treturn wl_display_roundtrip_queue(display, &display->default_queue);\n",
         "roundtrip wrapper entry")

# 2) set_queue
sub_once("\twl_proxy_set_queue((struct wl_proxy *) display_wrapper, queue);\n",
         "\tWLB(\"  set_queue wrapper=%p queue=%p\", (void *)display_wrapper,\n"
         "\t    (void *)queue);\n"
         "\twl_proxy_set_queue((struct wl_proxy *) display_wrapper, queue);\n",
         "set_queue marker")

# 3) sync (the marshal step)
sub_once("\tcallback = wl_display_sync(display_wrapper);\n",
         "\tcallback = wl_display_sync(display_wrapper);\n"
         "\tWLB(\"  sync(display_wrapper=%p) returned callback=%p\",\n"
         "\t    (void *)display_wrapper, (void *)callback);\n",
         "sync marker")

# 4) dispatch loop — log every iteration
sub_once("\twl_callback_add_listener(callback, &sync_listener, &done);\n"
         "\twhile (!done && ret >= 0)\n"
         "\t\tret = wl_display_dispatch_queue(display, queue);\n",
         "\twl_callback_add_listener(callback, &sync_listener, &done);\n"
         "\t{\n"
         "\t\tint wlbody_i = 0;\n"
         "\t\twhile (!done && ret >= 0) {\n"
         "\t\t\tWLB(\"  dispatch-loop iter=%d ENTER queue=%p\",\n"
         "\t\t\t    wlbody_i, (void *)queue);\n"
         "\t\t\tret = wl_display_dispatch_queue(display, queue);\n"
         "\t\t\tWLB(\"  dispatch-loop iter=%d LEAVE ret=%d done=%d\",\n"
         "\t\t\t    wlbody_i, ret, done);\n"
         "\t\t\twlbody_i++;\n"
         "\t\t}\n"
         "\t}\n",
         "dispatch loop")

# 5) read_events entry + branch
sub_once("\tdisplay->reader_count--;\n\tif (display->reader_count == 0) {\n",
         "\tWLB(\"  read_events ENTER tid=%lu reader_count=%d -> %d\",\n"
         "\t    (unsigned long)pthread_self(), display->reader_count,\n"
         "\t    display->reader_count - 1);\n"
         "\tdisplay->reader_count--;\n\tif (display->reader_count == 0) {\n"
         "\t\tWLB(\"    branch: this thread is the reader (blocking socket read)\");\n",
         "read_events entry/branch")

# 6) the blocking socket read itself
sub_once("\t\ttotal = wl_connection_read(display->connection);\n",
         "\t\tWLB(\"    wl_connection_read ENTER (native blocking read)\");\n"
         "\t\ttotal = wl_connection_read(display->connection);\n"
         "\t\tWLB(\"    wl_connection_read LEAVE total=%d errno=%d\",\n"
         "\t\t    total, errno);\n",
         "connection read")

# 7) the condvar branch — log entry, do NOT touch the wait itself
sub_once("\t\twhile (display->read_serial == serial)\n",
         "\t\tWLB(\"    branch: other readers present -> reader_cond WAIT"
         " (serial=%u, tid=%lu)\", serial, (unsigned long)pthread_self());\n"
         "\t\twhile (display->read_serial == serial)\n",
         "cond-wait enter")

# 8) public read_events wrapper
sub_once("\tret = read_events(display);\n",
         "\tWLB(\"  wl_display_read_events ENTER tid=%lu\",\n"
         "\t    (unsigned long)pthread_self());\n"
         "\tret = read_events(display);\n"
         "\tWLB(\"  wl_display_read_events LEAVE ret=%d errno=%d\", ret, errno);\n",
         "read_events wrapper")

# 9) prepare_read_queue: the reader_count++ site
sub_once("\t\tdisplay->reader_count++;\n\t\tret = 0;\n",
         "\t\tdisplay->reader_count++;\n"
         "\t\tWLB(\"  prepare_read_queue queue=%p -> reader_count=%d\",\n"
         "\t\t    (void *)queue, display->reader_count);\n"
         "\t\tret = 0;\n",
         "prepare_read")

# 10) native ppoll site
sub_once("\t\tret = ppoll(pfd, 1, remaining_timeout, NULL);\n",
         "\t\tWLB(\"    ppoll ENTER fd=%d events=%d timeout=%s\", pfd[0].fd,\n"
         "\t\t    pfd[0].events, remaining_timeout ? \"set\" : \"NULL(infinite)\");\n"
         "\t\tret = ppoll(pfd, 1, remaining_timeout, NULL);\n"
         "\t\tWLB(\"    ppoll LEAVE ret=%d errno=%d\", ret, errno);\n",
         "ppoll site")

# 11) dispatch_queue wrapper (the loop's callee)
sub_once("\tret = wl_display_dispatch_queue_timeout(display, queue, NULL);\n"
         "\tassert(ret == -1 || ret > 0);\n",
         "\tWLB(\"dispatch_queue ENTER tid=%lu queue=%p\",\n"
         "\t    (unsigned long)pthread_self(), (void *)queue);\n"
         "\tret = wl_display_dispatch_queue_timeout(display, queue, NULL);\n"
         "\tWLB(\"dispatch_queue LEAVE ret=%d\", ret);\n"
         "\tassert(ret == -1 || ret > 0);\n",
         "dispatch_queue wrapper")

# 12) read_events call inside the timeout loop
sub_once("\t\tret = wl_display_read_events(display);\n",
         "\t\tWLB(\"    dispatch: wl_display_read_events call\");\n"
         "\t\tret = wl_display_read_events(display);\n"
         "\t\tWLB(\"    dispatch: wl_display_read_events rc=%d errno=%d\",\n"
         "\t\t    ret, errno);\n",
         "timeout read_events call")

open(PATH, "w").write(src)
print("written:", PATH)
