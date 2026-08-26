# SPEC: сборка GUI-ветки darling-freebsd (Cocotron AppKit)

Статус: **скрипт `build-freebsd/build-gui.sh` НИ РАЗУ НЕ ЗАПУСКАЛСЯ.** Написан
по чтению `CMakeLists.txt` и соседних `build-freebsd/*.sh` без доступа к
FreeBSD-машине для проверки. Ничего в этом документе не означает "работает" —
только "так устроен код, и вот отсюда скрипт скорее всего сломается".

## 1. Почему GUI-ветка никогда не собиралась

`src/CMakeLists.txt:360-371` — `COMPONENT_gui` подключает
`add_subdirectory(external/cocotron)` (Cocotron: CoreGraphics, Onyx2D,
AppKit, CoreText, Cocoa, CoreData, QuartzCore), плюс CoreAudio/cups/pboard.
Ни один существующий скрипт в `build-freebsd/` не собирает проект через
общий CMake — `build-darlingserver.sh`, `build-mldr-only.sh`,
`build-real-macho-tests.sh` все генерируют **свой** изолированный
`CMakeLists.txt` (первые два) или вообще обходятся без CMake, вызывая
`clang`/`ld64.lld` построчно (третий). `COMPONENT_gui=ON` никогда не
передавался ни одному из них — грепом по репо `grep -rn COMPONENT_gui` вне
`src/CMakeLists.txt` ничего не находится.

## 2. Модель существующих скриптов (на чём построен `build-gui.sh`)

| Скрипт | Что строит | Метод | Целевые бинари |
|---|---|---|---|
| `build-darlingserver.sh` | darlingserver | генерирует свой `CMakeLists.txt`, обычный host-компилятор (C++17, FreeBSD ELF) | **host-нативный** ELF |
| `build-mldr-only.sh` | mldr | то же самое, C11, host-компилятор | **host-нативный** ELF |
| `build-real-macho-tests.sh` | hello-cctools/sqlite3/hello-objc/hello-cf/Foundation.dylib | **без CMake вообще** — `clang -target x86_64-apple-macos10.12` + `ld64.lld -syslibroot <staged overlay>`, файлы извлекаются построчным `python3`-парсингом `set(foundation_sources ...)` из `src/external/foundation/CMakeLists.txt` | **гостевые Mach-O** (запускаются под mldr) |

Разница принципиальна: darlingserver/mldr — это FreeBSD-процессы, которые сами
эмулируют часть XNU (обычная сборка `cmake && make`). AppKit/CoreGraphics/
Onyx2D — это **код, который будет исполняться как гостевой Mach-O под mldr**,
точно как Foundation.dylib. Поэтому `build-gui.sh` списан с
`build-real-macho-tests.sh`: свой `clang`/`ld64.lld`, свой staged overlay,
свой python-парсинг `set(<Component>_sources ...)` из `CMakeLists.txt`
Cocotron-компонентов — а не попытка дёрнуть `add_framework()` из
`cmake/darling_framework.cmake`.

### Почему не через `cmake/darling_framework.cmake` напрямую

`add_framework()` (`cmake/darling_framework.cmake:9-133`) и
`add_darling_library()` (`cmake/darling_lib.cmake:10-61`) — это тяжёлая
машинерия полного дерева: `-nostdlib`, привязка к `cctools-port`'ному `lipo`
(`add_dependencies(${name} lipo)`), `use_ld64()`
(`cmake/use_ld64.cmake:1-40+`), который для каждой из ~30 системных dylib
(`libsystem_kernel`, `libdyld`, `libunwind`, …) передаёт
`-Wl,-dylib_file,<install-path>:<путь до build-дерева>` — то есть **предполагает,
что вся эта библиотека уже собирается в том же дереве прямо сейчас**. У нас
собранного дерева нет — есть только то, что уже лежит готовыми `.dylib` в
`$DARLING_OVERLAY` (staged overlay `build-real-macho-tests.sh` использует
именно так, через `-syslibroot`, без `-dylib_file` вообще). Тянуть
`add_framework()`/`use_ld64()` в изолированный скрипт значило бы либо
перетащить туда пол-CMake-дерева, либо переписать их — то есть перестать
следовать принятому в репо шаблону "маленький целевой скрипт". `build-gui.sh`
вместо этого повторяет путь Foundation: линковка против готового overlay.

## 3. Что нужно Cocotron AppKit (по чтению `CMakeLists.txt`)

`src/external/cocotron/AppKit/CMakeLists.txt`:
- `find_package(X11 REQUIRED)` + Xrandr/Xkb/Xcursor/Xext/Xkbfile
  (строки 25-40) — **обязательны для CMake configure**, даже если бы мы не
  собирали X11-бэкенд: `find_package(X11 REQUIRED)` фейлит весь configure
  без этих пакетов.
