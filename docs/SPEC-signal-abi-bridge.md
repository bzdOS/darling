# SPEC: rt_sigaction (Linux ABI) → FreeBSD sigaction(2) bridge в mldr

Статус: спецификация для реализации. Код не менялся, это документ-исследование.

Место реализации: `src/startup/mldr/freebsd_syscall_trap.c`, функция
`dispatch_linux_syscall()`, case `LINUX_SYS_rt_sigaction` (сейчас — строки
2205–2209).

Текущий код (сломан, EINVAL):

```c
case LINUX_SYS_rt_sigaction:
    /* Signal *numbers* differ between Linux and macOS/FreeBSD; passing
     * through is only correct for numbers that happen to coincide
     * (mirrors the same caveat already noted on MACOS_SYS_sigprocmask). */
    return freebsd_raw_syscall(SYS_sigaction, a1, a2, a3, 0, 0, 0);
```//src/startup/mldr/freebsd_syscall_trap.c:2205-2209

---

## 0. Как вызов сюда попадает (маршрут)

`LINUX_SYS_rt_sigaction` в `dispatch_linux_syscall()` вызывается **только** из
`sigill_handler()` (`src/startup/mldr/freebsd_syscall_trap.c:2771`), т.е. через
"raw-Linux-syscall" путь #198: guest-код содержит настоящую x86-64
инструкцию `syscall` с Linux-номером в `eax`/`rax`, mldr на этапе загрузки
патчит такие места на `ud2` (`mldr_patch_linux_raw_syscalls`,
`src/startup/mldr/freebsd_syscall_trap.c:2436`), `#UD` ловится
`sigill_handler`, оттуда — сюда.

Кто реально делает такой `syscall`-инструкцией `rt_sigaction`:
`_linux_syscall` — ассемблерная обвязка, скомпилированная В ЭТОМ ДЕРЕВЕ
(`src/external/xnu/darling/src/libsystem_kernel/emulation/src/linux_premigration/linux-syscall.S:5-12`),
которую вызывает `sys_sigaction()`
(`src/external/xnu/darling/src/libsystem_kernel/emulation/src/xnu_syscall/bsd/impl/signal/sigaction.c:67-69`)
через макрос `LINUX_SYSCALL(__NR_rt_sigaction, linux_signum, &sa, &olsa, sizeof(sa.sa_mask))`.

x86-64 тело `_linux_syscall`:
```asm
movl	8(%rsp), %eax     ; syscall nr = 7-й Cdecl-аргумент (со стека)
movq	%rcx, %r10        ; syscall ABI fixup
syscall
ret
```//src/external/xnu/darling/src/libsystem_kernel/emulation/src/linux_premigration/linux-syscall.S:7-12

Это байт-в-байт `SIG_GENERIC_THUNK` из `mldr_patch_linux_raw_syscalls`
(`8b 44 24 08 / 49 89 ca / 0f 05 / c3`,
`src/startup/mldr/freebsd_syscall_trap.c:2379-2385`) — значит именно этот
компилируемый в дереве `_linux_syscall` и есть один из двух патчуемых
сигнатур, а не только байты аплоуда dyld, как написано в файловом
хедер-комментарии (`freebsd_syscall_trap.c:73-96`, там речь только про
overlay dyld — но `_linux_syscall` из `libsystem_kernel`, который тоже
загружается как Mach-O сегмент через `loader.c:295`, патчится тем же
кодом). Это не противоречие для спецификации ниже, просто уточнение
источника трафика — по нему приходит **konkретно тот wire-формат**,
который строит `sys_sigaction()`.

Аргументы, с которыми `dispatch_linux_syscall` вызывается для
`rt_sigaction` (из `sigill_handler`, `freebsd_syscall_trap.c:2763-2771`):
`a1 = linux_signum`, `a2 = &sa` (новый `struct linux_sigaction*` или NULL),
`a3 = &olsa` (старый, выходной, или NULL), `a4 = sizeof(sa.sa_mask)` (=8,
не используется текущим кодом), `mc` — доступен (mcontext SIGILL-обработчика).

---

## 1. Linux ABI wire-формат `struct sigaction` (то, что реально лежит по `a2`/`a3`)

Источник — сама точка формирования запроса, `sys_sigaction()`:

```c
struct linux_sigaction sa, olsa;
...
sa.sa_sigaction = &handler_linux_to_bsd_wrapper;   // или SIG_DFL/IGN/ERR из nsa
sigset_bsd_to_linux(&nsa->sa_mask, &sa.sa_mask);
sa.sa_flags = sigflags_bsd_to_linux(nsa->sa_flags) | LINUX_SA_RESTORER;
sa.sa_restorer = sig_restorer;

ret = LINUX_SYSCALL(__NR_rt_sigaction, linux_signum,
    (nsa != NULL) ? &sa : NULL, &olsa,
    sizeof(sa.sa_mask));
```//src/external/xnu/darling/src/libsystem_kernel/emulation/src/xnu_syscall/bsd/impl/signal/sigaction.c:58-69

Структура определена здесь:

