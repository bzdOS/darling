# bzero-forward-stub: стена suppl+0x2008 снята (шаг 41, веха 04)

База: pr-arm64 = 9a0e9dc0c50ef39af86ef606df788cb4b2eab5a1 (шаг 40 влит).
Ветка: task/bzero-forward-stub.

Стена шага 40: `gen-cg-supl.py` типизировал символы по префиксу `"___"` и
излучал `___bzero` как `.quad 0` в `__data`; бинд data-символа кладёт в GOT
адрес объекта, вызов уходил в `__data` (`signal 11 (code=2)`,
rip = `suppl.dylib+0x2008`).

## Что изменено

1. `build-freebsd/gen-cg-supl.py`: узкая таблица форвардов, проверяемая **до**
   правила префиксов:

```python
FORWARD_NAMES = {
    "___bzero": "_bzero",
    "_bzero": "__platform_bzero",
}
...
        if name in FORWARD_NAMES:
            out += [f".text", f".globl {name}", f"{name}:",
                    f"    jmp {FORWARD_NAMES[name]}"]
```

   `_bzero` в таблице потому, что генератор стабит и его (он тоже в списке
   недостающих): без этой записи `jmp _bzero` ушёл бы в локальный `xorl
   %eax,%eax; ret` — ровно та тихая порча, которую запретил наряд.
2. Линковка форвардного стаба требует `-undefined dynamic_lookup` (цель
   `__platform_bzero` резолвит dyld), печать рецепта в генераторе обновлена.
3. `build-freebsd/build-suppl-stubs.sh` — пересборка ОБОИХ дополнений из их
   текущих списков экспорта (`llvm-objdump --macho --exports-trie`), install_name
   как у установленных: `suppl.dylib` → `/usr/lib/supl.dylib`,
   `cg-supl.dylib` → `/usr/lib/suppl.dylib`.

Пересобрано: suppl — 858 символов (822 текстовых, 34 data, **2 форварда**),
cg-supl — 886 (848/36/2). Установлено в `$DARLING_OVERLAY/usr/lib/` (старые
сохранены в `$DARLING_BUILD_DIR/suppl-stubs/backup/`): suppl.dylib 54232 Б
sha256 `4ecd41f3aa09fc7a…`, cg-supl.dylib 56496 Б sha256 `76e515872e8f9e0b…`.

## Приёмка, нога 1 (статика, до прогона)

```
$ llvm-nm -m "$DARLING_OVERLAY/usr/lib/suppl.dylib" | grep -E '(___bzero|_bzero)$'
0000000000000648 (__TEXT,__text) external ___bzero
00000000000008b7 (__TEXT,__text) external _bzero

$ llvm-objdump -d "$DARLING_OVERLAY/usr/lib/suppl.dylib" | grep -A2 -E '<_?_?_bzero>:'
0000000000000648 <___bzero>:
     648: e9 6a 02 00 00   jmp  0x8b7 <_bzero>
00000000000008b7 <_bzero>:
     8b7: e9 f8 04 00 00   jmp  0xdb4 <dyld_stub_binder+0xdb4>

$ llvm-nm -u "$DARLING_OVERLAY/usr/lib/suppl.dylib"
__platform_bzero
dyld_stub_binder

$ llvm-nm -m "$DARLING_OVERLAY/usr/lib/cg-supl.dylib" | grep -E '(___bzero|_bzero)$'
0000000000000621 (__TEXT,__text) external ___bzero
0000000000000698 (__TEXT,__text) external _bzero

$ llvm-objdump -d "$DARLING_OVERLAY/usr/lib/cg-supl.dylib" | grep -A2 -E '<_?_?_bzero>:'
0000000000000621 <___bzero>:
     621: e9 72 00 00 00   jmp  0x698 <_bzero>
0000000000000698 <_bzero>:
     698: e9 67 07 00 00   jmp  0xe04 <dyld_stub_binder+0xe04>
```

Оба символа — `(__TEXT,__text)`; `___bzero` — форвард на `_bzero`, `_bzero` —
на слот `__platform_bzero` (`dyld_stub_binder+…`), т.е. цепь доходит до
настоящей функции в libsystem_platform (`__platform_bzero` и `___bzero` там
экспортированы с одного адреса +0x6c70).

## Приёмка, нога 2 (динамика)

**Прогон A** — ловушка на домене лейна, `MLDR_TRAP_AT='Foundation+0x790b0'`,
staging-деревья из closure шага 33, `timeout -k 5 300`:

```
... MLDR_TRAP_AT: armed Foundation+0x790b0 at 0x183d0279c0b0 (resume=0 emulate=0 orig=5548)
... dyld: loaded: <4C4C4462-…> /usr/lib/suppl.dylib
... [dyld-trace]   [103] mh=0x183d01e9c000 suppl.dylib <-NEW
... objc[42766]: Class NSCharacterSet is implemented in both /System/Library/Frameworks/CoreFoundation… and /usr/lib/FoundationExtras.dylib… (те же дубли классов, что и в шаге 39)
... SIGTRAP-AUDIT-DELIVER: native SIGTRAP -> guest handler=0x183cf159c2a0 (linux_signo=5) rip=0x183cf3472e43 si_code=1 si_addr=0x183cf3472e43
... SIGTRAP-AUDIT: rt_sigaction(5) from 0x223178 <unknown> old=0x183cf159c2a0 new=0x0
```

