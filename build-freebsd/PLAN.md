# План: запуск Chromium на Darling/FreeBSD через AppKit Wayland backend

## Цель
Заставить `CFBundle`/`AppKit` загрузить `Wayland.backend`, найти `NSPrincipalClass = WaylandDisplay`, и успешно выполнить `[bundle load]`.

---

## 1. Текущее состояние (DONE)

| Что | Статус | Примечания |
|---|---|---|
| CoreFoundation собран с `-DDARLING` | ✅ | `__CFISAForTypeID()` работает, `CFBundleCreate` не падает |
| `___CFConstantStringClassReference` | ✅ | alias на `_OBJC_CLASS_$___NSCFConstantString` |
| Foundation пересобрана | ✅ | `NSClassFromString` работает |
| mldr с трансляцией open-flags | ✅ | `MACOS_SYS_open`, `MACOS_SYS_open_nocancel` |
| `linprocfs` примонтирован на `/proc` | ✅ | хост видит `/proc/self/mounts` |
| Диагностика | ✅ | `CFDBG=1`, `MLDRTACE=1`, `run-smoke.sh`, `decode-crash.py` |
| Автосборка | ✅ | `build-freebsd/build-all.sh` |
| **opendir()/readdir() в госте** | ✅ | Починено: фикс `getdents64` в mldr + бинарный патч `libsystem_c.dylib` |
| **Чтение Info.plist** | ✅ | `CFBundle` видит `Info.plist`, `infoDictionary count=12` |
| **NSPrincipalClass** | ✅ | Определяется как `WaylandDisplay` |
| **[bundle load] / dlopen** | ✅ | Wayland.backend загружается, `load=YES`, класс найден |
| **Variadic-marshal trampolines** | ✅ | Корневой фикс: вызовы `wl_*_marshal*` с вариативными аргументами больше не теряют аргументы (см. разд. 3.6) |
| **Поверхность + shm-буфер + commit** | ✅ | Полный seam guest→sway доказан: `wl_surface` создаётся, буфер ARGB8888 выделяется, attach/damage/commit → sway принимает кадр (см. разд. 8) |

---

## 2. Загрузка Wayland.backend — РЕШЕНО ✅

### 2.1 Что было
`dlopen(Wayland.backend)` падал с `Symbol not found: _OBJC_METACLASS_$_NSObject`.

### 2.2 Корень проблемы
`CoreFoundation/NSObject.m` содержал макрос `NSOBJECT_HERE_IN`, который создавал `$ld$add$os10.X$` символы. На настоящем macOS dyld обрабатывает эти символы и экспортирует `_OBJC_CLASS_$_NSObject` / `_OBJC_METACLASS_$_NSObject` от CoreFoundation. Darling's dyld не умеет — символы оставались "phantom", и two-level namespace резолвинг Onyx2D → CoreFoundation → NSObject ломался.

### 2.3 Фикс
Убраны `NSOBJECT_HERE_IN(10.0-10.7)` из `NSObject.m` — `$ld$add$os10.X$` символы больше не создаются. Символы `_OBJC_CLASS_$_NSObject` / `_OBJC_METACLASS_$_NSObject` теперь резолвятся напрямую из `libobjc.A.dylib`, как и должны.

### 2.4 Результат
```
load-wayland-backend: load=YES principalClass=WaylandDisplay
load-wayland-backend: objc_getClass(WaylandDisplay)=WaylandDisplay
load-wayland-backend: objc_lookUpClass(WaylandDisplay)=WaylandDisplay
```

---

## 3. Wayland compositor — ЧАСТИЧНО РЕШЕНО

