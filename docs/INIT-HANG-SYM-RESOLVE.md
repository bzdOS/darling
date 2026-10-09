# Шаг 8: Оффлайн-символизация умирающего инициализатора

Задача: резолвить адрес инициализатора `0xb06751214c0` в символ, используя только существующие логи (без нового гостевого прогона).

## Исходные данные

- Лог 158: `/tmp/foundation-probe-158-init-hang.log` (1292690 строк)
- Лог 158b: `/tmp/foundation-probe-158b-init-hang.log` (1292537 строк)

## Гейт (0): полная строка initializer + образ-владелец по карте UUID

### Последняя строка «calling initializer» из 158b

```
1292537:dyld: calling initializer function 0xb06751214c0 in /Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework
```

### Образ-владелец по UUID-карте

```
26521:dyld: loaded: <4C4C4409-5555-3144-A170-AD4644F310AD> /Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework
```

UUID образа: `4C4C4409-5555-3144-A170-AD4644F310AD`

### Подтверждение UUID бинаря в overlay

```sh
llvm-otool -l "$DARLING_OVERLAY/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework" | grep -A2 LC_UUID
```

Результат:
```
cmd LC_UUID
cmdsize 24
uuid 4C4C4409-5555-3144-A170-AD4644F310AD
```

UUID бинаря в overlay совпадает с UUID из лога. Образ-владелец подтверждён.

## Гейт (1): резолв адреса в символ

### Замер: печатает ли лог адреса загрузки образов

```sh
LOG=/tmp/foundation-probe-158b-init-hang.log
grep -cE 'dyld: (rebase|bind).*Google Chrome for Testing Framework' $LOG
grep -cE 'dyld: (slide|image base|mapping|segment|mapped)' $LOG
grep -c '__dyld_get_image_vmaddr_slide' $LOG
```

Результат:
```
0
0
3
```

Все 3 вхождения `__dyld_get_image_vmaddr_slide` возвращают 0:
```
_dyld_get_image_vmaddr_slide(0)
```

### Вывод

Лог не печатает адреса загрузки образов (slide/base). Без slide резолв адреса `0xb06751214c0` в символ невозможен: невозможно вычислить смещение в файле для `llvm-nm -n` и `llvm-objdump -d`.

## Гейт (2): отрицательный замер

Точная команда grep и счётчик:

```sh
LOG=/tmp/foundation-probe-158b-init-hang.log
grep -cE 'dyld: (rebase|bind).*Google Chrome for Testing Framework' $LOG
```

Результат: `0`

Цитата соседней строки (последняя строка с упоминанием Chrome Framework в контексте fixup):
```
168354:dyld: fixup: Google Chrome for Testing Framework:0xb06821c5150 = 0xb0675120db0
```

## Итог

Адреса загрузки образов в логе отсутствуют. Резолв адреса инициализатора `0xb06751214c0` в символ невозможен без инструмента, который печатает slide/base для каждого образа. Это задача шага 9.

---

# Шаг 9: Слайд из существующих логов (off-line)

Задача: вычислить slide фреймворка из runtime-адресов лога 158b и предпочитаемых
vmaddr бинаря в overlay, без гостевого прогона. Бинарь только читался.

## Исходные данные (проверены в этом шаге)

```sh
sed -n '1292537p;168354p' /tmp/foundation-probe-158b-init-hang.log
```

```
dyld: fixup: Google Chrome for Testing Framework:0xb06821c5150 = 0xb0675120db0
dyld: calling initializer function 0xb06751214c0 in /Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework
```

Бинарь: overlay-копия фреймворка, 267024384 байт, UUID `4C4C4409-5555-3144-A170-AD4644F310AD`
(совпадает с логом, шаг 8). Сегменты: `__TEXT` vmaddr 0x0 (preferred base = 0),
`__DATA_CONST` 0xf1a3000, `__DATA` 0xfd50000, `__LINKEDIT` 0x1011f000;
`LC_DYLD_CHAINED_FIXUPS` dataoff 266809344 datasize 86208.

