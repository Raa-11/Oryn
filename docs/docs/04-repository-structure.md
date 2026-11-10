# 04 — Struktur Folder Source Code (Final Blueprint)

> Ini adalah **target struktur repository** setelah implementasi; banyak modul masih berupa blueprint, bukan file yang sudah ada. Interface C++ internal memakai `.cppm`, bukan `.hpp`; nama modul yang sudah diimplementasikan dicatat di bagian C++ modules di bawah. `../../BUILD.bazel` ditempatkan pada paket yang benar-benar punya target.

```text
desktop-companion/
├── MODULE.bazel                  # Bzlmod module & deps
├── BUILD.bazel                   # optional root aliases/assets
├── .bazelrc                      # compiler, cache, platform flags
├── .bazelversion                 # version pin via Bazelisk
├── README.md
├── AGENTS.md
├── CONTEXT.md
│
├── bazel/
│   ├── slint/                     # codegen rules, C++ AOT integration
│   │   └── defs.bzl
│   └── platforms/                 # target/toolchain/platform config
│       └── BUILD.bazel
│
├── src/
│   ├── app/                       # bootstrap / dependency wiring
│   │   ├── main.cpp
│   │   ├── application.{cppm,cpp}
│   │   ├── composition_root.cpp
│   │   └── BUILD.bazel
│   │
│   ├── core/
│   │   ├── events/                # EnTT typed event dispatcher
│   │   │   ├── event_bus.{cppm,cpp}
│   │   │   ├── event_inbox.cppm
│   │   │   └── BUILD.bazel
│   │   ├── async/                 # stdexec + Boost.Asio bridge
│   │   │   ├── asio_runtime.{cppm,cpp}
│   │   │   ├── sender_adapter.cppm
│   │   │   ├── task_scope.cppm
│   │   │   └── BUILD.bazel
│   │   ├── lifecycle/             # stop ordering and RAII
│   │   └── contracts/             # small shared interfaces / IDs
│   │
│   ├── features/                  # MODULAR MONOLITH
│   │   ├── mirror/
│   │   │   ├── mirror_controller.{cppm,cpp}
│   │   │   ├── mirror_state.cppm
│   │   │   └── BUILD.bazel
│   │   ├── ask_ai/
│   │   │   ├── chat_controller.{cppm,cpp}
│   │   │   ├── conversation_service.{cppm,cpp}
│   │   │   ├── ai_provider.cppm
│   │   │   ├── providers/
│   │   │   │   └── openai/{openai_provider.cpp,BUILD.bazel}
│   │   │   └── BUILD.bazel
│   │   ├── music/
│   │   │   ├── music_controller.{cppm,cpp}
│   │   │   ├── music_state.cppm
│   │   │   └── BUILD.bazel
│   │   ├── calendar/
│   │   │   ├── task_service.{cppm,cpp}
│   │   │   ├── calendar_state.cppm
│   │   │   ├── sync/
│   │   │   │   ├── teams/{teams_sync.cpp,BUILD.bazel}
│   │   │   │   └── gitlab/{gitlab_sync.cpp,BUILD.bazel}
│   │   │   └── BUILD.bazel
│   │   ├── time/
│   │   │   ├── timer_service.{cppm,cpp}
│   │   │   ├── timer_state.cppm
│   │   │   ├── pomodoro_service.cppm
│   │   │   └── BUILD.bazel
│   │   ├── agents/
│   │   │   ├── agent_controller.{cppm,cpp}
│   │   │   ├── session_manager.{cppm,cpp}
│   │   │   ├── activity_monitor.{cppm,cpp}
│   │   │   ├── acp/
│   │   │   │   ├── acp_protocol.{cppm,cpp}
│   │   │   │   ├── acp_proxy.{cppm,cpp}
│   │   │   │   └── BUILD.bazel
│   │   │   └── BUILD.bazel
│   │   └── settings/
│   │       ├── settings_service.{cppm,cpp}
│   │       ├── ai_usage/{usage_service.cpp,BUILD.bazel}
│   │       └── BUILD.bazel
│   │
│   ├── infrastructure/
│   │   ├── database/
│   │   │   ├── connection.{cppm,cpp}
│   │   │   ├── migrations/        # 001_init.sql, 002_...sql
│   │   │   ├── repositories/      # implementation per domain
│   │   │   └── BUILD.bazel
│   │   ├── http/
│   │   │   ├── http_client.{cppm,cpp}
│   │   │   ├── sse_decoder.{cppm,cpp}
│   │   │   └── BUILD.bazel
│   │   ├── serialization/         # Glaze contracts / adapters
│   │   ├── logging/               # Quill configuration
│   │   └── security/              # credential vault abstraction
│   │
│   └── platform/                  # PAL
│       ├── interfaces/
│       │   ├── camera.cppm
│       │   ├── media.cppm
│       │   ├── window.cppm
│       │   ├── notifications.cppm
│       │   ├── credential_vault.cppm
│       │   └── BUILD.bazel
│       ├── windows/                # camera, GSMTC, windows, secrets
│       │   └── BUILD.bazel
│       ├── linux/                  # camera, MPRIS, window, secrets
│       │   └── BUILD.bazel
│       └── macos/                  # .mm Objective-C++ bridges
│           └── BUILD.bazel
│
├── ui/
│   ├── main.slint
│   ├── components/              # island, icons, dialogs, status
│   │   ├── floating-island.slint
│   │   ├── icon-button.slint
│   │   └── navigation-tab.slint
│   ├── screens/
│   │   ├── mirror.slint
│   │   ├── ask_ai.slint
│   │   ├── music.slint
│   │   ├── calendar.slint
│   │   ├── time.slint
│   │   ├── agents.slint
│   │   └── settings.slint
│   └── themes/                  # colors, typography, motion
│       ├── colors.slint
│       └── typography.slint
│
├── assets/{icons,images,fonts}/
├── tests/{unit,integration,smoke}/
├── docs/                        # detailed specs
├── features/                    # one Markdown specification/feature
└── .github/workflows/build.yml  # Win/Linux/macOS CI
```

