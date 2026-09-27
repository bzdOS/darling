# SPEC: нативные Mach-O-обёртки над host ELF-библиотеками (`/usr/lib/native/`)

Статус: **`build-freebsd/build-native-wrappers.sh` НИ РАЗУ НЕ ЗАПУСКАЛСЯ.**
Написан по чтению CMake-файлов и `wrapgen.cpp`, без доступа к FreeBSD-машине
для проверки. Ничего здесь не означает "работает" — только "так устроен код,
и вот отсюда скрипт скорее всего сломается". См. §5 "Риски" в конце.

## 0. Зачем это нужно (контекст: первая GUI-сборка)

`build-gui.sh` (см. `docs/SPEC-gui-build.md`) первым делом успешно
компилирует Onyx2D целиком, но падает на линковке:

```
ld64.lld: error: /usr/local/lib/libdispatch.so: dylib missing LC_ID_DYLIB load command
ld64.lld: error: /usr/local/lib/libfontconfig.so: unhandled file type
ld64.lld: error: cannot open .../staged-overlay/usr/lib/libz.dylib
```

Причина — `ld64.lld`, Mach-O линкер, получает на вход host ELF `.so` файлы
напрямую (через `pkg-config --libs freetype2 fontconfig libpng zlib` и
голые `-ltiff -ljpeg -lgif` в `build-gui.sh`, см. `docs/SPEC-gui-build.md`
§3 "Native-библиотеки — не то, чем кажутся" и §6.1). Mach-O линкер не умеет
читать ELF `.so` — отсюда `unhandled file type`. У Darling для этого есть
отдельный механизм: маленькие настоящие Mach-O dylib-"обёртки" в
`/usr/lib/native/`, которые НЕ содержат кода библиотеки, а на старте
процесса `dlopen()`-ят настоящую host `.so` и перенаправляют туда каждый
символ.

## 1. Механизм — как устроено (все ссылки — file:line)

### 1.1. Что описывает CMake (полная сборка, никогда не запускалась целиком)

`src/native/CMakeLists.txt:1-26` — под `if (COMPONENT_gui)` вызывает
`wrap_elf(<name> <host-soname> [destination])` для 15 библиотек:
FreeType, jpeg, png, tiff, gif, EGL, fontconfig, X11, Xext, XRandR, Xcursor,
xkbfile, cairo, dbus, GL, GLU. **Заметь: тут нет `wrap_elf(z ...)` — zlib
не входит в этот механизм** (см. §2 ниже).

`cmake/wrap_elf.cmake:8-33` — функция `wrap_elf(name elfname [destination])`:
1. (строки 9-19) генерирует `${name}.c` командой `wrapgen <elfname>
   <name>.c <name>_vars.h` — `wrapgen` сам является зависимостью
   (`DEPENDS wrapgen`), то есть тулза уже должна быть собрана.
2. (строки 21-25) `destination` по умолчанию — `usr/lib/native`.
3. (строка 27) `DYLIB_INSTALL_NAME` = `/${destination}/lib${name}.dylib` —
   то есть install-name, зашиваемый в load command обёртки, ВСЕГДА
   `/usr/lib/native/lib<Name>.dylib` (с оригинальным регистром `<Name>`,
   не host-именем: `FreeType`, не `freetype`).
4. (строка 28) `include_directories(.../src/startup/mldr/elfcalls)` — вот
   откуда обёртка берёт `elfcalls.h`.
5. (строка 29) `add_darling_library(${name} SHARED ${name}.c)` — собирает
   как ОБЫЧНУЮ darling-библиотеку (то есть через весь `use_ld64()` —
   см. §1.4 про риск R1).
6. (строка 30) `target_link_libraries(${name} PRIVATE system)` — линкуется
   против гигантского `add_circular(system FAT ...)` таргета
   (`src/external/libsystem/CMakeLists.txt:35`), который реэкспортирует
   ~35 под-библиотек всего Darling-рантайма — это тот самый
   `libSystem.B.dylib`.
