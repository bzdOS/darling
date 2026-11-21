# SERIES fix24 (rerun): класс OBJC-FWD-MISS после ответа NSConstantDictionary — 25 селекторов

Диагностика одной серии в стиле SERIES-FIX24-DIAG.md: здесь только измеренное
и воспроизводимые команды. Гипотеза в конце отмечена как гипотеза.

Плейсхолдеры: `$DARLING_SRC_DIR` — исходное дерево, `$DARLING_OVERLAY` — оверлей
с собранными образами, `$DARLING_BUILD_DIR` — каталог логов и сборки.

## Источник

- Лог серии: `$DARLING_BUILD_DIR/fix24-series-1120c.log`
  (md5 `dd099aa538ae5e5ee72718b1b4d31d6c`)
  `LAUNCH fix24 10 120 start=2026-11-20T17:02:17Z driver_md5=6c7711b4fdb1ef6d0e4d209bea7fcefd git_head=4e1330c3a6fe6d93fb1f3dd7a3aff8d11f1fc735`
- Логи ранов: `$DARLING_BUILD_DIR/chrome-sr-fix24{1..10}.log`
- SUMMARY: `$DARLING_BUILD_DIR/../series-summaries/fix24.txt`
  (md5 `2c3749da2d5e33b707abccde4f7d07df`)
  `SERIES fix24 N=10 valid=10 survived20=5 top=rc133x10 windows=0 fwdmiss=25 mldr=68a3c9ede158fdee2aea9749f794fab6 args=""`
- Предыдущая серия (для сравнения): `fwdmiss=32`, `top=rc133x7`, 256 попаданий.
- Ценз: все 10 ранов `valid rc=133`, длительность 12–91 с.

Сбор селекторов:

```sh
grep -ah 'OBJC-FWD-MISS' "$DARLING_BUILD_DIR"/chrome-sr-fix24[0-9].log "$DARLING_BUILD_DIR"/chrome-sr-fix2410.log | sort | uniq -c | sort -rn
```

Контроль сходимости (вывод):

```
$ ... | sort -u | wc -l
      25
$ ... | wc -l
     250
$ grep -ao 'fwdmiss=[0-9]*' "$DARLING_BUILD_DIR"/../series-summaries/fix24.txt
fwdmiss=25
```

`fwdmiss` в SUMMARY — число уникальных селекторов (25), а не сумма попаданий:
сумма частот = 250 = 25 × 10 ранов. (В SERIES-FIX24-DIAG.md сумма 256 при
`fwdmiss=32` названа «ровно как SUMMARY» — это неточность: сошлись уникальные,
сумма — нет.) Каждый селектор печатается один раз на пару класс×селектор в
процессе (`FoundationExtras.dylib` forward.c), поэтому частота = число ранов,
где он встретился. Здесь все 25 имеют частоту 10 из 10 — стабильный набор.

## Таблица всех 25 уникальных селекторов