## Гейт (0): кандидаты слайда

- `__mod_init_func` в бинаре **нет** (`llvm-otool -l`: 78 load commands, ни одной
  секции типа S_MOD_INIT_FUNC_POINTERS). Запасной путь наряда (`llvm-nm -n`) тоже
  неработоспособен: 2700 симов, 2697 undefined, определённых 3 —
  `_ChromeAppModeStart_v8` @0x2840, `_ChromeWebAppShortcutCopierMain` @0x2a00,
  `_ChromeMain` @0x3fe0; энумерировать функции __TEXT по nm нельзя.
- Фактический список инициализаторов — секция `__TEXT,__init_offsets`
  (addr 0xdbc15fc, size 8, flags 0x16 = S_INIT_FUNC_OFFSETS; сегмент __TEXT
  read-only, условия dyld выполнены). Две uint32-энтрии:
  `E1 = 0x0212a4c0`, `E2 = 0x02065300` (чтение 8 байт по offset 230430204).
- Семантика энтрий подтверждена в исходнике того dyld, который печатал лог,
  `src/ImageLoaderMachO.cpp:2346-2355`:
  `Initializer func = (Initializer)((uint8_t*)this->machHeader() + funcOffset)`,
  а `machHeader_rt = slide + preferred base`. Preferred base = vmaddr(__TEXT) = 0x0,
  значит `pref_i = E_i`.
- `slide_i = 0xb06751214c0 - pref_i`. Фильтр: page-aligned (`slide_i & 0xFFF == 0`;
  маппинг заголовка образа page-aligned ⇒ слайд page-aligned) и `slide_i >= 0`:

| кандидат | pref_i | slide_i | фильтр |
|---|---|---|---|
| E1 | 0x0212a4c0 | 0xb0672ff7000 | выровнен, >= 0 → **прошёл** |
| E2 | 0x02065300 | 0xb06730bc1c0 | младшие биты 0x1c0 → отсеян |

## Гейт (1): кросс-чек якорем (выжил ровно один: slide = 0xb0672ff7000)

**(a) Слот якоря.** `0xb06821c5150 - 0xb0672ff7000 = 0xf1ce150`; выравнивание 8 ✓;
попадает в `__DATA_CONST,__const` [0xf1a80c0, 0xfd464c0) — секцию с цепочечными
fixups. Прямая проверка парсером `LC_DYLD_CHAINED_FIXUPS`: слот 0xf1ce150 **есть
в цепочке**: bind=0 (rebase), target = 0x2129db0, high8 = 0.
`target + slide = 0x2129db0 + 0xb0672ff7000 = 0xb0675120db0` — точное совпадение
со значением из строки лога.

Важная деталь формата: stride цепочки для DYLD_CHAINED_PTR_64 = **4**, не 8
(`dyld3/MachOLoaded.cpp:1196-1216`, `strideSize()`); обход со stride 8 слот
не находит. Структура: `dyld_chained_fixups_header` @dataoff, starts_in_image
seg_count=5, сегменты с fixups только __DATA_CONST (fmt=2, 2989 страниц) и
__DATA (fmt=2, 290 страниц), `page_start[]` начинается с offset 22 структуры
`dyld_chained_starts_in_segment`.

Контрольные счётчики обхода всей цепочки бинаря:
`1 066 255` (__DATA_CONST) + `60 654` (__DATA) = `1 126 909` слотов, и это ровно

```sh
grep -c 'dyld: fixup: Google Chrome for Testing Framework:' /tmp/foundation-probe-158b-init-hang.log
```

`= 1126909` (все слоты лога различимы, повторов 0; один слот не 8-выровнен —
следствие stride 4). Первый слот лога `0xb068219a000 - slide = 0xf1a3000` =
vmaddr(__DATA_CONST) = начало `__got` ✓.

