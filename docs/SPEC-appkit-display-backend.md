# SPEC: путь Cocoa-приложение → пиксели на экране (AppKit/CoreGraphics сторона)

Дерево: `/path/to/darling` (Darling — слой совместимости macOS,
порт на FreeBSD). Документ описывает **только** сторону AppKit/CoreGraphics/
Onyx2D: что рисует пиксели сегодня, какой класс/интерфейс — точка подключения
своего дисплейного бэкенда, и как приходит ввод. Архитектура стыковки с
Wayland-стримингом bsdOS — вне зоны этого документа (её делает параллельный
агент).

## 1. Какие реализации AppKit есть в дереве

| Путь | Объём | Живая/мёртвая | Чем подтверждается |
|---|---|---|---|
| `src/frameworks/AppKit` | **не существует** (`ls` → No such file or directory) | — | Каталога нет вообще; не подмодуль (нет записи в `.gitmodules`, `git submodule status` ничего не находит). В задании упомянут как гипотеза — гипотеза не подтвердилась, каталога просто нет. |
| `src/frameworks/dev-stubs/AppKit` | 2 файла: `src/classes.m`, `src/main.m` + `CMakeLists.txt` | **stub**, собирается только в CLI/dev-only режиме | `src/frameworks/CMakeLists.txt:156-172` — цель `dev-stubs/AppKit` добавляется в `add_subdirectory` только когда `(COMPONENT_cli_dev AND NOT COMPONENT_gui) OR COMPONENT_cli_dev_gui_stubs`, т.е. когда собираем без GUI. Комментарий в коде (`src/frameworks/CMakeLists.txt:150-153`): нужны, чтобы Xcode/CLI-инструменты линковались без реального AppKit. Пиксели не рисует — это заглушка символов. |
| `src/external/cocotron/AppKit` (submodule `darling-cocotron`) | 375 `.m`-файлов | **реальная реализация, подключена в CMake, но НЕ строится текущими build-скриптами** | Реестр подмодуля: `.gitmodules:` `[submodule "src/external/cocotron"]` → `url = ../darling-cocotron.git`; закреплён на коммите `085c77f4` (`git submodule status`). Подключение в сборку: `src/external/cocotron/CMakeLists.txt:1-8` (`add_subdirectory(AppKit)` и др.), вызывается из `src/CMakeLists.txt:360-369` только внутри `if (COMPONENT_gui)`. Но реальный build-путь проекта (`build-freebsd/build-darlingserver.sh`) вообще не запускает `cmake` по корневому `CMakeLists.txt` с этим компонентом — он собирает только `darlingserver`/`mldr`/`dtape`/`simple` вручную (`build-freebsd/build-darlingserver.sh:78-106`, grep на `COMPONENT_` даёт 0 совпадений). `README.md` статус-лист останавливается на «dyld / dynamic linking» — ещё не сделано, GUI-цепочка (AppKit) даже не следующий пункт. Вывод: код реализации живой (полный Cocotron), но COMPONENT_gui-путь в этом порту никогда фактически не собирался и не тестировался на FreeBSD. |

Итого: **единственная нестабовая реализация AppKit — Cocotron** (`src/external/cocotron/AppKit`), унаследованная от апстрима Darling. Она рассчитана на X11 (см. §2-3) и требует пакетов, которых пока нет в build-freebsd-скриптах (`find_package(X11 REQUIRED)` — `src/external/cocotron/AppKit/CMakeLists.txt:27-31`).

## 2. Абстракция дисплея/оконного сервера

Cocotron построен как **class cluster + plugin-бэкенды**, а не как жёстко
привязанный к X11 код:

- **`NSDisplay`** (`src/external/cocotron/AppKit/include/AppKit/NSDisplay.h:29-122`) — абстрактный базовый класс окружения показа: экран(ы), pasteboard, курсор, раскладка клавиатуры, `mouseLocation`, очередь событий (`nextEventMatchingMask:...`, `postEvent:atStart:`).
- Реализация `+(NSDisplay *)currentDisplay` (`src/external/cocotron/AppKit/NSDisplay.m:42-92`) — это **plugin loader**: сканирует `Backends/*.backend` бандлы внутри AppKit.framework (`pathsForResourcesOfType:@"backend" inDirectory:@"Backends"`), сортирует по ключу `NSPriority` из `Info.plist`, инстанцирует `principalClass` первого, кто успешно проинициализировался.
- Сегодня в дереве **единственный** бэкенд — `X11.backend` (`src/external/cocotron/AppKit/X11.backend/Info.plist`):
  ```
  NSPriority = 200
  NSPrincipalClass = X11Display
  ```
  Второй каталог, `Win32.subproj` (`src/external/cocotron/AppKit/Win32.subproj`), существует в дереве, но не собирается в текущем `CMakeLists.txt` (в списке источников AppKit фигурирует только `X11.backend/*` — `src/external/cocotron/AppKit/CMakeLists.txt:599-607`) и не является кандидатом на FreeBSD.