| # | селектор | частота (из 10) | образ-владелец |
|---|----------|-----------------|----------------|
| 1 | `-[NSConstantDictionary _encodingCantBeStoredInEightBitCFString]` | 10 | CoreFoundationExtras |
| 2 | `-[NSConstantDictionary getCharacters:range:]` | 10 | CoreFoundationExtras |
| 3 | `-[NSFileManager ubiquityIdentityToken]` | 10 | Foundation |
| 4 | `-[NSUserNotificationCenter deliveredNotifications]` | 10 | Foundation |
| 5 | `-[__NSCFDictionary symbolicTraits]` | 10 | Foundation (чужой получатель) |
| 6 | `+[NSImage imageWithSystemSymbolName:accessibilityDescription:]` | 10 | AppKit |
| 7 | `+[NSEvent pressedMouseButtons]` | 10 | AppKit |
| 8 | `+[NSAccessibilityRemoteUIElement remoteTokenForLocalUIElement:]` | 10 | AppKit |
| 9 | `-[NSKVONotifying_BrowserCrApplication isFullKeyboardAccessEnabled]` | 10 | AppKit (NSApplication) |
| 10 | `-[BrowserNativeWidgetWindow addTitlebarAccessoryViewController:]` | 10 | AppKit (NSWindow) |
| 11 | `-[BrowserNativeWidgetWindow convertPointFromScreen:]` | 10 | AppKit (NSWindow) |
| 12 | `-[BrowserNativeWidgetWindow setAnimationBehavior:]` | 10 | AppKit (NSWindow) |
| 13 | `-[BrowserNativeWidgetWindow setTitlebarAppearsTransparent:]` | 10 | AppKit (NSWindow) |
| 14 | `-[NSTitlebarAccessoryViewController setFullScreenMinHeight:]` | 10 | AppKit |
| 15 | `-[NSTitlebarAccessoryViewController setHidden:]` | 10 | AppKit |
| 16 | `-[NSTitlebarAccessoryViewController setLayoutAttribute:]` | 10 | AppKit |
| 17 | `+[CALayer layer]` | 10 | QuartzCore |
| 18 | `-[CALayer setAnchorPoint:]` | 10 | QuartzCore |
| 19 | `-[CALayer setAutoresizingMask:]` | 10 | QuartzCore |
| 20 | `-[CALayer setGeometryFlipped:]` | 10 | QuartzCore |
| 21 | `-[CALayerContext setLayer:]` | 10 | QuartzCore |
| 22 | `-[CALayerContext setSubwindow:]` | 10 | QuartzCore |
| 23 | `+[NSViewBackingLayer layer]` | 10 | QuartzCore (унаследован, см. ниже) |
| 24 | `+[UTType typeWithMIMEType:]` | 10 | UniformTypeIdentifiers |
| 25 | `+[ASWebAuthenticationSessionWebBrowserSessionManager sharedManager]` | 10 | AuthenticationServices |

Отнесение к образу измерено, не по префиксу имени:

```sh
nm "$DARLING_OVERLAY"/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation | grep -E 'OBJC_CLASS_.*(NSFileManager|NSUserNotificationCenter)$'
nm "$DARLING_OVERLAY"/System/Library/Frameworks/AppKit.framework/Versions/Current/AppKit | grep -E 'OBJC_CLASS_.*(NSEvent|NSImage|NSTitlebarAccessoryViewController|NSViewBackingLayer|NSWindow)$'
nm "$DARLING_OVERLAY"/System/Library/Frameworks/AppKit.framework/Versions/Current/AppKit | grep -E 'OBJC_CLASS_.*CALayer'
```

Первая команда: `NSUserNotificationCenter` определён в Foundation, а не в AppKit
(в прошлой таблице он стоял у AppKit). Третья: в AppKit `CALayer` и `CALayerContext`
помечены `U` — разрешаются из QuartzCore. `NSViewBackingLayer` объявлен как
`@interface NSViewBackingLayer : CALayer`
(`$DARLING_SRC_DIR/src/external/cocotron/AppKit/NSViewBackingLayer.h:3`), в его
`.m` метода `+layer` нет, значит промах `+[NSViewBackingLayer layer]` — это
унаследованный `+[CALayer layer]`, и относится к QuartzCore.

## Группировка по образу-владельцу

| кластер | селекторов | попаданий | доля от 250 |
|---------|-----------|-----------|-------------|
| AppKit: `BrowserNativeWidgetWindow` (4), `NSTitlebarAccessoryViewController` (3), `NSImage`, `NSEvent`, `NSAccessibilityRemoteUIElement`, `NSKVONotifying_BrowserCrApplication` (по 1) | 11 | 110 | 44 % |
| QuartzCore: `CALayer` (4, включая `+layer`), `CALayerContext` (2), `NSViewBackingLayer` (1, унаследован) | 7 | 70 | 28 % |
| Foundation: `NSConstantDictionary` (2), `NSFileManager`, `NSUserNotificationCenter`, `__NSCFDictionary` (по 1) | 5 | 50 | 20 % |
| UniformTypeIdentifiers: `UTType` | 1 | 10 | 4 % |
| AuthenticationServices: `ASWebAuthenticationSession…` | 1 | 10 | 4 % |
| **итого** | **25** | **250** | **100 %** |

Если считать `NSViewBackingLayer` по префиксу (AppKit), будет 12/120/48 % и 6/60/24 %:
порядок кластеров тот же. Кандидат из прошлой таблицы (QuartzCore) на новой —
второй, не первый; первый — AppKit.

