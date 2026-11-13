# chrome-desc-trap: повторный chrome-прогон на текущем overlay (шаг 39, веха 04)

База: pr-arm64 = e6629a2676756fd6680829cd22d5f08c0e8bbd59 (шаг 38 влит). Ветка:
task/chrome-desc-recheck.

## Вопрос

Шаг 38 показал: гость грузит overlay-сборку `CoreFoundationExtras.dylib`
(baseMethods=0x21c8, 3 метода), а не staged-копию, против которой слинкована
проба; класс `NSConstantDictionary` теперь сам отвечает `description`. Значит,
исходный chrome-исход лейна (SIGSEGV в `__NSCFCopyDescription2`,
Foundation+0x790b0) на текущем overlay может не воспроизводиться. Проверка —
прямой прогон chrome с ловушкой на Foundation+0x790b0.

## Ход

1. Перед прогоном в лог напечатано, какую `CoreFoundationExtras.dylib` грузит
   гость (источник — `$DARLING_OVERLAY`, из него launch-dynamic копирует
   guest-root в `/tmp/darling-local-overlay`):

```
=== pre-run: source of the guest CoreFoundationExtras.dylib
-rwxrwxr-x  1 freebsd  fleet  13768 Oct 13 10:04 <overlay>/usr/lib/CoreFoundationExtras.dylib
class name NSConstantDictionary baseMethods 0x21c8 count=3 [description, copyWithZone:, copy]
meta name NSConstantDictionary baseMethods 0x0
```

2. Состав staging-дерева. Без `DARLING_STAGING_TREES` launch-dynamic берёт
   встроенный дефолт `usr/lib:System/Library/Frameworks`, и chrome умирает
   раньше — на `dlopen` фреймворка:

```
dlopen //Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework: dlopen(…, 261): Library not loaded: /System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D
  Referenced from: /System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics
  Reason: image not found.
```

   Поэтому прогон шёл со списком деревьев из closure шага 33 (берётся из строки
   `staging trees: derived from the closure -- …` старого лога; в нём есть
   `System/Library/PrivateFrameworks/Onyx2D.framework`). С ним фреймворк
   загружается: `dyld: loaded: … Onyx2D`, трейс доходит до инициализаторов.

3. Прогонов два (второй — контроль детерминизма), оба с
   `MLDR_TRAP_AT='Foundation+0x790b0'` (resume=0), `timeout -k 5`.

## Лог (выдержка, 64 строки)

Первый блок — ловушка взведена (полный лог запуска 1: 14949 строк,
запуск 2: 14855 строк; оба в `$DARLING_BUILD_DIR`, не коммитятся — локальные пути).