```c
struct linux_sigaction
{
	linux_sig_handler* sa_sigaction;
	int sa_flags;
	void (*sa_restorer)(void);
	linux_sigset_t sa_mask;
};
```//src/external/xnu/darling/src/libsystem_kernel/emulation/include/conversion/signal/sigaction.h:96-102

`linux_sigset_t` — 8-байтовая битовая маска (не kernel `sigset_t` из 64
слов, а компактный тип, который используется во всём этом дереве для rt_*
эмуляции):

```c
typedef unsigned long long linux_sigset_t;
```//src/external/xnu/darling/src/libsystem_kernel/emulation/include/conversion/signal/duct_signals.h:53

### Раскладка полей (x86-64, натуральное выравнивание Си, `alignof`=8)

| offset | size | поле | смысл |
|---|---|---|---|
| 0  | 8 | `sa_sigaction` | указатель на обработчик (Linux ABI: `void(*)(int, struct linux_siginfo*, void*)`), либо `SIG_DFL`(0)/`SIG_IGN`(1)/`SIG_ERR`(-1) |
| 8  | 4 | `sa_flags` | битовая маска, см. §3 |
| 12 | 4 | *padding* | **не инициализируется явно** — см. риск ниже |
| 16 | 8 | `sa_restorer` | всегда `sig_restorer` (см. §4), если `nsa->sa_sigaction` не DFL/IGN/ERR — практически всегда установлен |
| 24 | 8 | `sa_mask` | `linux_sigset_t`, один 64-битный битовый вектор, бит `i-1` = сигнал `i` |

Итого `sizeof(struct linux_sigaction) == 32`, `alignof == 8`.

**Риск (не проверено динамически, только по чтению кода):** байты
offset 12-15 — это padding компилятора между `int sa_flags` и указателем
`sa_restorer`; `sa` — локальная переменная на стеке, `memset` над ней не
делается нигде в `sys_sigaction()`. Если mldr при чтении `sa_flags` со
стороны FreeBSD когда-либо прочитает **8 байт** вместо 4 (например по
ошибке скопирует как `long`), верхние 4 байта будут содержать мусор со
стека guest-процесса. **Правило для реализации: читать `sa_flags` строго
как `int32_t` по смещению `+8`, никогда не как 8-байтовое значение.**

### Как использовать в mldr

Поскольку в mldr нет доступа к хедеру `sigaction.h` из `libsystem_kernel`
(это guest-side заголовок, не входит в сборку mldr), в
`freebsd_syscall_trap.c` нужно завести **локальную копию layout**, по
образцу уже существующего паттерна для `sigaltstack`
(`freebsd_syscall_trap.c:2038-2042`, локальный `struct linux_sigaltstack`
объявлен прямо в `case`-блоке):

```c
struct linux_sigaction_wire {
    void   *sa_handler;     /* offset 0, sig_handler* или SIG_DFL/IGN/ERR */
    int32_t sa_flags;       /* offset 8 */
    /* 4 байта padding, offset 12 — НЕ трогать/не читать */
    void  (*sa_restorer)(void); /* offset 16 */
    uint64_t sa_mask;       /* offset 24, linux_sigset_t */
};
_Static_assert(sizeof(struct linux_sigaction_wire) == 32, "wire layout drift");
```

---

## 2. FreeBSD `struct sigaction` (нативный layout)

В дереве darling-freebsd своих FreeBSD-заголовков нет
(`sys/signal.h` не поставляется в составе этого репозитория — это внешний
системный хедер). **Ниже layout был всё же сверен** не "по памяти", а по
реальному `sys/signal.h` из FreeBSD 15, найденному на этом хосте вне
дерева darling-freebsd (закэшированный build-root aarch64-таргета bsdOS,
путь `$BUILD_CACHE/aarch64/work/rootfs/usr/include/sys/signal.h`
— это настоящий FreeBSD-заголовок, а не написанный в этой сессии текст, но
он лежит **вне `src/external` и вне `darling-freebsd`**, поэтому
помечаю его отдельно от "ссылок в дереве" из остального документа).

```c
struct sigaction {
	union {
		void    (*__sa_handler)(int);
		void    (*__sa_sigaction)(int, struct __siginfo *, void *);
	} __sigaction_u;
	int	sa_flags;
	sigset_t sa_mask;
};
#define	sa_handler	__sigaction_u.__sa_handler
#define	sa_sigaction	__sigaction_u.__sa_sigaction
```

`sigset_t`:

```c
#define	_SIG_WORDS	4
typedef struct __sigset {
	__uint32_t __bits[_SIG_WORDS];
} __sigset_t;   /* sizeof == 16 */
```

### Раскладка полей (x86-64/aarch64, оба LP64, layout не зависит от арх.)

| offset | size | поле |
|---|---|---|
| 0  | 8  | `__sigaction_u` (handler или sigaction fn ptr) |
| 8  | 4  | `sa_flags` |
| 12 | 16 | `sa_mask` (`__sigset_t`, 4×`uint32_t`, без доп. padding — `alignof(uint32_t)==4`) |
| 28 | 4  | *tail padding* (структура выравнивается на 8 из-за ведущего union с указателями) |