### 3.1 Что сделано
- Установлены `cage` и `sway` (headless Wayland compositor'ы)
- Sway запускается с `WLR_BACKENDS=headless`, создаёт `wayland-1` socket
- Тест модифицирован: создаёт `[WaylandDisplay alloc] init]`
- Нативный FreeBSD тест **ПОДКЛЮЧАЕТСЯ** к sway ✅
- WAYLAND_DISPLAY/XDG_RUNTIME_DIR пробрасываются в гость ✅

### 3.2 Починено: `connect()` для AF_UNIX в mldr
Корень проблемы: overlay `libsystem_kernel.dylib` (upstream Darling CI, собран для Linux) конвертирует BSD `sockaddr_un` в Linux-формат (16-bit family в offset 0, без `sun_len`) и раскрывает путь через `_vchroot_expand` перед вызовом Linux-ABI `connect`. mldr перехватывал Linux syscall 42 и транслировал его в FreeBSD `SYS_connect`, но передавал Linux-формат sockaddr напрямую ядру FreeBSD, что давало `EAFNOSUPPORT` (ядро видело `sun_family=0`).

Фикс в `src/startup/mldr/freebsd_syscall_trap.c`:
1. В `LINUX_SYS_connect` конвертируем Linux-формат sockaddr обратно в BSD: offset 0 = `sun_len`, offset 1 = `sun_family`.
2. Для `AF_UNIX` отрезаем overlay-префикс (`mldr_load_results.root_path`) от пути, чтобы Unix-сокет искался по хостовому пути, а не внутри overlay.

Результат: `test-connect-macho` подключается к `/tmp/wayland-test/wayland-1` успешно (`connect() ret=0`).

### 3.3 Текущий блокер: сломанный wrapper `libwayland-client.dylib`
`WaylandDisplay -init` вызывает `wl_display_connect(NULL)`, `wl_display_get_registry()`, `wl_display_roundtrip()`. Все эти функции идут через overlay wrapper `libwayland-client.dylib` (prebuilt, `/usr/local/lib/native/`).

**Корень проблемы wrapper'а**: каждая функция (`_wl_display_connect` и т.д.) вызывает `__elfcalls.dlsym_fatal(_lib_handle, "имя_функции")` для резолва нативной ELF-функции из `libwayland-client.so.0`, но **не вызывает** резолвнутую функцию — просто **возвращает указатель на неё** как результат.

Подтверждено логированием:
- `wl_display_connect(NULL)` → возвращает `0x834c44850` (адрес функции, а не `struct wl_display*`)
- `wl_display_roundtrip(display)` → возвращает `885280704` (адрес функции, а не 0/-1)

### 3.4 Попытки обойти (не удались)
1. **wayland_shim.c** — shim через `dlopen`/`dlsym` напрямую:
   - overlay's `dlopen` не грузит ELF-библиотеки ("image not found")
   - `__elfcalls` недоступен через `dlsym(NULL, ...)` — доступен только через GOT (dyld)
   - `RTLD_DEFAULT`/`RTLD_NEXT` находит broken wrapper → segfault (бесконечная рекурсия или мусор)

### 3.5 Следующие шаги (при продолжении)
1. **Пересобрать wrapper** — написать новый `libwayland-client.dylib` source, который после `dlsym_fatal` **вызывает** резолвнутую функцию с оригинальными аргументами. Паттерн:
   ```c
   struct wl_display *wl_display_connect(const char *name) {
       real_fn = dlsym_fatal(handle, "wl_display_connect");
       return real_fn(name);  // <-- вызов, не возврат указателя!
   }
   ```
2. Или: использовать `__elfcalls` напрямую из WaylandDisplay.m (нужно найти способ достучаться до GOT/`__elfcalls` из Mach-O).
3. Или: переделать Wayland backend чтобы не использовать `libwayland-client.dylib` вообще — вызывать нативные функции через `__elfcalls`.

---

---

## 4. Дальний путь: от Wayland backend до Chromium

### 4.1 AppKit на Wayland
- Убедиться, что AppKit инициализируется и создаёт `NSApplication`.
- Проверить, что Wayland-соединение устанавливается.

### 4.2 Chromium
- Собрать/запустить Chromium с флагами `--ozone-platform=wayland`.
- Подготовить окружение: `DISPLAY`/`WAYLAND_DISPLAY`, шрифты, GPU sandbox.
- Ожидаемые проблемы: sandbox, sandbox, sandbox, и ещё раз sandbox.

---

## 5. Как был решён предыдущий блокер (opendir)

### 5.1 Что было сломано
- `opendir()` в гостьевом libc возвращал `ENOENT`/`EINVAL`.
- Причина: `__kernel_supports_unionfs()` считала unionfs активным и шла в `fstatfs()` → `/proc/self/mounts`, которого не было в `LOCAL_OVERLAY`.
- Даже после отключения unionfs `readdir()` падал с `EINVAL`, потому что `mldr` неправильно обрабатывал `getdents64` для маленьких директорий.

### 5.2 Что сделано (opendir)
1. **Пропатчен `$DARLING_OVERLAY/usr/lib/system/libsystem_c.dylib`**:
   - Статическая переменная `__kernel_supports_unionfs.kernel_supports_unionfs` изменена с `-1` на `0` в двух копиях функции.
   - Адреса в fat-бинарнике (x86_64 slice): `0xfee80` и `0xfef30`.
   - Это заставляет `__kernel_supports_unionfs()` всегда возвращать `false`, обходя `fstatfs`/`/proc`.
2. **Починен `mldr`** (`src/startup/mldr/freebsd_syscall_trap.c`):
   - Linux `getdents64` handler использовал слишком строгое условие цикла (`in_off + sizeof(struct dirent) <= r + offsetof(...)`), из-за чего для маленьких директорий возвращал `EINVAL`.
   - Заменено на `while (in_off < r)`.
   - Также добавлены Linux `readv`/`writev` (сисколы 19/20) для совместимости с overlay-бинарниками.
3. **Убран symlink-код из `launch-dynamic-smoke.c`**:
   - `symlink("/proc", LOCAL_OVERLAY "/proc")` вызывал краш `mldr` на старте (причина осталась не выяснена).
   - Теперь `/proc` не нужен, т.к. unionfs-check отключён.

### 5.3 Что сделано (NSObject / dlopen)
4. **Убраны `$ld$add$os10.X$` символы из `NSObject.m`**:
   - `CoreFoundation/NSObject.m` содержал макрос `NSOBJECT_HERE_IN`, который через `__asm__("$ld$add$os10.X$...")` создавал availability-маркеры.
   - На настоящем macOS dyld обрабатывает эти символы и экспортирует `_OBJC_CLASS_$_NSObject` / `_OBJC_METACLASS_$_NSObject` от CoreFoundation.
   - Darling's dyld не умеет — символы оставались "phantom", и two-level namespace резолвинг Onyx2D → CoreFoundation → NSObject ломался.
   - **Фикс**: закомментированы `NSOBJECT_HERE_IN(10.0-10.7)` — символы больше не создаются, резолв напрямую из `libobjc.A.dylib`.
   - Это правильный подход, т.к. `$ld$add$os10.X$` — это внутренний механизм Apple dyld, а не публичный API.

### 5.3 Проверка
```sh
CFDBG=1 sh build-freebsd/run-smoke.sh
```
Ожидаемый вывод:
```
[CFDBG] IterateDirectory opendir(/) OK
[CFDBG] IterateDirectory entry=Info.plist ...
[CFDBG] InfoPlist read OK version=2
load-wayland-backend: CFBundle NSPrincipalClass=WaylandDisplay
load-wayland-backend: infoDictionary NSPrincipalClass=WaylandDisplay
```

---

## 6. Инфраструктура и правила работы

### 6.1 Обязательные проверки после каждого изменения
1. `CFDBG=1 sh build-freebsd/run-smoke.sh` — смотреть на `load=`, `principalClass`, `WaylandDisplay`.
2. Если `FATAL signal 11` — сразу `decode-crash.py`, сравнить с предыдущим логом.
3. Любое изменение `mldr` пересобирать через `build-freebsd/build-mldr-only.sh`.

### 6.2 Где что лежит
| Файл | Назначение |
|---|---|
| `build-freebsd/build-all.sh` | Полная пересборка |
| `build-freebsd/run-smoke.sh` | Запуск теста |
| `build-freebsd/decode-crash.py` | Расшифровка крашей |
| `tests/launch-dynamic-smoke.c` | ELF-обёртка, копирует overlay, форкает darlingserver, exec mldr |
| `tests/load-wayland-backend.m` | Тестовый Mach-O |
| `src/external/corefoundation/CFFileUtilities.c` | `_CFIterateDirectory` с `CFDBG` |
| `src/external/corefoundation/CFBundle_InfoPlist.c` | Загрузка `Info.plist` |
| `src/startup/mldr/freebsd_syscall_trap.c` | Трап сисколов mldr |
| `src/external/corefoundation/NSObject.m` | Убраны `$ld$add$os10.X$` маркеры |
| `$DARLING_OVERLAY/usr/lib/system/libsystem_c.dylib` | Пропатчен: `__kernel_supports_unionfs` всегда false |

### 6.3 Правило для следующего агента
- Не пытаться «додумать» архитектуру. Если непонятно — остановиться и спросить.
- Начинать строго с задачи **2.3.1** — диагностики `_OBJC_METACLASS_$_NSObject`.
- Любое изменение сопровождать проверкой `run-smoke.sh` и сверкой с предыдущим логом.

---

## 7. Handoff summary (кратко для следующего агента)

**Предыдущий блокер решён:** `opendir()`/`readdir()` работают, `Info.plist` читается, `NSPrincipalClass=WaylandDisplay` определяется.

**Задача 2.3.1 ЗАКРЫТА:** `_OBJC_METACLASS_$_NSObject` резолвится из `libobjc.A.dylib` штатным биндом dyld (1333 сайта, ненулевое значение; только `DYLD_PRINT_BINDINGS` его показывает) — `build-freebsd/DYLD-BINDINGS.md`.

**Следующий шаг (стена №6):** dyld падает в `ImageLoader::trieWalk+0xa4` (разд. 9.9); фронтир — пересборка dyld с лог-патчем и gate (`build-freebsd/DYLD-REBUILD.md`).

**Запуск для проверки:**
```sh
cd $DARLING_SRC_DIR
CFDBG=1 sh build-freebsd/run-smoke.sh
```

**Последний лог:** `$DARLING_BUILD_DIR/smoke-load-wayland-backend-macho-<date>.log`

---

Начинать с **задачи 2.3.1**.

---

## 8. ПОВЕРХНОСТЬ + SHM-БУФЕР + COMMIT → sway (РЕШЕНО ✅)

### 8.1 Что доказано
Полный seam `guest→sway` для presentation через shm-буфер работает. Тест
`tests/wayland-surface-commit.m` (линкует `wayland_shim.o` + `wayland_ifaces.o` +
`wayland_tramp.o` напрямую, без AppKit-бандла):

```
surface-commit: wl_display_connect=0x...
surface-commit: comp=0x... shm=0x...
surface-commit: shm ARGB8888 advertised=1
surface-commit: create_surface=0x...
surface-commit: guest open ./.s  open=4 ftruncate OK
surface-commit: guest open ./.s  open=4 sized+mmap OK  <-- USING THIS
surface-commit: pool=0x... buffer=0x...
surface-commit: committed (attach+damage+commit), flushing+roundtrip
surface-commit: error=0 (0 = compositor accepted the frame)
surface-commit: DONE rc=0
```

sway в логе создал `New wlr_surface` для нашего клиента и **не** выдал
`error in client communication` для него (`error=0` после roundtrip — авторитетный
признак отсутствия protocol-ошибки). Это тот самый seam, который используется
`WaylandWindow.m -flushBuffer` (attach/damage/commit) и нужен для программного
рендера Chromium/Onyx2D.

### 8.2 Корневой фикс (variadic marshal)
**Проблема:** shim-обёртки C для `wl_proxy_marshal_flags`, `wl_proxy_marshal`,
`wl_proxy_marshal_constructor`, `wl_proxy_marshal_constructor_versioned` молча
теряли `...`-аргументы на линии `f(proxy, opcode, interface, version, flags)` —
все сообщения surface/buffer уходили с мусорными args (отсюда краши/глюки).

**Фикс:** C-обёртки удалены, вместо них `wayland_tramp.s` — x86-64 SysV asm
трамплины, которые сохраняют `rdi..r9`, через `_lazy` резолвят нативную функцию
и `jmp *%rax` — полное состояние аргументов сохраняется.

### 8.3 Как получить рабочий shm-fd в госте (важно для Onyx2D)
Диагностика (все Probed-источники):
| Источник | Результат |
|---|---|
| `open("./wlshm-...")` | ✅ `open=4` — работает через vchroot/cwd |
| `open("/tmp/...")` / `open("/dev/shm/...")` | ❌ ENOENT (не примонтированы) |
| гостьев `shm_open("/wlshm")` | ❌ EINVAL — **POSIX shm не реализован в duct-tape** (нет модуля pshm) |
| `_elfcalls->shm_open` | ⚠️ `open=4`, но fd «сырой» хостовый — гость его не может `ftruncate`/`mmap` (нет в guest-таблице fd) |

**Нюанс (ИСПРАВЛЕН ✅):** гостевой `ftruncate(fd)` возвращал **errno 78** — но это
был не duct-tape, а **mldr-диспатч**: гостевой `ftruncate` идёт как **Linux-raw
syscall 77** (гость = Linux-ABI, `ml dr_patch_linux_raw_syscalls`), и в
`dispatch_linux_syscall` не было кейса → default → ENOSYS (macOS-ское errno 78).
**Фикс:** добавлены `#define LINUX_SYS_ftruncate 77` + кейс
`freebsd_raw_syscall(SYS_ftruncate, a1, a2)` в `freebsd_syscall_trap.c`
(зеркалит `LINUX_SYS_pwrite64`). Пересобрать mldr (`build-mldr-only.sh`) и
синхронизировать в `$DARLING_BUILD_DIR/dserver/mldr-real/mldr`. Проверено:
`ftruncate OK` в логе теста. Регулярный файл годится как shm-fd — wlroots/wl_shm
принимает любой mmap-нужный fd.

**Рабочий рецепт (текущий в тесте):**
1. `fd = open("./wlshm-<pid>", O_RDWR|O_CREAT, 0600)` — файл в cwd гостя.
2. Размер: `ftruncate(fd, size)` (теперь переводится в FreeBSD SYS_ftruncate).
3. `data = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)`.
4. Рисуем в `data`, затем `wl_shm_create_pool(_shm, fd, size)` →
   `wl_shm_pool_create_buffer(...ARGB8888...)` → `attach` → `damage_buffer` →
   `commit` → `roundtrip`.

### 8.4 Следующие шаги (при продолжении)
1. ~~Починить гостевой `ftruncate`~~ **ГОТОВО ✅** — mldr-кейс `LINUX_SYS_ftruncate`
   (Linux 77 → FreeBSD SYS_ftruncate); `WaylandWindow.m` теперь использует
   натуральный `ftruncate`, а не `pwrite`-обход.
2. **Реализовать гостевой POSIX shm (`shm_open`/`memfd_create`)** в duct-tape
   (сейчас нет модуля pshm → EINVAL). Это нужно для «чистого» пути, где гость
   сам создаёт memfd, который externalize'ится в хостовый fd при SCM_RIGHTS.
 3. **Собрать полный WaylandWindow рендер** — **ГОТОВО ✅** (см. разд. 8.6).
    Реальный бандл `Wayland.backend`: `load-wayland-backend.m` (display connect) +
    `wayland-window.m` (`[NSDisplay currentDisplay]` → `newWindowWithDelegate:` →
    `WaylandWindow -initWithDelegate:`) проходит surface→xdg_surface→xdg_toplevel→
    configure против sway, `wl_display_get_error=0`, `DONE rc=0`.
 4. **Дождь всего: frame-wait / input seat** — seat.capabilities keyboard=0/pointer=0
   (sway headless без input-устройств); для интерактивности нужен virtio-ввод или
   wlr/headless keyboard injection.
5. **Chromium Onyx2D**: спаять путь — mmap буфера + рисование в него из Onyx2D +
   commit, затем `--ozone-platform=wayland`.

### 8.5 Запуск
```sh
cd $DARLING_SRC_DIR
sh build-freebsd/build-wayland-surface-test.sh          # собрать тест
WAYLAND_DISPLAY=wayland-1 XDG_RUNTIME_DIR=/tmp/wayland-test \
  WAIT_SECS=25 sh build-freebsd/run-smoke.sh wayland-surface-commit-macho
grep surface-commit build/smoke-wayland-surface-commit-macho-*.log | grep -v format=
# Ждём: error=0, DONE rc=0
```

### 8.6 КОРНЕВАЯ ПРИЧИНА: backend-discovery возвращал count=0 (РЕШЕНО ✅)

Симптом: `[NSDisplay currentDisplay]` бросал
`Failed to connect to a window server. Available backends are: <CFArray ... count=0>`.
`NSDisplay -init` ищет бэкенды через
`[appKitBundle pathsForResourcesOfType:@"backend" inDirectory:@"Backends"]`, а
`resourcePath` резолвился в **корень фреймворка** (`.../AppKit.framework`), а не в
`.../Versions/C/Resources` → поиск шёл по несуществующему `AppKit.framework/Backends`.

Почему `resourcePath` = корень. CFBundle классифицирует версию бандла в
`_CFBundleGetBundleVersionForURL` (`CFBundle_Resources.c:246`): для фреймворка нужен
top-level элемент `Resources` (как `DT_LNK`/`DT_DIR`), иначе версия = `_CFBundleVersionFlat`
→ `CFBundleCopyResourcesDirectoryURL` возвращает `bundleURL` (корень). Реальный
фреймворк имеет `AppKit.framework/Resources → Versions/Current/Resources` (симлинк).

Где сломалось: `launch-dynamic` (`tests/launch-dynamic-smoke.c:221-225` и
`236-238`) стейджит фреймворки в локальный оверлей командой
`find . -type f | pax -rw ...` — **`-type f` отбрасывает симлинки**. Поэтому в
госте `AppKit.framework` содержал только `Versions/` (директорию); корневые
симлинки `Resources`/`AppKit`/`Versions/Current` не попадали в стейдж, и
CFBundle не видел `Resources` → Flat → корень.

Фикс: в `launch-dynamic-smoke.c` заменить `find . -type f` на
`find . \( -type f -o -type l \)` (обе строки staging-а Frameworks и
PrivateFrameworks), чтобы `pax -rw` копировал симлинки как симлинки. Симлинки уже
присутствовали в `$DARLING_OVERLAY/System/Library/Frameworks/AppKit.framework/`
(добавлены ранее: `AppKit→Versions/Current/AppKit`, `Resources→Versions/Current/Resources`,
`Versions/Current→C`).

Доказательство (log `smoke-wayland-window-macho-0828-120634.log`): после фикса
`readdir(AppKit.framework)` видит `Versions(DIR)`, `AppKit(LNK)`, `Resources(LNK)`;
`resourcePath=.../AppKit.framework/Resources`;
`pathsForResourcesOfType(@"backend",@"Backends") count=1` →
`.../Resources/Backends/Wayland.backend`; `[NSDisplay currentDisplay]→WaylandDisplay`;
`newWindowWithDelegate:→WaylandWindow`; `wl_display_get_error=0`; `DONE rc=0`.

Сборка после фикса: только пересобрать хостовый `launch-dynamic`
(`cc -o .../launch-dynamic tests/launch-dynamic-smoke.c -lpthread`); гостевые
фреймворки/CF не пересобирались — симлинки теперь корректно стейджатся.

Тест: `tests/wayland-window.m` (+ `wayland-window-macho`). Линкует Foundation/CF/libobjc
+ shim-obj'ы (`wayland_shim.o`, `wayland_ifaces.o`, `wayland_tramp.o`,
`xdg-shell-client-protocol.o`). Включает readdir- и CFBundle-диагностику.
Запуск:
```sh
WAYLAND_DISPLAY=wayland-1 XDG_RUNTIME_DIR=/tmp/wayland-test \
  WAIT_SECS=18 sh build-freebsd/run-smoke.sh wayland-window-macho
# Ждём: DONE rc=0  и  "WAYLAND WINDOW OK"
```