```
[darling-mldr] patch_linux_raw_syscalls: no known raw-syscall trampoline signature matched in a 1810432-byte executable mapping at 0x274515eab000 — if this dylib makes raw Linux-ABI syscalls, they will silently run as FreeBSD syscalls under Linux numbers instead of failing (see freebsd_syscall_trap.c's file header)
dyld: loaded: <4C4C4453-5555-3144-A16A-FCDA1D4EF2FA> /System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation
[darling-mldr] patch_linux_raw_syscalls: no known raw-syscall trampoline signature matched in a 3854336-byte executable mapping at 0x274516193000 — if this dylib makes raw Linux-ABI syscalls, they will silently run as FreeBSD syscalls under Linux numbers instead of failing (see freebsd_syscall_trap.c's file header)
dyld: loaded: <07FDFD50-21F2-37B4-9504-151ABA0B51CE> /usr/lib/libicucore.A.dylib
[darling-mldr] MLDR_TRAP_AT: armed Foundation+0x790b0 at 0x27451679d0b0 (resume=0 emulate=0 orig=5548)
[darling-mldr] patch_linux_raw_syscalls: no known raw-syscall trampoline signature matched in a 1273856-byte executable mapping at 0x274516724000 — if this dylib makes raw Linux-ABI syscalls, they will silently run as FreeBSD syscalls under Linux numbers instead of failing (see freebsd_syscall_trap.c's file header)
dyld: loaded: <4C4C441B-5555-3144-A16E-F7A9057BE9BD> /System/Library/Frameworks/Foundation.framework/Versions/C/Foundation
[darling-mldr] patch_linux_raw_syscalls: no known raw-syscall trampoline signature matched in a 483328-byte executable mapping at 0x2745169ad000 — if this dylib makes raw Linux-ABI syscalls, they will silently run as FreeBSD syscalls under Linux numbers instead of failing (see freebsd_syscall_trap.c's file header)
dyld: loaded: <4C4C44CA-5555-3144-A186-74318AFA1BCB> /System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D
[darling-mldr] patch_linux_raw_syscalls: no known raw-syscall trampoline signature matched in a 249856-byte executable mapping at 0x274516aa8000 — if this dylib makes raw Linux-ABI syscalls, they will silently run as FreeBSD syscalls under Linux numbers instead of failing (see freebsd_syscall_trap.c's file header)
dyld: loaded: <4C4C44B6-5555-3144-A1F2-D5F373A23FFF> /usr/lib/native/libGL.dylib
dyld: loaded: <4C4C44A3-5555-3144-A138-DA967023C3EE> /System/Library/Frameworks/IOKit.framework/Versions/A/IOKit
[darling-mldr] patch_linux_raw_syscalls: no known raw-syscall trampoline signature matched in a 94208-byte executable mapping at 0x274516b1a000 — if this dylib makes raw Linux-ABI syscalls, they will silently run as FreeBSD syscalls under Linux numbers instead of failing (see freebsd_syscall_trap.c's file header)
dyld: loaded: <4C4C44E0-5555-3144-A169-A45955997029> /usr/lib/libz.1.dylib
[darling-mldr] patch_linux_raw_syscalls: no known raw-syscall trampoline signature matched in a 24576-byte executable mapping at 0x274516b34000 — if this dylib makes raw Linux-ABI syscalls, they will silently run as FreeBSD syscalls under Linux numbers instead of failing (see freebsd_syscall_trap.c's file header)
dyld: loaded: <4C4C44F3-5555-3144-A1F9-1C9B54FA1816> /usr/lib/native/libFreeType.dylib
[darling-mldr] patch_linux_raw_syscalls: no known raw-syscall trampoline signature matched in a 20480-byte executable mapping at 0x274516b3f000 — if this dylib makes raw Linux-ABI syscalls, they will silently run as FreeBSD syscalls under Linux numbers instead of failing (see freebsd_syscall_trap.c's file header)
dyld: loaded: <4C4C4465-5555-3144-A1F9-73EFAE964D7F> /usr/lib/native/libjpeg.dylib
[darling-mldr] patch_linux_raw_syscalls: no known raw-syscall trampoline signature matched in a 28672-byte executable mapping at 0x274516b48000 — if this dylib makes raw Linux-ABI syscalls, they will silently run as FreeBSD syscalls under Linux numbers instead of failing (see freebsd_syscall_trap.c's file header)
dyld: loaded: <4C4C44EA-5555-3144-A1E6-3AAF012147E8> /usr/lib/native/libpng.dylib
[darling-mldr] patch_linux_raw_syscalls: no known raw-syscall trampoline signature matched in a 20480-byte executable mapping at 0x274516b54000 — if this dylib makes raw Linux-ABI syscalls, they will silently run as FreeBSD syscalls under Linux numbers instead of failing (see freebsd_syscall_trap.c's file header)
dyld: loaded: <4C4C4424-5555-3144-A135-4A242C9425FE> /usr/lib/native/libtiff.dylib
[darling-mldr] patch_linux_raw_syscalls: no known raw-syscall trampoline signature matched in a 24576-byte executable mapping at 0x274516b5d000 — if this dylib makes raw Linux-ABI syscalls, they will silently run as FreeBSD syscalls under Linux numbers instead of failing (see freebsd_syscall_trap.c's file header)
dyld: loaded: <4C4C44BA-5555-3144-A155-B8F3E5814621> /usr/lib/native/libfontconfig.dylib
[darling-mldr] patch_linux_raw_syscalls: no known raw-syscall trampoline signature matched in a 12288-byte executable mapping at 0x274516b68000 — if this dylib makes raw Linux-ABI syscalls, they will silently run as FreeBSD syscalls under Linux numbers instead of failing (see freebsd_syscall_trap.c's file header)
dyld: loaded: <4C4C447F-5555-3144-A168-49F37EE4E471> /usr/lib/native/libgif.dylib
[darling-mldr] patch_linux_raw_syscalls: no known raw-syscall trampoline signature matched in a 1564672-byte executable mapping at 0x274516b6d000 — if this dylib makes raw Linux-ABI syscalls, they will silently run as FreeBSD syscalls under Linux numbers instead of failing (see freebsd_syscall_trap.c's file header)
```

