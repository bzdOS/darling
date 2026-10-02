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
    ' * behaviour change — see build/wl-debug-copy/README notes.\n'
    ' * roundtrip-wakeup lane: the marker path is LOCALE-FREE — the fault\n'
    ' * object measured in the survive-window slice was locale data read\n'
    ' * inside libc vfprintf via localeconv_l, so the markers format with\n'
    ' * a hand-rolled converter subset (%lu %ld %d %p %s %%) and emit with\n'
    ' * write(2). No printf/snprintf/fprintf on the marker path. */\n'
    '#include <stdarg.h>\n'
    '#include <string.h>\n'
    '#include <unistd.h>\n'
    '#include <sys/ioctl.h>\n'
    'static unsigned long wlbody_seq;\n'
    'static void wlb_out(const char *s, int n) { if (n > 0) (void)!write(2, s, (size_t)n); }\n'
    'static char *wlb_dec(char *end, long v) {\n'
    '\tchar t[24]; int n = 0, i; unsigned long u; int neg = 0;\n'
    '\tif (v < 0) { neg = 1; u = (unsigned long)(-(v + 1)) + 1; } else { u = (unsigned long)v; }\n'
    '\tdo { t[n++] = (char)(\'0\' + (int)(u % 10)); u /= 10; } while (u && n < 24);\n'
    '\tfor (i = 0; i < n; i++) *--end = t[i];\n'
    '\tif (neg) *--end = \'-\';\n'
    '\treturn end;\n'
    '}\n'
    'static char *wlb_hex(char *end, unsigned long u) {\n'
    '\tstatic const char d[] = "0123456789abcdef";\n'
    '\tchar t[20]; int n = 0, i;\n'
    '\tdo { t[n++] = d[u & 0xf]; u >>= 4; } while (u && n < 20);\n'
    '\tfor (i = 0; i < n; i++) *--end = t[i];\n'
    '\treturn end;\n'
    '}\n'
    '/* mini-formatter: the marker subset only — %lu %ld %d %p %s %% */\n'
    'void wlb_log(const char *fmt, ...)\n'
    '{\n'
    '\tchar out[256]; char tail[64]; int o = 0;\n'
    '\tva_list ap; const char *f = fmt;\n'
    '\tva_start(ap, fmt);\n'
    '\twhile (*f && o < 200) {\n'
    '\t\tif (*f != \'%\') { out[o++] = *f++; continue; }\n'
    '\t\tf++;\n'
    '\t\tif (*f == \'%\') { out[o++] = \'%\'; f++; continue; }\n'
    '\t\tif (*f == \'l\' && (f[1] == \'u\' || f[1] == \'d\')) {\n'
    '\t\t\tunsigned long v = (unsigned long)va_arg(ap, unsigned long);\n'
    '\t\t\tchar *q = wlb_dec(tail + sizeof(tail),\n'
    '\t\t\t    (f[1] == \'d\') ? (long)v : (long)v);\n'
    '\t\t\tint l = (int)((tail + sizeof(tail)) - q);\n'
    '\t\t\tmemcpy(out + o, q, (size_t)l); o += l; f += 2; continue;\n'
    '\t\t}\n'
    '\t\tif (*f == \'u\') {\n'
    '\t\t\tunsigned int v = va_arg(ap, unsigned int);\n'
    '\t\t\tchar *q = wlb_dec(tail + sizeof(tail), (long)v);\n'
    '\t\t\tint l = (int)((tail + sizeof(tail)) - q);\n'
    '\t\t\tmemcpy(out + o, q, (size_t)l); o += l; f++; continue;\n'
    '\t\t}\n'
    '\t\tif (*f == \'d\') {\n'
    '\t\t\tchar *q = wlb_dec(tail + sizeof(tail), (long)va_arg(ap, int));\n'
    '\t\t\tint l = (int)((tail + sizeof(tail)) - q);\n'
    '\t\t\tmemcpy(out + o, q, (size_t)l); o += l; f++; continue;\n'
    '\t\t}\n'
    '\t\tif (*f == \'p\') {\n'
    '\t\t\tunsigned long v = (unsigned long)(uintptr_t)va_arg(ap, void *);\n'
    '\t\t\tchar *q;\n'
    '\t\t\tout[o++] = \'0\'; out[o++] = \'x\';\n'
    '\t\t\tq = wlb_hex(tail + sizeof(tail), v);\n'
    '\t\t\t{ int l = (int)((tail + sizeof(tail)) - q);\n'
    '\t\t\t  memcpy(out + o, q, (size_t)l); o += l; }\n'
    '\t\t\tf++; continue;\n'
    '\t\t}\n'
    '\t\tif (*f == \'s\') {\n'
    '\t\t\tconst char *s = va_arg(ap, const char *);\n'
    '\t\t\tint l = s ? (int)strlen(s) : 0;\n'
    '\t\t\tif (l > 40) l = 40;\n'
    '\t\t\tif (s) { memcpy(out + o, s, (size_t)l); o += l; }\n'
    '\t\t\tf++; continue;\n'
    '\t\t}\n'
    '\t\tout[o++] = *f++;\n'
    '\t}\n'
    '\tva_end(ap);\n'
    '\tout[o++] = \'\\n\';\n'
    '\twlb_out(out, o);\n'
    '}\n'
    '#define WLB(...) do { wlb_log("[wlbody] #%lu ", (unsigned long)++wlbody_seq); \\\n'
    '\twlb_log(__VA_ARGS__); } while (0)\n'
    '/* wrap: entry markers that do NOT consume the #N sequence, so existing\n'
    ' * number-to-site mapping stays comparable across instrumentation rounds. */\n'
    '#define WLBW(...) do { wlb_log("[wlbody] wrap "); \\\n'
    '\twlb_log(__VA_ARGS__); } while (0)\n'
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
         "\t\t    pfd[0].events, remaining_timeout ? \"set\" : \"NULL\");\n"
         "\t\t{\n"
         "\t\t\tint _qn = -1;\n"
         "\t\t\tif (ioctl(pfd[0].fd, FIONREAD, &_qn) < 0) _qn = -errno;\n"
         "\t\t\tWLB(\"    fd-state fd=%d fionread=%d\", pfd[0].fd, _qn);\n"
         "\t\t}\n"
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