## Aturan untuk folder

- **Folder per fitur** bukan lapisan `controllers/`, `services/`, `models/` global yang membesar.
- `ui/screens` hanya komponen visual. Logic networking dan SQL tidak boleh di `.slint`.
- `platform/interfaces` memuat capability contracts, implementasi per OS berada di sibling masing-masing.
- SQLite `repositories/` disusun menurut kebutuhan persistence, tetapi domain logic tetap di `features/`.
- `core/async` mencakup interop stdexec–Boost.Asio, bukan copy libuv.
- Jika ACP proxy benar-benar harus executable terpisah, tambahkan `src/app/acp_proxy_main.cpp` dan Bazel `cc_binary(name="acp_proxy",...)`, reuse kode `features/agents/acp`. Tidak perlu microservices.

## Contoh batas paket Bazel (ilustratif)

```python
# src/features/music/BUILD.bazel
cc_library(
    name = "music",
    srcs = ["music_controller.cpp"],
    module_interfaces = ["music_controller.cppm", "music_state.cppm"],
    textual_hdrs = ["music_controller.cppm", "music_state.cppm"],
    features = ["cpp_modules"],
    local_defines = select({
        "//:windows_opt": ["NDEBUG"],
        "//conditions:default": [],
    }),
    deps = [
        "//src/core/events:events",
        "//src/platform/interfaces:media",
    ],
    visibility = ["//src/app:__pkg__", "//tests:__subpackages__"],
)
```

Target lain harus dibuat nyata sebelum contoh di atas dapat dibangun. Jangan mengklaim repository siap `bazel build` hanya dari blueprint.

Standar C++23 dan mode opt default berada di `.bazelrc`, bukan diulang pada setiap target. `windows_opt` menyamakan macro interface module dan consumer karena rules_cc 0.2.26 belum memasukkan `NDEBUG` pada action kompilasi module. Bzlmod/pin dependency tetap dipakai; cache remote tidak diperlukan untuk repository sekecil ini.

### Modul C++ dan sinkronisasi CLion

