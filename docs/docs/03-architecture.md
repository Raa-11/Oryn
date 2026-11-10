# 03 — Arsitektur: Modular Monolith + Event-Driven + PAL

## Gambaran besar

```text
                ┌──────────────────────────────┐
                │ UI .slint / Slint event loop  │
                └──────────────┬───────────────┘
                               │ commands / view-model updates
                ┌──────────────▼───────────────┐
                │ app/composition_root          │
                │ modul, services, lifetime     │
                └──────────────┬───────────────┘
                               │
        ┌──────────────────────▼──────────────────────┐
        │          FEATURE MODULES (satu process)      │
        │ Mirror AskAI Music Calendar Time Agents Set.│
        └─────┬────────────────┬────────────────┬──────┘
              │                │                │
     ┌────────▼─────┐   ┌──────▼───────┐ ┌──────▼─────────┐
     │ Core events  │   │ Infrastructure│ │ PAL interfaces │
     │ EnTT, async  │   │ SQLite, curl,  │ │ camera, media, │
     │ Asio, stdexec│   │ JSON, logging  │ │ window, secrets│
     └──────────────┘   └──────────────┘ └──────┬─────────┘
                                              Win/Linux/mac
```

**Modular Monolith:** satu aplikasi desktop dan build artifacts seperlunya; setiap feature memiliki batas API dan `cc_library` sendiri. Bukan microservices. Helper `acp-proxy` adalah pengecualian teknis yang mungkin diperlukan untuk menjadi perantara proses ACP, bukan backend produk.

**Event-Driven:** event bertipe memberitahu perubahan (`AgentActivityChanged`, `TimerFinished`, `SyncCompleted`), bukan menggantikan semua synchronous commands. Core memakai EnTT Dispatcher. Hindari mengirim event lintas thread ke EnTT tanpa aturan ownership; worker harus mengirim ke bounded inbox, lalu dispatcher dipanggil di thread yang ditetapkan.

**Platform Abstraction Layer:** feature memanggil interface seperti `ICamera`, `IMediaSession`, `IWindowCapabilities`, `ICredentialVault` — bukan langsung menyisipkan Windows/macOS/Linux API di business logic. Platform-specific implementations dipilih di composition root dan Bazel config.

## Batas module

- `app/`: bootstrap, composition root, shutdown, dependency injection manual.
- `core/`: event bus, async runtime, lifecycle, contracts generik yang benar-benar dipakai bersama.
- `features/`: behavior aplikasi menurut domain masing-masing.
- `infrastructure/`: HTTP, SQLite, JSON, Quill, credential plumbing.
- `platform/`: adapter OS, perizinan dan kapabilitas.
- Batas C++ internal dinyatakan sebagai **named modules**, bukan header: `.cppm` untuk interface yang diekspor, `.cpp` untuk implementation unit/entry point. `#include` hanya untuk standard library, SDK/dependency, dan generated Slint; pada module unit berada sebelum deklarasi named module di global module fragment.
- `../../ui`: Slint components/screens/view models, tanpa blocking task.

## Aturan dependency

```text
app -> features, core, infrastructure, platform, ui
features -> core, contracts, exposed infrastructure interfaces, PAL interfaces
infrastructure -> core/contracts, platform interfaces bila perlu
platform implementations -> PAL interfaces
ui -> feature-facing view-model/callback contract
```

- Fitur A **tidak** mengimpor internal Fitur B; gunakan kontrak publik atau event yang benar.
- `core/` jangan berisi logic chat, agent, music atau calendar.
- Komunikasi `UI -> service` bisa berupa command langsung; `service -> UI` berupa event state atau callback terjadwal di thread Slint.
- Jangan menaruh credentials/prompt/code di event shared tanpa kebutuhan eksplisit.

## Contoh alur Agents

1. Zed meluncurkan ACP agent (atau proxy yang diluncurkan sebagai custom external agent) dan berbicara lewat JSON-RPC/stdio.
2. ACP proxy **jika POC berhasil** meneruskan kedua arah tanpa mengubah semantik; menangkap `session/update` relevan.
3. `features/agents/acp` mengubah event menjadi `AgentActivity` typed domain event.
4. `core/events` mengantrikan notifikasi; update UI melalui Slint main event loop.
5. View collapsed menunjukkan agent + aktivitas + file (jika tersedia). Expanded memberi ringkasan. Detail dibuka di Zed.

## Contoh alur Ask AI

`Slint send -> ChatController -> AIProvider(OpenAI) -> HttpClient(libcurl) -> SSE decoder -> Glaze -> delta batching -> Slint`; histori ditulis asynchronous lewat SQLite worker.

## State dan lifecycle

- Pemilik service jelas: composition root; setiap service punya `start()` / `stop()` idempotent.
- UI boleh ditutup/minimize sementara proses aplikasi berjalan: status Agents tetap bisa diterima jika monitoring aktif.
- Camera lifecycle hanya selama preview aktif; jangan membiarkan kamera hidup saat panel tutup.
- Shutdown urut: tolak pekerjaan baru -> batalkan/selesaikan I/O -> drain event -> commit/close DB -> join worker -> tutup UI/runtime.

## Anti-pattern

- Menjalankan network/DB/launch process sinkron di Slint UI thread.
- Semua fitur dihubungkan via global singleton/event tanpa kontrak.
- Menampilkan progres agent buatan ketika sumber ACP tidak ada.
- Poll 100ms semua integrasi saat UI idle.
- Berasumsi PAL dapat memaksa always-on-top pada setiap Wayland compositor.

Detail [Async](05-async-event-lifecycle.md), [folder](04-repository-structure.md), [feature Agents](../features/06-agents.md).
