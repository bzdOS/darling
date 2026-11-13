# chrome-desc-trap: хозяин резолва description на реальном классе (шаг 38, веха 04)

База: pr-arm64 = fb60acb0c14fc9cec51744ac2ff4c547c28658d4 (принят шагом 37), ветка
task/chrome-desc-trap. Проба: `tests/objc-constdict-desc.m`, блок `real-class`:
`objc_getClass("NSConstantDictionary")` по имени класса из шага 36, method-list
класса и метакласса, `class_getMethodImplementation(description)` + `dladdr` до и
после установки логгера на `+resolveInstanceMethod:` метакласса, затем одна
отправка `description` приёмнику, у которого isa — реальный класс.

Замер шёл с `MLDR_TRAP_AT='Foundation+0x790b0'` (resume=0, ловушка фатальна на
хите), поэтому всё, что читается без отправки сообщения, прочитано и напечатано
до неё: отправка — последней.

## Repro

```sh
cd "$DARLING_SRC_DIR" && sh build-freebsd/build-constdict-desc-probe.sh
# -> tests/objc-constdict-desc-macho: Mach-O 64-bit x86_64 executable

/usr/local/bin/sudo -n env \
  DARLING_SRC_DIR="$DARLING_SRC_DIR" DARLING_OVERLAY="$DARLING_OVERLAY" \
  DARLING_BUILD_DIR="$DARLING_BUILD_DIR" \
  DARLING_TEST_BINARY=objc-constdict-desc-macho DARLING_SMOKE_REFRESH=1 \
  MLDR_TRAP_AT='Foundation+0x790b0' \
  timeout -k 5 300 "$DARLING_BUILD_DIR/launch-dynamic" > "$DARLING_BUILD_DIR/desc-realclass.log" 2>&1
# -> rc=124 (гость умер на FATAL signal 11, супервизор launch-dynamic не завершился)
grep -an 'real-stub\|real-class\|real-resolve' "$DARLING_BUILD_DIR/desc-realclass.log"
grep -an 'desc-imp=' "$DARLING_BUILD_DIR/desc-realclass.log"
```

Полный лог — 246 строк, в `$DARLING_BUILD_DIR/desc-realclass.log` (не коммитится:
содержит локальные пути узла). Ниже — выдержка без локальных путей.

## Лог (выдержка, 45 строк)

```
CONSTDICT real-stub-class=0x3bff1b067050 name=NSConstantDictionary
CONSTDICT real-stub method-list-count=3
CONSTDICT real-stub method[0]=copyWithZone: imp=0x3bff1b065700
CONSTDICT real-stub method[1]=copy imp=0x3bff1b065720
CONSTDICT real-stub method[2]=description imp=0x3bff1b0656e0
CONSTDICT real-stub responds(description)=1 getMethodImplementation=0x3bff1b0656e0
CONSTDICT where-real-stub-getimp value=0x3bff1b0656e0 dladdr=1 fname=/usr/lib/CoreFoundationExtras.dylib fbase=0x3bff1b065000 sname=-[NSConstantDictionary description] saddr=0x3bff1b0656e0
CONSTDICT real-class class=0x3bff1b067050 name=NSConstantDictionary meta=0x3bff1b067078 meta-name=NSConstantDictionary
CONSTDICT real-class method-list class=3 meta=1
CONSTDICT real-class pre-hook getMethodImplementation(description)=0x3bff1b0656e0 responds=1
CONSTDICT where-real-class-pre-hook-getimp value=0x3bff1b0656e0 dladdr=1 fname=/usr/lib/CoreFoundationExtras.dylib fbase=0x3bff1b065000 sname=-[NSConstantDictionary description] saddr=0x3bff1b0656e0
CONSTDICT real-class resolve-hook=addMethod added=1 meta-responds-now=1
CONSTDICT real-class post-hook getMethodImplementation(description)=0x3bff1b0656e0
CONSTDICT where-real-class-post-hook-getimp value=0x3bff1b0656e0 dladdr=1 fname=/usr/lib/CoreFoundationExtras.dylib fbase=0x3bff1b065000 sname=-[NSConstantDictionary description] saddr=0x3bff1b0656e0
CONSTDICT real-class resolve-fired-after-hook=0
CONSTDICT real-class resolve-fired-before-send=0
CONSTDICT real-class isa=0x3bff1b067050 class=NSConstantDictionary receiver=0x7fffffdffd40 expected_foundation_790b0=0x3bff1bf610b0
CONSTDICT real-class before-send
CONSTDICT real-class after-send
desc-imp=0x7fffffdffd40 foundation_790b0=0x3bff1bf610b0 match=no
CONSTDICT real-class description=0x7fffffdffd40 same-as-receiver=yes
CONSTDICT real-class value=0x7fffffdffd40 dladdr=0 fname=(none) fbase=0x0 sname=(none) saddr=0x0
CONSTDICT real-class before-copy-send
CONSTDICT real-class after-copy-send
CONSTDICT real-class copy-ret=0x7fffffdffd40 same-as-receiver=yes
CONSTDICT empty-stub-class=0x7f9f7c77d230 name=EmptyConstDictProbe
CONSTDICT empty-stub method-list-count=0
CONSTDICT empty-stub responds(description)=0 getMethodImplementation=0x3bff1b0b1e80
CONSTDICT where-empty-stub-getimp value=0x3bff1b0b1e80 dladdr=1 fname=/usr/lib/libobjc.A.dylib fbase=0x3bff1b069000 sname=_objc_msgForward saddr=0x3bff1b0b1e80
CONSTDICT empty-stub isa=0x7f9f7c77d230 class=EmptyConstDictProbe receiver=0x7fffffdffd40 expected_foundation_790b0=0x3bff1bf610b0
CONSTDICT empty-stub before-send
CONSTDICT empty-stub after-send
desc-imp=0x3bff1ce10eda foundation_790b0=0x3bff1bf610b0 match=no
CONSTDICT empty-stub description=0x3bff1ce10eda same-as-receiver=no
CONSTDICT empty-stub value=0x3bff1ce10eda dladdr=1 fname=/usr/lib/system/libsystem_trace.dylib fbase=0x3bff1ce0b000 sname=sbuf_delete saddr=0x3bff1ce0f050
CONSTDICT empty-stub before-copy-send
[darling-mldr] FATAL signal 11 (code=1) at addr=0x697263736578
  si_pid=0 si_uid=0 si_value=0x0 sival_ptr=0x0
  rip=0x00003bff1b0b10dd  rax=0x00003bff1cdb594d  rbx=0x00000008269a589b
  rcx=0x0000000000000000  rdx=0x0000000000000000  rsi=0x00003bff1cdb594d
  rdi=0x00003bff1ce10eda  rbp=0x00007fffffdffdd0  rsp=0x00007fffffdffd38
  r8 =0x0000000000000000  r9 =0x0000000000000005  r10=0x0000697263736560
  r11=0x00003bff1cdb594d  r12=0x00003bff1ce10eda  r13=0x00003bff1bf610b0
  r14=0x00000008269a5a06  r15=0x00000008269a5a0a
  backtrace (3 frames):
```