**(b) Значение якоря.** `0xb0675120db0 - 0xb0672ff7000 = 0x2129db0` ∈ `__text`
[0x2840, 0xdbbf560) ✓. Проверка «внутри функции по nm» неработоспособна (бинарь
stripped: ближайший ниже `_ChromeMain` @0x3fe0, разрыв ~32.5 МБ), поэтому
проверено двумя способами: попадание в границы `__text` и `llvm-objdump -d
--start-address=0x2129db0` — на адресе лежит пролог функции
(`pushq %rbp; movq %rsp,%rbp; pushq %rbx; ...`), то есть цель fixup'а сама
является точкой входа функции.

## Гейт (2): резолв умирающего инициализатора

`pref(init) = 0x212a4c0`. Смещение в файле:
`0x212a4c0 - vmaddr(__TEXT) + fileoff(__TEXT) = 0x212a4c0 - 0 + 0 = 0x212a4c0`.

```sh
llvm-objdump -d --start-address=0x212a4c0 --stop-address=0x212a580 "$FW"
```

40+ строк, начинается с чистого пролога (`pushq %rbp; movq %rsp,%rbp; pushq %r15;
pushq %r14; pushq %r13; pushq %r12; pushq %rbx; subq $0x18,%rsp`) — 0x212a4c0
это **точка входа функции-инициализатора**, а не середина. Дальше: GOT-загрузка
`movq 0xd07904b(%rip)` → 0xf1a3538 (в `__got`), `callq` в `__stubs`
(импорт), сравнение с 0x636F6C6C416E6F69 = ASCII "ionAlloc".

Связь с якорем: `0x212a4c0 - 0x2129db0 = 0x710` — та же дельта, что видна в
рантайме (0xb06751214c0 - 0xb0675120db0 = 0x710): fixup-слот хранит указатель
на соседнюю функцию в 0x710 ниже точки входа инициализатора.

## Итог шага 9

**Slide фреймворка = 0xb0672ff7000** (preferred base 0x0 ⇒ рантайм-база образа
равна слайду). Подтверждён двумя независимыми якорями: (1) энтрия
`__init_offsets` даёт page-aligned слайд; (2) только этот слайд превращает
fixup-пару лога в (слот из цепочки бинаря, его rebase-target) с точным
совпадением значения и счётчиком 1 126 909 слотов = числу fixup-строк лога.
Умирающий инициализатор — функция по смещению 0x212a4c0 от базы образа
(stripped, имя неизвестно; в бинаре только 3 экспорта).

## Repro (короткий)

```sh
FW=<overlay>/Frameworks/Google\ Chrome\ for\ Testing\ Framework.framework/Versions/154.0.8029.0/Google\ Chrome\ for\ Testing\ Framework
LOG=/tmp/foundation-probe-158b-init-hang.log
sed -n '1292537p;168354p' $LOG                 # runtime-адреса
llvm-otool -l $FW | grep -B6 -A6 __init_offsets # секция: addr 0xdbc15fc size 8
# python3: прочитать 8 байт @230430204 (LE uint32 x2) -> E1=0x0212a4c0 E2=0x2065300
#   slide1 = 0xb06751214c0 - 0x212a4c0 = 0xb0672ff7000  (page-aligned, прошёл)
#   slide2 = 0xb06751214c0 - 0x2065300 = 0xb06730bc1c0  (не выровнен, отсеян)
# python3: парсер LC_DYLD_CHAINED_FIXUPS (stride 4): слот 0xf1ce150 в цепочке,
#   bind=0 target=0x2129db0; всего 1126909 слотов = grep -c строк лога
llvm-objdump -d --start-address=0x212a4c0 --stop-address=0x212a580 $FW  # пролог функции
```

Результат: slide = 0xb0672ff7000 подтверждён; инициализатор =
`__TEXT + 0x212a4c0` (function entry, stripped).

---

# Шаг 10: офлайн-символизация init-каскада (без гостевого прогона)

Задача: (1) локализовать функции-владельцы кадров каскада Control #77
(PC `0x212ab3f` и `0x212a9d2`), (2) вывести имена импортов, вызываемых
умирающим инициализатором `__TEXT+0x212a4c0`, (3) карта функций кластера
`0x2129000..0x212b000` + таблица «call site → импорт».