# 13) sync-callback fire — the smoking gun: did the lane's callback run
sub_once("static void\n"
         "sync_callback(void *data, struct wl_callback *callback, uint32_t serial)\n"
         "{\n"
         "\tint *done = data;\n"
         "\n"
         "\t*done = 1;\n",
         "static void\n"
         "sync_callback(void *data, struct wl_callback *callback, uint32_t serial)\n"
         "{\n"
         "\tint *done = data;\n"
         "\n"
         "\twlb_log(\"sync-callback fired cb=%p q=%p ser=%u\",\n"
         "\t    (void *)callback,\n"
         "\t    (void *)(((struct wl_proxy *)callback)->queue), serial);\n"
         "\t*done = 1;\n",
         "sync-callback fire")

# 14) queue_event landing — which queue each decoded event goes to
sub_once("\tclosure->proxy = proxy;\n"
         "\tincrease_closure_args_refcount(closure);\n"
         "\n"
         "\tif (proxy == &display->proxy)\n"
         "\t\tqueue = &display->display_queue;\n"
         "\telse\n"
         "\t\tqueue = proxy->queue;\n"
         "\n"
         "\tif (!queue)\n"
         "\t\twl_abort(\"Tried to add event to destroyed queue\\n\");\n"
         "\n"
         "\twl_list_insert(queue->event_list.prev, &closure->link);\n",
         "\tclosure->proxy = proxy;\n"
         "\tincrease_closure_args_refcount(closure);\n"
         "\n"
         "\tif (proxy == &display->proxy)\n"
         "\t\tqueue = &display->display_queue;\n"
         "\telse\n"
         "\t\tqueue = proxy->queue;\n"
         "\n"
         "\tif (!queue)\n"
         "\t\twl_abort(\"Tried to add event to destroyed queue\\n\");\n"
         "\n"
         "\twlb_log(\"queue_event: id=%u op=%u proxy=%p -> queue=%p\",\n"
         "\t    id, opcode, (void *)proxy, (void *)queue);\n"
         "\twl_list_insert(queue->event_list.prev, &closure->link);\n",
         "queue_event landing")

open(PATH, "w").write(src)
print("written:", PATH)

# ── connection.c phase (roundtrip-wakeup lane): when do the request
# bytes actually leave for the compositor — the client-side "post" of
# the wakeup contract. wlb_log is global (defined above); connection.c
# only needs the declaration.
CPATH = "src/connection.c"
csrc = open(CPATH).read()

def sub_once_c(old, new, what):
    global csrc
    n = csrc.count(old)
    if n != 1:
        print(f"FATAL: anchor for {what}: count={n}")
        sys.exit(1)
    csrc = csrc.replace(old, new, 1)
    print(f"ok(c): {what}")

# c0) declaration after the first include
_clines = csrc.split("\n")
for _i, _ln in enumerate(_clines):
    if _ln.startswith("#include"):
        _clines.insert(_i + 1, "void wlb_log(const char *fmt, ...);")
        _clines.insert(_i + 2, "#include <sys/thr.h>")
        _clines.insert(_i + 3, "#include <pthread.h>")
        break
else:
    print("FATAL: no #include line in connection.c")
    sys.exit(1)
csrc = "\n".join(_clines)
print("ok(c): wlb_log declaration + thr.h")

# c1) the flush site — bytes to the compositor
sub_once_c("\t\t} while (len == -1 && errno == EINTR);\n"
           "\n"
           "\t\tif (len == -1)\n"
           "\t\t\treturn -1;\n",
           "\t\t} while (len == -1 && errno == EINTR);\n"
           "\n"
           "\t\twlb_log(\"flush: sendmsg fd=%d -> %ld\",\n"
           "\t\t    connection->fd, (long)len);\n"
           "\n"
           "\t\tif (len == -1)\n"
           "\t\t\treturn -1;\n",
           "flush sendmsg")

# c2) the read site — every read from the display fd, with tid: whose
# dispatch consumed the bytes (the done-24 question of reply-delivery)
sub_once_c("\t\tdo {\n"
           "\t\t\tlen = wl_os_recvmsg_cloexec(connection->fd, &msg, MSG_DONTWAIT);\n"
           "\t\t} while (len < 0 && errno == EINTR);\n",
           "\t\tdo {\n"
           "\t\t\tlen = wl_os_recvmsg_cloexec(connection->fd, &msg, MSG_DONTWAIT);\n"
           "\t\t} while (len < 0 && errno == EINTR);\n"
           "\t\twlb_log(\"read: fd=%d -> %ld\", connection->fd, (long)len);\n"
           "\t\t{\n"
           "\t\t\tlong _tid = 0;\n"
           "\t\t\tthr_self(&_tid);\n"
           "\t\t\twlb_log(\"read-tid fd=%d lwp=%lu pt=%lu\", connection->fd,\n"
           "\t\t\t    (unsigned long)_tid, (unsigned long)pthread_self());\n"
           "\t\t}\n",
           "read site")

open(CPATH, "w").write(csrc)
print("written:", CPATH)
