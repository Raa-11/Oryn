# 02 — Technology Stack (Keputusan Aktif)

> `TERPILIH` = disetujui sebagai arah proyek, bukan berarti sudah diuji pada ketiga OS. `TARGET/POC` = perlu pembuktian kompatibilitas. `DITUNDA` = bukan MVP.

## Bahasa, UI, build

| Area | Stack | Status | Catatan |
|---|---|---|---|
| Bahasa utama | **C++23 + named modules** | TERPILIH | Interface internal `.cppm`/`export module`, consumer `import`; `.cpp` untuk implementasi/entry point. Header dependency/generated tetap boleh di global module fragment |
| UI | **Slint C++ (AOT)** | TERPILIH | `.slint` dikompilasi; bukan runtime interpreter untuk release |
| Window backend | **Winit** melalui Slint | TERPILIH | Tidak berarti seluruh fitur native window tersedia sama pada Wayland |
| Renderer | **Slint software renderer** | TERPILIH | Default `winit-software` sejak D-003 direvisi; divalidasi pada Windows |
| Grafik OS | Software rasterization + presentasi Winit | TERPILIH | Tidak membuat konteks GPU renderer secara default; Linux/macOS belum diuji |
| Build | **Bazel + Bzlmod** | TERPILIH | C++ dependencies diselaraskan dengan Bzlmod |
| Toolchain version | **Bazelisk** | TERPILIH | `.bazelversion` dipin; compiler CI dipin |

## Core & runtime

| Area | Stack | Status | Catatan |
|---|---|---|---|
| Architecture | Modular Monolith + Event-Driven + PAL | TERPILIH | Detail [03](03-architecture.md) |
| Events | **EnTT Dispatcher** | TERPILIH | Publish/dispatch thread ownership, bounded queue |
| Async task composition | **NVIDIA stdexec** | TERPILIH; POC | Header C++20+ untuk sender/receiver; implementasi masih berkembang |
| Async I/O, timer | **Boost.Asio** | TERPILIH | Dedicated io_context worker, non-blocking operations |
| Spawn process/pipes | **Boost.Process V2** | TERPILIH | Gunakan Boost.Asio seragam, bukan Standalone Asio |
| Thread lifecycle | **`std::jthread`** | TERPILIH | Stop signal, shutdown order, join |
| HTTP | **libcurl** | TERPILIH | HTTPS, streaming SSE, redirects/proxy bila perlu |
| JSON | **Glaze** | TERPILIH | Typed parsing, validation; frames SSE dipisahkan dulu |
| Local DB | **SQLite + WAL** | TERPILIH | Worker untuk writes, migrations, pagination |
| Logger | **Quill** | TERPILIH | Release logs minimal, redaksi data sensitif |
| Tests | **doctest** | TERPILIH | Unit + integration + manual GUI tests |

## Integrasi per OS

| Kapabilitas | Windows | Linux | macOS |
|---|---|---|---|
| Camera | Native camera API (evaluasi Media Foundation / WinRT) | V4L2/PipeWire sesuai device/session | AVFoundation |
| Media now-playing | GSMTC | MPRIS (D-Bus) | Adapter capability-dependent; validasi dahulu |
| Floating window | Window API + Slint | X11/Wayland dengan batas compositor | Native window API + Slint |
| Credential vault | Windows Credential Manager / platform vault | Secret Service/keyring yang tersedia | Keychain |
| Notification | OS-native adapter | Portal/desktop notification | User notification center |

## Hal yang sengaja tidak dipilih

- **libuv** → digantikan Boost.Asio + Boost.Process V2.
- **Boost.Beast sebagai HTTP client utama** → libcurl dipertahankan untuk MVP.
- **Standalone Asio** → Boost.Asio dipilih agar seragam dengan Boost.Process V2.
- **Electron, WebView, Skia** → tidak sesuai pilihan arsitektur UI native saat ini.
- **nlohmann/json** → Glaze; **spdlog** → Quill; **Catch2** → doctest.
- **Qt/Flutter/Dear ImGui** → bukan UI toolkit utama.

## Catatan penting kompatibilitas

1. Startup menetapkan `SLINT_BACKEND=winit-software` hanya bila variabel belum diset. Override eksplisit untuk diagnosis tetap dihormati. SDK lokal menolak `winit-femtovg-wgpu` dan meskipun log fallback menyebut FemtoVG, performance log membuktikan renderer aktif justru Skia/WGPU; jangan mengandalkan fallback itu.
2. stdexec **tidak** menyediakan socket/process/file I/O; adapter Boost.Asio ke sender tidak otomatis siap pakai.
3. libcurl punya event/multi interface sendiri. Jangan menjalankan dua loop secara blocking pada UI thread; rancang bridge/asynchronous driver yang benar atau worker terpisah.
4. Library pilihan tidak membuktikan target RAM/CPU. Ukur binary debug vs release dan paket runtime sungguhan.
5. Periksa kewajiban atribusi lisensi Slint sebelum distribusi, serta lisensi dependency lain.

Lihat [async](05-async-event-lifecycle.md), [risiko](11-open-questions.md), [referensi resmi](12-references.md).
