# План работ: darling-freebsd

Дата: 2026-08-26. Документ исполняемый: он рассчитан на то, что его берёт в
работу человек или модель, не участвовавшие в предыдущих сессиях. Поэтому здесь
есть код, точные пути и команды проверки, а не только намерения.

---

## 0. Прочти это первым

### Правила, нарушение которых стоило дней

**Измеряй до того, как писать код.** За одну сессию четыре убедительные
гипотезы (`rt_sigreturn`, вложенность `pthread_canceled`, общий сокет у
потоков, `getattrlistbulk`) оказались неверны. Каждая была закрыта одной
проверкой. Если бы по ним начали писать код — потеряли бы дни.

**Заявления субагентов перепроверяй по исходникам.** В той же сессии один агент
взял неверную структуру, другой построил рекомендацию на неверной посылке про
«4000 строк, которые придётся переписать», третий нашёл настоящий баг, который
человек пропустил. Все три случая выявились чтением кода, а не спором.

**Порядок сборки значим.** `libdtape.a` — отдельно собираемая статическая
библиотека. Правка в `src/external/darlingserver/duct-tape/` **не подействует**,
пока не пересобран dtape. Это уже один раз стоило часа отладки «почему
изменение ничего не меняет».

```sh
sh build-freebsd/build-dtape.sh          # только если менялся duct-tape
sh build-freebsd/build-darlingserver.sh
sh build-freebsd/build-mldr-only.sh
sh build-freebsd/build-real-macho-tests.sh
```

**Не транслируешь — значит молча ломаешь.** Совпадение имён констант между
Linux и FreeBSD ничего не гарантирует. Реальные примеры из этого порта:

| Что | Linux | FreeBSD | Что было при passthrough |
|---|---|---|---|
| `O_CREAT` | 0x40 | 0x200 | попадал в `O_ASYNC`, файл не создавался |
| `O_APPEND` | 0x400 | 0x8 | попадал в `O_TRUNC`, файл обнулялся |
| `SIG_BLOCK` | 0 | 1 | EINVAL, блокировка сигналов не работала никогда |
| `SIGUSR1` | 10 | 30 | маска накладывалась на другой сигнал |
| `ENOSYS` | 38 | 78 | гость видел `EREMCHG` |
| `sigset_t` | 8 байт | 16 байт | ядро писало за границу объекта гостя |

**Регрессия после каждого изменения:**

```sh
cc -O0 -ggdb -o /tmp/dynsmoke tests/launch-dynamic-smoke.c
DARLING_TEST_BINARY=hello-foundation-macho /tmp/dynsmoke   # ждём count=3
echo "select 21*2;" | DARLING_TEST_BINARY=sqlite3-real-macho /tmp/dynsmoke  # ждём 42
```

### Диагностика: чем смотреть внутрь

mldr исполняет гостевые образы в своём адресном пространстве, поэтому отладчик
не видит в них символов. Отсюда собственные средства:

```sh
MLDR_LOG_MAPPINGS=1   # карта «адрес → образ»; base+offset скормить llvm-nm
MLDR_LOG_EPOLL=1      # что epoll-шим отдаёт гостю
MLDR_LOG_RPC_SOCKET=1 # какой сокет использует каждый поток
MLDR_TRAP_AT='libc++abi.dylib+0x2c670'   # ud2 в точке; при попадании — регистры,
                                          # тип исключения, вызывающий, стек
DSERVER_LOG_STDERR=1 DSERVER_LOG_LEVEL=debug   # логи самого darlingserver
```

Как получить смещение для `MLDR_TRAP_AT`:

```sh
llvm-nm19 --defined-only /tmp/darling-local-overlay/usr/lib/libc++abi.dylib \
  | grep '___cxa_throw$'
```

Гостевой RPC-слой пишет свою диагностику в `/tmp/dserver-client-rpc.log` —
именно там нашлась строка `BAD RECEIVE MESSAGE: number=72 (expected 73)`,
раскрывшая баг с адресами сокетов.

**Нельзя** пользоваться `KQUEUE_DEBUG=1`: вывод гостя идёт через RPC `Kprintf`, и
два таких вызова с одного потока роняют darlingserver раньше, чем проявится
исследуемое.

---