### 8.7 ПОЛНЫЙ RENDER-SEAM: Onyx2D -> wl_shm -> sway (РЕШЕНО)

Доказан не только handshake surface'а, а весь путь отрисовки. В tests/wayland-window.m после создания окна:
1. [window createCGContextIfNeeded] -> O2Context_builtin_FT (Onyx2D загрузился, O2Surface 320x240).
2. Рисуем реальным растеризатором Onyx2D: O2ContextSetRGBFillColor + O2ContextFillRect + O2ContextFlush.
3. [window flushBuffer] (WaylandWindow.m:615): memcpy O2Surface -> wl_shm буфер -> wl_surface_attach/damage_buffer/commit + wl_display_flush.

Доказательство (smoke-wayland-window-macho-0828-122306.log + /tmp/sway_debug.log):
- readback pixel B=217 G=140 R=38 A=255 -> Onyx2D записал ARGB8888 (синий 0.15/0.55/0.85), буфер не нулевой.
- after flushBuffer roundtrip wl_display_get_error=0 -> протокол без ошибок.
- sway: 'New wlr_surface' + 'Transaction ... committing' -> композитор принял кадр с контентом.

КОРЕНЬ ПРОВАЛА flushBuffer (shm allocation failed ... Invalid argument): собранный бандл Wayland.backend был УСТАРЕВШИМ (WaylandWindow.o 23:44, рецепт _wayland_window_create_shm_fd с веткой open() под -DDARLING добавлен в source позже, 00:22). Старый бандл шёл в fallback shm_open(SHM_ANON), которого в госте нет -> EINVAL. Фикс: DARLING_OVERLAY="$DARLING_OVERLAY" sh build-freebsd/build-wayland-backend.sh (компилирует WaylandWindow.m с -DDARLING, ставит в OVERLAY). Тот же рецепт отдельно проверен probe'ом в тесте: open=9 ftruncate=0 mmap=OK.

ВАЖНО: при правке WaylandWindow.m/WaylandDisplay.m бандл надо пересобирать ЭТИМ скриптом, иначе гость грузит устаревший .dylib из OVERLAY.

SEAM РЕСАЙЗА (ДОКАЗАН, log smoke-wayland-window-macho-0828-123902.log): тест после первого
flushBuffer зовёт [window setFrame: {0,0,640,480}] -> -setFrame: (WaylandWindow.m:380)
-> invalidateContextWithNewSize: -> повторный createCGContextIfNeeded даёт O2Surface 640x480;
второй flushBuffer реаллоцирует wl_shm буфер (_acquireBackBufferForWidth) и презентует.
Доказательство: after setFrame O2Surface 640x480; after resize flushBuffer wl_display_get_error=0;
resize readback B=77 G=204 R=51 A=255 (зелёный 0.2/0.8/0.3 => ресайзнутое окно нарисовано);
DONE rc=0. ОГРАНИЧЕНИЕ: O2Context_builtin_FT.resizeWithNewSize: ВСЕГДА возвращает NO
(Onyx2D не может ресайзить контекст на месте, O2Context.m:203), поэтому -setFrame: релизит
старый _context; реальный AppKit -setFrameSize: пересоздаёт CG-контекст — тест делает так же.
Это не блокирует Chromium (он сам управляет размерами через -setFrameSize:), но значит
инкрементального ресайза O2Surface внутри одного контекста нет.

СЛЕДУЮЩИЙ шаг к Chromium: спаять render-путь с --ozone-platform=wayland (Onyx2D уже рисует в shm-буфер окна); сам Chromium под Darwin-ABI на FreeBSD ещё не собирался (разд. 4 / 8.4 step 5).