Итого `sizeof(struct sigaction) == 32`, `alignof == 8`. **Совпадение
общего размера с Linux-версией (тоже 32) — случайность на уровне суммы,
раскладка полей после offset 8 полностью иная** (у Linux там ещё
restorer-указатель + 8-байтовая маска, у FreeBSD сразу 16-байтовая
маска).

`SYS_sigaction` — номер syscall берётся из системного `<sys/syscall.h>`,
использованного уже существующим (рабочим) кодом
`freebsd_raw_syscall(SYS_sigaction, ...)` в
`freebsd_syscall_trap.c:2209` — это компилируется и, значит, корректен
как имя символа; сам номер не переносится в спецификацию (не нужен, если
не переписывать `freebsd_raw_syscall`-обвязку).

---

## 3. Таблица соответствия `sa_flags`

### 3.1 Числовые значения

| Флаг | Linux (bit) | FreeBSD (bit) | Комментарий |
|---|---|---|---|
| SA_NOCLDSTOP | `0x00000001` | `0x0008` | |
| SA_NOCLDWAIT | `0x00000002` | `0x0020` | |
| SA_SIGINFO   | `0x00000004` | `0x0040` | mldr обязан **всегда** ставить FreeBSD `SA_SIGINFO`, см. §3.3 |
| SA_ONSTACK   | `0x08000000` | `0x0001` | |
| SA_RESTART   | `0x10000000` | `0x0002` | |
| SA_NODEFER   | `0x40000000` | `0x0010` | |
| SA_RESETHAND | `0x80000000` | `0x0004` | |
| SA_RESTORER  | `0x04000000` | *(нет аналога)* | см. §3.2 |

Linux-значения — из
`src/external/xnu/darling/src/libsystem_kernel/emulation/include/conversion/signal/duct_signals.h:42-49`
(идентичные повторно определены в
`.../include/conversion/signal/sigaction.h:17-23`, без `SA_RESTORER` там).
FreeBSD-значения — из `sys/signal.h` (см. §2, тот же кэшированный
заголовок, вне дерева darling-freebsd, непроверено сборкой в этой сессии
на 185/186 — но это дословный текст официального FreeBSD-заголовка, не
"по памяти").

**sa_flags, приходящий от `sys_sigaction()`, всегда содержит и `LINUX_SA_SIGINFO`,
и `LINUX_SA_RESTORER`** (жёстко ORятся на guest-стороне, строка
`sigaction.c:64`) — это фиксированная часть каждого вызова, не зависящая
от того, что просил вызывающий guest-код.

### 3.2 SA_RESTORER — у FreeBSD нет аналога

Linux `rt_sigaction` в его нативном ядре *требует* `sa_restorer` +
`SA_RESTORER`, потому что ядро само не знает, как вернуться из
пользовательского обработчика — оно прыгает на `sa_restorer`, которая
обязана дернуть `rt_sigreturn`. FreeBSD устроено иначе: **ядро FreeBSD
само подставляет системный trampoline** (`sigcode`/`freebsd_sigcode`,
через `psl_sigcode`), сохранённый ещё при `execve()`. FreeBSD `sigaction(2)`
не принимает пользовательский restorer вообще — в структуре
`struct sigaction` такого поля просто нет (см. §2).

**Что делать с `LINUX_SA_RESTORER`/`sa_restorer` при трансляции:**
- бит `LINUX_SA_RESTORER` (`0x04000000`) **отбрасывается** — у него нет
  ни аналога, ни смысла на FreeBSD-стороне, и, что важно, если случайно
  передать это значение как есть — оно не совпадёт ни с одним валидным
  FreeBSD `SA_*`-битом и (по общей семантике BSD `sigaction()`,
  отклоняющей нераспознанные `sa_flags`) само по себе может быть
  источником `EINVAL` — **это наиболее вероятная непосредственная причина
  наблюдаемого `EINVAL` уже сегодня**, поскольку текущий код передаёт `a2`
  как есть, а по нему уже "зашит" этот бит. (Это заключение — по общей
  архитектуре BSD syscalls, а не по чтению кода ядра FreeBSD в этой
  сессии; см. «Открытые вопросы», п. 1, где это явно помечено как
  неподтверждённое.)
- сам указатель `sa_restorer` (offset 16, значение — адрес
  `sig_restorer` из `sig_restorer.S`) — **не копируется** в FreeBSD-структуру,
  там для него просто нет поля. Игнорируется целиком.

### 3.3 Обязательная досборка: FreeBSD `SA_SIGINFO`

mldr сам ставит свои обработчики (`sigsys_handler`, `sigill_handler`,
`crash_debug_handler`) с `SA_SIGINFO` (`freebsd_syscall_trap.c:2920,2946,2958`)
и полагается на трёхаргументную форму (`sa_sigaction`, не `sa_handler`).
Guest всегда просит Linux `SA_SIGINFO` (жёстко ORится в
`sigaction.c:64`), и указатель, который реально ставится ядру — либо
`handler_linux_to_bsd_wrapper` (трёхаргументная сигнатура), либо
`SIG_DFL`/`SIG_IGN`/`SIG_ERR` (спец-значения, не указатели на код). Значит:
FreeBSD-сторона **обязана** транслировать `nsa` в вызов с `SA_SIGINFO`
всегда, когда `sa_handler` — не один из спец-значений, и передавать
guest-указатель напрямую в `sa_sigaction` (не оборачивать его в свой C
trampoline — он уже трёхаргументный на guest-стороне, и именно guest-код
после получения сигнала переведёт `linux_ucontext`/`linux_siginfo` из
FreeBSD-нативного контекста — это уже существующая логика в
`sigrt_handler`/`sigexc.c`, вне зоны этой спецификации).