Ловушка: строка 69 полного лога — `MLDR_TRAP_AT: armed Foundation+0x790b0 at
0x3bff1bf610b0 (resume=0 emulate=0 orig=5548)`; строки `TRAP_AT hit` в логе нет.

## Факты

1. Реальный класс в текущем overlay — не пустой стаб: `method-list-count=3`
   (`copyWithZone:`, `copy`, `description`), `responds(description)=1`.
   Runtime IMP `0x3bff1b0656e0` = fbase + 0x6e0, `dladdr` имя
   `-[NSConstantDictionary description]`, образ `/usr/lib/CoreFoundationExtras.dylib`.
2. `+resolveInstanceMethod:` не срабатывал: на метаклассе его не было
   (`resolve-hook=addMethod added=1`), после установки логгера
   `resolve-fired=0` и до отправки, и после неё.
3. Хита `Foundation+0x790b0` нет: `description` вернул сам приёмник
   (`desc-imp=0x7fffffdffd40 match=no`), тот же указатель на `copy` — без
   падения (в отличие от пустого стаба, шаг 37).
4. Исход — не (a) и не (b), а отдельный (d): IMP `description` — собственный
   метод класса стаба в CoreFoundationExtras, резолв в этом сценарии не
   участвует вовсе; `Foundation+0x790b0` не появляется ни как IMP, ни как
   результат.
5. Противоречие с шагом 36: в `docs/OBJC-DESC-IMP-BIND.md` записано
   `baseMethods=0x0`. Сейчас `$DARLING_OVERLAY/usr/lib/CoreFoundationExtras.dylib`
   (13768 байт, mtime 2026-10-13 10:04) на диске имеет `baseMethods=0x21c8`,
   count 3: `description` imp=0x6e0, `copyWithZone:` imp=0x700, `copy` imp=0x720.
   Staged-копия, против которой слинкована проба
   (`$DARLING_BUILD_DIR/real-macho/staged-overlay/usr/lib/`, 13352 байта, mtime
   2026-10-09 21:57), по-прежнему `baseMethods=0x0` — то есть гость загрузил
   overlay-копию, а не staged (номера IMP совпали с файловыми смещениями overlay).
6. Не измерено: статический dict из `__DATA` chrome в этом процессе недоступен;
   собрать константный словарный литерал нельзя — `nm` по staged
   Foundation/CoreFoundation/CoreFoundationExtras символа `__NSConstantDictionary`
   не находит (вывод пуст).
7. Имя единственного метода метакласса (`method-list ... meta=1`) не печаталось —
   не измерено.
8. `rc=124`: после `FATAL signal 11` на `empty-stub before-copy-send` супервизор
   `launch-dynamic` не завершился, `timeout -k 5 300` снял его; сам прогон дошёл
   до конца пробы, лог полный (246 строк).

## Что это меняет для лейна

Условие шага 36/37 («NSConstantDictionary — пустой стаб без методов») к текущему
overlay не относится: dylib пересобрали после шага 36, и класс теперь сам
отвечает на `description`, `copy` и `copyWithZone:`, возвращая self. На этом
классе ни резолв, ни `Foundation+0x790b0` не воспроизводятся. Любой повтор
chrome-сценария на текущем overlay будет мерить уже другой класс — до замера
надо сверить, какую версию CoreFoundationExtras.dylib видит гость.