## 1. North star и измеримая цель

**North star — Chrome.** Как направление, а не как задача: он заставляет строить
многопроцессность, Mach IPC и графику честно, без заглушек, которые пришлось бы
выбрасывать. Раздел про него — в конце документа.

**Мерить по нему нельзя.** Апстримный Darling с многолетней историей и
несравнимо большими силами держит консольные приложения и экспериментальный
Cocoa. Chrome сегодня за пределами состояния отрасли.

**Измеримая цель — Cocoa GUI-приложение, отдающее картинку в wlstream.**

---

## 2. Что работает сегодня (ступень 0)

Проверяется командами выше. Динамическая линковка, ~41 гостевая dylib на
процесс, настоящий `sqlite3`, запись настроек через CFPreferences.

```
$ echo "select 21*2;" | sqlite3          → 42
$ defaults write com.bsdos.demo Greeting Hello
  → Library/Preferences/com.bsdos.demo.plist с <key>Greeting</key><string>Hello</string>
```

Не работает: `defaults read` (CF отдаёт пустой список доменов), создание
процессов, GUI, aarch64.

---

## 3. Ступень 1 — создание процессов

**Почему первая.** Без неё ступень 2 не проверяется, Chrome многопроцессный по
природе, а `NSTask` исключён из сборки Foundation именно из-за её отсутствия.

**Состояние.** Написан, но не собран и не подключён: `src/startup/mldr/process_spawn.{c,h}`.
Спецификация: `docs/SPEC-mldr-process-creation.md`.

**Установленный факт, экономящий время.** Флаг `is_fork` в checkin — ложный
след: `Checkin::processCall()` его не читает (проверено, вызывает только
`notifyCheckin`). Настоящий признак — серверное состояние `_pendingReplacement`,
выставляемое вызовом `checkout` перед `execve`.

**Текущий `fork` сломан:**

```c
/* freebsd_syscall_trap.c — как есть сейчас: */
case MACOS_SYS_fork:
    return freebsd_raw_syscall(SYS_fork, 0, 0, 0, 0, 0, 0);
```

Потомок не делает checkin, поэтому делит с родителем живой RPC-сокет и
невидим для сервера.

### Как подключать

Файл добавлен в `build-freebsd/build-mldr-only.sh` в список `add_executable`.
Ветки в `dispatch_linux_syscall` (`freebsd_syscall_trap.c`) по образцу соседей:

```c
#define LINUX_SYS_execve   59
#define LINUX_SYS_clone    56

case LINUX_SYS_execve:
    /* Guest image is replaced wholesale; darlingserver must be told first via
     * checkout, otherwise it keeps the old process record. See
     * docs/SPEC-mldr-process-creation.md. */
    return mldr_sys_execve((const char *)a1, (char *const *)a2, (char *const *)a3);
```

### Известный долг, который надо закрыть здесь же

`__mldr_close_rpc_socket` строит путь для `unlink` из pid **закрывающего**
процесса, а сокет привязан под pid **создавшего**. После fork потомок удалит
чужое имя. В `mldr.c` уже есть реестр `__mldr_rpc_socket_pid()` — убедись, что
он используется.

### Проверка ступени

Собрать python.org-сборку Python как гостевой Mach-O и выполнить:

```python
import subprocess
print(subprocess.run(["/usr/bin/true"]).returncode)   # ждём 0
```

Промежуточно достаточно любого бинаря, делающего `posix_spawn`.

---

## 4. Ступень 2 — Mach IPC между процессами

**Почему.** У Chrome на Mach-портах весь межпроцессный обмен. Мы упираемся в
`MACH_SEND_INVALID_DEST` и `KERN_INVALID_NAME` **внутри одного** процесса — то
есть работа начинается до многопроцессности.

**Известный долг.** Механизм прерываний darlingserver не работает:
`InterruptEnter` не объявляется, протокол ловит устаревшие ответы. Гость
оборачивает обработчики так (`xnu_syscall/bsd/impl/signal/sigaction.c`):

```objc
static void handler_linux_to_bsd_wrapper(int linux_signum,
                                         struct linux_siginfo* info, void* ctxt) {
    dserver_rpc_interrupt_enter();
    handler_linux_to_bsd(linux_signum, info, ctxt);
    dserver_rpc_interrupt_exit();
}
```