---

## 4. Возврат из обработчика — риск с `sa_restorer`/sigreturn

### 4.1 Что уже есть в mldr

`SIG_SIGRETURN_TRAMP` (`freebsd_syscall_trap.c:2387-2391`):

```c
static const uint8_t SIG_SIGRETURN_TRAMP[] = {
    0xb8, 0x0f, 0x00, 0x00, 0x00, /* mov eax, 15  (Linux __NR_rt_sigreturn) */
    0x0f, 0x05                    /* syscall */
};
```

Это байт-в-байт `sig_restorer` для x86-64
(`src/external/xnu/darling/src/libsystem_kernel/emulation/src/linux_premigration/signal/sig_restorer.S:6-8`:
`movl $15, %eax` / `syscall`). `mldr_patch_linux_raw_syscalls` находит эту
сигнатуру в загруженных Mach-O EXEC-сегментах и патчит `syscall`→`ud2`
(`SIG_SIGRETURN_TRAMP_SYSCALL_OFF = 5`), так что при попытке
`sig_restorer` выполнить `syscall` вместо этого прилетает `#UD`, который
ловит `sigill_handler`, а тот при `linux_nr == 15` (Linux
`__NR_rt_sigreturn`) должен маршрутизироваться в
`dispatch_linux_syscall` — **но в текущем коде `LINUX_SYS_rt_sigreturn` в
таблице `dispatch_linux_syscall` не определён вообще** (нет такого
`#define LINUX_SYS_...` рядом с остальными в блоке
`freebsd_syscall_trap.c:196-255`, нет `case` в `switch`). Значит сейчас
любой дошедший до этой точки `rt_sigreturn` попадёт в `default:` и
получит `-ENOSYS` (`freebsd_syscall_trap.c:2354-2357`), а `mc_rax` будет
установлен в `(uint64_t)-ENOSYS` и `mc_rip += 2` — **выполнение
продолжится ПОСЛЕ несостоявшегося sigreturn, а не восстановит контекст
до сигнала.** Это отдельный, не менее важный пробел, но он **не входит
в тему `rt_sigaction`** — фиксирую его тут как явно необходимый соседний
кусок работы (см. «Открытые вопросы», п. 2), раз без него `rt_sigaction`
всё равно не даст работающего end-to-end сценария "поймать сигнал и
вернуться".

### 4.2 Ключевой архитектурный риск: чей trampoline реально используется

Guest **обязан** ставить `sa_restorer` в глазах Linux ABI — это
жёстко закодировано `sys_sigaction()`
(`sigaction.c:64-65`, `sa.sa_flags |= LINUX_SA_RESTORER; sa.sa_restorer = sig_restorer;`)
и не является чем-то, что mldr может "выключить" на guest-стороне без
патча этого файла (что запрещено заданием — не трогать существующие
файлы за пределами `docs/`).