- `find_package(Freetype REQUIRED)`, `find_package(OpenGL REQUIRED)` (42-46)
- `pkg_check_modules(PC_CAIRO cairo)`, `pkg_check_modules(PC_FONTCONFIG
  fontconfig)` (49-50) — не `REQUIRED`, но заголовки реально используются.
- `AppKit_sources` (82-459) — 375 файлов `.m` (см. также
  `find src/external/cocotron/AppKit -iname '*.m' | wc -l`), плюс ресурсы
  (`.tiff`/`.nib`, строки 468-526) — которые сборка дилиба не требует, но
  которые реальному запуску AppKit-приложения понадобятся (шрифты панелей,
  цветовые пикеры и т.п.) и которые этот скрипт не устанавливает.
- `DEPENDENCIES` линковки AppKit (541-558): `objc system CoreFoundation
  Foundation Onyx2D CoreText CoreData OpenGL QuartzCore CoreGraphics ImageIO
  FreeType fontconfig jpeg png tiff CoreServices`.
- `add_backend(X11 ...)` (597-625) — единственный существующий backend,
  линкуется против `X11 XRandR Xcursor fontconfig xkbfile`.

`CoreGraphics/CMakeLists.txt`: `DEPENDENCIES objc system CoreFoundation
Foundation Onyx2D GL IOKit` (131-138) + свой **отдельный** X11-бэкенд
(`CoreGraphics/X11.backend/CMakeLists.txt`, отличается от AppKit-шного —
`CGSConnectionX11.m`/`CGSWindowX11.m`/`CGSSurfaceX11.m`).

`Onyx2D/CMakeLists.txt`: `find_package(Freetype/PNG/TIFF/JPEG/GIF REQUIRED)`
(17-21), `DEPENDENCIES objc system CoreFoundation Foundation z FreeType
fontconfig jpeg png tiff gif` (154-164) — **никаких Apple-фреймворков кроме
CoreFoundation/Foundation**. Это единственный компонент, у которого весь
список зависимостей уже либо есть в overlay, либо является обычной host/
native библиотекой.

### Проверено на хосте (2026-08-26): чего физически нет в overlay

```
$ ls /path/to/darling-overlay/System/Library/Frameworks/
CoreFoundation.framework  DirectoryService.framework  Foundation.framework
LDAP.framework  SystemConfiguration.framework
```

Нет: **IOKit, CoreText, CoreData, QuartzCore, ImageIO, CoreServices.** Ни
разу не собирались этим портом. Значит:
- Onyx2D.dylib — единственный реалистичный кандидат на успешную линковку.
- CoreGraphics.dylib **не слинкуется** — не хватает IOKit.framework.
- AppKit.dylib **не слинкуется** — не хватает пяти фреймворков разом.

`build-gui.sh` проверяет это явно (`require_frameworks`) и падает с понятным
сообщением ДО попытки линковки, а не после кучи undefined-symbol ошибок от
`ld64.lld`.

### Заголовки: framework-include/ — не то, чем кажется

`src/CMakeLists.txt:132` добавляет `${CMAKE_SOURCE_DIR}/framework-include` в
глобальный include path — но `framework-include/AppKit` (как и
`CoreGraphics`, `Onyx2D`, ...) сам является **симлинком**
(`framework-include/AppKit -> ../Developer/Platforms/MacOSX.platform/
Developer/SDKs/MacOSX.sdk/System/Library/Frameworks/AppKit.framework/
Headers`), который через ещё один уровень симлинков резолвится в
`src/external/cocotron/AppKit/include/AppKit`. `tests/vendor/README.md`
документирует, что `readlink()` через virtiofs-mount (`/path/to/workspace`)
сломан и возвращает `EIO` — та же причина, по которой
`build-real-macho-tests.sh` использует **распакованный на хосте**
`macosx-sdk-flat.tar.gz`, а не живой SDK-дерево. `build-gui.sh` по той же
причине не трогает `framework-include/` вообще, а `-I` указывает прямо в
`src/external/cocotron/<Component>/include/` — то есть туда же, куда в итоге
резолвится симлинк, но без похода через сам симлинк.

### Native-библиотеки — не то, чем кажутся

`cmake/use_ld64.cmake:169-172` показывает, как реальная полная сборка линкует
`jpeg`/`png`/`tiff`: НЕ напрямую в host `.so`, а через
`-Wl,-dylib_file,/usr/lib/native/libjpeg.dylib:<build-tree>/src/native/
libjpeg.dylib` — то есть через маленькие Mach-O "native"-обёртки
(`src/native/`, `add_subdirectory(native)` в `src/CMakeLists.txt:414`),
которые сами при рантайме прыгают в реальную host-библиотеку. В overlay
сейчас есть только `usr/lib/native/libfuse.dylib` — обёрток для jpeg/png/
tiff/gif/GL нет вообще. `build-gui.sh` **не** пытается воспроизвести эту
обёрточную схему (это отдельный, ещё не собиравшийся компонент) — вместо
этого он линкует Onyx2D через голое `-ljpeg -lpng -ltiff -lgif` в надежде,
что `ld64.lld -syslibroot ${STAGED_OVERLAY}` при отсутствии
`/usr/lib/libjpeg.dylib` в staged overlay как-то найдёт host `.so` — **это
не проверено и по описанной выше архитектуре, вероятно, неверно**. Смотри
§5 "Что почти наверняка не соберётся" — это первый и главный кандидат.