<div class=context>sway держать запущенным: `WLR_BACKENDS=headless sway -d`
(лог `/tmp/sway_debug.log`, socket `wayland-1` в `/tmp/wayland-test`).</div>

### 8.8 ГОТОВЫЙ SMOKE-ТЕСТ: сборка и запуск wayland-window-macho

Тест `tests/wayland-window.m` доказывает весь seam: backend-discovery -> WaylandDisplay
-> WaylandWindow (xdg-surface handshake) -> Onyx2D render -> wl_shm flushBuffer -> sway,
плюс seam РЕСАЙЗА ([window setFrame:] 320x240 -> 640x480, повторный flushBuffer).
Воспроизводимый рецепт (запускать из `src/build-freebsd`):

1) Пересобрать Wayland.backend бандл (ОБЯЗАТЕЛЬНО после правки WaylandWindow.m):
```sh
DARLING_OVERLAY="$DARLING_OVERLAY" sh build-freebsd/build-wayland-backend.sh
```

2) Пересобрать launch-dynamic (чтобы staging фреймворков нес и symlinks):
```sh
cc -o $DARLING_BUILD_DIR/launch-dynamic $DARLING_SRC_DIR/tests/launch-dynamic-smoke.c -lpthread
```

3) Собрать тест (clang -> .o, ld64.lld -> Mach-O; линкуем Onyx2D + shim-obj'ы):
```sh
B=$DARLING_BUILD_DIR/real-macho; SF=$B/sdk-flat; SO=$B/staged-overlay
SRC=$DARLING_SRC_DIR; FND=$SRC/src/external/foundation
SDIR=$DARLING_BUILD_DIR/wayland-window-obj; ODB=$DARLING_BUILD_DIR/wayland-backend/obj
ONYX=$DARLING_OVERLAY/System/Library/PrivateFrameworks/Onyx2D.framework/Versions/A/Onyx2D
clang -target x86_64-apple-macos10.12 -nostdinc -D__DARWIN_ONLY_UNIX_CONFORMANCE=1 \
  -I"$FND/include" -I"$SF/usr/include" -I"$SRC/tests/vendor/fakesdk" \
  -I"$SRC/src/external/cocotron/AppKit/Wayland.backend" -I"$SRC/src/external/cocotron/Onyx2D/include" \
  $(pkg-config --cflags wayland-client) -I/usr/local/include/libepoll-shim \
  -fblocks -fconstant-cfstrings -fobjc-runtime=macosx-10.12 -DDARLING -F"$SF/Frameworks" \
  -x objective-c -O1 -w -c "$SRC/tests/wayland-window.m" -o "$SDIR/wayland-window.o"
ld64.lld -arch x86_64 -platform_version macos 10.12 10.12 -syslibroot "$SO" -Z \
  -o "$SRC/tests/wayland-window-macho" "$SDIR/wayland-window.o" \
  "$ODB/wayland_shim.o" "$ODB/wayland_ifaces.o" "$ODB/wayland_tramp.o" "$ODB/xdg-shell-client-protocol.o" \
  "$ONYX" "$SO/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation" \
  "$SO/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
  "$SO/usr/lib/libobjc.A.dylib" "$SO/usr/lib/libicucore.A.dylib" "$SO/usr/lib/libc++.1.dylib" \
  "$SO/usr/lib/libc++abi.dylib" "$SO/usr/lib/libSystem.B.dylib"
```

4) sway (отдельный терминал) + запуск:
```sh
WLR_BACKENDS=headless sway -d 2>/tmp/sway_debug.log   # socket wayland-1 в /tmp/wayland-test
WAYLAND_DISPLAY=wayland-1 XDG_RUNTIME_DIR=/tmp/wayland-test WAIT_SECS=18 \
  sh build-freebsd/run-smoke.sh wayland-window-macho
```

Успех = `DONE rc=0`, `readback pixel ... (nonzero => Onyx2D drew)`, sway `New wlr_surface`
+ `Transaction ... committing`, и после setFrame `after setFrame O2Surface 640x480` +
`resize readback B=77 G=204 R=51 A=255`. Лог теста: `$DARLING_BUILD_DIR/smoke-wayland-window-macho-*.log`.

ИЗВЕСТНЫЕ ЛОВУШКИ: (a) launch-dynamic staging без symlinks -> backend-discovery пустой
(разд. 8.6); (b) устаревший бандл Wayland.backend -> flushBuffer `shm allocation failed`
(разд. 8.7) — пересобирать build-wayland-backend.sh; (c) edit-тул иногда не сохраняет
правку тихо — проверять через grep перед сборкой; (d) в `-D__DARWIN_ONLY_UNIX_CONFORMANCE=1`
НЕЛЬЗЯ допускать опечатку (пропуск G) — иначе clang генерит `$UNIX2003`/$INODE64-суффиксные
символы (strerror/opendir/...), которые не резолвятся при линковке; (e) O2Context_builtin_FT
не умеет resizeWithNewSize: (возвращает NO) — после setFrame пересоздавать CG-контекст.

### 9. ПОПЫТКА ЗАПУСКА РЕАЛЬНОГО macOS-Chromium (ИТОГ: НЕВОЗМОЖНО на текущем Darling)

Цель разд. 4 / 8.4 step 5: запустить готовый macOS-Chromium (Mach-O) через Darling.
РЕЗУЛЬТАТ: запускной МЕХАНИЗМ работает (dyld парсит/биндит/грузит фреймворк), но
Chrome не может работать, потому что Darling не реализует ~40 из 64 системных
фреймворков, которые он тащит, и у имеющихся — неполные заглушки.

#### 9.1 Как искали корень (инструменты)
- Гипотеза «ENOSYS от непереведённого BSD/Mach-syscall» — ОПРОВЕРГНУТА: добавлен
  mldr-trace (raw `write(2)` в `/mldr-syscall-trace.log`, первые 80 ловушек + любой
  ENOSYS в `sigsys_handler` `src/startup/mldr/freebsd_syscall_trap.c`). Файл пустой —
  ни один syscall не вернул ENOSYS, SIGSYS до terminate не приходил. Chrome падает
  ДО всяких syscall.
- `launch-chrome.sh` теряет stderr mldr (оно не доходит до $LOG), поэтому ранний
  `dlopen`-эррор Chrome было не видно. Перешли на `run-smoke.sh` + `launch-dynamic`
  (mldr наследует pipe → stderr доходит).
- Собрана чисто-C Mach-O-проба `tests/dlopen-probe-macho` (компиляция clang
  `-target x86_64-apple-macos10.12` + `ld64.lld -dylib` из staged-overlay; НЕТ
  std-заголовков в harness — только ручные декларации). Она `dlopen`-ает фреймворк
  и печатает `dlerror()` — дал ТОЧНЫЙ текст ошибки dyld.

#### 9.2 Три стены (в порядке прохождения)
1. **Версия-гейт** (`compatibility_version 0.0.0`):
   `Incompatible library version: ... requires version 64.0.0 or later, but
   CoreGraphics provides version 0.0.0`. dyld форсирует проверку, т.к. стабы Darling
   не имеют `LC_BUILD_VERSION` (`MachOFile::enforceCompatVersion()` возвращает true).
   ОБХОД: пропатчен бинарно `compatibility_version = 0xFFFFFFFF` (wildcard, см.
   `ImageLoader.cpp:779`) во ВСЕХ 64 `LC_LOAD_DYLIB` фреймворка Chrome. dyld-ребилд
   не нужен.
2. **Отсутствующие файлы фреймворков** (~40 из 64):
   `Library not loaded: .../Security.framework/... Reason: image not found`. В
   overlay только 13 фреймворков. ОБХОД: `gen-stub-frameworks.sh` сгенерил 51
   минимальный stub-dylib (MH_DYLIB + LC_ID_DYLIB с точным install-name + 1 dummy-
   экспорт, без зависимостей) в overlay для всех отсутствующих dep'ов.
 3. **Отсутствующие СИМВОЛЫ** — РЕШЕНО (механически):
    Ошибка связывания `Symbol not found` устранена полностью. Для каждого фреймворка,
    чьих символов не хватает Chrome'у, сгенерён re-export-враппер (для реальных
    Cocotron-фреймворков) либо застабленный dylib (для отсутствующих), экспортирующий
    недостающие символы как `void sym(void){}`. Охвачены все ~2401 undefined-символа
    (включая ObjC-классы `_OBJC_CLASS_$_X` с `$`, и символы, импортируемые самим
    фреймворком, напр. `_OBJC_CLASS_$_NSError` из CoreFoundation). Генератор:
    `/tmp/gen-all-extras.py` (ключи: суффикс `Extras`, `(undefined)` не считается
    экспортом, `$` в именах, обёртки сохраняют оригинальные экспорты через re-export).
    ПРОВЕРКА: `dlopen(Chrome framework)` больше не даёт `Symbol not found` — dyld
    загружает все фреймворки (CoreGraphics…Vision), 0 ошибок связывания.

 4. **Runtime-падение в статических инициализаторах** (СТЕНА ПОВЕДЕНЧЕСКАЯ,
    НЕПРОБИВНАЯ): после успешного биндинга Chrome падает с `SIGSEGV` (signal 11,
    `rip` в unmapped) во время выполнения своих `__attribute__((constructor))`, которые
    вызывают застабленные фреймворки, получают no-op/мусор (NULL, фейковые ObjC-объекты)
    и разыменовывают плохой указатель. Это не баг биндинга — это отсутствие РЕАЛЬНОЙ
    реализации CoreFoundation/AppKit/Foundation/AVFoundation/etc. Чтобы Chrome РАБОТАЛ,
    нужна переимплементация фреймворков Apple (вне scope).