Seluruh interface internal saat ini memakai `module_interfaces` untuk file `.cppm`:

| Modul | Interface |
|---|---|
| `oryn.app` | `src/app/application.cppm` |
| `oryn.core` | `src/core/app_info.cppm` |
| `oryn.platform.window` | `src/platform/window.cppm` |
| `oryn.time` | `src/features/time/timer_service.cppm` |
| `oryn.time.state` | `src/features/time/timer_state.cppm` |
| `oryn.time.pomodoro` | `src/features/time/pomodoro_service.cppm` |
| `oryn.settings` | `src/features/settings/settings_service.cppm` |
| `oryn.calendar` | `src/features/calendar/calendar_service.cppm` |
| `oryn.database` | `src/infrastructure/database/database.cppm` |
| `oryn.logging` | `src/infrastructure/logging/logging.cppm` |
| `oryn.platform.paths` | `src/platform/paths.cppm` |
| `oryn.platform.credentials` | `src/platform/credentials.cppm` |

`oryn.time` mengekspor kembali state dan tipe Pomodoro yang menjadi bagian kontrak publiknya; `format_remaining` dimiliki `oryn.time` bersama implementation unit `timer_service.cpp`. Tidak ada header internal, header unit, atau compatibility re-export untuk nama header lama. Implementasi dan `main()` tetap dapat berada di `.cpp`; ekstensi bukan penentu keanggotaan modul.

Tahap 03 memakai `SettingsService` sebagai owner noncopy/nonmove worker `std::jthread`: satu slot latest-save, condition-variable wait saat idle, drain/join saat shutdown. Semua filesystem dan SQLite termasuk migration/WAL/close berada di worker. `oryn.database` memiliki connection/statement/transaction RAII dan kontrak bertipe untuk singleton Settings; tidak mengekspor handle SQLite ke UI. Draft UI berada di `ui/settings-state.slint`, bukan SQL/callback domain di screen. `oryn.platform.paths` menyelesaikan path tanpa membuat folder di thread UI.

Tahap 04 menambah `CalendarService` noncopy/nonmove dengan worker `std::jthread` dan bounded FIFO 64 command; mutation task tidak boleh memakai latest-value coalescing karena create/delete harus berurutan dan tidak hilang. Worker memiliki connection `oryn.database` dan EnTT dispatcher; event setelah commit memperbarui snapshot/revision. App memulai Calendar setelah attempt load Settings pertama untuk mengurutkan migration awal, mengoaleskan snapshot UI ke satu slot, dan menguras Calendar sebelum Settings/logging berhenti.

`oryn.database` menjalankan migration v1 → v2 dan mengekspos `TaskDraft`/`TaskRecord` serta bound CRUD/list/count, bukan raw SQLite. Task list mengambil 50+lookahead dan mengurutkan tanggal/ID secara stabil. Slint memakai `CalendarData`/`CalendarActions` dan ID string int64; `CalendarDraft` bertahan lintas navigasi. `ui/screens/calendar.slint` dan `components/calendar-controls.slint` menyusun agenda/editor dari desain Pencil tanpa mengubah ukuran/clipping native shell atau menambahkan data provider palsu.


Vault Windows berada pada `oryn.platform.credentials`: WinCred, native blob ownership, dan move-only `SecureBuffer` dengan secure wipe. App belum memiliki provider yang perlu bergantung pada modul vault; target diuji mandiri. `oryn.logging` memiliki lifetime Quill, queue bounded, label/kode nonsensitif ke stderr; Settings/Timer worker dihentikan sebelum owner logging dihancurkan.


`oryn.platform.window` memisahkan deklarasi di `window.cppm` dari implementasi di `window.cpp`. Pencarian HWND dan owner RAII Win32 bersifat privat, bukan API modul tambahan. `app` tidak lagi bergantung pada `core` yang tidak dipakainya. UI hanya dihasilkan dari `ui/main.slint` melalui `//ui:compile_ui`; salinan generated dan target lama `ui-gen` yang tidak memiliki caller sudah dihapus.

