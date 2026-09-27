# SPEC: контракт источника кадров для wlstream-конвейера

Статус: черновик, зафиксировано 2026-08-26. Зона документа — конвейер стриминга
экрана (`cage → wayland-tunnel → bsdos-core → Zenoh → metal-viewer`) и его
контракт с источником кадров. Внутренности будущего AppKit/macOS-совместимого
слоя (Darling) — вне зоны, ими занят другой агент.

Каждое утверждение ниже — со ссылкой `file:line` на код, который я реально
открыл. Там, где я не нашёл источник (например tokenized-исходники
`wayland-tunnel`, написанные на Zig, а не Rust — HAL, GPU-детали), это сказано
явно.

---

## 1. Топология конвейера (как есть сегодня)

```
cage (headless Wayland-композитор, wlroots)
  │  wl_shm buffers, surface commits — обычный Wayland-протокол
  ▼
wayland-tunnel (Zig, `$WAYLAND_TUNNEL/src/relay.zig`, stream.zig, input.zig)
  │  парсит Wayland wire-protocol из cage-сокета,
  │  кодирует в wlstream v1 wire-формат,
  │  пишет в Unix-сокет WLSTREAM_STREAM_SOCK
  ▼
bsdos-core::wayland_forwarder (bsdos-core/src/wayland_forwarder.rs)
  │  читает v1-фреймы из Unix-сокета, публикует как есть в Zenoh
  ▼
Zenoh topic bsdos/app/{app_id}/stream
  ▼
mac-companion/metal-viewer (Swift/Metal, потребитель)
  │  парсит теми же структурами, что определены в крейте wlstream
  ▼
Compositor (wlstream::compositor) → RGBA-кадр → Metal-текстура
```

Обратный канал (ввод, resize) идёт в обратную сторону через отдельные
Zenoh-топики → `bsdos-core::stream_input_handler` /
`stream_resize_handler` → Unix-сокеты `input.sock` / `wlr-randr` →
`wayland-tunnel` инжектит Wayland-события клиенту (cage/app).

**Ключевой факт: `bsdos-core` не знает, что такое Wayland.** Он вообще не
парсит wlstream-события по смыслу (кроме двух полей — `ev_type` и `pool_id` —
нужных для keepalive-кеша, см. §2). Он видит только опаковые
length-prefixed байтовые фреймы и туннелирует их из Unix-сокета в Zenoh и
обратно. Это значит: контракт, который должен выполнить новый источник
кадров — это контракт `wayland-tunnel`'s STREAM_SOCK output, а не контракт
"Wayland". Если источник эмитит валидные wlstream v1 фреймы в тот же
Unix-сокет (или в замену `wayland-tunnel`), `bsdos-core` не заметит разницы.

---

## 2. Контракт источника кадров: формат кадра

### 2.1 Обрамление сокета (framing over the wire)

Источник в `bsdos-core/src/wayland_forwarder.rs:120-145` читает из Unix-сокета
`WLSTREAM_STREAM_SOCK`:

```
[u32 LE payload_size]   ← wayland_forwarder.rs:120, 134
[payload_size bytes]    ← wayland_forwarder.rs:140-145
```
`payload_size` ограничен `MAX_PAYLOAD = 4 * 1024 * 1024` (`wayland_forwarder.rs:47,
135-138`; фрейм с size==0 или > 4 МиБ — отбрасывается, соединение
пересоздаётся).

Это тот же формат, что задокументирован в
`wlstream` crate как "Wire Format" (`spec/WAYLAND_STREAM_PROTOCOL.md:12-18`,
файл `spec/WAYLAND_STREAM_PROTOCOL.md` в cargo-чекауте wlstream)
и реализован в `wlstream::sender::Encoder::finish()`
(`.../925b8f7/src/sender.rs:96-101`): `[total_size: u32 LE][events...]`.
Иными словами — length-prefixed фрейм ЦЕЛИКОМ ПОВТОРЯЕТ обрамление
`Encoder::finish()`; `bsdos-core` не добавляет свой собственный внешний
конверт сверху.