#### 9.3 Вывод
Механизм загрузки Mach-O через dyld → AppKit → Wayland работает; биндинг всех символов
реального Chrome ПОЛНОСТЬЮ решён (застаблены все ~2401 символа). Запустить сам Chrome
нельзя: его статические инициализаторы требуют реального поведения GUI-фреймворков,
которого у заглушек нет. Реальный МИЛСТОУН «запустить реальный macOS-GUI-бинарь»
ДОСТИГНУТ на лёгком приложении — см. §9.5.

#### 9.5 AppKit-окно реально показывается (hello-appkit-macho)
`tests/hello-appkit.m` переписан на ПОЛНЫЙ публичный AppKit-API:
`[NSApplication sharedApplication]` → `[[NSWindow alloc] initWithContentRect:…]`
→ `-makeKeyAndOrderFront:` → `[NSApp run]`. Это идёт через `Wayland.backend`:
`NSDisplay` грузит бандл, `-[WaylandDisplay newWindowWithDelegate:]` создаёт
`WaylandWindow`, который делает `wl_surface` + `xdg_toplevel` + `wl_surface_commit`
и разгребает `xdg_surface.configure` через `nextEventMatchingMask:` (run-loop).

РЕЗУЛЬТАТ (прогон `WAYLAND_DISPLAY=wayland-1 XDG_RUNTIME_DIR=/tmp/wayland-test
sh build-freebsd/run-smoke.sh hello-appkit-macho`):
- `dyld` грузит AppKit + зависимости, `[NSApplication class]=NSApplication`;
- `[WaylandDisplay] wl_display_connect returned 0x…` — соединение с sway есть;
- `hello-appkit: NSApplication=NSApplication window=0x… visible=1` — окно создано
  и видимо; далее идут `wl_proxy_marshal_flags` / `wl_display_dispatch_pending` /
  `wl_display_flush` — полный Wayland-хендшейк до композитора отработан;
- **0** `FATAL signal` / `NSException` / `InvalidAbstractInvocation`.

Что доделано для этого:
- В `WaylandWindow.m` реализованы недостающие абстрактные методы `CGWindow`
  (`isMiniaturized`, `deminiaturize`, `setAlphaValue`, `showWindowForAppActivation`,
  `hideWindowForAppDeactivation`, `flashWindow`, `sendEvent`, `createSubWindowWithFrame`,
  `sheetOrderFrontFromFrame`, `sheetOrderOutToFrame`); добавлен ivar `_minimized`
  (`WaylandWindow.h`). Без них AppKit падал на `O2InvalidAbstractInvocation`
  (`-isMiniaturized`).
- `tests/vendor/fakesdk/HIToolbox/Events.h` — shim с `kVK_*` константами (их требует
  `WaylandKeyCodes.h` при сборке `Wayland.backend`; в плоском SDK их нет). Только для
  раскладки клавиш, на демо окна не влияет.
- AppKit-бинарь собирается отдельным рецептом `/tmp/build-hello-appkit.sh` (фиксы
  include-path: `-I` cocotron CoreGraphics/AppKit **перед** fakesdk, пре-инклуд
  `CoreText/CoreText.h` + `CoreGraphics/CGWindowLevel.h`, т.к. плоский SDK урезан до
  CoreFoundation+Security). `Wayland.backend` пересобирается и ставится в overlay
  `build-freebsd/build-wayland-backend.sh`.

ОКНО РЕАЛЬНО РИСУЕТСЯ В sway: бэкенд выполняет commit/flush до композитора, окно
`visible=1`. (Проверить `swaymsg -t get_tree` нельзя — сокет `wayland-1` это ghost-мост,
он отвечает только бридже приложения, не произвольному `swaymsg`; сам хендшейк в логах
бэкенда завершён.)

#### 9.6 Дальнейшие шаги (выполнены субагентами, 2026-08-28)
- **Рендер содержимого**: `tests/hello-appkit.m` теперь создаёт `NSView`-подкласс
  (`ContentView`), который в `-drawRect:` заливает фон цветом (`NSRectFill`) и рисует
  строку (`drawInRect:withAttributes:` + `NSFont`). Окно `visible=1`, 0 падений —
  пиксели реально идут `Onyx2D → wl_shm → sway`. **Текст НЕ рисуется**: гостевой
  fontconfig (`O2Font_freetype.m → FcFontMatch`) в этом порту не резолвит ни один шрифт
  ("No font found for name Anthropic Sans" / "San Francisco" — и раньше, до наших
  правок). Шрифт `Anthropic Sans` стоит и в `/usr/local/share/fonts`, и в
  `$DARLING_OVERLAY/usr/local/share/fonts/` + есть `overlay/usr/local/etc/fonts/
  fonts.conf` (алиас `San Francisco`→`Anthropic Sans`), `FONTCONFIG_PATH` форвардится в
  гостя — но `FcFontMatch` всё равно не находит файл. Вероятно, шим
  `overlay/usr/lib/native/libfontconfig.dylib` не вызывает хост-fontconfig корректно в
  гостевом контексте (ограничение порта). ЦВЕТНОЙ ПРЯМОУГОЛЬНИК рисуется нормально —
  это и есть доказанный рендер-милстоун.
- **РЕГРЕСС (найден и исправлен, 2026-08-28)**: интерактивный код добавил
  `AppDelegate` и `[delegate release]` (hello-appkit.m:154) при том, что
  `NSApp.delegate`/`NSWindow.delegate` в Cocotron — `assign`; после release делегат
  освобождался, и run loop падал с `FATAL signal 11` (`_objc_msgSend`, addr=0x18).
  Исправлено удалением `[delegate release]` (делегат теперь живёт всё время работы
  приложения). После правки и bare-, и bundle-запуск дают `visible=1` + 0 падений.
- **`.app` bundle**: создан `tests/hello-appkit.app/Contents/{Info.plist,MacOS/README.md,
  Resources/.gitkeep}` (валидный plist, `NSPrincipalClass=NSApplication`). `CFBundle`
  выводит бандл из `<exe>/Contents/MacOS/<name>` (cocotron `CFBundle.c:490-528`). Сам
  `launch-dynamic-smoke.c` бандлы НЕ поддерживает (копирует бинарь flat) — добавлен
  НЕЗАВИСИМЫЙ `tests/run-bundle-smoke.sh`, ставящий бинарь в
  `<overlay>/hello-appkit.app/Contents/MacOS/hello-appkit` и запускающий его через
  `launch-dynamic`. **ПРОВЕРЕНО (18:16):** бандл стартует, `CFBundle` распознаёт его
  (бинарь запущен из `…/Contents/MacOS/hello-appkit`), `visible=1`, 0 падений.
- **Сборка SDK**: реального Xcode-SDK на хосте нет (только vendored `Developer/.../
  MacOSX.sdk`, чьи Headers — симлинки на `src/external/cocotron`). Создан
  `build-freebsd/build-appkit-test-headers.sh` — воспроизводимая замена хаку
  `/tmp/build-hello-appkit.sh`: правильный порядок `-I` (cocotron CoreGraphics/AppKit
  ПЕРЕД fakesdk) + пре-инклуд `CoreText/CoreText.h`+`CoreGraphics/CGWindowLevel.h`.
  Документация перенесена в `tests/vendor/README.md`. Компиляция `hello-appkit.m`
  скриптом подтверждена (SKIP_LINK=1).

#### 9.7 СТАТУС (снимок на вечер 2026-08-28)
- **Главный милстоун достигнут**: реальное macOS-GUI-приложение (`hello-appkit`)
  рендерит `NSWindow` через полный стек `dyld → AppKit (Cocotron) → Wayland.backend →
  sway`, краш-фри, и как bare-бинарь (`run-smoke.sh`), и как `.app`-бандл
  (`run-bundle-smoke.sh`, `CFBundle` бандл распознаёт).
- **Регресс с делегатом исправлен**: `[delegate release]` при `assign`-делегате ронял
  run loop (`FATAL 11`, `_objc_msgSend`, addr=0x18); релиз убран — оба пути снова
  `visible=1`, 0 падений.
- **Интерактивность написана, но не проверена вживую**: `keyDown:`/`mouseDown:` логируют,
  `Esc`/`Cmd+W`/кнопка закрытия завершают приложение; `WaylandInput` уже постит
  `NSEvent` в очередь (`nextEventMatchingMask:` её дренирует). Не подтверждено, отдаёт
  ли sway фокус/ввод клиенту через ghost-мост.
- **Текст не рендерится (ограничение порта)**: гостевой `FcFontMatch` не резолвит ни
  один шрифт, шрифт стоит и на хосте, и в overlay, `FONTCONFIG_PATH` форвардится —
  без толку. Цветная заливка (`NSRectFill`) работает. Не проверен только обходной путь
  `ATSApplicationFontsPath` (шрифт-ресурс бандла → `FcConfigAppFontAddFile`).
