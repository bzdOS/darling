# SERIES fix24: rc133 и класс OBJC-FWD-MISS — 32 селектора

Диагностика одной серии, fixation-стиль: здесь только измеренное и его
воспроизводимые команды. Гипотеза в конце отмечена как гипотеза.

## Источник

- Лог серии: `$DARLING_BUILD_DIR/fix24-series-1120b.log`
  (шапка `LAUNCH fix24 10 120 start=2026-11-20T14:41:29Z driver_md5=6c7711b4fdb1ef6d0e4d209bea7fcefd git_head=77925d7f207a2097d23148835ced8009a403f02b`)
- Логи ранов: `$DARLING_BUILD_DIR/chrome-sr-fix24{1..10}.log`
- SUMMARY: `$DARLING_BUILD_DIR/../series-summaries/fix24.txt`
  `SERIES fix24 N=10 valid=10 survived20=3 top=rc133x7 windows=0 fwdmiss=32 mldr=68a3c9ede158fdee2aea9749f794fab6`
- Ценз rc по ран-логам: `rc=133 ×7, rc=139 ×1, rc=132 ×1, rc=124 ×1`
  (команда: `for f in "$DARLING_BUILD_DIR"/chrome-sr-fix24*.log; do grep -a '^WRAPPER_RC=' "$f" | tail -1; done | sort | uniq -c`)

Сбор селекторов:

```sh
grep -ah 'OBJC-FWD-MISS' "$DARLING_BUILD_DIR"/chrome-sr-fix24*.log | sort | uniq -c | sort -rn
```

Контроль сходимости с SUMMARY (вывод):

```
$ grep -ah 'OBJC-FWD-MISS' "$DARLING_BUILD_DIR"/chrome-sr-fix24*.log | sort -u | wc -l
      32
$ grep -ah 'OBJC-FWD-MISS' "$DARLING_BUILD_DIR"/chrome-sr-fix24*.log | wc -l
     256
$ grep -o 'fwdmiss=[0-9]*' "$DARLING_BUILD_DIR"/fix24-series-1120b.log | tail -1
fwdmiss=32
```

Частота = в скольких логах ранов селектор встречается (каждый логируется один
раз на пару класс×селектор, `FoundationExtras.dylib` forward.c:127); сумма
частот = 256, уникальных = 32, ровно как `fwdmiss=32` в SUMMARY.

Семантика строки (источник, не пересказ): `src/freebsd-shims`-путь из наряда
отсутствует в дереве (см. «Сверка путей»), взято
`build-freebsd/stub-frameworks/behaviors/FoundationExtras.dylib/forward.c`:
глобальный forward-хендлер отвечает nil/0 на неизвестный селектор и печатает
`OBJC-FWD-MISS` один раз на пару класс×селектор.

## Таблица всех 32 уникальных селекторов с частотами