Boundary PAL window memakai `WindowResult = std::expected<void, WindowError>`. `WindowError` memuat label operasi statis dan kode sistem yang ditangkap langsung saat API dengan kontrak last-error gagal. API tanpa rincian error terdokumentasi memakai kode 0. Owner Win32 serta TimerService secara eksplisit menolak copy dan move.

Pada Windows, `initialize_window(window, expansion)` memasang subclass, membersihkan `WS_CAPTION` dengan frame recalculation satu kali, dan menerapkan clipping pertama setelah HWND tersedia. `WS_SYSMENU | WS_MINIMIZEBOX` dipertahankan untuk kontrak shell. `WM_NCACTIVATE` memakai `lParam=-1` hanya pada window non-iconic, sesuai [kontrak Win32](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-ncactivate). Transisi `WM_SIZE` minimized → restored/maximized diteruskan ke backend terlebih dahulu, lalu seluruh client diinvalidasi sekali agar surface software tidak memakai pixel iconic. Subclass dilepas pada `WM_NCDESTROY`. `update_window_shape` tetap hanya mengurus reveal; tidak memasang hook/style berulang, polling, atau mengubah layout/renderer.

`TrayIcon` adalah owner RAII dalam modul PAL yang sama: borrowed HWND, owned HICON/HMENU dan registrasi `Shell_NotifyIconW` version 4. Setelah registrasi sukses, extended style membuat panel tool window tanpa tombol taskbar; minimize/native close hanya hide. Callback shell menerapkan show/hide dan menu Tampilkan/Keluar, sedangkan `Application::run()` memakai `RunUntilQuit`. Destructor menghapus ikon/menu/subclass tanpa menghancurkan borrowed HWND. Explorer `TaskbarCreated` mendaftarkan ulang ikon; jika shell tidak dapat menyediakan tray yang usable, panel dibuat terlihat dan eligibility taskbar dipulihkan. Tidak ada worker/polling tray tambahan.

`apply_window_preferences` menerapkan topmost dan remembered physical coordinates pada work area monitor yang tersedia. Subclass preference menjaga policy topmost ketika Winit mengaplikasikan ulang style/z-order saat show. `window_position` hanya meminjam HWND. UI membedakan **Start expanded** pada startup berikutnya dari collapse/hide sementara.


