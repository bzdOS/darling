# suppl-bzero-wall: стена §40 — вызов `__bzero` через data-слот suppl.dylib

База: pr-arm64 = d4cffdea32df1bfd99084bb278b23e0911b90183 (шаг 39 влит). Ветка:
task/suppl-bzero-wall. Только офлайн-статика по локальным файлам, прогонов нет;
лог смерти взят из замера шага 39.

## Вопрос 1 — что такое Chrome Framework+0x212aaa2

Символов нет: `llvm-nm` по фреймворку даёт 2700 записей, и ниже 0x212aaa2 нет
ничего, кроме экспортов:

```
$ llvm-nm -n "$FW" | awk '$1 <= "000000000212aaa2"' | tail -2
0000000000002a00 T _ChromeWebAppShortcutCopierMain
0000000000003fe0 T _ChromeMain
```

То есть адрес **не символизируется** (бинарник stripped вне экспортов);
ближайший символ ниже — `_ChromeMain` на 0x3fe0, к делу не относится.
Границы функции видны по дизассемблеру: пролог на **0x212aa00**, `retq` на
**0x212abae** — функция [0x212aa00, 0x212abae).

```
$ llvm-objdump -d --start-address=0x212aa00 --stop-address=0x212b200 "$FW" | grep -E 'callq|retq'
 212aa9d: e8 24 45 a9 0b   callq 0xdbbefc6        <- вызов, смертельный (ret 0x212aaa2)
 212aaec: e8 d5 44 a9 0b   callq 0xdbbefc6        <- второй такой же
 212ab3a: e8 21 30 ba fe   callq 0xccdb60         <- стена §9.11 (ret 0x212ab3f)
 212ab4b: e8 10 30 ba fe   callq 0xccdb60         <- стена §9.11 (ret 0x212ab50)
 212ab81: ...
 212abae: c3               retq
```

Ответ: **0x212aaa2 — в той же функции, что каскад §9.11.** Кадр §9.11
+0x212ab3f — это возврат после `callq 0xccdb60` на 0x212ab3a внутри этой же
функции; кадр +0x212a9d2 — возврат в вызывающего, который её вызывает
(`callq 0x212aa00` на 0x212a9cd, ret 0x212a9d2). Но сама смерть — **до** стены
§9.11: за 0x9d байт до неё, на вызове 0x212aa9d.

Что за вызов: аргументы готовятся как `bzero(ptr, len)` — `leaq 0xc8(%rbx), %rdi`
на 0x212aa7b и `movl $0x14f9, %esi` на 0x212aa98, затем `callq` на 0x212aa9d
(возврат 0x212aaa2 — ровно rip из смерти).

## Вопрос 2 — цепочка бинда `___bzero`

1. Вызов идёт через стаб `__stubs` 0xdbbefc6, который читает GOT-слот
   `__DATA_CONST` 0xf1a3098:

```
 dbbefc6: ff 25 cc 40 5e 01   jmpq *0x15e40cc(%rip)   ## 0xf1a3098
```

2. Слот — не rebase, а **bind**: `build-freebsd/chained-slot-peek.py` (добавлен
   этим коммитом, разбирает LC_DYLD_CHAINED_FIXUPS) на нём печатает:

```
$ python3 build-freebsd/chained-slot-peek.py "$FW" 0xf1a3098
__DATA_CONST+0x98 BIND import[18] lib_ordinal=66 weak=0 name=___bzero addend=0
```

   То же в дампе llvm:

```
dyld chained import[18]
  lib_ordinal = 66 (LSysX)
  weak_import = 0
  name_offset = 187 (___bzero)
```

   Ординал 66 — `/usr/lib/LSysX.dylib` (LC_LOAD_DYLIB, cmdsize 56). В прогоне
   шага 39 **LSysX не грузится вовсе** (`grep -aic lsys` по логу = 0), и в
   LSysX.dylib нет ни `___bzero`, ни `_bzero` (exports-trie пуст по bzero);
   LSysX только реэкспортирует libSystem.B.

3. Символ в итоге разрешился в **suppl.dylib** (образ загружен: база
   0x274515e9c000, трейс `[103] mh=0x274515e9c000 suppl.dylib`), а там это
   **переменная, а не функция**:

```
$ llvm-nm -m "$DARLING_OVERLAY/usr/lib/suppl.dylib" | grep ___bzero
0000000000002008 (__DATA,__data) external ___bzero

$ llvm-objdump -s "$DARLING_OVERLAY/usr/lib/suppl.dylib" | sed -n '/^ 2000 /p'
 2000 00000000 00000000 00000000 00000000  ................
```

   8 байт нулей в `__DATA,__data` по 0x2008; `__text` — 0x370–0xd3c.