Дано шагом 9: slide `0xb0672ff7000` (preferred base 0), инициализатор — вход
функции `__TEXT+0x212a4c0`, fixup-target `__TEXT+0x2129db0` — вход соседней
функции (дельта `0x710`). Бинарь stripped (3 экспорта), гостевого прогона не
было, бинарь только читался.

## Метод (три независимых источника, все read-only)

1. **Лог самого dyld.** Он печатает имена биндов в чужих образах:
   `dyld: bind: <client>:<slot> = <provider>:<NAME>, *<slot> = <addr>` —
   18 775 строк, 1 863 различных адреса.
2. **Якорь по образу-поставщику.** Те же строки дают слайд каждого System-dylib:
   имя, присутствующее и в логе, и в symtab локальной копии dylib, даёт
   `slide = адрес_в_runtime − vmaddr_в_бинаре`; слайд берётся консенсусом по
   всем якорям образа. Итог: 28 образов, 1 434 якоря. Для
   `libsystem_c.dylib` 279/279, `libsystem_kernel.dylib` 368/368,
   `libsystem_pthread.dylib` 73/73, `libsystem_malloc.dylib` 23/23.
3. **Байты образа.** Запись `__TEXT,__stubs` — это `ff 25 disp32`
   (`jmpq *disp(%rip)`), т.е. адрес `callq` однозначно задаёт слот указателя;
   строка `dyld: fixup: …:<slot> = <value>` этого слота даёт адрес символа,
   который именуется пунктами 1–2. Bind (значение вне образа) против rebase
   (значение внутри образа `0x2840..0x10154000` после вычитания слайда)
   различаются механически: 1 122 079 rebase против 4 830 bind.

**Что не сработало и почему.** Таблица импортов `LC_DYLD_CHAINED_FIXUPS`
(`dataoff+0x1a08`, 2 696 записей u32, `imports_offset + 2696*4 ==
symbols_offset` сходится) не использована: её значения не являются смещениями
в пул символов — из 2 696 записей **1** попадает на начало строки пула, при
базе `header+symbols_offset` общей базы, дающей попадание всем, не существует
(перебор сдвигов ±8 байт). Это и есть названная причина, почему имена взяты
из лога и symtab, а не из import table.

## Гейт (1): владельцы кадров каскада

Оба PC — **адреса возврата** из `callq`, то есть верхушки кадра:

| PC | владелец (вход функции) | инструкция по адресу | размер владельца |
|---|---|---|---|
| `0x212a9d2` | `__TEXT+0x212a8c0` | `0x212a9cd: callq 0x212aa00` | `0x140` (320 B) |
| `0x212ab3f` | `__TEXT+0x212aa00` | `0x212ab3a: callq 0xccdb60` | `0x280` (640 B) |

Цепочка каскада, восстановленная по коду: `0x212a8c0` → `0x212aa00` →
`0xccdb60` → … Следующий кадр (`0xccdb60`) — прямой вызов внутри `__TEXT`,
не импорт.

## Гейт (2): карта функций кластера `0x2129000..0x212b000`

Кластер нельзя линейно разбирать с его начала (`0x2129000` не граница
инструкции), поэтому входы найдены в сырых байтах: пара настройки кадра
`55 48 89 e5` (`pushq %rbp; movq %rsp,%rbp`), всегда 16-байтово выровненная,
перед ней — padding линковщика (`0x90`/`0xcc`/`0x00`) или хвост предыдущей
функции. Каждый кандидат потом разобран от своего входа, т.е. все листинги
корректно выровнены. Метод даёт 20 входов и оба известных из шага 9
(`0x2129db0`, `0x212a4c0`) попадают в список — проверка метода.