7. (строка 32) `install(TARGETS ${name} DESTINATION
   libexec/darling/${destination})`.

### 1.2. `wrapgen` — что он реально генерирует

`src/libelfloader/wrapgen/wrapgen.cpp` — **host-инструмент** (обычный ELF,
компилируется обычным clang++/g++ под FreeBSD, не Mach-O; собирается как
`add_executable(wrapgen wrapgen.cpp)` в
`src/libelfloader/wrapgen/CMakeLists.txt:10`, никакого Mach-O-тулчейна не
требует).

- `main()` (строки 31-100): принимает имя ELF-библиотеки. Если это не
  прямой путь (`access(..., R_OK) == -1`), пробует `dlopen(elfLibrary,
  RTLD_LAZY | RTLD_LOCAL)` + `dlinfo(handle, RTLD_DI_LINKMAP, &lm)`, чтобы
  узнать у host-линкера, где реально лежит файл (строки 52-79) — то есть
  можно передавать голое имя `libjpeg.so`, как и делает
  `src/native/CMakeLists.txt`.
- `parse_elf()` (строки 102-252): мапит ELF64 в память, ищет
  `PT_DYNAMIC` → `DT_SONAME` (для последующего `dlopen()` в рантайме) и
  проходит `.dynsym`, собирая **экспортируемые** (`STB_GLOBAL`,
  `STV_DEFAULT`, не `SHN_UNDEF`) `STT_FUNC` в `symbols` и `STT_OBJECT` в
  `vars` (строки 213-227).
- `generate_wrapper()` (строки 254-279) — для каждого символа-функции:
  ```c
  void* <sym>() {
      __asm__(".symbol_resolver _<sym>");
      return _elfcalls->dlsym_fatal(lib_handle, "<sym>");
  }
  ```
  `.symbol_resolver` — Mach-O-специфичная директива ассемблера, помечающая
  функцию как **ifunc-подобный резолвер**: dyld при первом связывании
  вызывает эту функцию РОВНО ОДИН РАЗ, берёт возвращённый адрес и
  привязывает вызывающий код напрямую к нему — то есть после первого
  вызова обёртка больше не участвует, реальный ELF-код вызывается напрямую.
  Плюс конструктор/деструктор (строки 260-267):
  ```c
  __attribute__((constructor)) static void initializer() {
      lib_handle = _elfcalls->dlopen_fatal(__elfname);
  }
  __attribute__((destructor)) static void destructor() {
      _elfcalls->dlclose_fatal(lib_handle);
  }
  ```
  где `__elfname` — распакованный `DT_SONAME` (строка 276-278), не то имя,
  что было передано `wrapgen` в командной строке.
- Переменные (`generate_var_wrappers`, строки 281-301) — тот же трюк через
  `#define <var> (*__elf_get_<var>())`, для будущего использования (в
  нашем минимальном наборе из 5 библиотек ни одна переменная не
  используется Onyx2D напрямую, но механизм существует).

### 1.3. `_elfcalls` — кто на самом деле выполняет `dlopen`

`extern struct elf_calls* _elfcalls;` (объявлено в каждой сгенерированной
обёртке, `wrapgen.cpp:257`) — это НЕ статический символ самой обёртки.
Реальное определение — глобальная переменная в
`src/external/xnu/darling/src/libsystem_kernel/emulation/src/other/mach/lkm.c:29`
(`struct elf_calls* _elfcalls;`), часть `libsystem_kernel`, которая
реэкспортируется в `libSystem.B.dylib` тем же `add_circular(system ...)`
из §1.1.

