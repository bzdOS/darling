# SPEC: сборка IOKit.framework для разлинковки CoreGraphics.dylib

Область: **только связка IOKit + CoreGraphics.** Onyx2D/CoreText/CoreData/
QuartzCore/ImageIO/CoreServices/OpenGL — не мой участок, см.
`docs/SPEC-gui-build.md` для общей картины Cocotron-сборки (метод
`build-freebsd/build-gui.sh`: свой `clang`/`ld64.lld` против staged overlay,
не общий CMake-граф — читать первым, этот документ его не повторяет).

Ничего не собрано и не проверено на FreeBSD-машине — весь документ построен
чтением исходников в рабочем дереве. Каждое утверждение — с `file:line`.

## 1. Состояние исходников

### 1.1 Три разных "IOKit" в дереве — не путать

| Путь | Что это | Состояние |
|---|---|---|
| `src/external/IOKitUser` | userspace-клиент IOKit (Apple `IOKitUser`, форк `darling-iokituser.git`) — то, что линкуется в `IOKit.framework` | **живой, полный submodule**, реально выкачан (`git submodule status` → `88927960...IOKitUser-1179.50.2-46-g8892796`, без `-` префикса) |
| `src/external/iokitd` | userspace-демон, который в даже официальном Darling отвечает на mach-запросы IOKitUser (`org.darlinghq.iokitd`) | **не инициализированный submodule**: `git submodule status` даёт `-5549ac4c...` (префикс `-` = не выкачан); `ls -la src/external/iokitd` — пустая директория, даже `.git`-файла нет |
| `src/external/xnu/iokit` | kernel-side IOKit (XNU C++ classes: `IOService.cpp`, `IOUserClient.cpp`, ...) | присутствует как часть submodule `xnu`, но это **kernel** код — не собирается и не нужен для userspace `IOKit.framework`/`iokitd`; чисто справочный, показывает, что реальные обработчики `io_service_get_matching_services` живут в kernel, которого у нас нет |

`.gitmodules:154-156` — `src/external/IOKitUser` → `../darling-iokituser.git`.
`.gitmodules:297-299` — `src/external/iokitd` → `../darling-iokitd.git`.

`framework-include/IOKit` — это **не пустышка**, это симлинк (как и соседние
фреймворки, см. `docs/SPEC-gui-build.md` §3 "Заголовки"): `framework-include/
IOKit -> ../Developer/Platforms/MacOSX.platform/.../MacOSX.sdk/.../IOKit.
framework/Headers`, который через ещё один уровень резолвится в
`src/external/IOKitUser/darling/include/IOKit/*`. Через `find -L` видно
реальные заголовки (`IOKitLib.h`, `IOService.h`, ...). Живой `readlink()`
через 9p ломается (см. тот же SPEC-gui-build.md §3) — при сборке нужно бить
`-I` прямо в `src/external/IOKitUser/darling/include`, а не в
`framework-include/IOKit`.

### 1.2 Уже подключено в CMake-граф (но требует `COMPONENT_gui`/`cli`)

`src/CMakeLists.txt:324` — `add_subdirectory(external/IOKitUser)` внутри
`if (COMPONENT_cli OR COMPONENT_dev_gui_common)` (строка 314). `src/CMakeLists.
txt:342-345` — `if (COMPONENT_iokitd) add_subdirectory(external/iokitd)
endif()` — этот блок **упадёт**, если кто-то включит `COMPONENT_iokitd`,
потому что `src/external/iokitd` пуст (нет `CMakeLists.txt`). Никто пока не
передавал `COMPONENT_iokitd=ON` — грепом по репо других упоминаний не найдено.

Ни один существующий `build-freebsd/*.sh` не строит через общий CMake-граф
(см. `docs/SPEC-gui-build.md` §1) — значит `add_subdirectory(external/
IOKitUser)` из `src/CMakeLists.txt:324` пока ни разу реально не исполнялся
для FreeBSD-таргета. `IOKit.framework` физически отсутствует в overlay
(`docs/SPEC-gui-build.md` §3, "Проверено на хосте 2026-08-26").

## 2. Что именно CoreGraphics берёт из IOKit — по коду, не предположению

Грепом `IOKit\|IOService\|io_service_t\|io_connect_t\|IOSurface\|IOGraphics\|
IODisplay` по всем `.m/.c/.h` в `src/external/cocotron/CoreGraphics/` (**весь**
каталог, не только верхний уровень) — ровно два файла:

1. `src/external/cocotron/CoreGraphics/CGDirectDisplay.m:24-25` — заголовки
   `#import <IOKit/graphics/IOGraphicsLib.h>` и `IOGraphicsTypes.h`.
   `CGDirectDisplay.m:394-421` — функция `CGDisplayIOServicePort()` вызывает:
   - `kIOMasterPortDefault` (`CGDirectDisplay.m:408`, читается как переменная)
   - `IOServiceMatching("IODisplayConnect")` (`:404`)
   - `IOServiceGetMatchingServices(kIOMasterPortDefault, matching, &iter)` (`:408`)
   - `IODisplayCreateInfoDictionary(serv, kIODisplayOnlyPreferredName)` (`:418`)
   Это **единственное** место в CoreGraphics, где IOKit реально вызывается
   (не просто заголовок). Функция используется только для получения
   человекочитаемого имени/EDID монитора — не для рисования.
2. `src/external/cocotron/CoreGraphics/include/CoreGraphics/CoreGraphicsPrivate.h:4`
   — `#include <IOKit/hidsystem/IOLLEvent.h>` — **только типы** (структуры
   низкоуровневых HID-событий из заголовка), нет вызовов функций из этого
   файла нигде в CoreGraphics — это чисто header-level зависимость, линковки
   не требует.

Разбор символов и где они реализованы в `IOKitUser`:

| Символ | Реализация | Что делает под капотом |
|---|---|---|
| `kIOMasterPortDefault` | `IOKitLib.c:112` — `const mach_port_t kIOMasterPortDefault = MACH_PORT_NULL;` | просто константа `0`, без побочных эффектов |
| `IOServiceMatching()` | `IOKitLib.c:1051-1056` | чистая CF-функция (`MakeOneStringProp`), **не трогает mach вообще** |
| `IOServiceGetMatchingServices()` | `IOKitLib.c:556-600+` | сериализует dict через `IOCFSerialize()`, затем реальный **mach RPC** (`io_service_get_matching_service_ool`/`io_service_get_matching_services_bin`, MIG-сгенерированные из `iokitmig.defs`) к порту `masterPort` |
| `__IOGetDefaultMasterPort()` / `IOMasterPort()` | `IOKitLib.c:115-124, 126-172` | при `#ifdef DARLING` (`IOKitLib.c:162-164`): `bootstrap_look_up(bootstrapPort, "org.darlinghq.iokitd", masterPort)` — **ищет живой bootstrap-сервис `iokitd`**, которого в дереве нет (см. §1.1) |
| `IODisplayCreateInfoDictionary()` | `graphics.subproj/IODisplayLib.c:1377-1382` | тонкая обёртка над `_IODisplayCreateInfoDictionary()`, которая тоже делает mach-запрос к `framebuffer` (io_service_t), т.е. зависит от того же `masterPort`/`iokitd` |
| `kIODisplayOnlyPreferredName` | `darling/include/IOKit/graphics/IOGraphicsLib.h:56` | это **enum-константа в заголовке** (`= 0x00000200`), не символ библиотеки — линковки не требует |

## 3. Полный список зависимостей CoreGraphics (CMakeLists.txt) и что уже есть

`src/external/cocotron/CoreGraphics/CMakeLists.txt:125-138`:
```
DEPENDENCIES
    objc
    system
    CoreFoundation
    Foundation
    Onyx2D
    GL
    IOKit
```
Состояние (см. `docs/SPEC-gui-build.md` §3, overlay-листинг):
- `objc`, `system` — обычные host/native, есть (`libobjc.A.dylib`,
  `libSystem.B.dylib` уже в overlay, использует `build-gui.sh`).
- `CoreFoundation`, `Foundation` — **есть** в overlay (единственные два
  Apple-фреймворка, что реально собраны).
- `Onyx2D` — **нет в overlay**, но чужой участок (собирается параллельно,
  зависимостей от IOKit не имеет — `Onyx2D/CMakeLists.txt:154-164`, только
  CF/Foundation/z/FreeType/fontconfig/jpeg/png/tiff/gif).
- `GL` — заголовки есть через `mesa-libs` (см. SPEC-gui-build.md §4), не мой
  участок.
- `IOKit` — **нет в overlay, предмет этого документа.**

## 4. X11-бэкенд CoreGraphics — отдельный таргет, не блокирует ядро

