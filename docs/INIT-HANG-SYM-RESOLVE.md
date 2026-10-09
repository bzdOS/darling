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