Таблица `struct elf_calls` (`src/startup/mldr/elfcalls/elfcalls.h:17-74`)
заполняется **не в Mach-O-мире вообще**, а в `mldr` (host-side ELF-загрузчик
Mach-O-бинарников) функцией `elfcalls_make()`
(`src/startup/mldr/elfcalls/elfcalls.c:87-129`):
```c
static void* dlopen_fatal(const char* name) {
    void* rv = dlopen(name, RTLD_LAZY);   // <-- ОБЫЧНЫЙ host libc dlopen()
    if (!rv) { fprintf(...); abort(); }
    return rv;
}
...
calls->dlopen_fatal = dlopen_fatal;
calls->dlsym_fatal  = dlsym_fatal;
```
(`elfcalls.c:17-31,94-96`). Указатель на заполненную таблицу передаётся
гостевому процессу через переменную окружения `elf_calls=<pointer>`
(`src/startup/mldr/stack.c:80,153`), а Mach-O-сторона достаёт его через
`__dyld_get_elfcalls` (`src/external/dyld/src/dyldAPIs.cpp:128,264`).

**Итог механизма:** обёртка не содержит НИ ОДНОЙ строчки кода реальной
библиотеки. `dlopen_fatal(__elfname)` в конструкторе обёртки — это в
буквальном смысле вызов host-`dlopen("libjpeg.so.8")` (или что там окажется
в `DT_SONAME`), выполненный из процесса `mldr`, использующий ОБЫЧНЫЕ
правила поиска host-динамического линкера FreeBSD (`/etc/ld-elf.so.conf`,
`LD_LIBRARY_PATH`, rpath и т.д.). Обёртка — это просто таблица
перенаправлений, заполняемая один раз при старте процесса.

### 1.4. Момент линковки vs момент рантайма — `-dylib_file`

`cmake/use_ld64.cmake:165-176` (внутри `use_ld64()`, который дергает КАЖДАЯ
`add_darling_library()`) содержит для каждого native-компонента:
```
-Wl,-dylib_file,/usr/lib/native/libjpeg.dylib:${CMAKE_BINARY_DIR}/src/native/libjpeg.dylib
```
Это два РАЗНЫХ пути с одинаковым именем файла:
- `/usr/lib/native/libjpeg.dylib` (до `:`) — install-name/runtime-path,
  зашиваемый в load command **приложения** (Onyx2D и т.д.), то, что `mldr`
  будет резолвить в рантайме относительно overlay/prefix.
- `${CMAKE_BINARY_DIR}/.../libjpeg.dylib` (после `:`) — файл, который
  `ld64.lld` **читает прямо сейчас**, на этапе линковки самого приложения,
  чтобы узнать таблицу экспортируемых символов (то есть саму обёртку,
  собранную по §1.1-1.2).

Это специально: `-dylib_file` разводит "что записано в бинарнике как путь
для рантайма" и "что линкер использует как источник символов сейчас" — без
этого при линковке приложения пришлось бы указывать реальный путь внутри
overlay/prefix, а не абстрактный `/usr/lib/native/...`.

**`build-native-wrappers.sh` этот шаг (4) НЕ выполняет** — он только
производит и устанавливает сами файлы `/usr/lib/native/lib*.dylib` в
overlay. Прописать `-Wl,-dylib_file,...` при линковке Onyx2D в
`build-gui.sh` — отдельное изменение, которое не входит в этот скрипт (см.
R5 ниже и явное ограничение задачи "не редактировать существующие файлы").

## 2. Почему zlib НЕ входит в набор обёрток