`src/external/cocotron/CoreGraphics/CMakeLists.txt:225` —
`add_subdirectory(X11.backend)`, но это **отдельная** цель
(`add_backend(X11 ...)`, функция определена на `:141-171`), собирается в
отдельный `.backend`-бандл (`X11_cgbackend`), а не линкуется в само
`CoreGraphics.dylib` (`add_framework(CoreGraphics ...)` на `:106-124` не
перечисляет `X11.backend`-исходники — только `CoreGraphics_sources` без
`X11.backend/*`). Значит: **CoreGraphics.dylib собирается без X11-бэкенда
в принципе** — бэкенд нужен только тому коду, который реально открывает
X11-соединение через `CGSConnectionX11.m`. Для `build-gui.sh`-подобного
ручного метода (`docs/SPEC-gui-build.md`) X11.backend можно смело пропускать
на этом этапе — символы `X11.backend/CGSConnectionX11.m` (`CGSConnectionX11.
h`) не входят в `CoreGraphics_sources` (`CMakeLists.txt:96-124`), поэтому их
отсутствие не создаёт undefined-symbol при линковке самого `CoreGraphics.
dylib`.

## 5. Насколько реален IOKit на FreeBSD — прямой ответ

**Реализация Apple `IOKitUser` — это клиент к реальному ядру XNU.**
`IOServiceGetMatchingServices()`/`IODisplayCreateInfoDictionary()` — не
чистые библиотечные функции, а mach RPC к kernel IOKit registry
(`is_io_service_get_matching_services` в `src/external/xnu/iokit/Kernel/
IOUserClient.cpp:2716`, который существует только *внутри* XNU-ядра). На
FreeBSD такого ядра нет и не будет — у нас FreeBSD kernel + Darling
duct-tape (частичная эмуляция mach/BSD-syscalls в userspace,
`src/external/darlingserver`), которая **не реализует IOKit device registry**
(`grep` по `src/external/darlingserver/src` на `io_service_get_matching_
service`/`IOMasterPort` — пусто; единственные совпадения — заголовки
duct-tape `xnu/osfmk/...` (декларации типов, не implementation) и
`kern/host.c` для `host_get_io_master`/`mach_host.defs`, что относится к
non-DARLING веткам кода, которые IOKitLib.c даже не берёт при `#ifdef
DARLING` — см. §2 таблицу).

Единственный реальный получатель этих запросов в архитектуре Darling —
демон `iokitd` (bootstrap-сервис `org.darlinghq.iokitd`), а его исходники —
**пустой неинициализированный submodule** (§1.1). Значит даже собранный
"честный" (полный apple-код) `IOKit.framework` на рантайме всегда будет
получать "нет сервиса" на `bootstrap_look_up` → `IOMasterPort()` вернёт
не-`KERN_SUCCESS` → `masterPort == MACH_PORT_NULL` → следующий mach-вызов с
`MACH_PORT_NULL` в качестве destination синхронно возвращает
`MACH_SEND_INVALID_DEST` (без зависания) → `CGDisplayIOServicePort()`
деградирует до `service = MACH_PORT_NULL` (`CGDirectDisplay.m:550-553`) —
**не падение, просто "имя монитора неизвестно"**. Для headless-стриминга
(вся текущая архитектура bsdOS — виртуальные дисплеи через
`cage --headless`, см. `CLAUDE.md` "Stream pipeline") это ничего не теряет:
реального физического монитора с EDID всё равно нет.

**Вывод: реализовывать `iokitd` и/или подключать mach MIG-цепочку
(`iokitmig.defs` → `build-mig` → `iokitmig{32,64}.c`) сейчас не нужно.**
Нужен ровно тот минимум, который даёт CoreGraphics слинковаться и не упасть
при вызове:

- `kIOMasterPortDefault` — тривиальная константа.
- `IOServiceMatching()` — чистая CF-функция, переносится как есть (не мach).
- `IOServiceGetMatchingServices()` — можно **не** тащить настоящий
  MIG/mach-путь: минимальная реализация, которая при `masterPort ==
  MACH_PORT_NULL` (или всегда, раз `iokitd` не существует) сразу
  `CFRelease(matching); *existing = MACH_PORT_NULL; return
  kIOReturnNoDevice;` — эквивалент того же деградировавшего поведения,
  без необходимости собирать `mig`-тулчейн и `iokitmig.defs` вообще.
- `IODisplayCreateInfoDictionary()` — минимальная реализация: `return NULL;`
  (или пустой `CFDictionaryCreate`) — вызывающий код (`CGDirectDisplay.m:418
  -421`) уже обрабатывает `NULL`/пустой `info` штатно (не разыменовывает
  без проверки — не проверялось построчно дальше `:421`, см. "Не проверено"
  ниже).