| # | селектор | частота (ранов из 10) | кластер-владелец |
|---|----------|----------------------|------------------|
| 1 | `-[NSConstantDictionary _fastCharacterContents]` | 10 | Foundation / CoreFoundationExtras |
| 2 | `-[NSConstantDictionary isNSArray__]` | 10 | Foundation / CoreFoundationExtras |
| 3 | `-[NSConstantDictionary isNSData__]` | 10 | Foundation / CoreFoundationExtras |
| 4 | `-[NSConstantDictionary isNSDate__]` | 10 | Foundation / CoreFoundationExtras |
| 5 | `-[NSConstantDictionary isNSDictionary__]` | 10 | Foundation / CoreFoundationExtras |
| 6 | `-[NSConstantDictionary isNSNumber__]` | 10 | Foundation / CoreFoundationExtras |
| 7 | `-[NSConstantDictionary isNSString__]` | 10 | Foundation / CoreFoundationExtras |
| 8 | `-[NSConstantDictionary length]` | 10 | Foundation / CoreFoundationExtras |
| 9 | `-[NSConstantDictionary release]` | 10 | Foundation / CoreFoundationExtras |
| 10 | `+[NSImage imageWithSystemSymbolName:accessibilityDescription:]` | 10 | AppKit |
| 11 | `-[NSFileManager ubiquityIdentityToken]` | 8 | Foundation |
| 12 | `-[NSUserNotificationCenter deliveredNotifications]` | 8 | AppKit |
| 13 | `+[ASWebAuthenticationSessionWebBrowserSessionManager sharedManager]` | 7 | AuthenticationServices |
| 14 | `+[CALayer layer]` | 7 | QuartzCore |
| 15 | `+[NSAccessibilityRemoteUIElement remoteTokenForLocalUIElement:]` | 7 | AppKit |
| 16 | `+[NSEvent pressedMouseButtons]` | 7 | AppKit |
| 17 | `+[NSViewBackingLayer layer]` | 7 | AppKit (слой) |
| 18 | `+[UTType typeWithMIMEType:]` | 7 | UniformTypeIdentifiers |
| 19 | `-[BrowserNativeWidgetWindow addTitlebarAccessoryViewController:]` | 7 | AppKit (NSWindow) |
| 20 | `-[BrowserNativeWidgetWindow convertPointFromScreen:]` | 7 | AppKit (NSWindow) |
| 21 | `-[BrowserNativeWidgetWindow setAnimationBehavior:]` | 7 | AppKit (NSWindow) |
| 22 | `-[BrowserNativeWidgetWindow setTitlebarAppearsTransparent:]` | 7 | AppKit (NSWindow) |
| 23 | `-[CALayerContext setLayer:]` | 7 | QuartzCore |
| 24 | `-[CALayerContext setSubwindow:]` | 7 | QuartzCore |
| 25 | `-[CALayer setAnchorPoint:]` | 7 | QuartzCore |
| 26 | `-[CALayer setAutoresizingMask:]` | 7 | QuartzCore |
| 27 | `-[CALayer setGeometryFlipped:]` | 7 | QuartzCore |
| 28 | `-[__NSCFDictionary symbolicTraits]` | 7 | Foundation (класс-получатель чужой) |
| 29 | `-[NSKVONotifying_BrowserCrApplication isFullKeyboardAccessEnabled]` | 7 | AppKit (NSApplication) |
| 30 | `-[NSTitlebarAccessoryViewController setFullScreenMinHeight:]` | 7 | AppKit |
| 31 | `-[NSTitlebarAccessoryViewController setHidden:]` | 7 | AppKit |
| 32 | `-[NSTitlebarAccessoryViewController setLayoutAttribute:]` | 7 | AppKit |

Порядок колонки частот сверяется напрямую:
`... | sort | uniq -c | sort -rn` (вывод этой команды — источник таблицы).

## Группировка по фреймворку / классу-владельцу

| кластер (куда должен упасть ответ) | селекторов | попаданий | доля от 256 |
|------------------------------------|-----------|-----------|-------------|
| Foundation: `NSConstantDictionary` (9), `NSFileManager` (1), `__NSCFDictionary` (1) | 11 | 105 | 41.0 % |
| AppKit: `BrowserNativeWidgetWindow`→NSWindow (4), `NSTitlebarAccessoryViewController` (3), `NSImage`, `NSUserNotificationCenter`, `NSEvent`, `NSAccessibilityRemoteUIElement`, `NSViewBackingLayer`, `NSKVONotifying_BrowserCrApplication`→NSApplication (по 1) | 13 | 95 | 37.1 % |
| QuartzCore: `CALayer` (4, из них `+layer`), `CALayerContext` (2) | 6 | 42 | 16.4 % |
| UniformTypeIdentifiers: `UTType` | 1 | 7 | 2.7 % |
| AuthenticationServices: `ASWebAuthenticationSession…` | 1 | 7 | 2.7 % |
| **итого** | **32** | **256** | **100 %** |

## Топ-3 кластера: недостающий провайдер

### 1. Foundation / `NSConstantDictionary` — 11 селекторов, 105 попаданий (41 %)

Провайдер: `CoreFoundationExtras.dylib` (класс объявлен и собран именно там),
а не Foundation.framework.

```sh
$ nm "$DARLING_OVERLAY"/usr/lib/CoreFoundationExtras.dylib | grep 'OBJC_CLASS_\$_NSConstantDictionary$'
0000000000002140 D _OBJC_CLASS_$_NSConstantDictionary
$ nm "$DARLING_OVERLAY"/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation | grep -c 'OBJC_CLASS_\$_NSConstantDictionary$'
0
$ sed -n '14,25p' "$DARLING_BUILD_DIR"/stub-data/CoreFoundationExtras_dylib.m
@interface NSConstantDictionary
@end
@implementation NSConstantDictionary
+ (id)alloc { return 0; }
+ (id)allocWithZone:(void *)z { (void)z; return 0; }
+ (id)new { return 0; }
- (id)description { return self; }
- (id)copyWithZone:(void *)zone { (void)zone; return self; }
- (id)copy { return self; }
- (id)class { return self; }
@end
```