Но в ядро он отдаёт обработчики **только для `SIGCHLD` и `SIGTTOU`**; остальные
хранит локально, а доставку делает сервер. Реальные ядерные обработчики ставит
`sigexc.c` — вот там `rt_sigaction` и падает с EINVAL.

Заготовки конвертеров лежат: `src/startup/mldr/linux_siginfo.{c,h}`,
`linux_ucontext.{c,h}`, спецификация `docs/SPEC-signal-abi-bridge.md`. Не
собирались.

**Осторожно:** нельзя отдавать гостю `SIGILL` и `SIGSYS` — на них держится
перехват syscall'ов. В `rt_sigprocmask` это уже сделано:

```c
sigdelset(&nset, SIGILL);
sigdelset(&nset, SIGSYS);
```

То же понадобится в `rt_sigaction`: возвращать успех, не переустанавливая.

**Проверка:** пара процессов, общающаяся через XPC.

---

## 5. Ступень 3 — GUI: `Wayland.backend` для Cocotron

**Это одна ступень, а не подпроект.** AppKit здесь — Cocotron
(`src/external/cocotron/AppKit`, 375 файлов). Он грузит бэкенды как бандлы по
`NSPriority`. Нужен **один бандл**: подкласс `NSDisplay` + подкласс `CGWindow`.

Спецификации: `docs/SPEC-appkit-display-backend.md`,
`docs/SPEC-wlstream-frame-source.md`. Образец — `AppKit/X11.backend/`.

### Точка передачи кадра

```objc
/* WaylandWindow.m — подкласс CGWindow.
 * purpose: hand the finished raster frame to the compositor.
 * Onyx2D renders into an O2Surface that owns its memory; the format is
 * BGRA8888 premultiplied little-endian with a tightly packed stride, which is
 * byte-for-byte WL_SHM_FORMAT_ARGB8888 on little-endian — no conversion. */
- (void) flushBuffer {
    O2Surface *surface = [self surface];
    const void *src = [surface pixelBytes];

    memcpy(_shmData, src, _height * _stride);

    wl_surface_attach(_wlSurface, _wlBuffer, 0, 0);
    wl_surface_damage_buffer(_wlSurface, 0, 0, _width, _height);
    wl_surface_commit(_wlSurface);
    wl_display_flush(_wlDisplay);
}
```

### Обратный канал

```objc
/* Ввод: собрать NSEvent и отдать его AppKit. Ничего X11-специфичного
 * переиспользовать не нужно. Внимание на систему координат: у Wayland
 * surface-local с началом сверху, у AppKit — снизу. */
NSEvent *event = [NSEvent mouseEventWithType: NSLeftMouseDown
                                    location: NSMakePoint(x, height - y)
                               modifierFlags: flags
                                   timestamp: ts
                                windowNumber: windowNumber
                                     context: nil
                                 eventNumber: 0
                                  clickCount: 1
                                    pressure: 1.0];
[[NSDisplay currentDisplay] postEvent: event atStart: NO];
```

### Почему Wayland, а не прямая врезка в wlstream

Два независимых аргумента, оба проверены по коду.

**Первый.** `wayland-tunnel` — это Wayland-**сервер**, принимающий клиентов
(«accept Wayland (Firefox/cage) clients», `wayland-tunnel/src/main.zig`).
AppKit в роли клиента подключается к нему как любой другой, и весь конвейер
`cage → wayland-tunnel → wlstream → зритель`, вместе с вводом, resize и ключами
Zenoh, работает **без единой правки**.

**Второй, со стороны GPU.** Vulkan выводит через WSI; `VK_KHR_wayland_surface`
— стандарт, а «WSI для wlstream» не существует нигде. Wayland-поверхность
объединяет программный CoreGraphics и будущий Metal→Vulkan вместо того, чтобы
заставлять выбирать.

### Препятствия

GUI-ветка (`COMPONENT_gui`) в этом порту **не собиралась ни разу**. Единственный
пример бэкенда завязан на Xlib и выводит даже программно нарисованный буфер
через GLX — списать «как есть» не выйдет.