Лог: 14858 строк, образов загружено 134, **строки `TRAP_AT hit` нет**,
**`FATAL signal` нет вообще**, rip = suppl+0x2008 больше не встречается.
Процесс не умирает, а стоит — `rc=124` (timeout снял супервизор).

**Прогон B** (диагностика следующей стены) — ловушка на сайте §9.11,
`MLDR_TRAP_AT='Google Chrome for Testing Framework+0x212ab3a'`:

```
[darling-mldr] MLDR_TRAP_AT: armed Google Chrome for Testing Framework+0x212ab3a at 0xe1e8d3ab3a (resume=0 emulate=0 orig=e821)
[darling-mldr] === MLDR_TRAP_AT hit #1: Google Chrome for Testing Framework+0x212ab3a ===
  rdi=0xe1f6c79f40 rsi=0xe1f6c76880 rdx=0x0 rcx=0x7fffffdfda00 rax=0xe1f6c77ec0
  code @rip: e8 21 30 ba fe 48 8d bb 78 37 00 00 48 89 de 31
  stack slots resolving into known images:
    [rsp+264] Google Chrome for Testing Framework+0x212a9d2
```

`code @rip` = `e8 21 30 ba fe` — это и есть `callq 0xccdb60`; кадр
`Framework+0x212a9d2` — вызывающий из каскада §9.11. Значит, каскад теперь
**доходит** до стены §9.11: стена bzero снята, следующая — `0xccdb60`.

## Найденное по входному списку (для головы)

Классификация всех `___`-слотов данных в suppl.dylib по настоящим провайдерам
(exports-trie + попадание в `__text`, `classify` офлайн):

- **FUNC (24)**: `____mb_cur_max_l`, `___bzero`, `___darwin_check_fd_set_overflow`,
  `___error`, `___exp10`, `___exp10f`, `___fpclassifyf`, `___mac_syscall`,
  `___maskrune`, `___math_errhandling`, `___memcpy_chk`, `___memmove_chk`,
  `___memset_chk`, `___sincos_stret`, `___sincosf_stret`, `___stack_chk_fail`,
  `___strlcat_chk`, `___strlcpy_chk`, `___tolower`, `___toupper`,
  `___ulock_wait`, `___ulock_wake` (и `_bzero` — не `___`-имя, но стабится).
- **DATA (4)**: `___stack_chk_guard`, `___stderrp`, `___stdinp`, `___stdoutp`.

Для FUNC, кроме bzero-пары, форвард невозможен по той же причине, по которой
сработал `_bzero`: провайдер экспортирует **то же имя**, и `jmp <self>` дал бы
петлю на локальный стаб. Такие символы нужно либо убрать из списка (пусть
dyld биндит их прямо в провайдера — имена в загруженных libsystem_* есть),
либо оставить как сейчас. Сейчас они оставлены `.quad 0` (поведение шага 40),
решение — за головой: это следующий класс той же болезни.

## Verdict

- Стена `suppl.dylib+0x2008` **снята**: ни FATAL, ни прыжка в `__data` в прогоне
  A; статика обеих ног приёмки выполнена в коммите.
- Ловушки `Foundation+0x790b0` прогон A **не достиг** и не упал — гость стоит
  без сигнала; по прогону B каскад доходит до `callq 0xccdb60`, т.е. следующая
  стена — §9.11 (`os_unfair_lock`), но теперь как остановка, а не abort.
- Ожидаемые нарядом исходы буквально: «TRAP_AT hit» — нет; «новая FATAL с
  rip ≠ suppl+0x2008» — тоже нет (FATAL нет вовсе). Есть третий: остановка без
  сигнала после загрузки всех 134 образов.

## Следующий ход

Мерить в `0xccdb60`: ловушка `Google Chrome for Testing Framework+0xccdb60`
(или `libsystem_platform+0x8237`, где §9.11 назвал рекурсивный
`os_unfair_lock`), плюс вопрос, кто держит lock word `0000000100000307` —
это и решит, почему теперь стоит, а не abort. Вопрос лейна
`Foundation+0x790b0` остаётся за этой стеной.

## Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
export DARLING_OVERLAY=... DARLING_BUILD_DIR=... DARLING_SRC_DIR=...

sh build-freebsd/build-suppl-stubs.sh                 # оба дополнения
cp "$DARLING_BUILD_DIR"/suppl-stubs/{suppl,cg-supl}.dylib "$DARLING_OVERLAY"/usr/lib/

llvm-nm -m "$DARLING_OVERLAY/usr/lib/suppl.dylib" | grep -E '(___bzero|_bzero)$'
llvm-objdump -d "$DARLING_OVERLAY/usr/lib/suppl.dylib" | grep -A2 -E '<_?_?_bzero>:'

# прогон A
sudo -n env DARLING_SRC_DIR="$DARLING_SRC_DIR" DARLING_OVERLAY="$DARLING_OVERLAY" \
  DARLING_BUILD_DIR="$DARLING_BUILD_DIR" DARLING_TEST_BINARY=chrome-macho \
  CHROME_APP=... "DARLING_STAGING_TREES=$(cat "$DARLING_BUILD_DIR/chrome-staging-trees.txt")" \
  WAYLAND_DISPLAY=wayland-1 XDG_RUNTIME_DIR=/tmp/wayland-test DARLING_SMOKE_REFRESH=1 \
  MLDR_TRAP_AT='Foundation+0x790b0' timeout -k 5 300 "$DARLING_BUILD_DIR/launch-dynamic" > log 2>&1
grep -c 'MLDR_TRAP_AT hit' log ; grep -c 'FATAL signal' log   # 0 и 0
```