**Важно:** один физический пакет на сокете (`[len][payload]`) может нести
**несколько** wlstream-событий подряд внутри `payload` (`sender.rs:96`:
`self.events` — накопитель, куда `Encoder` пишет любое число событий перед
`finish()`). Источник обязан упаковывать события именно так — как минимум
`bsdos-core` полагается на то, что `ev_type` = первый байт `payload`
(`wayland_forwarder.rs:147`: `payload[4]`, т.к. byte 0..4 — это заголовок
длины) соответствует событию, которое форвардер кеширует/детектирует; если в
одном фрейме несколько событий, форвардер видит только тип первого.
Практически безопаснее эмитить **один Wayland-эквивалентный "жест" (create
или pool_data+commit) на один сокетный write**, как это делает
`wayland-tunnel` (см. §2.4).

### 2.2 Событийная модель (event types)

Из `spec/WAYLAND_STREAM_PROTOCOL.md:28-109` (тот же документ — источник
истины и для Rust-парсера в `wlstream::parser`, который и `wayland_forwarder`,
и `metal-viewer` импортируют напрямую — `wayland_forwarder.rs:13`:
`use wlstream::parser::{EV_POOL_DATA, EV_SURFACE_COMMIT}`;
`mac-companion/metal-viewer/src/wayland_stream.rs:18`:
`pub use wlstream::parser::*;`):

| type | имя | назначение | обязательность для нового источника |
|---|---|---|---|
| `0x01` | `SURFACE_CREATE` | `[surface_id:u32 LE]` — новая поверхность | нужен минимум один surface_id на "окно/экран" |
| `0x02` | `SURFACE_DESTROY` | `[surface_id:u32 LE]` | нужен для корректного teardown |
| `0x03` | `POOL_DATA` | буфер пикселей целиком/обновлённый (см. §2.3) | обязателен |
| `0x04` | `SURFACE_COMMIT` | какой pool на какую surface, damage rect | обязателен |
| `0x05` | `CURSOR_MOVE` | `[x:i32 LE][y:i32 LE]` | опционален (курсор может рисоваться самим источником в кадр) |
| `0xFE` | `SESSION_RESET` | источник/композитор перезапустился — клиент обязан сбросить кеш | обязателен при рестарте источника |
| `0xFF` | `ERROR` | нефатальная ошибка, клиент логирует и продолжает | опционален, но полезен для диагностики |

### 2.3 Формат пикселей / stride / сжатие