- Конкретный класс: **`X11Display : NSDisplay`** (`src/external/cocotron/AppKit/X11.backend/X11Display.h:21-54`) — держит `Display *_display` (Xlib), карту окон по XID, обрабатывает Xkb/XRandR, конвертирует `XEvent` в `NSEvent` (см. §5).
- Окна: **`CGWindow`** (`src/external/cocotron/CoreGraphics/include/CoreGraphics/CGWindow.h:68-132`) — тоже абстрактный базовый класс (методы реализованы как `O2InvalidAbstractInvocation()` в `src/external/cocotron/CoreGraphics/CGWindow.m:31-80` и далее). Конкретная реализация — **`X11Window : CGWindow`** (`src/external/cocotron/AppKit/X11.backend/X11Window.h:31-72`), которая держит X11 `Window`, `GLXContext`(`CGLContextObj _cglContext`), Onyx2D-контекст `O2Context *_context` и X11 Input Context (`XIC _xic`) для ввода текста.
- Растеризация геометрически отделена от вывода на экран: `ApplicationServices` (`CoreGraphics`+`CoreText`) и `QuartzCore` собираются из бинарных билд-каталогов Cocotron (`src/frameworks/ApplicationServices/CMakeLists.txt:37-39`, `src/frameworks/Quartz/CMakeLists.txt:52-53`) — сам софт-рендер (Onyx2D) не завязан на конкретный window-server, завязан только слой X11.backend внутри AppKit.

## 3. Точный шов для нового бэкенда

Чтобы подключить собственный дисплейный бэкенд (не X11), нужно реализовать
**новую пару конкретных подклассов** по образцу `X11Display`/`X11Window`,
упакованную как загружаемый bundle (`*.backend`), с более высоким
`NSPriority`, чем 200, либо заменяющую `X11.backend`:

1. **`NSDisplay` subclass** (аналог `X11Display`, файл-эталон
   `src/external/cocotron/AppKit/X11.backend/X11Display.h:21`/`.m`). Обязана
   реализовать как минимум:
   - `+(NSDisplay *)currentDisplay`-путь автоматически подхватит её через
     `NSBundle`/`Info.plist` (`NSPrincipalClass`) — регистрация не через код,
     а через `Info.plist` + приоритет (`src/external/cocotron/AppKit/X11.backend/Info.plist`).
   - `- (NSArray *) screens`, `- (NSEvent *) nextEventMatchingMask:untilDate:inMode:dequeue:`, `- (void) postEvent:atStart:`, `- (CGWindow *) newWindowWithDelegate:`, курсор/раскладка клавиатуры/mouseLocation (полный список абстрактных методов — `src/external/cocotron/AppKit/include/AppKit/NSDisplay.h:33-121`, все они сегодня `NSInvalidAbstractInvocation()` в `NSDisplay.m`, кроме `+currentDisplay`).
2. **`CGWindow` subclass** (аналог `X11Window`, эталон
   `src/external/cocotron/AppKit/X11.backend/X11Window.h:31`). Ключевые методы
   для сдачи пикселей наружу (все объявлены в базовом классе
   `src/external/cocotron/CoreGraphics/include/CoreGraphics/CGWindow.h:68-132`,
   реализованы у X11Window):
   - `- (O2Context *) cgContext` / `createCGContextIfNeeded` — отдаёт
     Onyx2D-контекст, к которому AppKit рисует (у X11Window:
     `src/external/cocotron/AppKit/X11.backend/X11Window.m:428-444`).
   - `- (void) flushBuffer` — момент, когда нарисованное нужно доставить на
     экран (`X11Window.m:639-644`: `O2ContextFlush(_context)` затем
     `openGLFlushBuffer` — GLX composite). **Это и есть шов «кадр готов —
     отдай наружу»** — именно тут свой бэкенд обязан взять буфер surface и
     передать его в свой транспорт вместо `glXSwapBuffers`.
   - `- (void) invalidateContextWithNewSize:forceRebuild:` — ресайз буфера
     (`X11Window.m:449-459`).
   - `- (void) setFrame:`, `- (void) makeKey`, `- (void) hideWindow`/
     `showWindowWithoutActivation`, `- (CGSubWindow *) createSubWindowWithFrame:`
     — управление жизненным циклом окна.