Второй блок — смерть (строки `TRAP_AT hit` в логе нет):

```
objc[65694]: Class NSPropertyListSerialization is implemented in both /System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation (0x27451607e268) and /usr/lib/FoundationExtras.dylib (0x274515d9a280). One of the two will be used. Which one is undefined.
[darling-mldr] FATAL signal 11 (code=2) at addr=0x274515e9e008
  si_pid=0 si_uid=0 si_value=0x0 sival_ptr=0x0
  rip=0x0000274515e9e008  rax=0xffffffffffffffff  rbx=0x0000274515c76880
  rcx=0x00007fffffdfdbdb  rdx=0x00000000000f0000  rsi=0x00000000000014f9
  rdi=0x0000274515c76948  rbp=0x00007fffffdfdbb0  rsp=0x00007fffffdfdaa8
  r8 =0x000000082668d850  r9 =0x0000000000000000  r10=0x0000000000000000
  r11=0x00007fc39bc3df40  r12=0x0000000000000000  r13=0x0000000000000078
  r14=0x00007fffffdfdbc8  r15=0x0000000000000000
  backtrace (3 frames):
    #00 0x223923  0x223923 <crash_debug_handler+0xc3> at <build>/dserver/mldr-real/mldr
    #01 0x82150645a  0x82150645a <_pthread_sigmask+0x50a> at /lib/libthr.so.3
    #02 0x821505a5b  0x821505a5b <pthread_signals_unblock_np+0x5bb> at /lib/libthr.so.3
  guest stack dump at rsp=0x00007fffffdfdaa8:
  [gstack+   0] 0x0000274507d3aaa2
  [gstack+   8] 0x0000000000000106
  [gstack+  16] 0x00007fffffdfdb00
  [gstack+  24] 0x1f0700209bc81020
  [gstack+  32] 0x0000000000000000
  [gstack+  40] 0x0000000000000000
  [gstack+  48] 0x0000000000000000
  [gstack+  56] 0x0000000000000000
  [gstack+  64] 0x0000000000000000
  [gstack+  72] 0x00007fffffdfdb50
  [gstack+  80] 0x00000008265c251f
  [gstack+  88] 0x0000000000000000
  [gstack+  96] 0x0000000000000000
  [gstack+ 104] 0x0000000000000000
  [gstack+ 112] 0x00007fffffdfdb68
  [gstack+ 120] 0x00007fffffdfdb40
  [gstack+ 128] 0x0000000826553abd
  [gstack+ 136] 0x0000000000000000
  [gstack+ 144] 0x00007fffffdfdce8
  [gstack+ 152] 0x00007fffffdfdb70
  [gstack+ 160] 0x0000000826553a36
  [gstack+ 168] 0x00007fffffdfdb70
  [gstack+ 176] 0x0000000000000000
```

## Repro