`POOL_DATA` (`WAYLAND_STREAM_PROTOCOL.md:41-56`):
```
[pool_id:  u32 LE]
[width:    u16 LE]
[height:   u16 LE]
[stride:   u32 LE]   ← байт на строку
[format:   u32 LE]   ← 0=ARGB8888, 1=XRGB8888
[raw_len:  u32 LE]   ← размер до сжатия
[lz4_len:  u32 LE]   ← размер после LZ4
[lz4_data: lz4_len bytes]
```
Если `lz4_len == raw_len` — данные не сжаты (обратная совместимость,
`WAYLAND_STREAM_PROTOCOL.md:56`). Формат пикселей — LE 32-битные слова,
маппится напрямую на BGRA8 в памяти (`WAYLAND_STREAM_PROTOCOL.md:149-157`).
Референсная реализация компоновки в
`wlstream::compositor::handle_pool_data` подтверждает и границы: тест
`pool_data_oversized_raw_len_is_dropped` и
`pool_data_lz4_size_bomb_is_dropped`
(`.../925b8f7/src/compositor.rs:269-289`) — источник обязан заявлять честный
`raw_len`, иначе клиентский компоновщик молча отбрасывает пакет (защита от
LZ4-decompress-бомбы, упомянутая в
`mac-companion/metal-viewer/src/wayland_stream.rs:6-11` как security fix
#186 — bounding LZ4 decompress against a wire-controlled size).

`SURFACE_COMMIT` (`WAYLAND_STREAM_PROTOCOL.md:58-77`):
```
[surface_id: u32 LE]
[pool_id:    u32 LE]   ← attached pool
[offset:     u32 LE]   ← offset внутри pool
[buf_width:  u16 LE]
[buf_height: u16 LE]
[buf_stride: u32 LE]
[format:     u32 LE]
[damage_x/y/w/h: u16 LE ×4]  ← (0,0,0,0) = full surface
```
Клиент компонует surface из `pool[offset..offset+stride*height]`.

### 2.4 Damage rects — кто считает, кто владеет буферами

`Rect`, `clamp_damage`, `merge_damage` живут в `wlstream::protocol`
(`.../925b8f7` — исходники не отдельно приведены здесь, но реализацию я читал
через copy `mac-companion/metal-viewer/src/protocol.rs:11-13`, которая
`pub use`-ит те же три символа из `wlstream::protocol`; сама логика видна в
локальной копии её тестов, `metal-viewer/src/protocol.rs:38-73`):
- `clamp_damage(damage, surface_w, surface_h) -> Option<Rect>`: `(0,0,0,0)`
  трактуется как "весь surface"; частично выходящий за границы rect —
  обрезается; полностью выходящий — `None`.
- `merge_damage(d1, d2) -> Rect`: объединяющий bounding rect двух damage.

**Кто считает damage:** источник (продюсер кадра). Damage — часть события
`SURFACE_COMMIT`, которое эмитит источник; ни `bsdos-core`, ни
`wayland_forwarder` damage не трогают и не пересчитывают — форвардер только
кеширует байты `POOL_DATA` по `pool_id` (`wayland_forwarder.rs:149-154`) и
переотправляет их по таймеру / при коммите как есть.

**Кто владеет буферами:** референсная реализация в
`wayland-tunnel/src/stream.zig` — источник (Zig-код, читающий
`wl_shm`-буферы cage) владеет пиксельными данными до момента отправки; он
делает `tryCompressLz4` (`stream.zig:102-145`) и хеш-дедуп через FNV-1a
(`relay.zig:79-89`, `pool_hashes[32]` per-thread кеш, `relay.zig:379-380,
617-643`): если хеш пиксельных данных не изменился с прошлой отправки этого
`pool_id`, `POOL_DATA` **не отправляется повторно** — отправляется только
`SURFACE_COMMIT`, ссылающийся на уже известный клиенту `pool_id`. Это ключевая
оптимизация: **новый источник обязан либо повторить этот дедуп сам, либо
принять деградацию по трафику**, если будет слать `POOL_DATA` на каждый кадр
безусловно.

На принимающей стороне (`bsdos-core`) есть свой, независимый уровень
keepalive-кеша (не дедуп, а republish): `wayland_forwarder.rs:59-92` держит
`shared_cache: HashMap<pool_id, Vec<u8>>` и каждые 5 секунд
(`TIMER_KEEPALIVE_SECS`) переотправляет **все** закешированные `POOL_DATA` в
Zenoh — это защита от поздно подключившихся вьюеров (viewer открылся, когда
приложение уже не коммитило новых кадров, напр. `about:blank`). Плюс
отдельный `POOL_REFRESH_SECS = 3`-таймер (`wayland_forwarder.rs:48, 155-165`):
если `SURFACE_COMMIT` пришёл, а с последней публикации `POOL_DATA` для этого
`pool_id` прошло ≥3 c, форвардер повторно публикует закешированный
`POOL_DATA` перед публикацией самого commit-фрейма — подстраховка от
Zenoh-подписчиков, теряющих исходный `POOL_DATA`-пакет. **Источник не должен
дублировать эту логику** — она уже целиком на стороне `bsdos-core`,
источнику достаточно слать `POOL_DATA` один раз при реальном изменении
контента (или при создании pool), и `SURFACE_COMMIT` на каждый видимый кадр.

### 2.5 Синхронизация / темп кадров

Нет отдельного "vsync"-события в протоколе. Темп кадров задаётся тем, как
часто источник шлёт `SURFACE_COMMIT`. `wayland-tunnel` реагирует на реальные
Wayland `wl_surface.commit` от приложения (событийно, не по таймеру) —
т.е. кадры идут ровно тогда, когда что-то реально изменилось (плюс
форвардер-side keepalive раз в 5 c описанный выше, а не жёсткий FPS-cap).
Новый источник должен придерживаться той же модели: эмитить commit по факту
реального изменения контента, не слать constant-rate поток пустых кадров —
дедуп на приёмнике (`bsdos-core`) не защищает от переизбытка `SURFACE_COMMIT`
(commit публикуется в Zenoh безусловно на каждый вызов,
`wayland_forwarder.rs:168`), только от переизбытка `POOL_DATA`.

### 2.6 Session reset / ошибки

`0xFE SESSION_RESET` (`WAYLAND_STREAM_PROTOCOL.md:85-95`): при рестарте
источника — обязателен, клиент обязан сбросить кеш pool/surface
(`WAYLAND_STREAM_PROTOCOL.md:116`: "Compositor crash → send 0xFE reason=0
before exit"). Источник должен реализовать этот же сигнал на своём
рестарте/креше, иначе клиентские кеши (и `bsdos-core`'s собственный
`shared_cache`/`pool_cache`, который также не знает про рестарт источника,
кроме как через `wayland_forwarder`'s собственный TCP-level
disconnect-reconnect, `wayland_forwarder.rs:174-177`: `shared_cache` чистится
только при разрыве Unix-сокета) начнут расходиться с реальностью.

---

## 3. Обратный канал: ввод и resize

### 3.1 Zenoh-топики и bsdos-core сторона

`stream_input_handler` (`bsdos-core/src/stream_manager.rs:955-1043`)
подписывается на:
- `bsdos/app/{app_id}/input/keyboard`
- `bsdos/app/{app_id}/input/pointer`

и пишет в `input.sock` (`rundir/input.sock`, тот же rundir что и
`wayland-stream.sock`). Формат на входе с Zenoh (описан в
`bsdos-core/src/protocol.rs:154-193`):
- keyboard payload (Zenoh): `[key_code:u32][action:u8][modifiers:u8][pad:2][ts_ms:u64]`
  (`protocol.rs:158-159`), пересобирается в 7-байтный tunnel-frame:
  `[0x00][key_code LE:4][action][modifiers]` (`protocol.rs:163-173`).
- pointer payload (Zenoh): `[x:f32][y:f32][buttons:u8][scroll_x:f32][scroll_y:f32]`
  = 17 байт (`protocol.rs:179`), пересобирается в 18-байтный tunnel-frame:
  `[0x01][x:f32][y:f32][buttons:u8][scroll_x:f32][scroll_y:f32]`
  (`protocol.rs:184-192`).

Формат на `input.sock` — задокументирован независимо в
`wayland-tunnel/src/input.zig:21-23`:
```
keyboard: [type=0x00][keycode:u32 LE][action:u8][mods:u8][pad:1] = 7 bytes
pointer:  [type=0x01][x:f32 LE][y:f32 LE][buttons:u32 LE][scroll_x:f32][scroll_y:f32] = 18 bytes
```
(Несовпадение размера поля `buttons` — `u8` со стороны `bsdos-core`
(`protocol.rs:184` копирует `zh[0..17]` включая один байт buttons) против
комментария `buttons:u32 LE` в заголовке `input.zig:23` — сам разбор в коде
(`input.zig:445`: `const buttons = input_buf[9]`, один байт) читает как `u8`,
т.е. комментарий в заголовке `input.zig` расходится с реализацией; трактовать
как `u8`, реализация авторитетна.)

`wayland-tunnel` инжектит эти события напрямую в Wayland-протокол клиенту
(`cage`/приложению) как настоящие `wl_pointer`/`wl_keyboard` события —
`sendKeymap`, `wl_keyboard.enter`, `wl_pointer.enter`, motion, button, axis
(`input.zig:377-528`). **Это специфично для Wayland-приёмников.** Источник,
который не является Wayland-клиентом (например прямой AppKit-рендер),
получал бы ввод по другому механизму — либо ему нужен собственный
inject-путь (аналог `input.zig`, но переводящий эти же байтовые фреймы в
`NSEvent`), либо, если он идёт через Wayland-бэкенд (вариант (а) из §5),
получает их бесплатно как есть.

### 3.2 Resize

`stream_resize_handler` (`stream_manager.rs:1046-1091`) подписан на
`bsdos/app/{app_id}/viewer/size`, payload — строка `"WxH@S"` (физические
пиксели viewer + scale factor), парсится `parse_size_request`
(`bsdos-core/src/protocol.rs:37-56`: `<u32>x<u32>@<u32>`, scale по умолчанию 1
при не-числовом суффиксе). Handler зовёт `wlr-randr --output HEADLESS-1
--custom-mode WxH` (`stream_manager.rs:1084-1088`) с `XDG_RUNTIME_DIR` /
`WAYLAND_DISPLAY=wayland-0` — то есть **это чисто wlroots-специфичный вызов**:
`wlr-randr` меняет режим headless-output композитора `cage`. Источник без
`cage`-бэкенда не сможет использовать этот путь как есть; ему нужен либо свой
resize-механизм (изменение размера собственного офскрин-буфера рендера), либо
Wayland headless-output эмулирующий этот интерфейс.

---

## 4. Таблица используемых Zenoh-ключей

| ключ | направление | payload | назначение | ссылка |
|---|---|---|---|---|
| `bsdos/app/{app_id}/stream` | source→viewer | wlstream v1 length-prefixed бинарный | видеопоток кадров | `wayland_forwarder.rs:44,51` |
| `bsdos/app/{app_id}/input/keyboard` | viewer→source | `[key_code:u32][action:u8][modifiers:u8][pad:2][ts_ms:u64]` | клавиатурный ввод | `stream_manager.rs:966`, `protocol.rs:157-159` |
| `bsdos/app/{app_id}/input/pointer` | viewer→source | `[x:f32][y:f32][buttons:u8][scroll_x:f32][scroll_y:f32]` (17 байт) | указательный ввод | `stream_manager.rs:967`, `protocol.rs:179` |
| `bsdos/app/{app_id}/viewer/size` | viewer→source | строка `"WxH@S"` | resize вьюпорта | `stream_manager.rs:1058`, `protocol.rs:40-44` |
| `bsdos/ctl/stream/start` | control→bsdos-core | текст `app_id:app:url` (`splitn(3, ':')`) | запустить стрим | `main.rs:236-243`, `main.rs:254-263` |
| `bsdos/ctl/stream/stop` | control→bsdos-core | текст `app_id` | остановить стрим | `main.rs:264-267` |
| `bsdos/ctl/stream/list` | control→bsdos-core | (не читается) | список активных стримов (в лог) | `main.rs:268-270` |
| `bsdos/telemetry` | bsdos-core→любой | Cap'n Proto (см. CLAUDE.md) | heartbeat | `main.rs:212` |
| `bsdos/health` | bsdos-core→любой | JSON (`health_snapshot`) | здоровье пайплайна + список стримов | `main.rs:219`, `stream_manager.rs:282-305` |

Также встречен старый одиночный (не per-app) топик `bsdos/viewer/size`
(`main.rs:42`) — судя по имени это legacy path до введения per-stream
`{app_id}/viewer/size`; не рекомендуется использовать для нового источника,
ориентироваться на per-app топики из таблицы.

---

## 5. Главный вопрос: (а) Wayland-бэкенд + переиспользование `cage → wayland-tunnel` без изменений, или (б) прямая эмиссия в wlstream, минуя Wayland

### Что уже готово и чем оно является

- `wayland-tunnel` — не тонкая прокладка, это ~4000 строк Zig-кода
  (`wayland-tunnel/src/*.zig`, суммарно 4035 строк без тестов и
  cache-артефактов), включающий: полный парсер Wayland wire-protocol
  (`wl_registry.zig` 415 строк, `wl_shm.zig` 836 строк), relay-логику с FNV-1a
  дедупом и hash-кешем на 32 pool (`relay.zig` 748 строк), синтетическую
  инъекцию `wl_keyboard`/`wl_pointer` протокольных сообщений
  (`input.zig` 530 строк) с задокументированными неочевидными багами уже
  найденными и исправленными (CMSG-выравнивание на FreeBSD 64-bit,
  `input.zig:17-19`; opcode wl_keyboard.enter=1 не 0, `input.zig:20`).
- `bsdos-core` со стороны `stream_manager.rs` целиком построен вокруг
  трёхпроцессной модели `cage + wayland-tunnel + app` (spawn, socket-wait,
  jail-per-stream, health-check, restart-on-death — весь
  `spawn_processes`/`monitor_loop`, `stream_manager.rs:401-947` и
  `338-383`) — это подразумевает конкретные пути (`wayland-0`,
  `wayland-stream.sock`, `input.sock`, `cage-stdin.fifo`) и переменные
  окружения (`WLR_BACKENDS=headless`, `WLR_RENDERER=pixman`,
  `WLSTREAM_STREAM_SOCK` и т.д.).
- `wlstream` crate сам по себе (Rust) — компоновщик, парсер, LZ4, damage —
  переиспользуется УЖЕ и `bsdos-core`, и `metal-viewer` без форков
  (после того как в 2026 обе стороны конвергировали на upstream crate,
  `metal-viewer/src/wayland_stream.rs:6-11`, `protocol.rs:4-6`) — то есть сам
  формат кадра платформо-независим уже сегодня, дублирования логики нет.

### Вариант (а): дать AppKit-слою Wayland-бэкенд

Практически означает: реализовать headless Wayland-композитор (роль `cage`)
или Wayland-клиента поверх macOS-совместимого слоя, который создаёт
`wl_surface`/`wl_shm`-буферы так, как их видит `wayland-tunnel`. Это огромный
объём работы — по сути "научить Darling/AppKit-слой говорить настоящий
Wayland протокол", т.е. реализовать wl_compositor/wl_shm-семантику НА pixel
buffer'ах, которые AppKit-приложение и так рисует через Core
Graphics/Metal — двойная трансляция (AppKit backing store → синтетический
Wayland surface → `wayland-tunnel`'s собственный парсер этого же Wayland
протокола → wlstream события). Всё, что `wayland-tunnel` делает
(`wl_registry.zig`, `wl_shm.zig`) — это разбор реального libwayland
wire-protocol; писать в этот протокол "с нуля" со стороны источника —
не проще, чем то, что уже сделано в обратную сторону.

### Вариант (б): научить источник эмитить wlstream-события напрямую

Означает: источник (или тонкая обвязка вокруг него) вызывает
`wlstream::sender::Encoder` (уже существующий, протестированный, Rust,
`sender.rs:1-101`, публичный API `surface_create`/`pool_data`/
`surface_commit`/`cursor_move`/`finish()`) напрямую на своих собственных
пиксельных буферах (что бы AppKit-слой ни рендерил — CGImage, Metal
texture readback, IOSurface) и пишет получившийся `finish()`-байты в тот же
`WLSTREAM_STREAM_SOCK`, который сегодня слушает `bsdos-core`'s
`wayland_forwarder` (`wayland_forwarder.rs` не знает и не обязан знать, кто
на другом конце сокета — cage+tunnel или прямой продюсер). Источник должен
сам реализовать: (1) выбор `pool_id`/`surface_id`, (2) LZ4-компрессию
(`wlstream::lz4`, уже есть в крейте, переиспользуется без переписывания), (3)
дедуп по хешу (опционально, для трафика — логика тривиальна, FNV-1a на
~10 строк, см. `relay.zig:79-89` как референс), (4) damage rects
(`wlstream::protocol::{Rect, clamp_damage, merge_damage}` — тоже готовый
код, просто вызывается).

**Что при этом теряется:** обвязка `bsdos-core::stream_manager` вокруг
`cage`+`wayland-tunnel` (spawn/health/jail/FIFO-teardown,
`stream_manager.rs:387-947`) рассчитана на процессную модель "3 отдельных
процесса, wl-сокеты между ними". Для источника, который сам эмитит
wlstream-фреймы (не через Wayland), эта обвязка либо не нужна вовсе (если
источник — часть самого `bsdos-core` или отдельный процесс, слушающий свой
собственный `WLSTREAM_STREAM_SOCK` эквивалент), либо требует новой,
более простой версии `spawn_processes` — по объёму на порядок меньше, чем
писать Wayland-сервер.

**Что при этом ТОЖЕ теряется (и это надо явно взвесить):** ввод и resize.
`input.zig` инжектит через настоящий `wl_pointer`/`wl_keyboard` протокол —
если источник не Wayland-клиент, этот путь целиком бесполезен: нужен новый
inject-механизм, переводящий те же 7/18-байтные фреймы с `input.sock` в
`NSEvent`/AppKit-эквиваленты. `wlr-randr`-based resize (`stream_manager.rs:
1084-1088`) — та же история, требует замены на прямой resize офскрин-буфера
источника. Оба этих узла API-совместимы по контракту (Zenoh-топик, байтовый
формат кадра ввода/resize не меняются — таблица §4), но **реализация на
стороне источника заменяется целиком**, независимо от выбора (а)/(б) — они
не связаны с вопросом "через Wayland или нет" для видео-пути; это отдельная,
неизбежная работа в любом случае.

### Рекомендация: вариант (б)

Аргументы:
1. **Объём работы асимметричен на порядок в пользу (б).** (а) требует
   реализовать серверную сторону Wayland-протокола (роль cage) поверх
   AppKit-примитивов — это переписывание уже написанного (и
   отлаженного, с задокументированными issue) `wl_shm.zig`/`wl_registry.zig`
   с нуля на другой платформе, ради того чтобы затем `wayland-tunnel`
   распарсил его обратно в те же wlstream-события. (б) вызывает готовый,
   уже переиспользуемый `wlstream::sender::Encoder` напрямую.
2. **Формат кадра уже платформонезависим.** `wlstream` — отдельный
   Rust-крейт с публичным API именно для этого: `sender::Encoder` для
   продюсеров, `parser`/`compositor` для потребителей
   (`lib.rs:8-13,16-19`). Ничего Wayland-специфичного в самом wire-формате
   нет (см. §2) — "Wayland" в имени протокола отражает происхождение
   семантики (surface/pool/damage), не транспорт и не обязательность
   реального libwayland где-либо в цепи.
3. **`bsdos-core` уже агностичен к источнику на уровне forwarder'а.**
   `wayland_forwarder` работает с байтами из Unix-сокета вслепую
   (§1) — заменить "кто пишет в WLSTREAM_STREAM_SOCK" не требует
   правок в `wayland_forwarder.rs` вообще, только замену/дополнение
   `spawn_processes` в `stream_manager.rs`, чтобы для этого источника не
   спавнились `cage`/`wayland-tunnel`.
4. **Риск двойного бага-класса.** Вариант (а) заново реализует парсинг
   протокола, у которого уже была найдена и исправлена платформоспецифичная
   ошибка (CMSG offset на FreeBSD, `input.zig:17-19`) — велик шанс наступить
   на аналогичные "тонкости конкретной ОС" в реализации Wayland-сервера на
   macOS-слое, не получив от этого ничего, чего не даёт (б).

Условие, при котором стоило бы пересмотреть в пользу (а): если AppKit-слой
всё равно должен будет предоставлять честную Wayland-совместимость по
причинам вне стриминга (например для запуска немодифицированных
Linux/Wayland-приложений через тот же compat-слой) — тогда стриминг "бесплатно"
наследует это как побочный эффект, и объём в п.1 амортизируется другой
задачей. Но если единственная цель — стрим экрана AppKit-приложения, (б)
дешевле и с меньшим риском.

---

## 6. Открытые вопросы и риски

1. **`wayland-tunnel` написан на Zig, `wlstream::sender::Encoder` — на Rust.**
   Если новый источник — Rust-код (Darling/AppKit-мост, вероятно, тоже
   Rust-обвязка судя по остальному проекту), путь (б) даёт прямой доступ к
   `wlstream` crate без FFI. Если источник — Zig или Swift, придётся либо
   реализовывать encoder заново на том языке (протокол простой,
   `sender.rs` — 101 строка), либо городить FFI в Rust `wlstream`. Не
   исследовал, на каком языке будет сам AppKit-мост — это решение другого
   агента/сессии, но контракт кодека должен это учитывать.
2. **Дедуп и keepalive размазаны по трём слоям** (§2.4): FNV-1a-дедуп в
   `wayland-tunnel` (не отправлять неизменившийся `POOL_DATA`), 5-секундный
   republish-таймер и 3-секундный commit-triggered refresh в
   `wayland_forwarder.rs`. Новый источник, который не повторит дедуп, будет
   работать корректно, но с существенно большим трафиком по Zenoh при
   статичном контенте (каждый `SURFACE_COMMIT` таскает полный `POOL_DATA`,
   если источник шлёт его безусловно). Не блокер, но стоит поставить дедуп в
   явные требования к источнику, а не оставлять "опционально".
3. **`ev_type`-детект в `wayland_forwarder.rs:147` смотрит только на первый
   байт `payload`** (`payload[4]`, т.е. первое событие в фрейме). Если новый
   источник когда-нибудь захочет упаковывать несколько разнотипных событий в
   один сокетный write (что валидно по wire-формату — см. §2.1), keepalive-
   логика форвардера увидит только первое событие фрейма и может неверно
   определить тип всего пакета для целей кеширования. Референсная
   реализация (`wayland-tunnel`) не создаёт таких смешанных фреймов на
   практике — не подтверждено явным чтением `relay.zig`'s send call sites на
   предмет "может ли один write нести и pool_data, и commit", но
   `sendPoolData`/`sendSurfaceCommit` (`stream.zig:158,232`) выглядят как
   отдельные функции с отдельными вызовами `writeExact`, что предполагает
   раздельные `write()`, т.е. раздельные `[len][payload]`-фреймы на сокете
   — не проверено построчно, стоит подтвердить перед тем как источник
   станет полагаться на противоположное поведение.
4. **`bsdos/viewer/size` (не per-app) vs `bsdos/app/{app_id}/viewer/size`**
   — в `main.rs:42` есть подписка на первый, не-namespaced топик; неясно,
   жив ли этот путь ещё для какого-то сценария, или это чисто legacy. Не
   исследовал `main.rs` целиком за пределами процитированных строк. Новый
   источник должен ориентироваться на per-app топик (таблица §4), но если
   legacy-топик всё ещё на что-то влияет — стоит явно проверить перед
   интеграцией.
5. **Ввод и resize для не-Wayland источника — отдельная, недокументированная
   здесь задача.** §3 фиксирует контракт байтовых форматов и Zenoh-топиков
   (они не меняются), но конкретный механизм "как AppKit-слой получит эти
   байты и превратит в NSEvent/офскрин-resize" не специфицирован — это
   явно вне зоны этого документа (ввод/resize-инъекция на стороне AppKit —
   вероятно, зона того параллельного агента, занятого внутренностями
   AppKit-совместимости). Стоит явно закрепить владельца этой задачи, чтобы
   она не потерялась между двумя agent-зонами.
6. **`format_pointer_payload`/`input.zig`-заголовок расхождение по типу
   `buttons`** (§3.1: `u8` в реализации vs `u32 LE` в комментарии
   `input.zig:23`) — мелкое, но если новый источник имплементит
   инъекцию ввода самостоятельно (не переиспользуя `wayland-tunnel`),
   ориентироваться строго на код (`input.zig:445`: один байт), не на
   заголовочный комментарий.
7. **Не проверялось:** внутренности `wl_shm.zig` (836 строк) на предмет,
   какие именно wl_shm-форматы/multi-buffering паттерны `wayland-tunnel`
   поддерживает сегодня (single-buffer? double-buffer? damage-region
   merge на стороне relay?) — для варианта (б) это неважно (источник не
   идёт через `wl_shm` вообще), но если решение сместится к (а), этот файл
   надо будет прочитать целиком, не только сигнатуры функций.