## Топ-3 кластера: провайдеры

### 1. AppKit — 11 селекторов, 110 попаданий (44 %)

Провайдер: cocotron AppKit (образ `AppKit.framework/Versions/Current/AppKit`).

```sh
grep -rlE 'pressedMouseButtons|remoteTokenForLocalUIElement|isFullKeyboardAccessEnabled|imageWithSystemSymbolName|setTitlebarAppearsTransparent|setAnimationBehavior|convertPointFromScreen|addTitlebarAccessoryViewController' \
    "$DARLING_SRC_DIR"/src/external/cocotron "$DARLING_SRC_DIR"/build-freebsd/stub-frameworks "$DARLING_SRC_DIR"/src/frameworks/dev-stubs/AppKit
(пусто, rc=1)
sed -n '1,12p' "$DARLING_SRC_DIR"/src/external/cocotron/AppKit/NSTitlebarAccessoryViewController.m
(только methodSignatureForSelector: / forwardInvocation:, тел методов нет)
strings "$DARLING_OVERLAY"/System/Library/Frameworks/AppKit.framework/Versions/Current/AppKit | grep -xE 'setHidden:|setAnimationBehavior:|setTitlebarAppearsTransparent:|convertPointFromScreen:|pressedMouseButtons|isFullKeyboardAccessEnabled'
setHidden:
setAnimationBehavior:
setTitlebarAppearsTransparent:
```

Вывод: ни у одного из восьми имён нет реализации ни в cocotron-исходнике, ни
в стабах; три имени в образе — только строки (ссылки на селекторы), не методы.
Кластер разнородный: 8 разных классов, единого одноместного фикса нет.

### 2. QuartzCore — 7 селекторов, 70 попаданий (28 %)

Провайдер: `QuartzCore.framework`; в overlay это заглушка, не cocotron-реализация.

```sh
ls -l "$DARLING_OVERLAY"/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore
-rwxrwxr-x 1 ... 15168 Aug 28 ... QuartzCore
nm "$DARLING_OVERLAY"/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore | grep ' S _OBJC_CLASS_\$_'
(CALayer, CALayerContext, CAOpenGLLayer, CATransaction, CIContext, CIImage)
strings "$DARLING_OVERLAY"/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore | grep -xE 'setAnchorPoint:|setGeometryFlipped:|setAutoresizingMask:|setLayer:|setSubwindow:|layer'
(пусто, rc=1)
```

Источники того, что подключено и что нет:

```sh
grep -n 'QuartzCore' "$DARLING_SRC_DIR"/build-freebsd/build-gui.sh | sed -n '1,6p'
(строки 393–421: «Stub frameworks (CoreData, QuartzCore)» — оверлейный образ собирается как заглушка)
grep -n -A12 'implementation CALayer$' "$DARLING_SRC_DIR"/src/frameworks/dev-stubs/QuartzCore/src/classes.m
(только methodSignatureForSelector: / forwardInvocation: с NSLog)
grep -n 'setAnchorPoint:\|setLayer:\|setSubwindow:' "$DARLING_SRC_DIR"/src/external/cocotron/QuartzCore/CALayer.m "$DARLING_SRC_DIR"/src/external/cocotron/QuartzCore/CALayerContext.m
CALayer.m:85, CALayerContext.m:53, CALayerContext.m:62
grep -rn 'geometryFlipped\|autoresizingMask' "$DARLING_SRC_DIR"/src/external/cocotron/QuartzCore
(пусто)
```

Вывод: cocotron-QuartzCore (в его CMake `CALayer.m` и `CALayerContext.m`
перечислены) реализует `setAnchorPoint:`, `setLayer:`, `setSubwindow:`, но в
оверлейный образ эта реализация не попала; `+layer`, `setGeometryFlipped:`,
`setAutoresizingMask:` нет даже в cocotron-исходнике.

### 3. Foundation — 5 селекторов, 50 попаданий (20 %)

Провайдер: `CoreFoundationExtras.dylib` (`NSConstantDictionary`) и Foundation.