```sh
cd "$DARLING_SRC_DIR"
# список деревьев из closure шага 33 (нужен Onyx2D, иначе dlopen падает)
sed -n '14p' "$DARLING_SRC_DIR/../diag/chrome-fw-run.log" \
  | sed 's/^staging trees: derived from the closure -- //' > "$DARLING_BUILD_DIR/chrome-staging-trees.txt"

/usr/local/bin/sudo -n env \
  DARLING_SRC_DIR="$DARLING_SRC_DIR" DARLING_OVERLAY="$DARLING_OVERLAY" \
  DARLING_BUILD_DIR="$DARLING_BUILD_DIR" \
  DARLING_TEST_BINARY=chrome-macho \
  "CHROME_APP=$CHROME_APP" \
  "DARLING_STAGING_TREES=$(cat "$DARLING_BUILD_DIR/chrome-staging-trees.txt")" \
  WAYLAND_DISPLAY=wayland-1 XDG_RUNTIME_DIR=/tmp/wayland-test \
  DARLING_SMOKE_REFRESH=1 MLDR_TRAP_AT='Foundation+0x790b0' \
  timeout -k 5 300 "$DARLING_BUILD_DIR/launch-dynamic" > "$DARLING_BUILD_DIR/chrome-recheck.log" 2>&1
# -> rc=124: гость умирает, супервизор launch-dynamic не завершается

grep -c "MLDR_TRAP_AT hit" "$DARLING_BUILD_DIR/chrome-recheck.log"   # 0
grep -n "FATAL signal" "$DARLING_BUILD_DIR/chrome-recheck.log"
llvm-nm -n "$DARLING_OVERLAY/usr/lib/suppl.dylib" | grep 0000000000002008
```

## Факты

1. Ловушка взведена в обоих прогонах (`armed Foundation+0x790b0 … resume=0`),
   строк `MLDR_TRAP_AT hit` — **0** в обоих. До `Foundation+0x790b0` chrome не
   доходит.
2. Смерть одинаковая в двух прогонах: `FATAL signal 11 (code=2)`, адрес =
   rip = база `suppl.dylib` + **0x2008** (запуск 1: база 0x274515e9c000, адрес
   0x274515e9e008; запуск 2: база 0x3f533049c000, адрес 0x3f533049e008). Смещение
   совпало, ASLR разный.
3. `suppl.dylib+0x2008` — это слот в `__data` (`llvm-nm -n`: `0000000000002008 D
   ___bzero`; `__text` 0x370–0xd3c, `__data` с 0x2000). rip равен адресу слота,
   то есть прыжок/возврат пришёл на сам слот, а не на функцию.
4. Вершина гостевого стека `[gstack+0] 0x…212aaa2` — `Google Chrome for Testing
   Framework` + **0x212aaa2** в обоих прогонах (базы 0x274505c10000 и
   0x3f5320210000). Это вызывающий: он и уходит в `suppl.dylib`, а не в
   `Foundation+0x790b0`.
5. Без `DARLING_STAGING_TREES` прогон падает раньше: `dlopen` фреймворка →
   `Library not loaded: …/Onyx2D … Reason: image not found` (в overlay Onyx2D
   есть, дефолтные staging-деревья `usr/lib:System/Library/Frameworks` его не
   копируют).
6. Про `description` ничего не измерено: до `Foundation+0x790b0` и до вызовов
   `description` в этом прогоне дело не дошло.

## Что это значит для приёмки

Ни одна из двух веток приёмки не выполнена буквально: строки `TRAP_AT hit` нет
(ветка «лейн жив» не подтверждена), но и «полный лог без хита с корректным
description» не получен — chrome умирает раньше, на прыжке в `__data` слоя
`___bzero`, до пути `description`. Поэтому лейн на текущем overlay **не закрыт**
этим прогоном: исход шага 34 не воспроизведён, его маскирует более ранняя смерть
(по виду — незабинденный указатель: слот `__data` держит адрес самого себя).
Проверять это отдельным замером: что делает бинд `___bzero` у фреймворка и
почему слот остался неразрешённым.
