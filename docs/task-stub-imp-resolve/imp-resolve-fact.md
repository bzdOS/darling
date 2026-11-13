# stub-imp-resolve: живой факт резолва description для NSConstantDictionary (Chrome шаг 36)

Источник: существующий лог живого прогона `$DARLING_SRC_DIR/../diag/chrome-fw-run.log` (шаг 36, chrome-macho; FATAL signal 11 в libobjc+0x480DD). Артефакт фиксирует измеренный факт «образ+адрес IMP/результата резолва» и воспроизводимую выдержку лога. Отдельный прогон с MLDR_TRAP_AT/dyld-trace в этом ходе не запускался (не измерено здесь).

## Замер 1 — образ и адрес __NSCFCopyDescription2 (Foundation)

```
$ nm -a "$FOUND" | grep 790b0
00000000000790b0 t __NSCFCopyDescription2
```
($FOUND = $DARLING_OVERLAY/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation)

Адрес резолва в рантайме (тот же прогон, слайд образа 0x5e4f210e000): `Foundation+0x790b0 = 0x5e4f21870b0`.

## Замер 2 — выдержка живого лога (образ+адрес в момент вызова)

```
1336848:[darling-mldr] FATAL signal 11 (code=1) at addr=0x348e5894868
1336850:  rip=0x000005e4e141a0dd  rax=0x000005e4f21870b0  rbx=0x000005e4f2227a0b
1336852:  rdi=0x000005e4f21870b0  rbp=0x00007fffffdfda90  rsp=0x00007fffffdfda78
1336861-  [gstack+   0] 0x000005e4f21870e1
1336863-  [gstack+  16] 0x000005e4f14812e8
1336865-  [gstack+  32] 0x000005e4f1954264
```

Интерпретация по замеру:
- `[gstack+0]=0x5e4f21870e1` = Foundation+0x790e1 — возврат в `__NSCFCopyDescription2` после второго `objc_msgSend` (см. docs/OBJC-ISA-CALLER.md).
- `rax=0x5e4f21870b0`, `rdi=0x5e4f21870b0` = образ Foundation, смещение +0x790b0 (`__NSCFCopyDescription2`) — код-указатель, возвращённый как результат `[dict description]` и переданный вторым сообщением как receiver для `copy`.
- `[gstack+16]=0x5e4f14812e8` — статический NSConstantDictionary в __DATA Chrome framework (isa bind ordinal 0x33d -> `_OBJC_CLASS_$_NSConstantDictionary`, docs/OBJC-COPYDESC-PATH.md).
- `[gstack+32]=0x5e4f1954264` = CoreFoundation+0xED264 (`___CFStringAppendFormatCore`).

## Замер 3 — класс-стаб (файл) и путь

```
$ nm -gU "$CFE" | grep NSConstantDictionary
0000000000002050 D _OBJC_CLASS_$_NSConstantDictionary
0000000000002078 D _OBJC_METACLASS_$_NSConstantDictionary
```
($CFE = $DARLING_OVERLAY/usr/lib/CoreFoundationExtras.dylib). class_ro_t: root (super=0), baseMethods=0x0, строк description/copy нет (docs/OBJC-DESC-IMP-BIND.md).

Итог (факт): в живом прогоне резолв `description` для NSConstantDictionary дал код-указатель образа Foundation `Foundation+0x790b0 (__NSCFCopyDescription2, 0x5e4f21870b0)` — измерено по rax/rdi/gstack лога. В файле стаба IMP отсутствует (пустой root class, 0 методов) — соответствует артефакту гостевого разрешения/кэша (классификация b). Точная ветка lookupImp в момент возврата отдельной ловушкой здесь не подтверждена (не измерено).

## Repro (команда воспроизведения/проверки)

```sh
LOG="$DARLING_SRC_DIR/../diag/chrome-fw-run.log"
FOUND="$DARLING_OVERLAY/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation"
CFE="$DARLING_OVERLAY/usr/lib/CoreFoundationExtras.dylib"
nm -a "$FOUND" | grep 790b0
grep -n -B1 -A8 "FATAL signal 11" "$LOG" | head -20
nm -gU "$CFE" | grep NSConstantDictionary
```