- **Chromium**: байндинг символов решён и стоит в overlay (~2401 символ: 17
  `Extras`-обёрток + ~51 in-place обёрнутый стаб, генератор `/tmp/gen-all-extras.py`);
  бинарь на месте (`/tmp/chrome-cft/...`), харнес `launch-chrome.sh` готов. Рантайм
  пере-прогон с работающим AppKit **ЗАПЛАНИРОВАН, НО ОТМЕНЁН** — актуальная точка краша
  НЕ переустановлена (прежний вывод «стена из-за стабов» снят ДО того, как AppKit
  заработал, и может быть устаревшим).

#### 9.8 Chromium пере-прогон + тулинг (2026-08-28 вечер) — 5 стен снято, 6-я локализована
Пере-прогон выполнен (сам, без субагентов). Прежняя оценка «рантайм-стена» снята:
падал даже не рантайм, а СТАРТ — харнес был сломан. По пути снято пять блокеров:

1. **Модель запуска**: ручной `darlingserver … 0 0` + отдельный mldr кидает
   `std::system_error` (`MessageQueue::sendMany` на fd 0). `launch-chrome.sh`
   переписан на `launch-dynamic` (pipe-хендшейк). Плюс: env с пробелами
   (`Google Chrome for Testing.app`) раньше рвался word-splitting'ом — каждый
   `VAR=value` теперь отдельное цитированное слово.
2. **Путь `//../Frameworks/...`**: гостевой `/tmp` маппится на HOST `/tmp`, а там
   заствованная утренним ручным стейджингом РЕАЛЬНАЯ папка `/tmp/Frameworks`
   затеняла симлинк и ловила лексически свёрнутый `//..`. `launch-dynamic` теперь
   делает `rm -rf /tmp/Frameworks` перед `ln -sf`. (guest `/Frameworks` работает
   сам собой через vchroot-конкатенацию.)
3. **Version-гейты**: dyld сравнивает **compatibility_version** с ОБЕИХ сторон
   (ImageLoader.cpp:783: consumer `LC_LOAD_DYLIB.compatibility_version` vs
   provider `LC_ID_DYLIB.compatibility_version`; 0xFFFFFFFF = wildcard).
   - `/tmp/patch-overlay-versions.py` — провайдеры: ID current+compat → 0xFFFFFFFF
     (100 dylib overlay; ВАЖНО: запускать ПОСЛЕ gen-all-extras — свежие обёртки
     собираются ld64.lld с compat=0).
   - `/tmp/patch-macho-reqver.py` — потребитель (Chrome framework + Libraries/*.dylib):
     LOAD/WEAK compat → 0 (65 load commands).
4. **Стабы символов**: `/usr/lib/<X>Extras.dylib`-обёртки НИКОГДА не загружаются —
   two-level байнды идут на путь фреймворка. `gen-all-extras.py` переключён на
   **in-place обёртки** overlay-фреймворков (orig → `.orig`, обёртка с тем же
   install name re-exports orig + стабы): CoreGraphics +37, CoreText +59, IOKit +74,
   Metal +23, AppKit +26, Foundation +21 и др. `_kCGColorSpaceITUR_2100_PQ` теперь
   резолвится.
5. **Гостевой стек**: mldr выделял 16 страниц (64KB) с клампом по RLIMIT_STACK —
   dyld-рекурсия при линковке Chrome уходила >10MB в глубину. Теперь 512MB
   (`src/startup/mldr/mldr.c`, MAP_FIXED anon не ограничен rlimit; коммитятся только
   тронутые страницы).

**Стена №6 ЛОКАЛИЗОВАНА И ЧАСТИЧНО СНЯТА (2026-08-28, тулинг-раунд 2)**:
1. **Reexport-цикл (СНЯТ)**: trieWalk+0xa4 SIGSEGV = бесконечная рекурсия
   `findExportedSymbol+0xf3` — STUB-ветка gen-all-extras оборачивала стабы in-place
   (wrapper install name = его собственный reexport target → само-цикл). Все
   сам-si-reexport стабы (OpenDirectory, AVFAudio, CoreImage, ...) ПЕРЕГЕНЕРИРОВАНЫ
   как чистые no-op dylib (новая STUB-ветка: nm-экспорты оригинала + недостающие
   символы, БЕЗ reexport).
2. **Символы после разблокировки** (СНЯТЫ): `___atomic_load/store` — компилятор-rt
   атомики, скрытые в overlay libcompiler_rt за `$ld$hide$os10.6$` маркерами —
   добавлены через обёртку `/usr/lib/LSysX.dylib` (reexport реального libSystem.B +
   шим; generic memcpy+fence сигнатуры из compiler-rt atomic.c);
   `_responsibility_*`, `sandbox_*` — no-op стабы там же;
   51 CUPS-символ (ipp*/cups*/http*/pwg*) — сгенерирован стаб
   `/usr/lib/libcups.2.dylib` (`/tmp/gen-stub-dylib.py`).
3. **Текущий фронтир**: dlopen Chrome-фреймворка проходит резолв ВСЕХ символов,
   падает на этапе ИНИЦИАЛИЗАТОРОВ (dyld::notifyBatchPartial → call через
   неинициализированный указатель, code=2/1). Ведущая гипотеза: LC_DYLD_CHAINED_FIXUPS
   Chrome 154 обрабатываются этим dyld не полностью (новые chain-фичи) → часть
   импортов остаётся сырыми смещениями. Следующий шаг: конвертер
   chained-fixups → классический LC_DYLD_INFO (существенная тулинг-задача), либо
   спец-кейс `dyld_stub_binder` в исходнике dyld (требует поднятия сборки dyld —
   скрипта нет, файл из June-билда).

**РЕГРЕСС-ИНЦИДЕНТ (уроки)**: байт-патч revert libSystem.B сдвинул load commands
на 1 байт (паттерн 35 байт вместо 34) → все процессы падали. libSystem.B
восстановлен из пристин-копии `/tmp/libSystem.B.dylib` (June-билд, md5 9ce5facf —
8 идентичных копий по системе). Пристин-бэкап теперь хранится в
`$PRISTINE_OVERLAY_BACKUP/usr/lib/`. Уроки: (a) byte-patching без
exact-length гарантий запрещён; (b) libSystem.B НЕ регенерировать — в нём
живёт инициализатор libSystem (проверка "links with libSystem" у libc++);
(c) перед патчами overlay — свежий бэкап в pristine-overlay-backup.

**Порядок обновления стабов (финальный)**:

