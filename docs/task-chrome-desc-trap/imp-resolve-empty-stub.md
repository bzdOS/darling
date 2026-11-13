# chrome-desc-trap: резолв description на пустом стабе (шаг 37, веха 04)

База: pr-arm64 = f7b69686a9d28c340e9a80902883b01adf637d42, ветка task/chrome-desc-trap (поверх принятого 41c956e6d). Стаб пробы: `EmptyConstDictProbe` без методов (`method-list-count=0`, шаг 36). Прогон: launch-dynamic с `MLDR_TRAP_AT='Foundation+0x790b0'` (resume=0), свежий лог 2026-11-13.

## Repro (команды)

```sh
# шаг 1: sudo
ls -l /usr/local/bin/sudo; /usr/local/bin/sudo -n true; echo sudo_rc=$?
# -> /usr/local/bin/sudo setuid root:wheel (rwsr-xr-x), sudo_rc=0, passwordless root подтверждён

# сборка пробы (пустой стаб в tests/objc-constdict-desc.m)
cd "$DARLING_SRC_DIR" && sh build-freebsd/build-constdict-desc-probe.sh

# trap-прогон (по образцу Control #77, мимо run-скрипта; stop darlingserver по pid до/после)
# stop: pgrep -x darlingserver -> sudo kill -9 <pid>
/usr/local/bin/sudo -n env \
  DARLING_SRC_DIR="$DARLING_SRC_DIR" DARLING_OVERLAY="$DARLING_OVERLAY" DARLING_BUILD_DIR="$DARLING_BUILD_DIR" \
  DARLING_TEST_BINARY=objc-constdict-desc-macho DARLING_SMOKE_REFRESH=1 \
  MLDR_TRAP_AT='Foundation+0x790b0' \
  timeout -k 5 300 "$DARLING_BUILD_DIR/launch-dynamic" > "$DARLING_BUILD_DIR/chrome-desc-trap-empty-20261113-042527Z.log" 2>&1
```

## Лог

`docs/task-chrome-desc-trap/chrome-desc-trap-empty-20261113-042527Z.log` (228 строк, целиком).

## Приёмка (команды самопроверки и выдержка)

```
$ grep -an "empty-stub" chrome-desc-trap-empty-20261113-042527Z.log | head -40
125:CONSTDICT empty-stub-class=0x7f7edd87d190 name=EmptyConstDictProbe
126:CONSTDICT empty-stub method-list-count=0
127:CONSTDICT empty-stub responds(description)=0 getMethodImplementation=0x3c9c02cb1e80
128:CONSTDICT where-empty-stub-getimp value=0x3c9c02cb1e80 dladdr=1 fname=/usr/lib/libobjc.A.dylib fbase=0x3c9c02c69000 sname=_objc_msgForward saddr=0x3c9c02cb1e80
129:CONSTDICT empty-stub isa=0x7f7edd87d190 class=EmptyConstDictProbe receiver=0x7fffffdffd50 expected_foundation_790b0=0x3c9c03b610b0
130:CONSTDICT empty-stub before-send
131:CONSTDICT empty-stub after-send
133:CONSTDICT empty-stub description=0x3c9c04a10eda same-as-receiver=no
134:CONSTDICT empty-stub value=0x3c9c04a10eda dladdr=1 fname=/usr/lib/system/libsystem_trace.dylib fbase=0x3c9c04a0b000 sname=sbuf_delete saddr=0x3c9c04a0f050
135:CONSTDICT empty-stub before-copy-send

$ grep -an "desc-imp=" chrome-desc-trap-empty-20261113-042527Z.log | head -20
132:desc-imp=0x3c9c04a10eda foundation_790b0=0x3c9c03b610b0 match=no

$ grep -an '0x790b0\|790b0' chrome-desc-trap-empty-20261113-042527Z.log | head -60
69:[darling-mldr] MLDR_TRAP_AT: armed Foundation+0x790b0 at 0x3c9c03b610b0 (resume=0 emulate=0 orig=5548)
113:CONSTDICT empty-dict description=0x7f7edd87c410 (expected_foundation_790b0=0x3c9c03b610b0 match=no)
117:CONSTDICT placeholder description=0x7f7edd87cd60 (expected_foundation_790b0=0x3c9c03b610b0 match=no)
129:CONSTDICT empty-stub isa=0x7f7edd87d190 class=EmptyConstDictProbe receiver=0x7fffffdffd50 expected_foundation_790b0=0x3c9c03b610b0
132:desc-imp=0x3c9c04a10eda foundation_790b0=0x3c9c03b610b0 match=no
```

Сигнал/смерть (не хит ловушки 0x790b0): `136:[darling-mldr] FATAL signal 11 (code=1) at addr=0x697263736578` на `empty-stub before-copy-send` (копирование результата forward/не-объекта).

## Интерпретация (исход c, честный негатив-замер)

- Пустой стаб воспроизведён: `method-list-count=0`, `responds(description)=0`.
- Резолв ушёл в forward: `getMethodImplementation`/хит = `_objc_msgForward` (libobjc.A.dylib, saddr/value `0x3c9c02cb1e80`), `desc-imp` = `0x3c9c04a10eda` (libsystem_trace.dylib, `sbuf_delete`), `match=no` к `foundation_790b0`.
- Ловушка `MLDR_TRAP_AT` на `Foundation+0x790b0` **armed** (стр.69), **хита нет** — `__NSCFCopyDescription2` не вызван.
- Значит, на пустом стабе (без метод-листа) гость НЕ получает `Foundation+0x790b0` как IMP/результат `description`; возвращается результат forward-пути. Артефакт шага 36 (код-указатель Foundation+0x790b0) не воспроизводится методом «просто пустой метод-лист» и живёт вне обычного lookup/method-list в этом сценарии (кэш/bind/иной путь) — требуется отдельный замер в хром-контексте.