Задача, породившая этот документ, перечисляла zlib среди целей ("минимум:
zlib, jpeg, png, tiff, freetype, fontconfig"), но при чтении кода
выяснилось, что **`src/native/CMakeLists.txt` не содержит `wrap_elf` для
zlib вообще** — сравни список в §1.1 (15 записей, ни одна не `z`/`zlib`).

Вместо этого `cmake/use_ld64.cmake:135`:
```
-Wl,-dylib_file,/usr/lib/libz.1.dylib:${CMAKE_BINARY_DIR}/src/external/zlib/libz.1.dylib
```
— путь `/usr/lib/libz.1.dylib`, НЕ `/usr/lib/native/...`, и файл после `:`
собирается из **настоящих исходников** `src/external/zlib/zlib/*.c`
(`src/external/zlib/CMakeLists.txt:1-40+`, реальный `adler32.c`,
`deflate.c`, `inflate.c` и т.д., компилируемые `-target x86_64-apple-macos`
как обычный Mach-O-код, не обёртка).

Это буквально совпадает с одной из трёх ошибок из задачи:
```
ld64.lld: error: cannot open .../staged-overlay/usr/lib/libz.dylib
```
— `build-gui.sh` уже ищет именно `${OVERLAY}/usr/lib/libz.dylib` (см. его
`cp ... || echo "WARN: no libz.dylib in overlay"`), но ничего никогда не
кладёт этот файл туда, потому что **никто ещё не собрал
`src/external/zlib` из исходников**. Это отдельная, ещё не написанная
задача (шестой `build-freebsd/*.sh`, по образцу того, как
`build-real-macho-tests.sh` собирает `Foundation.dylib` из
`src/external/foundation`) — не то же самое, что "завернуть host
`libz.so`". Оборачивать host zlib вместо этого значило бы тихо подменить
предусмотренный архитектурой механизм на другой рабочий — прямо то, что
`CLAUDE.md` запрещает ("не подменять тихо").

`build-native-wrappers.sh` поэтому **не трогает zlib** и явно объясняет
почему в собственных комментариях (см. блок "WHY ZLIB IS DELIBERATELY NOT
HERE").

## 3. Что реально есть в overlay сейчас

```
$ ls $DARLING_OVERLAY/usr/lib/native/
libfuse.dylib
```
(проверено 2026-08-26, `ls -la`). Единственная существующая обёртка —
`libfuse.dylib` (85740 байт); ни `jpeg`, ни `png`, ни `tiff`, ни
`FreeType`, ни `fontconfig`, ни `EGL`, ни X11-семейство ещё никогда не
собирались этим портом.

## 4. Порядок сборки

```
1. build-darlingserver.sh          (host ELF, независимо)
2. build-mldr-only.sh              (host ELF, нужен для запуска гостевых Mach-O)
3. build-real-macho-tests.sh       (создаёт staged overlay + libSystem.B.dylib —
                                     ОБЯЗАТЕЛЬНАЯ предпосылка: build-native-
                                     wrappers.sh падает в preflight без
                                     libSystem.B.dylib в $DARLING_OVERLAY)
4. build-native-wrappers.sh        (этот скрипт — производит usr/lib/native/*.dylib)
5. build-gui.sh                    (Onyx2D и далее — СЕЙЧАС ещё не читает
                                     вывод шага 4, см. R5)
```

`build-native-wrappers.sh` устанавливает свой вывод прямо в
`$DARLING_OVERLAY/usr/lib/native/`, но **`build-gui.sh` его пока не
использует** — Onyx2D там до сих пор линкуется голыми `-ljpeg -lpng -ltiff
-lgif` плюс `pkg-config --libs fontconfig freetype2`. Кто-то должен
отдельно поправить `build-gui.sh`, чтобы вместо этого он передавал
`ld64.lld` файлы из `$DARLING_OVERLAY/usr/lib/native/lib{jpeg,png,tiff,
FreeType,fontconfig}.dylib` напрямую как linker-inputs (как этот же скрипт
уже делает для `libSystem.B.dylib`/`CoreFoundation`/`Foundation`) — это
задание прямо запрещало редактировать существующие файлы, так что
`build-gui.sh` остаётся нетронутым.

## 5. ЧЕСТНО: риски, по убыванию вероятности провала

Скрипт **не запускался**. Ничего ниже не проверено на живом FreeBSD.

**R1 — обход `target_link_libraries(${name} PRIVATE system)`.**
В настоящей CMake-сборке каждая обёртка линкуется против ЦЕЛОГО
`add_circular(system FAT ...)` (`src/external/libsystem/CMakeLists.txt:35`,
~35 реэкспортируемых под-библиотек, весь Darling-рантайм). Этот скрипт
вместо этого линкует только против уже собранного, застейдженного
`$DARLING_OVERLAY/usr/lib/libSystem.B.dylib` — тот же трюк, что уже
использует `build-real-macho-tests.sh`/`build-gui.sh` для Foundation/
CoreFoundation. Предположение: `_elfcalls` (единственный символ, реально
нужный обёртке из libSystem) уже экспортирован в том, что лежит в overlay
СЕЙЧАС. Не проверено — если overlay был собран до того, как `lkm.c`
завёл `_elfcalls` как публично экспортируемый символ, линковка обёртки
упадёт с undefined symbol на этапе `ld64.lld`, и первопричина будет не в
этом скрипте, а в устаревшем overlay.

**R2 — `.symbol_resolver` и интегрированный ассемблер clang.**
`src/CMakeLists.txt:88` **закомментирован**:
`#add_subdirectory(external/cctools-port/cctools/as)` — родной "as" из
`cctools-port`, который явно обрабатывает `.symbol_resolver`
(`src/external/cctools-port/cctools/as/read.c`), в этом дереве НЕ
собирается и никуда не подключается (`CMAKE_ASM_COMPILER` тоже нигде не
переопределён на него). Значит вся официальная CMake-сборка полагается на
то, что **встроенный (integrated) ассемблер самого clang/LLVM** понимает
`.symbol_resolver` для Mach-O-таргетов. У LLVM такая поддержка
исторически существует (`DarwinAsmParser`), но какая именно версия LLVM
стоит в `pkg install llvm` на этой FreeBSD 15.1 и понимает ли её
ассемблер эту директиву — **не проверялось**. Если нет — компиляция
сгенерированного `.c` из wrapgen свалится на первой же строке
`__asm__(".symbol_resolver ...")`, и в дереве НЕТ готового fallback'а на
альтернативный ассемблер (единственный кандидат закомментирован). Скрипт
явно объясняет это подозреваемой причиной в сообщении об ошибке при сбое
компиляции.

**R3 — `wrapgen` находит host-библиотеку по голому имени.**
`wrapgen.cpp:52-79` полагается на `dlopen("libjpeg.so", RTLD_LAZY|
RTLD_LOCAL)` + `dlinfo(RTLD_DI_LINKMAP)`, то есть на то, что каждая из
пяти host-библиотек реально доступна host-линкеру под НЕВЕРСИОНИРОВАННЫМ
именем (`libjpeg.so`, а не только `libjpeg.so.8`). Обычно это дают
`-dev`/`-devel`-пакеты (символическая ссылка без версии), но конкретные
имена пакетов/наличие такой ссылки на FreeBSD 15.1 pkg для jpeg-turbo/
png/tiff/freetype2/fontconfig **не проверялось** — `build-gui.sh`'ный
preflight (`docs/SPEC-gui-build.md` §4) проверяет только заголовки
(`pkg-config --exists` / поиск `.h`), не факт наличия невесионированного
`.so`. Если только версионированный файл есть — `wrapgen` упадёт с
"Cannot load libX.so: ..." и остановит именно эту обёртку (скрипт не
пытается угадывать версионированные имена сам).

**R4 — рассинхрон версий macOS deployment target.**
Этот скрипт использует `-target x86_64-apple-macos10.10` (совпадает с
`build-gui.sh`'ными флагами для Onyx2D — согласованно с тем, ЧТО будет эту
обёртку использовать), но `libSystem.B.dylib`, застейдженный в overlay,
собирался (судя по `build-real-macho-tests.sh:75`) под `10.12` для
Foundation-related тестов, а полное CMake-дерево целится в `11.0`
(`CMakeLists.txt:120`, `CMAKE_OSX_DEPLOYMENT_TARGET`). Три разных версии
одновременно. Ломает ли это конкретно `-syslibroot`-линковку такой
маленькой обёртки (в отличие от рантайм-семантики) — не проверялось;
`docs/SPEC-gui-build.md` §6.6 уже фиксирует тот же нерешённый вопрос для
Onyx2D/Foundation.

**R5 — `build-gui.sh` не подключает вывод этого скрипта.**
См. §4. Даже если `build-native-wrappers.sh` отработает идеально и
положит все 5 `.dylib` в `$DARLING_OVERLAY/usr/lib/native/`, следующий
прогон `build-gui.sh` **всё равно упадёт на тех же ошибках** — он не
знает про эти файлы, пока кто-то не поменяет его вызов `ld64.lld` для
Onyx2D (заменить `-ljpeg -lpng -ltiff -lgif` +
`pkg-config --libs fontconfig freetype2` на прямые пути к этим 5
`.dylib`). Эта задача прямо запретила редактировать существующие файлы,
поэтому `build-gui.sh` не тронут.

**R6 — `wrapgen` собирается компилятором хоста, а не Mach-O-тулчейном —
но какой именно `clang++`/`g++` стоит в `PATH` на 185, не проверялось.**
Скрипт использует голый `clang++ -std=c++14`; если на дев-VM установлен
только Mach-O-ориентированный `clang` без обычного host-таргета по
умолчанию (маловероятно, но не исключено при нестандартной настройке
`pkg install llvm`), сборка `wrapgen` сама может свалиться раньше, чем
дойдёт до генерации обёрток.

**R7 — `libpng`/`libtiff` DT_SONAME может не совпадать с тем, что ожидает
Onyx2D во время выполнения.** `wrapgen` берёт `DT_SONAME` из самого ELF
файла (например, реально `libpng16.so.16`, а не `libpng.so`) и именно эту
строку зашивает в обёртку как аргумент рантаймового `dlopen_fatal()`
(`wrapgen.cpp:276-278`). Значит итоговая обёртка `libpng.dylib` в
рантайме попытается `dlopen("libpng16.so.16")` — это ПРАВИЛЬНО и
ожидаемо (так и задумано), но означает, что при апгрейде host-пакета
`png` на другой major-ABI сама обёртка потребует пересборки — не
проверялось, насколько это релевантно для текущей версии пакета на 185.

## 6. Что НЕ проверялось (отдельный явный список)

- Скрипт `build-native-wrappers.sh` ни разу не запускался — ни сборка
  `wrapgen`, ни генерация `.c`, ни компиляция, ни линковка, ни установка
  в overlay.
- Не проверялось, поддерживает ли LLVM-ассемблер, поставляемый текущим
  `pkg install llvm` на FreeBSD 15.1, директиву `.symbol_resolver` для
  Mach-O (R2).
- Не проверялось, доступны ли `libjpeg.so`/`libpng.so`/`libtiff.so`/
  `libfreetype.so`/`libfontconfig.so` под НЕВЕРСИОНИРОВАННЫМИ именами
  через `dlopen()` на 185 (R3).
- Не проверялось, экспортирует ли `libSystem.B.dylib`, уже
  застейдженный в `$DARLING_OVERLAY`, символ `_elfcalls` публично (R1).
- Не проверялось содержимое `$DARLING_OVERLAY/usr/lib/native/` ни на
  какой другой машине, кроме факта (через `ls`, приведённый в §3) — что
  там лежит только `libfuse.dylib`.
- Не проверялось, компилируется ли и линкуется ли вообще
  `src/libelfloader/wrapgen/wrapgen.cpp` без ошибок на FreeBSD 15.1 —
  прочитан только исходный код.
- Изменения в `build-gui.sh`, нужные, чтобы реально использовать вывод
  этого скрипта (R5), не сделаны и не проверены — само задание запретило
  редактировать существующие файлы.