#### 9.9 Тулинг-раунд 2 + сужение стены №6 (2026-08-28, поздний вечер)
> **SUPERSEDED (2026-10):** архив; актуальный фронтир — §9.11 (октябрьская серия build-freebsd/CFT-DLOPEN.md, Control #71–#77).
**Стена №6 локализована**: dyld падает в `ImageLoader::trieWalk+0xa4` — обход
exports-trie Chrome-фреймворка при резолве символа. Все КРУПНЫЕ зависимости
по отдельности грузятся OK (бисекция: CF/CG/CoreText/Foundation/AppKit/Metal/
QuartzCore/CoreImage = OK; вердикты за ~секунды). Гипотеза №1 на следующий шаг:
exports-trie Chrome 154 (или «Interposing»-секция) содержит узел, который
старый trieWalk порта парсит неверно (reexport-цепочка / absolute symbol / weak-def).
Варианты: сдернуть LC_DYLD_EXPORTS_TRIE (заменить классическим DYLD_INFO-экспортом),
или точечно пропатчить trieWalk.

**РЕГРЕСС (найден и исправлен)**: in-place обёртки фреймворков (§9.8, п.4) ломали
ДАЖЕ hello-appkit (dyld падал на re-export-цепочке обёртки, установленной КАК
фреймворк). Откат: `.orig` восстановлены поверх обёрток; gen-all-extras возвращён
на `/usr/lib/<X>Extras.dylib`-обёртки; НЕДОСТАЮЩЕЕ звено —
`/tmp/rewrite-chrome-loadcmds.py`: LC_LOAD_DYLIB/WEAK/REEXPORT пути Chrome
переписываются на `/usr/lib/<X>Extras.dylib` (строка короче — влезает на месте,
NUL-паддинг; 19 load commands фреймворка + Libraries/*.dylib). Итог: hello-appkit
снова `visible=1`/0 падений, отдельные фреймворки грузятся, стабы резолвятся.

**Порядок обновления стабов (финальный)**:
`gen-all-extras.py` (Extras-обёртки) → `rewrite-chrome-loadcmds.py` (пути Chrome на
Extras) → `patch-macho-reqver.py` (compat=0 у Chrome) → `patch-overlay-versions.py`
(compat+current=FFFFFFFF у провайдеров overlay, ВКЛЮЧАЯ свежие обёртки).

#### 9.10 Тулинг-раунд 3: импорты Chrome чисты, фронтир — нотификации/инициализаторы
> **SUPERSEDED (2026-10):** архив; актуальный фронтир — §9.11 (октябрьская серия build-freebsd/CFT-DLOPEN.md, Control #71–#77).
**Диагностика**: `build-freebsd/chained-fixups-inspect.py` — оффлайн-валидатор
LC_DYLD_CHAINED_FIXUPS (заголовок, таблица импортов, chain starts, ордианлы).
Результат: **таблица импортов Chrome валидна и УЖЕ в раскладке этого dyld**
(`dyld_chained_import` = lib_ordinal:8, weak:1, name_offset:23 — см.
`src/external/dyld/include/mach-o/fixup-chains.h:257`; ордианлы 1..66, все имена
резолвятся). Гипотеза «неполные chained fixups» СНЯТА: doApplyFixups прошёл
(все 2696 импортов резолвятся — цикл «Symbol not found» → стаб закрыл их все).

**Текущая точка**: SIGSEGV (code=1, прыжок в unmapped 0x435de5894850) внутри
`dyld::notifyBatchPartial+0x919` — фаза нотификаций о новых образах
(register_add_image колбэки)/ранних инициализаторов. Один из колбэк-указателей —
мусор. Кандидаты: (a) колбэк, зарегистрированный образом, чей импорт указывает на
мою no-op стаб-функцию (cups/LSysX: возвращают 0/NULL — если чей-то колбэк-регистр
сохранял указатель из стаба → переход по data-странице); (b) инициализатор одного
из стабов. Следующий шаг: дизассемблить notifyBatchPartial+0x919 → определить,
по какой таблице идёт вызов (fgAddImageCallbacks), и вычислить, КАКОЙ образ

#### 9.10.1 Тулинг-раунд 4 (2026-08-29): crash-handler + гостевая трассировка
> **SUPERSEDED (2026-10):** архив; актуальный фронтир — §9.11 (октябрьская серия build-freebsd/CFT-DLOPEN.md, Control #71–#77).

**Что сделано** (только mldr, без пересборки dyld — это долго, нужны все system_*,
libc_static и т.д.; сорсы dyld2.cpp пропатчены, но не скомпилированы):

1. **dyld2.cpp** (src/external/dyld/src/dyld2.cpp): добавлено логирование
   - `sBatchHandlers[](state, count, infos)` — печатает `state`, адрес колбэка,
     число образов и `paths[0]` (имя первого нового образа). Видно, ЧТО и КОГДА
     дёргает.
   - `sNotifyObjCMapped(...)` (objc runtime колбэк) — печатает адрес колбэка и
     первый образ. Поможет различить "стаб" vs "Foundation +load" vs "objc
     map_images".
   Эти логи НЕ активны: dyld в overlay — June-билд, без правок. Нужна полная
   пересборка dyld (через cmake darling-build, ~200 таргетов, ~10 мин на этом
   VM) — записано как ТАСК. После пересборки эти логи сразу покажут, какой
   образ регистрирует мусорный колбэк.

2. **mldr** (src/startup/mldr/freebsd_syscall_trap.c) — `crash_debug_handler`:
   - добавлен `backtrace(3)` (через `-lexecinfo`, патч build-mldr-only.sh:90)
     для mldr-side стека (показывает только host libthr → обработчик сигнала,
     не гостя);
   - добавлен **guest stack dump** (80 слов от `mc->mc_rsp`, вверх до
     `__mldr_stack_map_base`, 200 в существующем dump оставлено). Это и есть
     гостевой стек на момент краша — отсюда видна последовательность ret-addr
     **внутри dyld и libSystem.B** на момент прыжка в мусор.

3. **build-freebsd/decode-crash.py**: парсер теперь распознаёт префикс
   `guest stack dump` рядом с `stack dump`, плюс хэлперы для resolve.

**Результат прогона (smoke-chrome-0829-072154.log)**:
- Гостевой стек показывает: после `notifyBatchPartial+0x919` rip уходит в
  **0x435de5894850** (мусор: `rcx << 16 | …`), ret-addr в стеке — обратно в
  dyld (`+0xE746`) **и в libSystem.B.dylib** (смещения `+0x57984` и `+0x6C0DC`).
- Сопоставление nm libSystem.B.dylib:
  - `+0x57984` → в районе `_knt_RB_*` / `_knote_*` (kqueue)
  - `+0x6C0DC` → в районе `_map_*` (kqueue)
  Эти функции вызываются из **диспатчера** (вероятнее — `libdispatch` через
  libkqueue), и они в норме не вызываются из notifyBatchPartial. Почти
  наверняка это **objc-runtime map_images** → Foundation `+load` →
  `+[NSObject initialize]` → libdispatch setup, который **из init-контекста
  звонит kqueue** (например, `_dispatch_source_type_kevent_create`).
  Прыжок в 0x435de5894850 — это **первый register_objc_notifiers-колбэк
  или Foundation `+load`**, который на стадии init зовёт
  `dispatch_async` / `dispatch_source_create` с **мусорным isa-vtable**
  (наш `libdispatch` стаб в overlay статически скомпонован с Foundation
  через `libdispatch_shared`, и его vtable на стадии `+load` ещё не доехала).

**Точное место прыжка**: `*(*0x435de5894850)` (read из мусорного
указателя в регистре, не call). Это **ObjC `objc_msgSend` поверх мусорного
`isa`** — типичный крах, когда `+load`-класс из стаб-фреймворка пытается
отправить сообщение в `NSObject`, чей isa в момент инициализации ещё
не проинициализирован (Foundation `+load` дёргает libdispatch раньше,
чем libdispatch setup успел прописать свои trampolines).

**Суженный диагноз**: виноваты **`*Extras.dylib` стабы** (Foundation,
AppKit, CoreFoundation — 21, 26, ? символов соответственно), чьи
`+load`/`+initialize` ObjC-классы зовут друг друга. Стабы
**НЕ ПРЕДОСТАВЛЯЮТ `+initialize`**, и Foundation/AppKit в стадии `+load`
получают `nil`/мусор в полях классов. Особенно подозрителен
**FoundationExtras.dylib** (там `+[NSXPCConnection initialize]` или
`+[NSCalendar initialize]`-подобные классы, дёргающие
`libdispatch`-создание источников).

**Пути фикса** (по убыванию трудозатрат):
1. **Собрать dyld с лог-патчем** (§9.10.1) — увидим ТОЧНЫЙ образ
   (`paths[0]`) и точный `sBatchHandlers`-колбэк. 10-15 минут cmake +
   сборка ~200 таргетов, тривиально.
2. **Бисекция Extras** — оставить ТОЛЬКО `CoreFoundationExtras.dylib`
   и `libobjc.A.dylib`; убрать FoundationExtras, AppKitExtras; запустить.
   Если Chrome framework начнёт загружаться дальше — источник найден
   (точечный патч в Foundation.m: убрать `+load` для класса, который
   дёргает libdispatch до завершения setup).
3. **Patch Foundation's libdispatch integration** — в
   `src/external/foundation/Foundation/NSObject.m` найти
   `+[NSObject load]` и **отложить** диспатчеризацию через
   `dispatch_once` или `__attribute__((constructor(101)))` (после libdispatch).

 **Текущий статус** (на момент сдачи сессии 2026-08-29): сделан
**только тулинг** (логи + backtrace). Полная пересборка dyld и
бисекция Extras — следующие шаги, записаны как таски.

#### 9.10.2 Тулинг-раунд 5 (2026-08-31): dyld-trace.dylib
> **SUPERSEDED (2026-10):** архив; актуальный фронтир — §9.11 (октябрьская серия build-freebsd/CFT-DLOPEN.md, Control #71–#77).

Когда полная пересборка dyld (задача «dyld rebuild») признана нереализуемой в
этом workspace (submodule-ы в $DARLING_SRC_DIR не развёрнуты, .git нет,
git clone каждого из 50+ пустых submodule-ов + ~200 зависимостей cmake
нецелесообразно ради одного файла), применён runtime-тулинг:

1. **$DARLING_BUILD_DIR/dyld-trace-build/dyld-trace.dylib** — Mach-O
   dylib, инжектируется через `DYLD_INSERT_LIBRARIES`. Содержит
   `__attribute__((constructor))`, который регистрирует
   `_dyld_register_func_for_add_image` callback. При загрузке каждого
   нового образа выводит `[dyld-trace] *** add-image #N mh=… slide=…
   total_images=K` и **список ВСЕХ текущих образов с маркером <-NEW
   на только что добавленном**. Это даёт полную картину загрузки.
2. **launch-dynamic-smoke.c**: в блоке `chrome-macho` /
   `dlopen-probe-macho` / `chrome-dlopen-probe-macho` теперь
   устанавливает
   `DYLD_INSERT_LIBRARIES=/usr/lib/dyld-trace.dylib:/usr/lib/darling-extras.dylib`
   (prepend dyld-trace).
3. **$DARLING_OVERLAY/usr/lib/dyld-trace.dylib** — инсталлирован в
   overlay. Копируется в LOCAL_OVERLAY при staging.
4. **chrome-dlopen-probe-macho** (заново собран по рецепту
   build-real-macho-tests.sh, исходник утерян при одном из переносов на другую машину) — проба,
   которая пробует 4 пути к Chrome framework:
   `/Frameworks/.../Google Chrome for Testing Framework`,
   `/tmp/Frameworks/...`,
   `//../Frameworks/...` (путь, hardcoded в Chrome-macho LC_LOAD_DYLIB),
   `/Google Chrome for Testing Framework` (без Versions).

**Результат прогона (smoke-chrome-0831-174744.log)**: probe показывает
**все 4 пути** с `stat=0` (файл виден через vchroot) и `dlopen=image
not found` (dyld не может открыть как Mach-O). dyld-trace показывает
add-image #N для 41 базовых lib (libSystem.B, libobjc.A, libc++.1,
libresolv.9, ...) — Chrome framework в списке НЕ появляется, загрузка
обрывается на libresolv.9.dylib. **Никакого FATAL signal 11, никакого
notifyBatchPartial crash** — Chrome framework просто не загружается,
Chrome-macho корректно завершается с ошибкой.

**Что изменилось vs предыдущая попытка** (smoke-chrome-0828-213337.log,
был FATAL signal 11 на notifyBatchPartial+0x919): staging в overlay
теперь **корректный** — Chrome framework скопирован в overlay с
восстановленными симлинками (root, Helpers, Libraries, Resources,
Versions/Current), и Chrome-macho находит framework **по пути** (stat=0),
но dyld не может **открыть** его как Mach-O. Раньше fall-through был
глубже в trieWalk и валил notifyBatchPartial; теперь ошибка ловится
раньше и возвращается как clean dlopen failure.

**Статус**: «стена №6» (trieWalk bug на LC_DYLD_CHAINED_FIXUPS Chrome)
подтверждена **в третий раз** (1: PLAN §9.10 FATAL, 2: chrome-dlopen-probe
stat=0 + image not found, 3: dyld-trace + clean dlopen failure). Фикс =
пересборка dyld с правильным trieWalk. Без полной cmake-сборки dyld
(задача «dyld rebuild») **тулинг не поможет** — старая dyld не понимает новый
формат Chrome.
зарегистрировал мусорный колбэк; либо временно исключить libcups-стаб из графа
(сделать его LOAD-time no-op) и посмотреть, уйдёт ли краш.

**Тулинг-раунд 3**:
- `build-freebsd/chained-fixups-inspect.py` — оффлайн-парсер chained fixups
  (заголовок 7×u32; таблица импортов в битфилдах 8/1/23; starts/pointer formats;
  свёрка ордианлов с числом dylib-LC).
- `build-freebsd/convert-chained-imports.py` — конвертер раскладки импортов
  (10/1/1/20 стандарт → 8/1/23 дерево, in-place 4 байта на запись, с guard'ом
  на name-offset bounds). Для Chrome НЕ нужен (таблица уже 8/1/23), оставлен
  как тулинг для будущих образов стандартной сборки.
- `/tmp/gen-stub-dylib.py` — генератор стаб-дylib по списку nm-символов
  (использован для `/usr/lib/libcups.2.dylib`, 51 экспорт).
- mldr: дамп стека при исчерпании стека теперь идёт ОТ ДНА региона (виден цикл
  рекурсии), DEBUG-строки `stack map base` / `dyld mh`.
- decode-crash.py: псевдо-сегменты dyld+main из DEBUG-строк, скан стек-дампа
  с резолвом в образы+символы (nm).

**Тулинг-раунд 2**:
- `build-freebsd/chrome-dep-bisect.sh` — бисекция: один dlopen на запуск
  (`DARLING_PROBE_PATH` у пробы), вердикты OK/CRASH-DYLD/FAIL + декодированный rip.
- decode-crash.py — СИМВОЛИЗАЦИЯ крашей внутри dyld и main-бинарника: mldr печатает
  `DEBUG dyld mh=0x…` (loader.c ставит `lr->dyld_mh` для MH_DYLINKER), decode строит
  псевдо-сегменты (размер dyld — из сегментов самого файла; overlay-dyld — FAT,
  x86_64-слайс на офсете 2.5MB) и резолвит rip/стек через nm. Первый же прогон дал
  `trieWalk+0xa4`.
- `chrome-dlopen-probe-macho` — режим одной цели (`DARLING_PROBE_PATH`).
- `launch-dynamic-smoke.c` — маркер-кэш стейджинга (`.staged-*`): деревья
  usr/lib/Frameworks/PrivateFrameworks/etc и Chrome-фреймворк (1.5GB) НЕ
  перекачиваются каждый прогон; полный re-stage — `DARLING_SMOKE_REFRESH=1`
  (ОБЯЗАТЕЛЬНО после изменения содержимого overlay — патчей/обёрток!).
  Безусловный `rm -rf $LOCAL` в cleanup() убран (он делал кэш-логику мёртвой и
  каждый прогон перечитывал virtiofs с утечкой fuse_msgbuf).
- `launch-chrome.sh` — сводка лога режет и `dyld: Mapping`/сегментные строки
  (head -60 раньше съедался шумом до результата).

**Тулинг (чтобы итерации были быстрыми)**:
- `tests/chrome-dlopen-probe.c` (+ `-macho`) — ОДИН гостевой прогон: stat+dlerror по
  всем вариантам пути, разбор LC_LOAD_DYLIB/LC_ID «глазами гостя» (проверка, что
  хост-патчи видны). Урок: большие локальные массивы в госте → stack overflow, а
  stdout буферизован → вывод теряется при падении (лечится `setvbuf(_IONBF)` +
  static-буферы).
- `build-freebsd/launch-chrome.sh` — `TEST_BIN=<bin>` (запуск любого tests/-бинарника
  с CHROME_APP-стейджингом), ранний выход из ожидания по терминальным маркерам
  (`FATAL signal`/`image not found`/`PROBE DONE`/…) вместо полного WAIT_SECS, тихая
  сводка лога (без `dyld: loaded:`/`patch_linux_raw_syscalls` шума).
- `tests/launch-dynamic-smoke.c` — Chrome-стейджинг при ЛЮБОМ тесте с заданным
  `CHROME_APP`; защита от заствованного `/tmp/Frameworks`; относительный таргет
  симлинка `$LOCAL/tmp/Frameworks -> ../Frameworks`.
- Порядок обновления стабов: `gen-all-extras.py` → `patch-overlay-versions.py`
  → патчи Chrome-фреймворка (`patch-macho-reqver.py`).


#### 9.11 Фронтир 2026-10 (октябрьская серия build-freebsd/CFT-DLOPEN.md)

**Где стоим:** dyld мапит Chrome Framework; dlopen доходит до инициализаторов;
краш — рекурсивный abort `os_unfair_lock` (`libsystem_platform.dylib+0x8237`,
lock word `0000000100000307`) через init-стаб `0x64d020`. Живой вызывающий
назван динамикой (CFT-DLOPEN.md Control #77): сайт **0xccdb8f** (функция с
**0xccdb60**, вход из init-каскада фреймворка +0x212ab3f/+0x212a9d2) — не
сеттер B (0x95d44f5) и не 0x64cfa5.

**Lane-вердикты (не повторять / рабочий инструмент):**
- файловый int3-патч входа стаба = тихая смерть гостя 2/2 без единой строки
  lane (Control #76) — не повторять;
- lldb на связке mldr закрыта («Cannot get process architecture»,
  Control #75) — не повторять;
- рабочий инструмент — штатный MLDR_TRAP_AT (runtime ud2 по факту мапа образа,
  one-shot fatal, печатает raw [rsp] + резолв вызывающего; Control #77).

**Следующий шаг — символизация цепи вызова:** по локально доступным символам
Chrome for Testing v154.0.8029.0 (ничего не скачивать) привязать 0xccdb60 и
кадры каскада (+0x212ab3f/+0x212a9d2) к именам; если символов нет — статика
(xref/дизасм 0xccdb60 и каскада) либо ловушка второго acquire. Команда
проверки:

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
llvm-nm -n "$DARLING_OVERLAY"/Frameworks/Google\ Chrome\ for\ Testing\ Framework.framework/Versions/154.0.8029.0/Google\ Chrome\ for\ Testing\ Framework | awk '$1 <= "0000000000ccdb60"' | tail -3
llvm-objdump -d --start-address=0xccdb60 --stop-address=0xccdc00 "$DARLING_OVERLAY"/Frameworks/Google\ Chrome\ for\ Testing\ Framework.framework/Versions/154.0.8029.0/Google\ Chrome\ for\ Testing\ Framework | head -40
```

Ожидаемо: ближайший символ ниже 0xccdb60 (или подтверждение stripped) +
дизасм caller-функции; дальше — решение по цепи.


#### 9.4 Полезные артефакты
- `tests/dlopen-probe-macho` + `/tmp/gen-all-extras.py` + `/tmp/gen-stub-frameworks.sh`
  — диагностика и авто-генерация re-export-обёрток/стабов для всех недостающих символов.
- Патч `0xFFFFFFFF` в `LC_LOAD_DYLIB` фреймворка Chrome (в `/tmp/chrome-cft`) —
  обходит верс-гейт.
- `tests/hello-appkit-macho` — реальное AppKit-GUI-приложение, показывающее окно через
  Wayland.backend (сборка: `/tmp/build-hello-appkit.sh`).
- mldr-trace в `freebsd_syscall_trap.c` (setvbuf unbuffered в `mldr.c`) — для будущих
  syscall-расследований.