## 4. Чего не хватает в системе (пакеты)

FreeBSD `pkg install`, по `find_package`/`pkg_check_modules` вызовам в
Cocotron `CMakeLists.txt`:

```
pkg install cmake llvm epoll-shim   # уже требуются другими build-freebsd/*.sh
pkg install pkgconf                 # pkg_check_modules
pkg install freetype2               # find_package(Freetype REQUIRED) — Onyx2D + AppKit
pkg install png tiff jpeg-turbo giflib   # find_package(PNG/TIFF/JPEG/GIF REQUIRED) — Onyx2D
pkg install fontconfig              # pkg_check_modules(PC_FONTCONFIG) — Onyx2D/CoreGraphics/AppKit
pkg install cairo                   # pkg_check_modules(PC_CAIRO) — CoreGraphics/AppKit (не REQUIRED, но используется)
pkg install mesa-libs               # find_package(OpenGL REQUIRED) — заголовки GL/gl.h
pkg install libX11 libXrandr libXcursor libXext libXkbfile  # find_package(X11 REQUIRED) — только для X11.backend
```

Для гипотетического Wayland backend (которого нет — см. §5):
```
pkg install wayland wayland-protocols libxkbcommon
```
плюс `wayland-scanner` (обычно ставится вместе с `wayland`) для генерации
`xdg-shell-client-protocol.h`/`.c` из `wayland-protocols`'ных `.xml` —
**ни один существующий Cocotron backend не генерирует protocol-заголовки
через `wayland-scanner`, потому что ни один Cocotron backend не Wayland.**

## 5. Порядок сборки относительно других скриптов

```
1. build-darlingserver.sh        (host ELF, независимо)
2. build-mldr-only.sh            (host ELF, нужен для запуска гостевых Mach-O)
3. build-real-macho-tests.sh     (создаёт staged overlay + Foundation.dylib —
                                   ОБЯЗАТЕЛЬНАЯ предпосылка для build-gui.sh:
                                   он падает в preflight без Foundation.dylib
                                   в $DARLING_OVERLAY)
4. build-gui.sh                  (новый, этот документ)
```

`build-gui.sh` не запускается сам по себе первым — как и
`build-real-macho-tests.sh`, он читает уже установленный `Foundation.dylib`
из `$DARLING_OVERLAY` вместо того, чтобы пересобирать его.

## 6. ЧЕСТНО: что почти наверняка не соберётся с первого раза, и почему

Скрипт **не запускался**. По убыванию вероятности провала:

1. **Onyx2D-линковка через голые `-ljpeg -lpng -ltiff -lgif`.** По §3
   ("Native-библиотеки — не то, чем кажутся") реальная архитектура порта
   ожидает Mach-O native-обёртки под `/usr/lib/native/`, которых нет.
   `ld64.lld -syslibroot <overlay>` может просто не найти host `.so` этим
   способом вообще — Mach-O линкер ищет `.dylib`, а не ELF `.so`, и не
   гарантированно умеет резолвить голый `-lname` в host-библиотеку так, как
   это делает обычный `cc`. Если это не работает — единственный компонент,
   у которого теоретически есть все зависимости, тоже не соберётся, и
   вообще ничего из этого скрипта не производит рабочий артефакт.
2. **CoreGraphics и AppKit гарантированно падают** на `require_frameworks`
   (IOKit; CoreText/CoreData/QuartzCore/ImageIO/CoreServices) — это не
   риск, это заведомый результат, проверенный по факту отсутствия файлов в
   overlay (§3). Чтобы продвинуться дальше, сначала нужно собрать (или хотя
   бы застабить) эти пять фреймворков — отдельная работа, не входящая в
   этот скрипт.
3. **`find_package(X11 REQUIRED)` в самом `AppKit/CMakeLists.txt`** — этот
   скрипт X11.backend не собирает вообще (обходит его питоновским
   извлечением только `AppKit_sources`, не `X11.backend`-файлов), но если
   бы кто-то захотел собрать X11-бэкенд тем же методом, `libX11`-заголовки
   должны быть на месте — не проверялось, есть ли они в `pkg` под тем же
   именем на FreeBSD 15.1, как на Linux.