- Заголовки: **брать настоящие** `darling/include/IOKit/graphics/
  IOGraphicsLib.h`, `IOGraphicsTypes.h`, `hidsystem/IOLLEvent.h` как есть —
  они существуют, компилируются как обычные C-заголовки, деклараций без
  реализации не создают проблем на этапе `#include`.

### Полный apple IOKitUser vs минимальный шим — рекомендация

| | Полный `IOKitUser` (иокит-клиент 1179.50.2, весь `iokit_sources` из `CMakeLists.txt:73-146`) | Минимальный shim (4 символа выше) |
|---|---|---|
| Объём кода | ~40 файлов: `hid.subproj` (30+ файлов), `kext.subproj`, `pwr_mgt.subproj`, `usb.subproj`, `network.subproj`, `ps.subproj` — почти всё **не используется** CoreGraphics | 1 маленький `.c`/`.m` файл |
| Требует `mig`/`iokitmig.defs` | Да (`CMakeLists.txt:36-58` — два прогона `mig()` под x86_64/i386) — отдельный тулчейн-таргет `build-mig`, не встроенный в standalone-скрипты (`docs/SPEC-gui-build.md` §1, §3: "модель build-gui.sh" не вызывает `cmake/mig.cmake` вообще) | Нет |
| Требует `iokitd` (пустой submodule) на рантайме, иначе просто деградирует | И то, и другое деградирует одинаково (см. разбор выше) — разница только в объёме кода, который надо собрать, чтобы получить тот же нулевой практический результат | — |
| Совместим с `build-gui.sh`-методом (свой `clang`+`ld64.lld`, без CMake-графа) | Нет без доп. работы — `extract_sources()`-регэксп скрипта тянет весь `iokit_sources`, включая MIG-generated файлы, которых даже не существует без прогона `mig` | Да, естественно |
| Обоснование выбора | Оправдан только если позже реально пишется `iokitd` и нужен полноценный HID/power/kext путь | **Рекомендуется сейчас**: CoreGraphics использует 4 символа, остальные ~40 файлов IOKitUser не имеют вызывающего кода нигде в дереве Cocotron (не проверялось для AppKit/QuartzCore — не мой участок, см. "Не проверено") |

Итог: **минимальный shim `IOKit.framework`**, дающий только 4 символа выше
плюс копию заголовков `darling/include/IOKit/{IOKitLib.h,graphics/*,
hidsystem/IOLLEvent.h}` — не полный `IOKitUser`. Полный `IOKitUser`
остаётся в дереве как есть (уже подключён в `src/CMakeLists.txt:324` для
будущего, когда `iokitd` появится) — этот shim не заменяет его в общем
CMake-графе, а даёт параллельный путь для ручной сборки
(`build-gui.sh`-подобным методом) уже сейчас.

## 6. Порядок сборки

1. **Заголовки** — не собирать ничего, просто указывать `-I
   src/external/IOKitUser/darling/include` (не `framework-include/IOKit` —
   9p `readlink()` ломается, см. §1.1) при компиляции CoreGraphics.
2. **Написать shim** (новый файл, например
   `src/external/IOKitUser/darling/freebsd-shim/IOKitShim.c`, вне scope
   правки существующих файлов) с 4 символами из §5, слинковать в маленький
   `IOKit.dylib` тем же ручным `clang -target x86_64-apple-macos10.10 ... &&
   ld64.lld -syslibroot ...`-методом, что использует
   `build-freebsd/build-gui.sh` для Onyx2D (см. `docs/SPEC-gui-build.md`
   §2, ряд `build-real-macho-tests.sh`) — **не** через
   `cmake/darling_framework.cmake`, по тем же причинам, что и остальной
   Cocotron (см. `docs/SPEC-gui-build.md` §3 "Почему не через
   darling_framework.cmake").
3. Установить получившийся `IOKit.dylib` в `${STAGED_OVERLAY}/System/
   Library/Frameworks/IOKit.framework/Versions/A/IOKit` — путь, который уже
   ожидает `build-freebsd/build-gui.sh:290` (`require_frameworks CoreGraphics
   IOKit` + прямая ссылка на этот файл в команде линковки).
4. После этого шаг `require_frameworks CoreGraphics IOKit` в
   `build-gui.sh:273` перестаёт быть блокирующим, и линковка
   `CoreGraphics.dylib` (`build-gui.sh:272-291`) может быть реально
   опробована на FreeBSD-машине (сама она — не моя задача, это шаг
   валидации, а не часть этого документа).
5. `iokitd` (пустой submodule, §1.1) и полноценный MIG-путь
   (`iokitmig.defs` → `build-mig`) — **явно откладываются**, не входят в
   объём этой задачи; см. §5 обоснование.

## 7. Риски

- **Поведенческий риск деградации.** Shim всегда возвращает "сервис не
  найден" — `CGDisplayIOServicePort()` и всё, что от него зависит (имя
  монитора, EDID-подобные метаданные), всегда будет пустым. Для текущей
  headless-архитектуры (`cage --headless`, virtual displays) это не должно
  быть проблемой, но не проверялось: что именно AppKit (не мой участок)
  делает с пустым результатом `CGDisplayIOServicePort` дальше по цепочке —
  если где-то есть `assert`/безусловный force-unwrap на непустой словарь,
  может понадобиться доп. правка на стороне AppKit/QuartzCore.
