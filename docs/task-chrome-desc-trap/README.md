# task/chrome-desc-trap (лейн 37, веха 04) — честный негатив

База: pr-arm64 = f7b69686a9d28c340e9a80902883b01adf637d42 (`git merge-base --is-ancestor f7b69686a HEAD` rc=0). Ветка: task/chrome-desc-trap.

## Замысел замера

1) В пробе `tests/objc-constdict-desc.m` добавлен вариант стаба БЕЗ методов: `EmptyConstDictProbe` — `objc_allocateClassPair(NULL, ..., 0)` + `objc_registerClassPair`, без добавления методов (ожидаемо `method-list-count=0`, как шаг 36). Staging/overlay не трогались. Проба собрана (`sh build-freebsd/build-constdict-desc-probe.sh`, rc=0; бинарь `tests/objc-constdict-desc-macho`).
2) План: прогон через launch-dynamic с ловушкой `MLDR_TRAP_AT="Foundation+0x790b0"` (вход __NSCFCopyDescription2, resume=0) на сценарии пустого стаба — зафиксировать хит + ≥2 именованных кадра над ним (кто кладёт код-указатель в IMP/результат).

## Что не сработало (среда) — прогон не выполнен

`launch-dynamic` требует root (`Must run as root`), а `sudo` на хосте нефункционален (не setuid root), поэтому живой прогон с `MLDR_TRAP_AT` в этом ходе выполнить невозможно.

Факты (2026-11-13, команда и вывод):

```
$ sudo -n true 2>&1; echo "sudo_rc=$?"
sudo: ... must be owned by uid 0 and have the setuid bit set
sudo_rc=1

$ timeout 15 env DARLING_SRC_DIR=<src> DARLING_OVERLAY=<overlay> DARLING_BUILD_DIR=<build> \
    DARLING_TEST_BINARY=objc-constdict-desc-macho DARLING_SMOKE_REFRESH=1 MLDR_TRAP_AT='Foundation+0x790b0' \
    <build>/launch-dynamic > docs/task-chrome-desc-trap/chrome-desc-trap-790b0-nosudo-20261113.log 2>&1; echo "rc=$?"
rc=1

$ tail -5 docs/task-chrome-desc-trap/chrome-desc-trap-790b0-nosudo-20261113.log
Must run as root
```

Контроль (без ловушки, тот же отказ): `docs/task-chrome-desc-trap/chrome-desc-trap-nosudo-20261113.log` → `Must run as root`, rc=1.

Итог: хит на `Foundation+0x790b0` и стек над ним **не получены** (прогон не стартовал из-за отказа прав/окружения — class `permissions/environment`). Ловушка на `lookUpImpOrForward`/кэше objc в этом ходе также не ставилась (зависит от того же запуска). Неизвестно, кто кладёт код-указатель Foundation+0x790b0 в IMP/результат на пустом стабе — **не измерено**.

## Repro (когда есть рабочий root/sudo)

```sh
cd $DARLING_SRC_DIR  # ветка task/chrome-desc-trap, база f7b69686a
sh build-freebsd/build-constdict-desc-probe.sh
# запуск от root (или исправленный sudo):
env DARLING_SRC_DIR=/opt/darling/src DARLING_OVERLAY=/opt/darling/overlay DARLING_BUILD_DIR=/opt/darling/build \
    DARLING_TEST_BINARY=objc-constdict-desc-macho DARLING_SMOKE_REFRESH=1 \
    MLDR_TRAP_AT='Foundation+0x790b0' timeout -k 5 300 $DARLING_BUILD_DIR/launch-dynamic > <NEW-log>
grep -n '0x790b0\|790b0' <NEW-log>  # ожидание: строка хита + ≥2 именованных кадра
```