Вывод: класс — порожневой шаблон (`objc_root_class` выше по файлу) из
генератора стаб-даты; все девять недостающих селекторов кластера
(`length`, `release`, `_fastCharacterContents` и шесть `isX__`) в шаблоне
отсутствуют. Проверка самого образа:

```sh
$ strings "$DARLING_OVERLAY"/usr/lib/CoreFoundationExtras.dylib | grep -xE 'length|isNSString__|isNSArray__|_fastCharacterContents|release|count'
(пусто)
```

Остальные два селектора кластера:
- `-[NSFileManager ubiquityIdentityToken]` — есть только объявление
  `src/external/foundation/include/Foundation/NSFileManager.h`, реализации в
  дереве нет (`grep -rl ubiquityIdentityToken src/external/cocotron` → пусто).
- `-[__NSCFDictionary symbolicTraits]` — `symbolicTraits` реализован в
  cocotron AppKit только у `NSFont`/`NSFontDescriptor`
  (`grep -rl symbolicTraits src/external/cocotron` → `NSFont.m`, `NSFontDescriptor.m`);
  словарь-получатель тут ошибка отправителя, у `__NSCFDictionary` такого API нет ни у кого.

### 2. AppKit — 13 селекторов, 95 попаданий (37 %)

Провайдер: cocotron AppKit (образ `AppKit.framework/Versions/Current/AppKit`,
3 351 984 байт) и его категории.

```sh
$ grep -rn 'setTitlebarAppearsTransparent|setAnimationBehavior|convertPointFromScreen|addTitlebarAccessoryViewController' \
    "$DARLING_SRC_DIR"/src/external/cocotron/AppKit/NSWindow*.m
(пусто)
$ sed -n '1,17p' "$DARLING_SRC_DIR"/src/external/cocotron/AppKit/NSTitlebarAccessoryViewController.m
... methodSignatureForSelector: / forwardInvocation: только (forward-стаб, тел методов нет)
$ for s in pressedMouseButtons deliveredNotifications remoteTokenForLocalUIElement isFullKeyboardAccessEnabled imageWithSystemSymbolName; do \
    grep -rl "$s" "$DARLING_SRC_DIR"/src/external/cocotron "$DARLING_SRC_DIR"/build-freebsd/stub-frameworks; done
(пусто по всем пяти)
$ strings "$DARLING_OVERLAY"/System/Library/Frameworks/AppKit.framework/Versions/Current/AppKit | \
    grep -xE 'setTitlebarAppearsTransparent:|setAnimationBehavior:|convertPointFromScreen:|addTitlebarAccessoryViewController:|pressedMouseButtons|imageWithSystemSymbolName:accessibilityDescription:|deliveredNotifications|isFullKeyboardAccessEnabled|remoteTokenForLocalUIElement:|setFullScreenMinHeight:|setLayoutAttribute:'
setAnimationBehavior:
setTitlebarAppearsTransparent:
```

Вывод: в образ AppKit два селектора попадают только как упоминания
(`__objc_selrefs`), но не как реализация —ран measures FWD-MISS на них тоже;
девять селекторов в образе отсутствуют целиком. `+[NSViewBackingLayer layer]`:
`src/external/cocotron/AppKit/NSViewBackingLayer.m` есть, метода `+layer` в нём нет.

### 3. QuartzCore — 6 селекторов, 42 попадания (16 %)

Провайдер: `QuartzCore.framework`, который в этой сборке — стаб, а не
cocotron-реализация.

```sh
$ stat -f '%Sm %z bytes' -t '%Y-%m-%d' "$DARLING_OVERLAY"/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore
2026-08-28 15168 bytes
$ nm "$DARLING_OVERLAY"/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore | grep -c 'OBJC_CLASS_'
13
$ strings <тот же QuartzCore> | grep -xE 'setAnchorPoint:|setGeometryFlipped:|setAutoresizingMask:|setLayer:|setSubwindow:|layer'
(пусто)
$ grep -rn 'D _OBJC_CLASS_\$_CALayer$' по всем образам overlay
overlay/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore
```