- **Расхождение с "полным" IOKit при появлении `iokitd`.** Если позже кто-то
  реализует `src/external/iokitd` всерьёз (реальные HID/graphics device
  объекты), shim нужно будет заменить настоящим `IOKitUser`-путём (MIG +
  bootstrap-сервис) — это осознанный технический долг, а не забытая
  недоделка; зафиксировать явно в коде shim-а комментарием.
- **`extract_sources()`-регэксп `build-gui.sh` не рассчитан на файл вне
  `CoreGraphics_sources`.** Если IOKit-shim добавляется как отдельный
  скрипт-шаг (не через `set(iokit_sources ...)` в существующем
  `IOKitUser/CMakeLists.txt`), это отдельная компиляция вне
  `extract_sources()`-механизма — не риск, а просто уточнение: shim не
  должен пытаться "притвориться" частью `iokit_sources`, чтобы не тянуть
  зависимость от MIG-файлов, которых нет.
- **Версия таргета.** `CoreGraphics/CMakeLists.txt:16` —
  `-mmacosx-version-min=10.10`; `build-gui.sh` уже использует те же 10.10
  флаги (`build-gui.sh:174`) — shim должен собираться с теми же
  `-target x86_64-apple-macos10.10`/`-platform_version macos 10.10 10.10`,
  иначе повторяется риск №6 из `docs/SPEC-gui-build.md` §6 (смешение версий
  между уже собранными и новыми Mach-O).
- **Ничего из этого не проверено на реальной FreeBSD-машине** — ни
  компиляция shim-а, ни линковка `ld64.lld -syslibroot`, ни рантайм-путь
  через `CGDisplayIOServicePort()`. Весь документ — чтение исходников.

## 8. Чего НЕ проверено (отдельным списком)

- Компиляция/линковка чего-либо из этого документа на живой FreeBSD-машине
  (задача явно запрещала сборку — весь анализ статический, по коду).
- Полный список символов, которые **AppKit** (не мой участок) тянет из
  `CoreGraphics.framework`, и не требует ли какой-то из них дополнительных
  IOKit-символов транзитивно (проверялся только сам `CoreGraphics/` каталог,
  не `AppKit/`).
- Поведение AppKit/QuartzCore при `CGDisplayIOServicePort() == 0` /
  `IODisplayCreateInfoDictionary() == NULL` — не читал их код (не мой
  участок), только сам `CGDirectDisplay.m`.
- Есть ли в `CGDirectDisplay.m` после строки 421 (за пределами
  `CGDisplayIOServicePort()`) другие функции, которые тоже трогают IOKit
  вне зоны, покрытой первым grep'ом (grep был по всему каталогу
  `CoreGraphics/`, но не построчно вычитывал весь `CGDirectDisplay.m`,
  например `CGDisplayCopyDisplayMode`/EDID-парсинг могли бы неявно зависеть
  от структуры, возвращаемой `IODisplayCreateInfoDictionary`, — не читал
  тело функций после `:421`).
- Существует ли где-то в дереве (вне `cocotron/CoreGraphics`) уже
  предпринятая попытка написать `iokitd`-заглушку или mach bootstrap-сервер
  общего назначения в `darlingserver`, помимо духового поиска по `grep`
  (не исключаю, что что-то называется иначе и не поймано ключевыми словами
  `io_service\|IOMasterPort\|io_master`).
- Реальный формат/наличие пакета `mig` или альтернативного MIG-компилятора
  в `pkg` для FreeBSD 15.1 — не проверялось (не нужно при выбранном
  minimal-shim подходе, но релевантно, если решение изменится в пользу
  полного `IOKitUser`).
- Содержимое `src/external/IOKitUser/darling/include/IOKit/graphics/
  IOGraphicsTypes.h` построчно (только `grep` на конкретную константу) —
  не проверял, нет ли там же макросов, которые сами по себе требуют
  дополнительных заголовков вне `darling/include`.
