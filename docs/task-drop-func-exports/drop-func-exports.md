# drop-func-exports: шаг 42 — стена §9.11 на уровне экспортов (попытка, откат)

База: pr-arm64 = a238582edaee03f4a3d21a30bd759fcc50cf2984 (шаг 41 влит).
Ветка: task/drop-func-exports.

Гипотеза шага: 24 FUNC-имени из классификации шага 41 не стабить вовсе — провайдер
экспортирует те же имена, и dyld привяжет вызов прямо к нему. Форвард на себя дал бы
петлю, `.quad 0` — ту же стену классом ниже, поэтому имя надо просто не излучать.

## Что изменено

`build-freebsd/gen-cg-supl.py`: таблица `SKIP_NAMES`, проверяемая **до**
`FORWARD_NAMES` и `DATA_PREFIXES`; пропущенное имя не попадает в `.s` вообще.
Счётчик `skipped` печатается в итоговой строке.

Классификация шага 41 пересчитана офлайн по провайдерам (`llvm-nm -m --defined-only`
по `usr/lib/system/*.dylib`, `libSystem.B.dylib` и `LSysX.dylib`): **24** имени,
у которых определение провайдера лежит в `__TEXT,__text`, — 22 из прозы дока шага 41
плюс `___atomic_load` и `___atomic_store` (оба в `LSysX.dylib`). bzero-пара в таблицу
не входит: `___bzero → _bzero → __platform_bzero` остаются форвардами.

## Скип включён: 23 имени, bzero-пара не тронута

```
$ sh build-freebsd/build-suppl-stubs.sh
=== suppl (install_name /usr/lib/supl.dylib, 858 symbols)
wrote .../suppl.s: 822 text stubs, 11 data stubs, 2 forwarding stubs, 23 skipped, 858 total
=== cg-supl (install_name /usr/lib/suppl.dylib, 886 symbols)
wrote .../cg-supl.s: 848 text stubs, 13 data stubs, 2 forwarding stubs, 23 skipped, 886 total
```

23 имени исчезли из экспорта, `___bzero` остался форвардом (проверка ниже — в откате,
тот же тип):

```
$ llvm-nm -m "$DARLING_OVERLAY/usr/lib/suppl.dylib" | grep -E '(___bzero|_bzero)$'
0000000000000603 (__TEXT,__text) external ___bzero
0000000000000617 (__TEXT,__text) external _bzero
```

## Прогон A со скипом: dyld отказывает

`MLDR_TRAP_AT='Foundation+0x790b0'`, staging из closure, `timeout -k 5 300`,
exit 124 (процесс стоял), 134 образа, `FATAL signal` = 0, но:

```
dlopen //Frameworks/Google Chrome for Testing Framework.framework/...: Symbol not found: ___stack_chk_fail
  Referenced from: //Frameworks/.../Google Chrome for Testing Framework (which was built for Mac OS X 13.0)
  Expected in: /usr/lib/suppl.dylib
```

## Почему провайдер не спасает: импорт двухуровневый и называет suppl

Фреймворк несёт `/usr/lib/suppl.dylib` в `LC_LOAD_DYLIB` (ordinal 67) и 844
chained import'а на него; **все 24** FUNC-имени (включая `___bzero`) — среди них:

```
$ llvm-objdump --macho --chained-fixups "$FW" | awk '/^dyld chained import\[/{getline; if ($0 ~ /lib_ordinal = 67 /) c++} END{print c}'
844
```

То, что `libsystem_c`/`libsystem_kernel`/`libsystem_m` экспортируют то же имя, роли не
играет: импорт ищется в suppl по двухуровневому пространству имён, а не в провайдере.
`___stack_chk_fail` — первое недостающее имя в таблице, дальше отказали бы остальные.

## Откат (fallback наряда)

`SKIP_NAMES` оставлен пустым, имена возвращены стабами, как в шаге 41:

```
$ sh build-freebsd/build-suppl-stubs.sh
wrote .../suppl.s: 822 text stubs, 34 data stubs, 2 forwarding stubs, 0 skipped, 858 total
wrote .../cg-supl.s: 848 text stubs, 36 data stubs, 2 forwarding stubs, 0 skipped, 886 total
```

Возвращённые имена названы в отчёте шага. Прогон A после отката — как в шаге 41:

```
FATAL signal = 0, Symbol not found = 0, dyld: loaded = 134, MLDR_TRAP_AT hit = 0, rc=124
```

Прогон B (`MLDR_TRAP_AT='Google Chrome for Testing Framework+0x212ab3a'`) — сайт
§9.11 на месте:

```
[darling-mldr] MLDR_TRAP_AT: armed Google Chrome for Testing Framework+0x212ab3a at 0x87ee373ab3a (resume=0 emulate=0 orig=e821)
[darling-mldr] === MLDR_TRAP_AT hit #1: Google Chrome for Testing Framework+0x212ab3a ===
  code @rip: e8 21 30 ba fe 48 8d bb 78 37 00 00 48 89 de 31
  stack slots resolving into known images:
    [rsp+264] Google Chrome for Testing Framework+0x212a9d2
```

`e8 21 30 ba fe` = `callq 0xccdb60`; 134 образа, `FATAL signal` = 0.

## Verdict

- Убрать имена «просто из экспорта» **нельзя**: все 23 привязаны к suppl
  двухуровневым импортом фреймворка, и dyld требует их именно в этом образе.
  Отсутствие провайдерского дубликата здесь не лечит: провайдер экспортирует те же
  имена, но импорт называет suppl.
- Единственный видимый способ убрать стабы — сделать suppl **реэкспортёром**
  настоящих провайдеров (`LC_REEXPORT_DYLIB` на `libsystem_c`, `libsystem_kernel`,
  `libsystem_m`, `libsystem_platform`): тогда импорт из suppl разрешится по цепи
  реэкспорта в настоящую функцию. Это смена владельца символов, а не удаление имён;
  решение — за головой.
- Состояние overlay: как в шаге 41 (34 data-стаба, bzero-форварды на месте);
  следующая стена — §9.11 (`0xccdb60`), прогон B её достигает.

## Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_SRC_DIR=... DARLING_OVERLAY=... DARLING_BUILD_DIR=...

sh build-freebsd/build-suppl-stubs.sh
cp "$DARLING_BUILD_DIR"/suppl-stubs/{suppl,cg-supl}.dylib "$DARLING_OVERLAY"/usr/lib/

llvm-nm -m "$DARLING_OVERLAY/usr/lib/suppl.dylib" | grep -E '(___bzero|_bzero)$'

sudo -n env DARLING_SRC_DIR="$DARLING_SRC_DIR" DARLING_OVERLAY="$DARLING_OVERLAY" \
  DARLING_BUILD_DIR="$DARLING_BUILD_DIR" DARLING_TEST_BINARY=chrome-macho \
  CHROME_APP=... "DARLING_STAGING_TREES=$(cat "$DARLING_BUILD_DIR/chrome-staging-trees.txt")" \
  WAYLAND_DISPLAY=wayland-1 XDG_RUNTIME_DIR=/tmp/wayland-test DARLING_SMOKE_REFRESH=1 \
  MLDR_TRAP_AT='Foundation+0x790b0' timeout -k 5 300 "$DARLING_BUILD_DIR/launch-dynamic" > log 2>&1
grep -cE 'FATAL signal|Symbol not found' log ; grep -c 'dyld: loaded' log
```
