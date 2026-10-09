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