3. **Регистрация бэкенда**: bundle-каталог `<Name>.backend/Info.plist` с
   ключами `CFBundleExecutable`, `NSPrincipalClass`, `NSPriority` (см.
   `src/external/cocotron/AppKit/X11.backend/Info.plist` как шаблон), плюс
   добавление исходников в `AppKit`'ов `CMakeLists.txt`
   (`src/external/cocotron/AppKit/CMakeLists.txt:599-607` — сейчас там
   перечислены только файлы `X11.backend/*`).

Если вместо параллельного бэкенда решат **подменить X11Window/X11Display на
уровне реализации** (не через plugin-приоритет, а прямой replace) — тот же
набор методов, но проще: не нужен bundle/`Info.plist`, просто другой .m
получает те же роли.

## 4. Что рисует пиксели сегодня — формат буфера и владение памятью

- Рендер полностью **программный (CPU rasterizer)**: **Onyx2D** —
  собственный форк OpenVG-эталонной реализации Khronos
  (`src/external/cocotron/Onyx2D/include/Onyx2D/O2Surface.h:1-10`, шапка
  файла явно ссылается на "Derivative of the OpenVG 1.0.1 Reference
  Implementation"). Никакого GPU-рендеринга геометрии/текста нет — только
  композиция готового кадра на GPU (см. ниже).
- Буфер — класс **`O2Surface : O2Image`**
  (`src/external/cocotron/Onyx2D/include/Onyx2D/O2Surface.h:66-73`):
  - Владелец памяти: сам `O2Surface`, поле `unsigned char *_pixelBytes` +
    флаг `BOOL m_ownsData` (там же, строки 67-71); аллокация — через
    `O2DataProvider`/`NSMutableData` внутри `initWithBytes:...`
    (`src/external/cocotron/Onyx2D/O2Surface.m:531-565`, `dataWithLength:
    bytesPerRow * height` на строке ~557).
  - Формат в реальном окне: `O2SurfaceCreateInit`-путь конкретно у
    X11Window — `bitsPerComponent: 8`, **RGB with premultiplied alpha,
    little-endian 32-bit** (`kO2ImageAlphaPremultipliedFirst |
    kO2BitmapByteOrder32Little`,
    `src/external/cocotron/AppKit/X11.backend/X11Window.m:428-441`),
    `bytesPerRow: 0` → библиотека сама считает как `width * bitsPerPixel / 8`
    (`O2Surface.m:548-554`). Т.е. **тесно упакованный BGRA8888
    premultiplied, stride = width*4**, никакого паддинга сверху библиотека
    не просит (но принимает произвольный `bytesPerRow`, если передать явно).
  - Доступ извне: `O2SurfaceGetPixelBytes()`, `O2SurfaceGetWidth/Height/
    BytesPerRow()` — экспортированные C-функции
    (`O2Surface.h:89-92`) — то, чем читать/писать буфер снаружи Objective-C.
- Доставка на экран у существующего (X11) бэкенда — **не blit пикселей
  напрямую в X-сервер**, а GPU-композиция: софт-буфер загружается как
  текстура в `CAWindowOpenGLContext` (`renderSurface:` —
  `src/external/cocotron/AppKit/X11.backend/X11Window.m`, метод, вызываемый
  из `openGLFlushBuffer`, около строк 617-637) и презентуется через
  `glFlush(); CGLFlushDrawable(_cglContext);` (X11Window.m:634-636), то есть
  **GLX обязателен** даже для чисто софтверного рендера — окно в X11
  создаётся с GLX-визуалом (`glXChooseVisual`,
  `X11Window.m:166-181`). Значит: если новый бэкенд не хочет тащить
  OpenGL/GLX, его `flushBuffer`/аналог `openGLFlushBuffer` нужно писать с
  нуля — он **не обязан** повторять GL-путь, `O2Surface`-буфер уже готов как
  чистый CPU RGBA-буфер (см. выше) и может быть скопирован напрямую в любой
  транспорт (shm, сокет и т.п.), минуя GL.

## 5. Metal/Vulkan/OpenGL в дереве

- **OpenGL/GLX** — используется существующим X11-бэкендом для композиции
  готового кадра на экран (см. §4): `glXChooseVisual`,
  `glXCreateContext`(через `CGLContextObj`), `CGLFlushDrawable`
  (`X11Window.m`, множественные вхождения, напр. строки 166-181, 634-636).
  Framework `OpenGL` собирается как «реальная» GUI-цель
  (`src/frameworks/CMakeLists.txt:76-80`, `if (COMPONENT_gui) ...
  add_subdirectory(OpenGL)`).
- **Metal** — есть **два независимых следа**, оба не про растеризацию AppKit:
  1. `src/external/cocotron/QuartzCore/CAMetalLayer.mm`,
     `CAMetalDrawable.mm`, заголовки `CAMetalLayerInternal.h`,
     `CAMetalDrawableInternal.h`, `include/QuartzCore/CAMetalLayer.h`,
     `CAMetalDrawable.h` (Cocotron's QuartzCore, `grep -rli metal` в дереве).
     Это Cocotron-реализация `CAMetalLayer`/`CAMetalDrawable` — присутствует
     в исходниках, но это **CoreAnimation-слой поверх** уже отрисованного
     контента (для приложений, которые сами рисуют через Metal), не
     относится к пути AppKit software-rasterizer → экран, который описан
     выше.
  2. Отдельный **подмодуль `src/external/metal`** (`darling-metal`,
     `.gitmodules`: `url = ../darling-metal.git`, populated,
     commit `ae20248`) — Darling-реализация Metal API как таковая (для
     запуска Metal-приложений через translation layer, вероятно поверх
     Vulkan — детали не изучались, вне рамок задачи). Подключается в сборку
     в `src/CMakeLists.txt:465-469`, только внутри
     `if (COMPONENT_dev_gui_common)`.
- **Vulkan** — прямых упоминаний в `src/external/cocotron` не найдено
  (`grep -rli vulkan` не дал совпадений в Cocotron-дереве); возможно
  используется внутри `src/external/metal` как бэкенд, но это отдельный
  подмодуль, не изучался в рамках этой задачи.
- Вывод: для нового дисплейного бэкенда **GL не обязателен** — можно писать
  прямой software-путь (см. §4), Metal-код в дереве не пересекается с
  AppKit software-rasterizer seam.

## 6. Событийный ввод (клавиатура/мышь)

- Источник событий — родной X11-event-loop внутри `X11Display`:
  `XNextEvent(_display, &e)` (`src/external/cocotron/AppKit/X11.backend/X11Display.m:1424-1425`), диспетчеризуется в `-(void) postXEvent:(XEvent *)ev` (`X11Display.m:1013-...`).
- Конвертация `XEvent` → `NSEvent` происходит прямо в этом методе:
  - Клавиатура: `Xutf8LookupString`/`XLookupString` → `NSString *str` →
    `[NSEvent keyEventWithType:location:modifierFlags:timestamp:
    windowNumber:context:characters:charactersIgnoringModifiers:isARepeat:
    keyCode:]` (`X11Display.m:1017-1074`). Есть маппинг X11-keycode →
    Carbon virtual keycode через таблицу `x11ToCarbon[]` (строка ~1065),
    нужен для приложений, использующих константы `kVK_*`.
  - Кнопки мыши: `case ButtonPress:` далее (за пределами прочитанного
    диапазона, но паттерн тот же — `postXEvent:` продолжает switch по
    `ev->type` для `ButtonPress/ButtonRelease/MotionNotify` и т.д.,
    `X11Display.m:1080+`).
- Готовое `NSEvent` кладётся в очередь через
  `- (void) postEvent:(NSEvent *)event atStart:(BOOL)atStart` (унаследовано
  от `NSDisplay`, `NSDisplay.h:63`), которую разгребает
  `-nextEventMatchingMask:untilDate:inMode:dequeue:` — обычный
  Cocoa/Cocotron event loop (`NSApplication` → `sendEvent:`).
- **Интерфейс для обратного канала ввода**: новому бэкенду не нужно
  порождать `XEvent` — достаточно **напрямую конструировать `NSEvent`** через
  фабричные методы класса `NSEvent` (`keyEventWithType:...`,
  соответствующий `mouseEventWithType:...` — сигнатуры в
  `src/external/cocotron/AppKit` framework headers, не читал отдельно, но
  паттерн виден из `X11Display.m`) и звать
  `[[NSDisplay currentDisplay] postEvent:atStart:]`. Т.е. шов для ввода —
  тот же класс `NSDisplay`(subclass), методы `postEvent:atStart:` +
  `nextEventMatchingMask:...` (`NSDisplay.h:55-63`), а конкретный маппинг
  raw-события в `NSEvent` — целиком забота нового бэкенда (X11-специфичный
  код типа `x11ToCarbon`/`X11KeySymToUCS` переиспользовать не нужно/нельзя).

## 7. Открытые вопросы и риски

- **GUI-путь ни разу не собирался в этом порту.** `build-freebsd/*.sh`
  вызывают только `darlingserver`/`mldr`/`dtape`, никогда — корневой
  `CMakeLists.txt` с `COMPONENT_gui`. Неизвестно, компилируется ли Cocotron
  вообще под clang/FreeBSD 15.1 сегодня (Xlib/Xrandr/Xkb/Xcursor/Xext/
  Xkbfile — жёсткие `REQUIRED`-зависимости в
  `src/external/cocotron/AppKit/CMakeLists.txt:27-40`, ни один пакет не
  упомянут в `README.md`-Prerequisites). Оценка объёма работ по самой сборке
  не входит в этот документ, но это первый барьер прежде, чем вообще будет
  что интегрировать.
- **`dyld`/динамическая линковка не готовы** (`README.md`: `[ ] dyld /
  dynamic linking`). AppKit.framework в реальном macOS — динамическая
  библиотека; неясно, может ли текущий загрузчик (`mldr`, ориентирован на
  статические Mach-O) вообще запустить процесс, слинкованный с
  AppKit/Cocotron. Если нет — весь план «подключить дисплейный бэкенд»
  висит на dyld как предусловии.
- **GLX как скрытая зависимость даже для software-рендера.** Существующий
  X11Window требует рабочий GLX-контекст для *презентации* уже готового
  CPU-буфера (§4). Если у bsdOS в целевом окружении нет GL (headless
  `cage`/`WLR_RENDERER=pixman` в текущем стриминговом стеке — согласно
  CLAUDE.md), то путь X11Window буквально не запустится там; новый бэкенд
  обязан обойти `CAWindowOpenGLContext`/`CGLFlushDrawable` целиком, а не
  переиспользовать `openGLFlushBuffer`.
- **Единственный существующий бэкенд-пример (X11) — Xlib-специфичный
  насквозь**, включая XIM/XIC для текстового ввода
  (`X11Window.h:54`, `_xic`), XRandR для экранов
  (`X11Display.h:41`, `_rrEventBase`), X11 Pasteboard/Cursor/SubWindow. Всё
  это придётся реализовать заново для не-X11-бэкенда — готовых
  «нейтральных» абстракций для буфера обмена/курсора в дереве нет, только
  X11-конкретика.
- **`Win32.subproj` не собирается и не поддерживается** — второй
  теоретический пример бэкенда в дереве, но исключён из
  `AppKit/CMakeLists.txt`. Как референс кода архитектурно полезен (показывает
  форму `NSDisplay`/`CGWindow` subclass под другую платформу), но не
  компилируется, соответственно не факт, что актуален относительно текущих
  абстрактных интерфейсов.
- **CAMetalLayer/CAMetalDrawable существуют в Cocotron QuartzCore, но не
  изучены на предмет живости** — не проверялось, ссылаются ли они на
  реально собираемый код, или это код, унаследованный из апстрима и
  никогда не тестировавшийся в данном порту (аналогично AppKit/X11.backend).
  Не размечать как «готовый путь Metal» без отдельной проверки.
- **`O2Surface` буфер-формат подтверждён только для X11Window's
  `createCGContextIfNeeded`** (BGRA8888 premultiplied little-endian). Не
  проверялось, использует ли остальной AppKit (напр. offscreen-рендер,
  `NSBitmapImageRep`, PDF-контекст) тот же формат по умолчанию — вероятно
  да (общий `O2Surface`/`O2ColorSpace` код), но это не подтверждено
  file:line для путей за пределами `X11Window.m`.