То есть `CALayer` и `CALayerContext` существуют в рантайме только как классы
15-килобайтного стаба, и ни один из 6 селекторов кластера не объявлен даже в
`__objc_methname` этого образа. Что стоит за стабом и что не подключено:

```sh
$ grep -n '@implementation CALayer$' "$DARLING_SRC_DIR"/src/frameworks/dev-stubs/QuartzCore/src/classes.m
161:@implementation CALayer        # только methodSignatureForSelector:/forwardInvocation:
$ grep -n 'dev-stubs/QuartzCore' "$DARLING_SRC_DIR"/src/frameworks/CMakeLists.txt
176: add_subdirectory(dev-stubs/QuartzCore)
$ grep -n 'setAnchorPoint:\|setLayer:\|setSubwindow:' \
    "$DARLING_SRC_DIR"/src/external/cocotron/QuartzCore/CALayer.m \
    "$DARLING_SRC_DIR"/src/external/cocotron/QuartzCore/CALayerContext.m
CALayer.m:85: - (void) setAnchorPoint: (CGPoint) value {
CALayerContext.m:53: - (void) setLayer: (CALayer *) layer {
CALayerContext.m:62: - (void) setSubwindow: (CGSubWindow*) subwindow
```

Вывод: у cocotron-QuartzCore есть реализация трёх из шести селекторов, но она
не подключена к сборке; `+[CALayer layer]`, `setGeometryFlipped:`,
`setAutoresizingMask:` нет даже в cocotron-исходнике.

### Сверка путей (из наряда)

- `src/freebsd-shims/stub-frameworks` — **в дереве отсутствует**
  (`ls $DARLING_SRC_DIR/src/freebsd-shims/` → `iokit_shim.c missing-headers`),
  при этом `build-freebsd/build-gui.sh:396` собирает оттуда стабы:
  `STUBS="${SRC}/src/freebsd-shims/stub-frameworks"`, а `${STUBS}/QuartzCore/QuartzCore.m`
  в дереве не найти (`find $DARLING_SRC_DIR -name QuartzCore.m` → пусто).
  Шаг сборки стабов QuartzCore/CoreData из build-gui.sh сейчас не имеет источника.
  Это наберено как факт наряда, чинить нечего с моей стороны — вопрос голове.
- Что реально проверено вместо него:
  `$DARLING_SRC_DIR/build-freebsd/stub-frameworks` (+ `behaviors/*.dylib`),
  `$DARLING_SRC_DIR/src/frameworks/dev-stubs`, `$DARLING_BUILD_DIR/stub-data`,
  `$DARLING_BUILD_DIR/objc-suppl`, и сами образы `$DARLING_OVERLAY`.

## Гипотеза минимального фикса (одна)

Гипотеза: стена класса rc133 начинается с порожневого `NSConstantDictionary` —
он единственный кластер, который горит в самом раннем падении (ран 1:
`OBJC-FWD-MISS`-строки 14472–14480 идут ещё до первого `FATAL signal 6` на
14883 и до финального `WRAPPER_RC=132`), а его ответ nil/0 на `length`/
`_fastCharacterContents` отдаёт Chrome пустую константную словарную структуру,
на которой его CHECK и падает (rc132/133 = int3). Минимальный фикс — в
генераторе стаб-даты (тот, что пишет `CoreFoundationExtras_dylib.m`) дать
шаблону `NSConstantDictionary` девять недостающих методов с честными ответами
(длина/количество — из хранимых пар, `isX__` — типизированный `NO`,
`release` — no-op): правится один генеренный файл, AppKit и QuartzCore не
трогаются; проверка — повтор той же серии: `fwdmiss` 32 → 23, и сдвиг
`top=rc133x7`. Если `fwdmiss` упадёт, а rc133 останется — гипотеза отвергнута,
следующий по времени кластер — QuartzCore (панель 3).

Команда проверки гипотезы:

```sh
sh build-freebsd/chrome-series.sh fix24 10 120   # тот же префикс, тот же барьер
grep -o 'fwdmiss=[0-9]*' "$DARLING_BUILD_DIR"/fix24-series-*.log | tail -1
```