Но FreeBSD `sigaction(2)` **не принимает пользовательский restorer** —
FreeBSD-ядро само решает, куда прыгать при доставке сигнала (свой
`sigcode`, который в итоге сделает `sigreturn(2)`, а не
`rt_sigreturn`-байты guest'а). Из этого следует развилка:

**Вариант A — довериться нативному FreeBSD-trampoline.**
mldr транслирует `rt_sigaction` в чистый FreeBSD `sigaction(2)` с
`sa_sigaction = guest_handler` (тем самым `handler_linux_to_bsd_wrapper`
из `sigaction.c:176-180`, компилируемым в guest-libsystem_kernel) и
`SA_SIGINFO`. Тогда при реальной доставке сигнала: FreeBSD-ядро вызывает
`guest_handler(signo, siginfo, ucontext)` напрямую (как обычный
sa_sigaction-обработчик), тот в конце делает `dserver_rpc_interrupt_exit()`
и возвращается (`return`) из функции — **а не** прыгает на `sig_restorer`
через `syscall $15`. FreeBSD-рантайм подхватывает этот `return` как
обычный возврат из signal-handler-функции, вызванной ядерным
`sigcode`-trampoline'ом, и сам делает `sigreturn(2)` с native-контекстом.
**В этом варианте `sa_restorer`/`SIG_SIGRETURN_TRAMP`-патч в принципе не
нужен для штатного возврата** — он существовал бы только если бы
guest-код когда-либо *явно* прыгал на `sig_restorer` (а не просто
`return`), чего в текущем `handler_linux_to_bsd_wrapper` нет — это
обычная Си-функция, вызываемая и возвращающая управление штатно.

**Вариант B — воспроизвести Linux-семантику буквально** (ядро прыгает
на guest-код `sig_restorer`, тот сам вызывает `rt_sigreturn`). Этот
вариант физически недостижим напрямую: FreeBSD `sigaction(2)` не
принимает восстанавливающий адрес от вызывающего — ядро всегда подставляет
своё. Практический риск здесь скорее не "какой вариант выбрать", а
**удостовериться, что в реализованном коде путь A действительно тот, что
происходит**, и что `SIG_SIGRETURN_TRAMP`-патч/`sigill_handler` не
окажется на пути штатного возврата (т.е. что `sig_restorer` физически
никогда не вызывается для сигналов, доставленных через FreeBSD
`sigaction`, — он остаётся мёртвым кодом до тех пор, пока какой-то другой
guest-путь не начнёт явно прыгать на него).

**Честный вывод:** по чтению кода в этом дереве вариант A выглядит
корректным и достаточным для `rt_sigaction`, а `rt_sigreturn`-заглушка
(SIG_SIGRETURN_TRAMP/ud2-патч) скорее относится к другому, более
экзотическому пути (возможно — `sigexc.c`/ptrace-эмуляция или future-proofing
на случай, если какой-то guest-компонент вызовет `rt_sigreturn` вручную,
не через возврат из функции). **Это не подтверждено динамически** —
пометка в разделе рисков.

---

## 5. Сигналы, которые сам mldr занимает — гостю их отдавать нельзя

`setup_macos_syscall_trap()` (`freebsd_syscall_trap.c:2901-2962`) ставит:

| FreeBSD-сигнал | Обработчик | Назначение | Где |
|---|---|---|---|
| `SIGSYS` | `sigsys_handler` | ловит macOS `syscall`-инструкции (класс `0x02.../0xFF...`) | `freebsd_syscall_trap.c:2916-2925` |
| `SIGILL` | `sigill_handler` | ловит `ud2`, которыми запатчены raw-Linux-syscall трамплины (#198) | `freebsd_syscall_trap.c:2942-2951` |
| `SIGBUS` | `crash_debug_handler` | диагностика падений dyld при загрузке | `freebsd_syscall_trap.c:2954-2960` |
| `SIGSEGV` | `crash_debug_handler` | то же | `freebsd_syscall_trap.c:2954-2960` |

Через `mldr_linux_signo_to_freebsd()` (§6) Linux-сигналы, ведущие к этим
же FreeBSD-номерам:

| Linux signo | Linux имя | → FreeBSD signo | FreeBSD имя | Занят mldr под |
|---|---|---|---|---|
| 4  | `SIGILL` | 4  | `SIGILL` | `sigill_handler` |
| 7  | `SIGBUS` | 10 | `SIGBUS` | `crash_debug_handler` |
| 11 | `SIGSEGV`| 11 | `SIGSEGV`| `crash_debug_handler` |
| 31 | `SIGSYS` | 12 | `SIGSYS` | `sigsys_handler` — **это сам механизм трансляции macOS-syscall'ов**, самый критичный |

Если guest вызовет `rt_sigaction` на любой из этих 4 Linux-номеров и
mldr слепо переустановит FreeBSD `sigaction()` для этого сигнала —
**он снесёт собственный обработчик mldr**, и весь механизм трансляции
(включая сам SIGSYS-трап, на котором стоит вообще всё взаимодействие с
darlingserver) перестанет работать после первого же такого вызова.

### Предлагаемое поведение

Не выполнять реальный FreeBSD `sigaction()` для этих четырёх FreeBSD-номеров
(4, 10, 11, 12) — вместо этого:
- **отвечать "успешно"**, ведя учёт guest-запрошенного обработчика в
  собственном mldr-стейте (по аналогии с тем, как `sys_sigaction()` на
  guest-стороне уже держит `sig_handlers[]`/`sig_flags[]`/`sig_masks[]`
  — `sigaction.c:25-30`, `96-102`);
- **не звать** реальный `sigaction()` для этих номеров вообще (в
  отличие от штатного пути в §1-3);
- на `olsa` (если `a3 != NULL`) возвращать текущее mldr-состояние для
  этого сигнала, не то, что реально стоит в ядре (там всегда стоит сам
  mldr-обработчик).

Обоснование именно "тихого успеха", а не `EINVAL`/`ENOSYS`: в
`sys_sigaction()` уже есть прецедент именно такого поведения — для
Linux-сигналов без BSD/macOS-эквивалента (`signum_bsd_to_linux() == 0`)
код **намеренно** отвечает `0` с обнулённым `osa`, а не ошибкой, с явным
комментарием, что некоторые программы (Node.js) перебирают все номера
сигналов подряд и падают на первой же ошибке (`sigaction.c:38-50`). Тот
же риск актуален и здесь — код guest'а, не знающий о существовании
mldr-инфраструктуры, не должен получать неожиданный `EINVAL` только
потому, что попросил обработчик на сигнал, который внутренне занят под
транспорт.

**Отдельно: `kill`/`raise` тех же сигналов guest-кодом.** Не в скоупе
этой спецификации (запрошен только `rt_sigaction`), но стоит явно
зафиксировать как смежный риск: если guest когда-либо намеренно пошлёт
себе `SIGSYS`/`SIGILL`/`SIGBUS`/`SIGSEGV` (Linux `kill(getpid(), N)` →
`LINUX_SYS_kill`, который уже транслируется как чистый passthrough,
`freebsd_syscall_trap.c:2203-2204`), он попадёт прямиком в mldr-обработчик,
а не в тот, что guest "думает", что установил — это уже сегодняшнее
поведение (не новый риск от этой спецификации), но стоит держать в уме
при тестировании.

---

## 6. Проверка `mldr_linux_signo_to_freebsd()`

Код (`freebsd_syscall_trap.c:320-341`):

```c
static const unsigned char map[32] = {
    [1]  = SIGHUP,  [2]  = SIGINT,  [3]  = SIGQUIT, [4]  = SIGILL,
    [5]  = SIGTRAP, [6]  = SIGABRT, [7]  = SIGBUS,  [8]  = SIGFPE,
    [9]  = SIGKILL, [10] = SIGUSR1, [11] = SIGSEGV, [12] = SIGUSR2,
    [13] = SIGPIPE, [14] = SIGALRM, [15] = SIGTERM,
    [16] = 0,            /* SIGSTKFLT — no FreeBSD counterpart */
    [17] = SIGCHLD, [18] = SIGCONT, [19] = SIGSTOP, [20] = SIGTSTP,
    [21] = SIGTTIN, [22] = SIGTTOU, [23] = SIGURG,  [24] = SIGXCPU,
    [25] = SIGXFSZ, [26] = SIGVTALRM, [27] = SIGPROF, [28] = SIGWINCH,
    [29] = SIGIO,
    [30] = 0,            /* SIGPWR — no FreeBSD counterpart */
    [31] = SIGSYS,
};
```

Сверка: Linux-номера (левая часть) — из
`src/external/xnu/darling/.../conversion/signal/duct_signals.h:8-40`
(`LINUX_SIGHUP=1` … `LINUX_SIGSYS=31`). FreeBSD-номера (символы `SIGxxx`
справа — компилируются против системного `<signal.h>`, реальные значения
сверены по тому же кэшированному `sys/signal.h`, что и в §2):
`SIGHUP=1, SIGINT=2, SIGQUIT=3, SIGILL=4, SIGTRAP=5, SIGABRT=6, SIGBUS=10,
SIGFPE=8, SIGKILL=9, SIGUSR1=30, SIGSEGV=11, SIGUSR2=31, SIGPIPE=13,
SIGALRM=14, SIGTERM=15, SIGCHLD=20, SIGCONT=19, SIGSTOP=17, SIGTSTP=18,
SIGTTIN=21, SIGTTOU=22, SIGURG=16, SIGXCPU=24, SIGXFSZ=25, SIGVTALRM=26,
SIGPROF=27, SIGWINCH=28, SIGIO=23, SIGSYS=12`.

**Ошибок в таблице не найдено.** Все 29 сопоставлений (кроме двух
намеренных нулей: Linux 16 `SIGSTKFLT` и 30 `SIGPWR`, у которых на
FreeBSD действительно нет аналога) семантически верны — таблица
построена через *имена* сигналов (`SIGBUS`, `SIGUSR1`, …), а не через
числа, поэтому расхождения в самих числах между Linux и FreeBSD (напр.
Linux `SIGBUS`=7 vs FreeBSD `SIGBUS`=10, Linux `SIGUSR1`=10 vs FreeBSD
`SIGUSR1`=30, Linux `SIGURG`=23 vs FreeBSD `SIGURG`=16) — это и есть то,
что таблица корректно устраняет, а не баг.

Единственное, что стоит отметить не как ошибку, а как **важное
наблюдение для §5**: `map[31] = SIGSYS`, `map[4] = SIGILL`,
`map[7] = SIGBUS`, `map[11] = SIGSEGV` — то есть ровно те 4 входа
таблицы, которые ведут на "занятые mldr" FreeBSD-сигналы. Это не повод
чинить саму таблицу (маппинг корректен), а повод обязательно применить
правило §5 в реализации `rt_sigaction`.

---

## 7. Черновой алгоритм `LINUX_SYS_rt_sigaction` (для реализации, не код)

1. `linux_signo = (int)a1`. Если `linux_signo < 1 || linux_signo > 31` (у
   таблицы `map[32]`, индексы 1..31 валидны) — `return -EINVAL`
   (аналогично уже принятому стилю границ в файле, напр.
   `mldr_linux_signo_to_freebsd()` сама делает такую проверку внутри
   и вернёт `0` за пределами — это надо явно отличать от "нет
   соответствия").
2. `native_signo = mldr_linux_signo_to_freebsd(linux_signo)`.
   Если `native_signo == 0` (Linux 16/`SIGSTKFLT`, 30/`SIGPWR`, либо >31
   realtime-сигналы, которых у FreeBSD нет вообще) — вернуть `0` с
   `olsa`, обнулённым по образцу `sys_sigaction()`'s
   `signum_bsd_to_linux()==0`-ветки (`sigaction.c:38-50`), **не** `EINVAL`
   — тот же resilience-аргумент про перебор сигналов подряд.
3. Если `native_signo` — один из {4 `SIGILL`, 10 `SIGBUS`, 11 `SIGSEGV`,
   12 `SIGSYS`} — см. §5, отдельная ветка без реального `sigaction()`.
4. Иначе — прочитать guest wire-структуру по `a2` (если не NULL) как
   `struct linux_sigaction_wire` (§1), собрать нативный
   `struct sigaction`:
   - `sa_sigaction = (void*)wire->sa_handler` (передаётся как есть —
     это либо `handler_linux_to_bsd_wrapper`, либо `SIG_DFL`(0)/`SIG_IGN`(1)/
     значение `-1` для `SIG_ERR`, которые численно совпадают между Linux
     и FreeBSD/POSIX (`SIG_DFL=0, SIG_IGN=1` по обоим ABI) — **это стоит
     явно перепроверить по `XNU_SIG_DFL/IGN/ERR`
     (`sigaction.h:71-79`, `0/1/-1`) против FreeBSD `SIG_DFL/IGN/ERR`
     (`0/1/-1` из `sys/signal.h:138-140`, см. §2) — совпадают, спец-значения
     передаются transparently**);
   - `sa_flags` = транслировать по таблице §3.1, **отбросив**
     `LINUX_SA_RESTORER`-бит и **добавив** FreeBSD `SA_SIGINFO`
     безусловно (см. §3.3) — т.е. не "переносить" Linux `SA_SIGINFO`
     как условие, а ставить FreeBSD `SA_SIGINFO` всегда, когда
     `sa_handler` не DFL/IGN/ERR (согласуется с тем, что guest всегда
     просит его сам);
   - `sa_mask` = разложить `wire->sa_mask` (8-байтовая Linux-маска, бит
     `i-1` → Linux-сигнал `i`) через `mldr_linux_signo_to_freebsd()` в
     FreeBSD `sigset_t` (4×32 бита) — тем же паттерном, что уже
     применяется для `LINUX_SYS_rt_sigprocmask`
     (`freebsd_syscall_trap.c:2264-2273`, `sigemptyset`/`sigaddset`-цикл
     1..64) — только диапазон 1..31 (Linux `sa_mask` тут 8-байтовый, не
     64-байтовый, как в `rt_sigprocmask`, но цикл 1..64 безопасен: биты
     32-64 просто не будут установлены, т.к. в 8-байтовой Linux-маске их
     нет физически).
5. Если `a3 != NULL` — после успешного `freebsd_raw_syscall(SYS_sigaction,
   native_signo, &native_sa, &native_osa, ...)` собрать `olsa` в обратную
   сторону (native → wire), включая обратную трансляцию `sa_flags` и
   `sa_mask`; `sa_restorer` в `olsa` **всегда** ставить в `sig_restorer`
   (тот же паттерн, что guest ожидает — но mldr не знает адрес
   guest-side `sig_restorer`, поэтому реалистичнее **оставить
   `olsa->sa_restorer` как guest прислал в предыдущем вызове** — mldr
   должен **кешировать** последний `sa_restorer`/полный `wire`-запрос на
   сигнал в собственной таблице по образцу guest-side `sig_handlers[]`,
   раз FreeBSD-ядро это поле никогда не хранит и не возвращает).
6. Вызвать `freebsd_raw_syscall(SYS_sigaction, native_signo,
   pss_or_null, poss_or_null, 0, 0, 0)`, вернуть код ошибки как есть
   (тот же стиль, что и `LINUX_SYS_sigaltstack`,
   `freebsd_syscall_trap.c:2060-2069`).

**Важно (см. п. 5 алгоритма и общий комментарий): раз FreeBSD
`sigaction(2)` не хранит и не отдаёт назад `sa_restorer`, а `osa`-запрос
от guest семантически ожидает получить назад именно то, что сам
установил — mldr обязан вести собственный per-signal registry
(handler/flags/mask/restorer как guest их передал), а не полагаться на
`oact`, возвращаемый ядром FreeBSD, для полей, которых там нет.** Это
прямая параллель тому, что уже делает `sig_handlers[]`/`sig_flags[]`/
`sig_masks[]` на guest-стороне (`sigaction.c:25-30`) — только теперь то
же самое нужно на mldr-стороне для полей, которых нет во FreeBSD ABI
вообще (`sa_restorer`).

---

## Открытые вопросы и риски (честно, без прикрас)

1. **Причина сегодняшнего `EINVAL` — не подтверждена чтением кода ядра
   FreeBSD в этой сессии.** Наиболее вероятная гипотеза — нераспознанный
   бит `LINUX_SA_RESTORER` (`0x04000000`) в `sa_flags`, который BSD
   `sigaction(2)` традиционно отклоняет как невалидный набор флагов (это
   общее свойство BSD `sigaction()`-семейства, а не что-то специфичное,
   проверенное здесь по исходнику `sys/kern/kern_sig.c` — в этой сессии
   такая проверка была прервана по прямому указанию не читать код за
   пределами `hal/darling-freebsd`). Возможны и другие/дополнительные
   причины (несовпадающий `signum`, если сигнал > 31 и ловится по кривому
   пути, garbage в padding-байтах при неаккуратном чтении структуры).
   Перед тем как считать исправление готовым — нужен живой тест с `truss`
   на dev VM (что тоже не делалось в этой сессии — только чтение кода).

2. **`LINUX_SYS_rt_sigreturn` (Linux nr=15) не реализован в
   `dispatch_linux_syscall`.** `SIG_SIGRETURN_TRAMP`-патч и
   `SIG_GENERIC_THUNK` в коде уже есть (`freebsd_syscall_trap.c:2387-2391`),
   но нет ни `#define LINUX_SYS_rt_sigreturn`, ни `case` для него — если
   guest-код когда-либо реально исполнит патченный `sig_restorer` (см.
   §4.2, вариант B), результат — тихий переход на случайный адрес
   (`mc_rip += 2` без восстановления контекста). Для целей ЭТОЙ
   спецификации (обычный `handler_linux_to_bsd_wrapper`, который просто
   `return`ит) это, по чтению кода, не должно исполняться в штатном
   режиме — но если тестирование покажет, что `sig_restorer` всё же
   вызывается, это отдельная, обязательная к реализации задача.

3. **Guest-side `sys_sigaction()` сегодня реально шлёт `rt_sigaction`
   только для `LINUX_SIGCHLD` и `LINUX_SIGTTOU`** (условие
   `linux_signum == LINUX_SIGCHLD || linux_signum == LINUX_SIGTTOU`,
   `sigaction.c:52`). Для всех остальных сигналов активная ветка
   (`sigaction.c:84-94`) **не делает syscall вообще** — только
   локальная bookkeeping-запись в `sig_handlers[]`/`sig_flags[]`/
   `sig_masks[]`, `ret = 0` без похода в ядро. Рядом в том же файле
   лежит закомментированный (`#if 0`, `sigaction.c:107-174`) полностью
   общий вариант той же функции, который шлёт `rt_sigaction` для
   **любого** сигнала. Это прямо противоречит формулировке задачи
   ("гость не может установить НИ ОДНОГО обработчика") в буквальном
   прочтении активного кода: сегодня, если верить активной (не `#if 0`)
   версии, обработчики для большинства сигналов вообще не пытаются
   дойти до ядра, а значит и до mldr. **Возможные объяснения (не
   проверено): (а) задача описывает поведение после мысленного
   восстановления полного `#if 0`-варианта — то есть фикс на стороне
   mldr готовится ПОД БУДУЩЕЕ включение общего пути; (б) наблюдаемый
   `EINVAL`-баг репортился именно с CHLD/TTOU (единственных, что реально
   доходят до syscall сегодня); (в) в дереве есть более новая версия
   `sigaction.c`, которая уже не соответствует прочитанной здесь.**
   Перед реализацией стоит явно сверить с автором задачи, какая версия
   `sys_sigaction()` актуальна/будет включена — иначе фикс в mldr
   корректен, но затронет только 2 сигнала из 31 при сегодняшнем
   guest-коде.

4. **`sa_restorer`-registry на стороне mldr (п. 5 алгоритма) — новое
   состояние, которого сегодня в mldr нет.** Нужно решить формат
   хранения (статический массив `[32]` по образцу
   `mldr_linux_signo_to_freebsd`'s `map[32]`, живущий как файл-статик в
   `freebsd_syscall_trap.c`) и его потокобезопасность — mldr переходит в
   многопоточный guest уже сегодня (`mldr_setup_thread_signal_stack`,
   комментарий про `libdispatch`-воркеров, `freebsd_syscall_trap.c:2846-2863`),
   а `sigaction()` может вызываться параллельно из разных guest-потоков.
   Файл сегодня нигде не использует блокировки для похожих таблиц (напр.
   `_sigsys_altstack` — per-process, не per-signal-table), поэтому нужно
   явно продумать: либо `pthread_mutex`, либо смириться с тем, что race
   на этой таблице почти никогда не будет наблюдаем (сигналы
   регистрируются, как правило, на старте, не под нагрузкой) — но это
   осознанный компромисс, а не то, что уже решено кодом.

5. **Соответствие `linux_siginfo`/`ucontext`, которые реально видит
   guest-обработчик при доставке через нативный FreeBSD `sigaction`, — вне
   скоупа этой спецификации.** `handler_linux_to_bsd_wrapper` ожидает
   `struct linux_siginfo*`/`ctxt` в Linux ABI-формате
   (`sigaction.c:176`, `handler_linux_to_bsd(linux_signum, info, ctxt)`),
   а FreeBSD ядро передаст `siginfo_t*`/`ucontext_t*` в FreeBSD ABI —
   если сама трансляция (не `rt_sigaction`, а именно доставка) не
   реализована где-то ещё, guest-обработчик получит указатели неверного
   layout при первом же реальном срабатывании поставленного обработчика.
   Задание просило только `rt_sigaction`, но эта смежная проблема
   напрямую определяет, будет ли результат ЭТОЙ спецификации
   демонстрируемо рабочим (сигнал ставится без `EINVAL`, но реальная
   доставка может тут же упасть/дать мусор) — стоит зафиксировать как
   следующий тикет.