```sh
nm "$DARLING_OVERLAY"/usr/lib/CoreFoundationExtras.dylib | grep 'NSConstantDictionary' | grep -E ' t [-+]\['
(девять методов предыдущего фикса на месте: length, release, _fastCharacterContents, isNSArray__ … isNSString__)
strings "$DARLING_OVERLAY"/usr/lib/CoreFoundationExtras.dylib | grep -xE 'getCharacters:range:|_encodingCantBeStoredInEightBitCFString'
(пусто, rc=1)
sed -n '212,240p' "$DARLING_SRC_DIR"/build-freebsd/stub_data_rules.py
(_NSCT_METHODS — те же девять селекторов; два новых в шаблоне отсутствуют)
strings "$DARLING_OVERLAY"/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation | grep -xc ubiquityIdentityToken
0
grep -rl 'symbolicTraits' "$DARLING_SRC_DIR"/src/external/cocotron
(NSFont.m, NSFontDescriptor.m, include/AppKit/NSFontDescriptor.h — у словаря такого API нет)
```

Вывод: девять селекторов прошлого фикса больше не промахиваются (были 9 из 32),
на их месте — следующий слой того же класса: `getCharacters:range:` и
`_encodingCantBeStoredInEightBitCFString` (оба — строковые обращения к
константной записи). Три остальных селектора кластера — API без реализации.

## Порядок промахов относительно аборта (ран 1)

```sh
grep -an 'OBJC-FWD-MISS\|WRAPPER_RC' "$DARLING_BUILD_DIR"/chrome-sr-fix241.log | cut -c1-110
```

Строки 14472–14473 — два селектора `NSConstantDictionary`; 14864–14956 —
Foundation/AppKit/UTType/AuthenticationServices и первые `CALayerContext`/
`NSViewBackingLayer`; 14982–14987 — `+[CALayer layer]`,
`setGeometryFlipped:`, `setAutoresizingMask:`, `setAnchorPoint:`,
`-[CALayerContext setSubwindow:]` и последним `convertPointFromScreen:`;
`WRAPPER_RC=133` на строке 15010. Кадры аборта
(`$DARLING_BUILD_DIR/chrome-sr-fix241-crash.txt`, строка `ABORT`) идут из
`WebUIToolbarWebView` вглубь `ChromeFW`, начиная с `ChromeFW+0x1e3a378`.
Порядок — наблюдение; причинность он не доказывает.

## Гипотеза (одна)

Гипотеза: аборт rc133 следует за слоем, который строит Chrome, а слой в
оверлее — заглушка `QuartzCore` без реализации. Если дать `CALayer` и
`CALayerContext` в заглушке минимальные методы (`+layer` возвращает
инициализированный экземпляр; `setAnchorPoint:`, `setGeometryFlipped:`,
`setAutoresizingMask:`, `-[CALayerContext setLayer:]`, `setSubwindow:` —
сохраняют значение), то:

1. `fwdmiss` падает с 25 ровно до 18 (шесть своих селекторов плюс унаследованный
   `+[NSViewBackingLayer layer]`); другое значение — ошибка измерения или
   наследования, не ответ на гипотезу;
2. кадры `ABORT` меняются: первый кадр `ChromeFW+0x1e3a378` исчезает или уходит
   дальше по стеку хотя бы в одном из десяти ранов, либо `top=rc133x10` перестаёт
   быть 10.

Опровержение: `fwdmiss=18`, но все десять ранов снова `rc=133` с тем же
первым кадром — тогда слой QuartzCore не причина rc133, следующий по таблице
кластер — AppKit (кластер 1), в котором 8 разных классов.

Команда проверки (после правки заглушки и пересборки только QuartzCore):

```sh
sh build-freebsd/chrome-series.sh fix24 10 120
grep -a '^SERIES' "$DARLING_BUILD_DIR"/../series-summaries/fix24.txt
grep -ah '^ABORT' "$DARLING_BUILD_DIR"/chrome-sr-fix24*-crash.txt | grep -c 'frames=ChromeFW+0x1e3a378'
```

Ожидание при подтверждении: `fwdmiss=18`; третья команда < 10. База на
текущей серии: третья команда даёт 10 (все 10 строк `ABORT` в
`chrome-sr-fix24*-crash.txt` начинаются с `ChromeFW+0x1e3a378`).