| вход | размер | вход | размер |
|---|---|---|---|
| `0x2129010` | `0x1f0` | `0x212a3a0` | `0x120` |
| `0x2129200` | `0x100` | `0x212a4c0` | `0x400` |
| `0x2129300` | `0x530` | `0x212a8c0` | `0x140` |
| `0x2129830` | `0x70` | `0x212aa00` | `0x280` |
| `0x21298a0` | `0x10` | `0x212ac80` | `0x380` |
| `0x21298b0` | `0x1a0` | | |
| `0x2129a50` | `0x180` | | |
| `0x2129bd0` | `0x1e0` | | |
| `0x2129db0` | `0x30` | | |
| `0x2129de0` | `0xf0` | | |
| `0x2129ed0` | `0x60` | | |
| `0x2129f30` | `0x60` | | |
| `0x2129f90` | `0xc0` | | |
| `0x212a050` | `0x330` | | |
| `0x212a380` | `0x20` | | |

## Гейт (3): «call site → импорт»

Инициализатор `0x212a4c0..0x212a8c0` (`0x400` B, 213 инструкций): **18 `callq`,
из них 16 в `__stubs` и 16 из 16 разрешены в имена импортов**; два оставшихся —
прямые вызовы внутри `__TEXT` (`0x212a8c0`, `0x212abc0`), импортами не
являются.

| call site | stub | слот | импорт | образ |
|---|---|---|---|---|
| `0x212a4f1` | `0xdbc0022` | `0xf1a4fd8` | `_malloc_get_all_zones` | libsystem_malloc |
| `0x212a540` | `0xdbbefb4` | `0xf1a3080` | `__platform_strlen` | libsystem_platform |
| `0x212a558` | `0xdbc06a0` | `0xf1a59f0` | `_malloc_default_zone` | libsystem_malloc |
| `0x212a55d` | `0xdbc068e` | `0xf1a59d8` | `_malloc_default_purgeable_zone` | libsystem_malloc |
| `0x212a562` | — | — | прямой вызов `0x212a8c0` | __TEXT |
| `0x212a749` | `0xdbc0022` | `0xf1a4fd8` | `_malloc_get_all_zones` | libsystem_malloc |
| `0x212a78d` | `0xdbbefb4` | `0xf1a3080` | `__platform_strlen` | libsystem_platform |
| `0x212a7a7` | `0xdbbeff6` | `0xf1a30f8` | `__platform_memcmp`¹ | libsystem_platform |
| `0x212a7b7` | `0xdbc0694` | `0xf1a59e0` | `_malloc_zone_register` | libsystem_malloc |
| `0x212a7bf` | `0xdbc069a` | `0xf1a59e8` | `_malloc_zone_unregister` | libsystem_malloc |
| `0x212a7e4` | `0xdbc0694` | `0xf1a59e0` | `_malloc_zone_register` | libsystem_malloc |
| `0x212a7ec` | `0xdbc069a` | `0xf1a59e8` | `_malloc_zone_unregister` | libsystem_malloc |
| `0x212a7f4` | `0xdbc0694` | `0xf1a59e0` | `_malloc_zone_register` | libsystem_malloc |
| `0x212a813` | `0xdbc0022` | `0xf1a4fd8` | `_malloc_get_all_zones` | libsystem_malloc |
| `0x212a86f` | — | — | прямой вызов `0x212abc0` | __TEXT |
| `0x212a884` | `0xdbc001c` | `0xf1a4fd0` | `_abort_report_np` | libsystem_c |
| `0x212a897` | `0xdbc001c` | `0xf1a4fd0` | `_abort_report_np` | libsystem_c |
| `0x212a8ad` | `0xdbc001c` | `0xf1a4fd0` | `_abort_report_np` | libsystem_c |

¹ один адрес, два имени: лог самого dyld для этого адреса печатает `_memcmp`
(и `__platform_bzero` для `___bzero`, `_memcpy` для `__platform_memmove`) —
это алиасы реэкспортов libsystem_platform, симtab и лог называют один адрес
по-разному.