Seluruh transisi visibility tray memakai borrowed `slint::Window` yang outlive owner. Pada [backend Slint 1.18.1](https://github.com/slint-ui/slint/blob/v1.18.1/internal/backends/winit/winitwindowadapter.rs), `show()` menginvalidasi full software-renderer region karena Windows membuang pixel window hidden tanpa memperbarui buffer age. `ShowWindow` saja tidak menggantikan lifecycle ini. Pada Windows default, hide mempertahankan HWND; override diagnostik `SLINT_DESTROY_WINDOW_ON_HIDE` berada di luar asumsi native owner.

Gaya C++ tulisan tangan mengikuti `.clang-format` di root: indent 4 spasi termasuk namespace, batas 100 kolom, braces eksplisit untuk blok kontrol, fungsi tidak satu baris, dan pemisah antar-definisi. Mapping state/UI berada pada helper lokal bernama jelas bila callback menjadi panjang; tidak perlu membuat module/layer tambahan. Resource OS dimiliki scope owner/`unique_ptr` dengan deleter yang sesuai; fungsi yang meminjam handle tidak menghapusnya. Array tetap memakai `std::array` bila cocok dengan boundary Win32, tanpa alokasi heap tambahan.


Header standard library, Win32, Boost, EnTT, dan Slint masuk global module fragment pada module unit (`module;`, lalu `#include`, lalu deklarasi named module). Header UI hasil codegen Slint tetap `.h`; jangan mengedit generated code atau mengubahnya menjadi modul secara artifisial.

Pada Bazel for CLion `2026.10.14.0-api-version-262`, aspect belum membaca atribut `module_interfaces`. Daftarkan interface yang sama juga pada `textual_hdrs` agar CLion memetakannya ke target; atribut ini bukan header API tambahan dan `module_interfaces` tetap bertanggung jawab atas kompilasi. Build/runtime migrasi app/core/time/window sudah diverifikasi pada Windows; sinkronisasi CLion setelah penambahan time/window belum diuji.

Jangan mengedit `.clwb/aspects`: plugin menyalin ulang file-file tersebut setiap sync. Setelah perubahan target, jalankan **Bazel → Non-Incrementally Sync Project with BUILD Files**. `.clwb/.bazelproject` dan target Windows sama-sama memakai `/std:c++23preview`; target non-Windows memakai `-std=c++23`. Jangan gunakan `c++latest` karena standar proyek dibatasi C++23. Sinkronisasi/indexing CLion dengan flag yang diselaraskan belum diuji melalui UI IDE.

Compiler lokal MSVC 19.51.36248 menerima `/std:c++23preview` dengan `_MSVC_LANG=202302L`. Probe menggunakan `/Zc:__cplusplus` juga menghasilkan `__cplusplus=202302L`; switch tersebut hanya dipakai dalam probe, bukan flag tambahan proyek. `/std:c++23` belum dikenali compiler ini (D9002, opsi diabaikan, lalu `_MSVC_LANG=201402L`), sehingga memaksakannya justru menurunkan mode bahasa. Nama `preview` adalah nama switch C++23 yang tersedia pada toolchain ini, bukan izin memakai draft C++26.


## Dependensi terarah

```text
:app
 ├── :ui / Slint generated headers
 ├── //src/features/...
 ├── //src/core/...
 ├── //src/infrastructure/...
 └── //src/platform/{windows|linux|macos}:adapter
```

Gunakan Bazel `select()`/platform constraints untuk memilih **satu implementasi PAL** per target OS. Jaga agar source `.mm` hanya dikompilasi di macOS. Pembuatan codegen Slint AOT + linking runtime melalui Bazel adalah POC pertama, bukan prosedur yang sudah terbukti.

## Konvensi source Slint

- `ui/main.slint` adalah kontrak window/C++ dan composition root: state window/navigation, native Palette synchronization, serta re-export global domain/tema. Root tidak meneruskan seluruh properti dan callback Time.
- `time-state.slint` mengekspor `TimeData`, `TimeActions`, dan enum `TimerStatus`; C++ memakai global melalui referensi konstan dan memetakan enum domain secara eksplisit. Screen/shell mengonsumsi global langsung.
- `themes/theme.slint` menyimpan warna semantik dan ukuran tipografi. Native `Palette.color-scheme` disinkronkan pada root init dan perubahan Theme; tidak ada state tema duplikat di shell.
- `components/floating-island.slint` menyusun shell, konten, animasi reveal, dan navigasi; `icon-button.slint` dan `navigation-tab.slint` memiliki kontrol masing-masing.
- `screens/time.slint` mengekspor hanya `TimeScreen`; `DurationField` dan `TimeButton` tetap privat karena hanya dipakai screen tersebut. `FeaturePanel` yang tidak memiliki caller sudah dihapus.
- `components/settings-controls.slint` mengelompokkan button, switch, text input, dan setting row untuk `screens/settings.slint`. Kontrol visual mengikuti Pencil lewat token Theme; switch tetap semantic checkbox dan seluruh aksi pointer/keyboard/accessibility menghormati enabled. Form memakai `SettingsDraft`, bukan state database pada kontrol.
- Urutan deklarasi: interface properties, callbacks, state privat, binding visual/animasi, lalu child elements. Gunakan nama komponen PascalCase dan identifier properti/callback kebab-case.
- Ikuti [konvensi file Slint](https://docs.slint.dev/latest/docs/slint/guide/language/coding/file/) dan formatter resmi `slint-lsp format --inline <files>`, bukan clang-format. `.editorconfig` mengatur UTF-8, LF, indent empat spasi, dan final newline untuk `.slint`.
- Perapihan source tidak boleh diam-diam mengganti ukuran floating window, kurva clipping, susunan layout, hit area, accessible labels, binding dua arah, atau pengaturan reduced motion.