Понадобятся пакеты: `wayland`, `wayland-protocols`, `libxkbcommon`; заголовки
xdg-shell генерируются `wayland-scanner`.

**Проверка:** примеры из `src/external/cocotron/examples`.

---

## 6. Ступень 4 — Metal → Vulkan

**Приоритет намеренно низкий.** Подавляющее большинство Cocoa-приложений Metal
не используют — обычный AppKit рисует через CoreGraphics программно. Metal нужен
Chrome-классу и играм.

У Darling начат Metal-бэкенд поверх Vulkan; писать Metal с нуля нереально,
заимствование здесь без оговорок.

На дев-VM Vulkan сегодня не запустится — установлены только `vulkan-headers`:

```sh
pkg install vulkan-loader mesa-dri   # lavapipe: программный Vulkan
```

Корректность вперёд скорости. Настоящий GPU в QEMU на FreeBSD — отдельный
проект через virtio-gpu venus. Вывод идёт в **ту же** Wayland-поверхность из
ступени 3, ничего переделывать не требуется.

---

## 7. Chrome

Здесь он и должен стоять — в конце, после того как всё перечисленное работает.

**Что он требует сверх ступеней 1–4:**

| Требование | Статус | Комментарий |
|---|---|---|
| Многопроцессность (zygote, GPU, рендереры) | ступень 1 | без неё не стартует вообще |
| Mach IPC между процессами | ступень 2 | весь IPC Chrome на портах |
| GUI | ступень 3 | |
| GPU | ступень 4 | **обходится** флагом `--disable-gpu` |
| Sandbox | не делаем | **обходится** флагом `--no-sandbox` |
| Code signing | не делаем | |
| IOSurface, разделяемая память между процессами | не начато | передача кадров GPU↔рендерер |
| XPC-сервисы, launchd | не начато | |

Первый запуск, когда до него дойдёт, разумно пробовать так:

```sh
"Google Chrome" --no-sandbox --disable-gpu --single-process
```

`--single-process` снимает требование многопроцессности для самой первой
проверки — но это костыль для одного эксперимента, а не путь: в этом режиме
Chrome работает иначе и многого не покажет.

**Честно о дистанции.** Между ступенью 3 и Chrome лежит не одна ступень, а
класс задач, который никто в отрасли пока не прошёл. North star нужен, чтобы
выбирать правильные решения на ступенях 1–4, а не чтобы обещать сроки.

---

## 8. Чего не делаем и почему

- **Sandbox и code signing** — Chrome обходится флагами, про совместимость они
  ничего не проверяют.
- **aarch64** — пока не закрыт x86-64.
- **Свой AppKit или форк Cocotron** — обнуляет заимствование у апстрима.
- **Уход на Darwin/XNU вслед за ravynOS.** Они [ушли с FreeBSD в октябре
  2025](https://github.com/ravynsoft/ravynos/discussions/529), потому что для
  них совместимость **была продуктом** и терять было нечего; заплатили
  поддержкой железа, ZFS и Linux-совместимостью. У нас FreeBSD несущая: jails и
  `jailrun`, ZFS, kolkhoz, Zenoh-меш, конвейер стриминга. Их вывод верен для их
  целевой функции и не переносится на нашу автоматически.

---

## 9. Отношения с апстримом

Чем меньше расхождение, тем дешевле забирать их GUI-работу. Большинство наших
правок — специфика FreeBSD и уйдёт наверх только как порт целиком. Но два
исправления самодостаточны и чинят Linux тоже:

1. **`darling-foundation`** — `setPersistentDomain:forName:` вызывал
   `registerDefaults:`, то есть писал в энергозависимый `NSRegistrationDomain`;
   `defaults write` не работает и на Linux. Рядом
   `removePersistentDomainForName:` чистит `self` вместо названного домена.
2. **`darling-libkqueue`** — в `evfilt_machport_copyout` проверяется только
   `rv < 0`, а `recv()`, вернувший 0, проскакивает как успех, после чего
   неинициализированная структура со стека разбирается как уведомление.

Плюс сообщение (не патч) для `darlingserver`: он молча закладывается на
уникальный адрес у каждого сокета — на Linux это даётся бесплатно через
autobind и нигде не записано.

В описаниях PR честно указывать, что проверено только на FreeBSD.