Два владельца кадров каскада, для полноты: `0x212a8c0` — 2 `callq`, 1
разрешён (`___stack_chk_fail`), второй — прямой вызов `0x212aa00`;
`0x212aa00` — 12 `callq`, 9 разрешены: `___bzero`, `__platform_memmove`,
`___stack_chk_fail`, `__platform_strlen`, `_write` (2 вызова), `___error`
(2 вызова), три прямых вызова внутри `__TEXT` (в т.ч. `0xccdb60` — следующий
кадр каскада). Набор импортов `0x212aa00` (`_write`, `___error`) — это
диагностический писатель в stderr, то есть кадр каскада принадлежит коду
логирования/проверок, а не бизнес-логике.

**Смысл инициализатора по именам импортов:** `_malloc_default_zone`,
`_malloc_default_purgeable_zone`, `_malloc_get_all_zones`,
`_malloc_zone_register`/`_malloc_zone_unregister`, `__platform_strlen`,
`__platform_memcmp` — это инициализация зон malloc (partition/zone allocator),
а `_abort_report_np` — аварийный выход. Это согласуется с шагом 7 (умирание
гостя, а не ожидание) и с шагом 9 (инициализатор — точка входа функции).

## Приёмка (команда и её результат)

```sh
FW="<overlay>/Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework"
llvm-objdump -d --start-address=0x212a4c0 --stop-address=0x212a540 "$FW"
```

Вывод начинается чистым прологом и содержит один `callq`:

```
 212a4c0: 55                           	pushq	%rbp
 212a4c1: 48 89 e5                     	movq	%rsp, %rbp
 212a4c4: 41 57                        	pushq	%r15
 ...
 212a4f1: e8 2c 15 b5 ff               	callq	0xdbc0022
```

Механический счёт питон-парсером (три источника из раздела «Метод»):
**окно 0x212a4c0..0x212a540 — 36 инструкций, 1 `callq`, 1 разрешено
(`_malloc_get_all_zones`, libsystem_malloc.dylib); вся функция
0x212a4c0..0x212a8c0 — 213 инструкций, 18 `callq`, 16 в `__stubs`,
16 разрешено (16/16), 2 прямых вызова не являются импортами.** По всем
четырём окнам вместе: 33 `callq`, 27 разрешено, 6 прямых вызовов внутри
`__TEXT`.

## Repro (короткий)

```sh
LOG=/tmp/foundation-probe-158b-init-hang.log
# 1) имена биндов в чужих образах -> якорь слайдов System-dylib
grep -c "^dyld: bind:" $LOG                       # 16 495 bind-строк всего
grep -c "^dyld: fixup: Google Chrome for Testing Framework:" $LOG   # 1 126 909
# 2) классификация fixup-строк фреймворка: rebase (значение в 0x2840..0x10154000)
#    против bind (значение вне образа) -> 1 122 079 / 4 830
# 3) адрес символа -> имя: symtab локальной копии образа по якорному слайду
llvm-nm --defined-only <overlay>/usr/lib/system/libsystem_malloc.dylib
# 4) адрес символа -> имя (второй источник): та же строка bind в логе
# 5) callq -> stub -> слот: ff 25 disp32 в записи __TEXT,__stubs (0xdbbef60, 0x269a)
# 6) входы функций кластера: поиск 55 48 89 e5 в байтах, выравнивание 16,
#    padding перед входом, затем objdump от каждого входа
```

## Итог шага 10

Оба кадра каскада локализованы по коду: `0x212a9d2` — возврат из вызова
`0x212aa00` внутри функции `0x212a8c0`, `0x212ab3f` — возврат из вызова
`0xccdb60` внутри функции `0x212aa00`; цепочка каскада
`0x212a8c0 → 0x212aa00 → 0xccdb60`. Имена импортов инициализатора выведены
полностью для всех 16 вызовов через `__stubs` (16/16): это инициализация зон
malloc. Кластер `0x2129000..0x212b000` разобран на 20 функций с входами и
размерами. Import table образа не использована — названа причина (значения не
индексируют пул символов: 1 из 2 696 попадает на начало строки).

Не измерено: имена функций самого образа (бинарь stripped, 3 экспорта);
содержимое кадра `0xccdb60` (прямой вызов, не импорт — следующий шаг).