4. **`extract_sources()`'ный regex** (`set\(VARNAME\n(.*?)\n\)`) хрупкий —
   рассчитан на ровно тот формат отступов, что в `AppKit_sources`/
   `CoreGraphics_sources`/`Onyx2D_sources` сейчас. Комментированные строки
   вида `# NSOpenGL/NSOpenGLDrawable.m` (AppKit/CMakeLists.txt:302)
   отфильтровываются по `#`, что должно работать, но не проверено на живом
   `python3` этой версии.
5. **Заголовочные пути AppKit** — `AK_FLAGS` в скрипте включает основные
   `*.subproj`-директории (`nib.subproj`, `NSMenu.subproj`, ...), но список
   `include_directories()` в `AppKit/CMakeLists.txt:52-80` мог быть
   скопирован не полностью — 375 файлов `.m` почти наверняка тянут
   `#include`/`#import` на что-то, чего в `AK_FLAGS` нет (`CoreText`
   заголовки в частности используются, но `CoreText/include` реального
   `.h`-дерева не проверялось на полноту).
6. **`-mmacosx-version-min=10.10` vs Foundation, собранный под 10.12**
   (`build-real-macho-tests.sh:75`) — версии смешаны между уже собранным
   `Foundation.dylib` и новым `Onyx2D`/`CoreGraphics`/`AppKit`. Не
   проверялось, ломает ли это `-syslibroot`-линковку или рантайм.
7. **Ресурсы AppKit** (`.nib`, `.tiff`, `StandardKeyBindings.keybindings`,
   `AppKit/CMakeLists.txt:468-526`) вообще не устанавливаются этим
   скриптом — только код. Даже если бы AppKit.dylib собрался и слинковался,
   реальный запуск (открытие панели шрифтов, цветового пикера, любого NIB)
   тут же упрётся в отсутствующие ресурсы.
8. **Wayland.backend — не "почти наверняка не соберётся", а гарантированно
   не соберётся: собирать нечего.** Скрипт падает на шаге 4 намеренно и
   сразу, без попытки. См. §7.

## 7. Про Wayland — главный содержательный вывод

**В дереве Cocotron нет ни одного файла с "Wayland" в имени** (проверено
`find src/external/cocotron -iname '*wayland*'` — пусто, `grep -rn wayland
src/external/cocotron` — пусто). Единственный backend и у AppKit
(`AppKit/X11.backend/`), и у CoreGraphics (`CoreGraphics/X11.backend/`) — это
X11: `X11Display.m`, `X11Window.m`, `X11SubWindow.m`, `X11Event.m`,
`X11Pasteboard.m`, `X11Cursor.m`, `X11KeySymToUCS.m` (AppKit) плюс
`CGSConnectionX11.m`, `CGSWindowX11.m`, `CGSSurfaceX11.m` (CoreGraphics),
все — обёртки над Xlib/XRandR/Xcursor/Xkbfile.

Это значит, что запрошенный в задаче "`Wayland.backend`-бандл" — не то, что
можно **собрать** этим или любым скриптом: это то, что нужно **написать**.
Объём работы сопоставим с написанием `X11.backend/`-файлов заново: реализовать
те же 7 Objective-C файлов AppKit-бэкенда (создание/показ/движение окна,
обработка ввода, курсор, буфер обмена) поверх `libwayland-client` +
сгенерированных `xdg-shell-client-protocol.h/.c` вместо Xlib-вызовов, плюс
эквивалент `CGSWindowX11.m`/`CGSSurfaceX11.m` на стороне CoreGraphics (там,
где рисуется в X-surface, нужен `wl_surface`/`wl_buffer` через `wl_shm`
или явный passthrough в уже существующий `wayland-tunnel`/Cage
компонент этого репозитория, к которому Cocotron сейчас не имеет вообще
никакого отношения). Это отдельная задача разработки, не входящая в
"собрать существующий код".

`build-gui.sh` фиксирует этот факт явной FATAL-остановкой на шаге 4, чтобы
никто не потратил время, ожидая, что скрипт молча создаст то, чего в
исходниках не существует.

## 8. Итоговая честная оценка

- Скрипт `build-freebsd/build-gui.sh` написан по образцу существующих
  (`build-real-macho-tests.sh`), но **ни разу не запускался** — нет доступа
  к FreeBSD-машине для проверки.
- Единственная стадия с реальным шансом собраться — Onyx2D, и то под
  вопросом (см. §6.1 про native-библиотеки).
- CoreGraphics и AppKit **заведомо** упрутся в отсутствующие фреймворки
  (IOKit; CoreText/CoreData/QuartzCore/ImageIO/CoreServices) — это не
  гипотеза, а проверенный факт по содержимому overlay на 2026-08-26.
- Wayland-бэкенда собрать нельзя в принципе, потому что его нет в
  исходниках — нужна отдельная задача на написание кода, не на сборку.