4. Слот никто и не собирался заполнять: у suppl.dylib классический
   `LC_DYLD_INFO_ONLY` (не chained fixups), и записей bind/rebase на 0x2008 нет
   (`llvm-objdump --macho --bind|--rebase … | grep -c 0x2008` → 0 и 0).

5. Бинд data-символа кладёт в GOT **адрес объекта**, а не его значение, поэтому
   `jmp *0xf1a3098` уходит на `suppl_base+0x2008` — в `__data`. Отсюда
   `FATAL signal 11 (code=2)` (SEGV_ACCERR на выборке инструкции), rip = адрес =
   suppl+0x2008, как в обоих прогонах шага 39.

## Почему в suppl.dylib вообще появился `___bzero`-слот

Генератор дополнений в дереве — `build-freebsd/gen-cg-supl.py`; он типизирует
символ **по префиксу имени**:

```python
DATA_PREFIXES = ("_kCG", "_kCM", "_kCA", "_kCV", "_kCI", "_kMT",
                 "_OBJC_CLASS_", "_OBJC_METACLASS_", "___")
...
        if name.startswith(DATA_PREFIXES):
            out += [f".data", f".globl {name}", f"{name}:", ".quad 0"]
        else:
            out += [f".text", f".globl {name}", f"{name}:",
                    "    xorl %eax, %eax", "    ret"]
```

`___bzero` попадает под правило `"___"` и становится 8-байтовым нулевым
объектом в `__data` вместо текстового стаба. Тот же слот есть и в
`cg-supl.dylib` (0x2018) — тот же генератор, тот же префикс.

Что при этом просит настоящее окружение: настоящий провайдер
`libsystem_c.dylib` реэкспортирует **`_bzero`** из libsystem_platform
(`[re-export] _bzero (__platform_bzero)`), а `___bzero` не отдаёт ни
libSystem.B, ни libsystem_c — то есть символа в overlay не было, и он попал в
список недостающих, который и сгенерировал слот.

## Вердикт

- Символ 0x212aaa2: **не символизируется** (stripped; ниже — только
  `_ChromeMain` 0x3fe0). Он внутри функции [0x212aa00, 0x212abae), той же, где
  стоит вызов стены §9.11 `0xccdb60` (ret 0x212ab3f).
- Стена: **новая**, но в той же функции каскада. Смерть — вызов `__bzero` через
  GOT-слот, за 0x9d байт до `callq 0xccdb60`; стена §9.11 (`os_unfair_lock`,
  0xccdb60) в этом прогоне не достигается.
- Причина — **дефект генератора стабов** (`gen-cg-supl.py`, правило `"___"` →
  data), а не просто отсутствие реэкспорта. Отсутствие реэкспорта `___bzero` —
  лишь условие, из-за которого символ попал в список; лавину даёт то, что
  генератор отдал функцию как переменную, и бинд кладёт в GOT её адрес.
- Следствие для §9.11: пока `___bzero` — data, вопрос «что делает 0xccdb60» на
  текущем overlay не проверить: chrome умирает раньше.

## Следующий ход

Пересобрать дополнение (`suppl.dylib`/`cg-supl.dylib`) так, чтобы `___bzero`
был текстовым стабом (или реэкспортом `_bzero`), и повторить chrome-прогон с
`MLDR_TRAP_AT='Foundation+0x790b0'`: ожидание — либо строка `TRAP_AT hit`
(лейн жив, дальше мерить класс), либо следующая стена с новым адресом. Решение
по типу символа — за головой.

## Repro

```sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:/usr/sbin
FW="$DARLING_OVERLAY"/Frameworks/Google\ Chrome\ for\ Testing\ Framework.framework/Versions/154.0.8029.0/Google\ Chrome\ for\ Testing\ Framework

llvm-nm -n "$FW" | awk '$1 <= "000000000212aaa2"' | tail -2
llvm-objdump -d --start-address=0x212aa00 --stop-address=0x212b200 "$FW" | grep -E 'callq|retq'
llvm-objdump -d --start-address=0xdbbefb0 --stop-address=0xdbbefe0 "$FW"
python3 build-freebsd/chained-slot-peek.py "$FW" 0xf1a3098
llvm-objdump --macho --chained-fixups "$FW" | grep -B2 -A2 -- '___bzero'
llvm-nm -m "$DARLING_OVERLAY/usr/lib/suppl.dylib" | grep ___bzero
llvm-objdump -s "$DARLING_OVERLAY/usr/lib/suppl.dylib" | sed -n '/^ 2000 /p'
llvm-objdump --macho --exports-trie "$DARLING_OVERLAY/usr/lib/system/libsystem_c.dylib" | grep bzero
grep -n 'DATA_PREFIXES' -A2 build-freebsd/gen-cg-supl.py
```
