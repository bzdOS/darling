# imp-resolve-dynamic: ветка резолва description для NSConstantDictionary (шаг 37, свежий замер)

База: pr-arm64 = 5ff185102be5e34a96bd695ce398feefc7cfed74 (ветка task/imp-resolve-dynamic, `git merge-base --is-ancestor 5ff185102 pr-arm64` / HEAD rc=0).
Цель наряда: измерить, КАКОЙ веткой резолва гость получает Foundation+0x790b0 (__NSCFCopyDescription2) как IMP description для пустого NSConstantDictionary. Инструменты: существующий probe-харнесс (`tests/objc-constdict-desc-macho`, `build-freebsd/run-constdict-desc-probe.sh`, launch-dynamic; без пересборки mldr/dyld, без скачиваний).

## Свежий прогон (этот ход)

- Лог: `docs/task-imp-resolve-dynamic/constdict-desc-probe-1113-034115.log` (снят 2026-11-13 03:41Z).
- Команда (repro): `cd $DARLING_SRC_DIR && DARLING_SRC_DIR=/opt/darling/src DARLING_OVERLAY=/opt/darling/overlay DARLING_BUILD_DIR=/opt/darling/build sh build-freebsd/run-constdict-desc-probe.sh 45`

### Именованная ветка резолва + адрес хита (выдержка)

```
118:CONSTDICT stub method-list-count=3
119:CONSTDICT stub method[0]=copyWithZone: imp=0x19607f465700
120:CONSTDICT stub method[1]=copy imp=0x19607f465720
121:CONSTDICT stub method[2]=description imp=0x19607f4656e0
122:CONSTDICT stub responds(description)=1 getMethodImplementation=0x19607f4656e0
123:CONSTDICT where-stub-getimp value=0x19607f4656e0 dladdr=1 fname=/usr/lib/CoreFoundationExtras.dylib fbase=0x19607f465000 sname=-[NSConstantDictionary description] saddr=0x19607f4656e0
124:CONSTDICT stub isa=0x19607f467050 class=NSConstantDictionary receiver=0x7fffffdffd60 expected_foundation_790b0=0x1960803610b0
125:CONSTDICT stub before-send
126:CONSTDICT stub after-send
127:desc-imp=0x7fffffdffd60 foundation_790b0=0x1960803610b0 match=no
128:CONSTDICT stub description=0x7fffffdffd60 same-as-receiver=yes
```

### Интерпретация (по измеренному)

- Ветка: **found method in class method list** (не cache-miss→forward, не forward-handler). `responds(description)=1`, `getMethodImplementation=0x19607f4656e0` разрешился в образ **CoreFoundationExtras.dylib** (`fbase=0x19607f465000`, символ `-[NSConstantDictionary description]`, `saddr==value`) — т.е. lookup нашёл метод в метод-листе класса (branch #1/#2/#3 `lookUpImpOrForward` / найденный IMP), без `_objc_msgForward`.
- Адрес IMP: `0x19607f4656e0` (CoreFoundationExtras.dylib+offset внутри образа; `sname=-[NSConstantDictionary description]`).
- Результат send: возврат receiver (`desc-imp==receiver`), а **не** `Foundation+0x790b0` (`expected_foundation_790b0=0x1960803610b0`, `match=no`).
- Контекст: в текущем состоянии overlay/staging `NSConstantDictionary` имеет `method-list-count=3` (copyWithZone:, copy, description) в CoreFoundationExtras.dylib — класс уже не «пустой root class без методов» для этого прогона.

## Сопоставление с шагом 36 (хром, существующий лог) и честный негатив

- Шаг 36 (`diag/chrome-fw-run.log`, зафиксировано в `docs/task-stub-imp-resolve/imp-resolve-fact.md`): `[dict description]` вернул код-указатель **Foundation+0x790b0** `__NSCFCopyDescription2` = `0x5e4f21870b0` (rax/rdi/gstack).
- В свежем probe-прогоне (1113-034115) **ветка/адрес шага 36 не воспроизведены**: резолв идёт по методу класса в CoreFoundationExtras (`-[NSConstantDictionary description] @ 0x19607f4656e0`), возврат — receiver, `Foundation+0x790b0` не возвращён. Хит/стек резолва именно на `Foundation+0x790b0` в этом прогоне **не наблюдался** (не видно); ловушка MLDR_TRAP_AT на Foundation+0x790b0/libobjc lookupImp в этом ходе не ставилась (не измерено). Возможная причина расхождения — изменённое состояние стаба/кэша/окружения относительно chrome-прогона шага 36 (здесь класс с 3 методами); подтверждение для chrome-сценария требует отдельного целевого прогона с ловушкой на lookup/IMP в том же контексте.

## Замечание по прогону

После variant-b probe получает `FATAL signal 11 (code=1) at addr=0x214a25f8` (ожидаемо для варианта с isa=code-pointer, см. сам probe) — это завершение прогона варианта B, не краш интересующего замера (stub description/copy уже измерены выше, строки 118–132).

## Repro (сводно)

```sh
export DARLING_SRC_DIR=/opt/darling/src DARLING_OVERLAY=/opt/darling/overlay DARLING_BUILD_DIR=/opt/darling/build
cd $DARLING_SRC_DIR
sh build-freebsd/run-constdict-desc-probe.sh 45
# лог: $DARLING_BUILD_DIR/constdict-desc-probe-*.log
grep -n "stub method-list-count\|getMethodImplementation\|where-stub-getimp\|desc-imp=" <log>
```
