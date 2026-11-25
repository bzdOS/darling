# MLDR-BIN-DIFF — pin against the vintage build, symbol by symbol

## Verdict (one line)

The checkin send path is **not** in the delta — `dserver_rpc_explicit_checkin`
differs by 3 bytes (1017 vs 1014) and `dserver_rpc_checkin` is byte-identical
(37/37) — while the difference that separates the two binaries sits in `load64`,
where the pin synthesises `errno = 3` three times and the vintage build does it
zero times.

## 1. Method

Two binaries already on disk, nothing built, nothing run:

```
pin     md5 bb8571303d6a4598b08a76fff1ca542d  530784 B  /tmp/ck61-test/dserver/mldr-real/mldr
vintage md5 979075c6351ece4c71126926798a7f06  510640 B  /tmp/mldr-bisect/mldr.vintage8fa5
nm -S --defined-only <bin>     (GNU nm 2.44), symbols joined by name
```

```
pin symbols=224  vintage symbols=225
common=224  only_in_pin=0  only_in_vintage=1
sum(function sizes) pin=88384  vintage=87962  delta=-422
```

The single symbol only the vintage has is data, not code:
`__mldr_create_rpc_socket.__mldr_rpc_sock_path` (108 B).

## 2. Largest deltas (vintage − pin, bytes)

```
  +114  pin=  205 vintage=  319 T  __mldr_create_rpc_socket
  +101  pin=  825 vintage=  926 t  load
   -76  pin= 2392 vintage= 2316 t  load64
   -66  pin=  677 vintage=  611 t  setup_stack32
   -62  pin=  679 vintage=  617 t  setup_stack64
   -53  pin= 1035 vintage=  982 T  main
   -48  pin=  492 vintage=  444 T  __darling_thread_create
   +43  pin=   26 vintage=   69 T  __mldr_close_rpc_socket
   -27  pin=  390 vintage=  363 t  socket_bitmap_get
```

Everything else moves by 3–19 bytes in either direction across ~50
`dserver_rpc_explicit_*` wrappers — the same noise floor in both binaries
(shared inlined send/reply code compiled at different addresses) — and about
half the wrappers are smaller in the vintage.

Largest symbols are the same in both: the four 4096-byte buffers
(`load64.path_buffer`, `process_special_env.root_path`,
`setup_space.vchroot_buffer`, `setup_stack32/64.executable_buf`).

## 3. The checkin path, asked directly

```
dserver_rpc_checkin             pin=37   vintage=37    delta=0
dserver_rpc_explicit_checkin    pin=1017 vintage=1014  delta=-3
dserver_rpc_explicit_checkout   pin=999  vintage=996   delta=-3
dserver_rpc_explicit_mach_msg_overwrite  pin=864 vintage=861
dserver_rpc_explicit_s2c_perform pin=901 vintage=898
```

**No**: neither `dserver_rpc_explicit_checkin` nor the send wrapper it uses is
in the delta — the 3 bytes it loses are the same 3 bytes ~50 other wrappers
lose, and the hook `dserver_rpc_hooks_send_message` is a `static inline` in the
header, so it has no symbol to differ by; its body is part of that noise floor.

**Largest delta overall: `__mldr_create_rpc_socket`, +114 bytes (205 → 319).**
The difference is concrete, from the disassembly of both:

```
pin      socket → socket_bitmap_get → dup2 → close → fcntl ×2 → bind(short name) → bitmap_put ×2 → close ×2
vintage  socket → socket_bitmap_get → dup2 → close → fcntl ×2 → getpid → snprintf → strlcpy → unlink → bind → bitmap_put ×2 → close ×2
```

i.e. the vintage names the socket `/tmp/darling-mldr-<pid>-<fd>`, unlinks a
stale one and binds that; the pin binds a short name computed elsewhere and has
no `__mldr_rpc_sock_path` buffer at all. This is bind-side, before any checkin
message exists.

## 4. The one difference that changes behaviour

```
movl $0x3, (%rax)      # errno = ESRCH, the WSL1 MAP_FIXED_NOREPLACE hack
  in pin:     3 occurrences, all three inside <load64>
  in vintage: 0 occurrences
```

which matches the sizes of `load64` (2392 pin vs 2316 vintage, −76) and the
behaviour measured earlier: the pin's guest dies at the stack mapping and never
checks in (0 checkin lines in its runs), every other binary reaches checkin and
reports `-61`.

## 5. Hypothesis (one line)

The pin's `OK` is not a working checkin but a guest that dies earlier — its
`load64` still turns a non-fixed `mmap` success into `errno = 3` and exits
before any checkin message is sent, so `-61` shows up in every build that
survives the stack mapping, and no symbol of the checkin send path explains it.

## 6. Not measured

- Byte-level disassembly of `dserver_rpc_explicit_checkin` beyond size and call
  set — the 3-byte delta was not chased instruction by instruction.
- Whether the bind-side socket naming of §3 matters at all: no run was made
  here, and this note fixes nothing (no fix, by order).
