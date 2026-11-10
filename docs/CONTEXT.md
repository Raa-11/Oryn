# CONTEXT — Baca Ini Saat Memulai Sesi Baru

**Proyek:** Desktop Companion native Windows/Linux/macOS, pengembangan utama Windows. **Status:** tahap 04 Calendar manual offline tersedia setelah floating UI, Time, dan Settings. Task CRUD/status/due date/notes/filter, SQLite WAL/schema v2, preferensi persisten, Windows vault, dan Quill sudah tersedia. Software renderer tetap dipakai; angka historis Home idle 28,19 MiB di `progress/02-time.md` bukan pengukuran ulang tahap 04. CI runner, sleep/resume nyata, dan platform lain belum diverifikasi.

**Verifikasi terbaru:** Oryn Windows kini aplikasi system tray, bukan tombol taskbar biasa. Klik ikon toggle panel; menu native Tampilkan/Keluar; minimize/native close hide tanpa menghentikan timer. Owner RAII dan Slint-managed visibility menjaga resource serta full software repaint. Build normal dan MSVC ASan meluluskan 3/3 test terkait; klik tray/menu, full panel restore, zero ordinary taskbar buttons, serta countdown hidden diverifikasi pada aplikasi normal. Detail/batasan ada di `progress/02-time.md`.

**Time terbaru:** tab Standard/Pomodoro menyimpan dua sesi/deadline independen; navigasi tidak reset/pause. Worker/wake tetap satu, melayani kedua deadline. Regresi Time 16 case/151 assertion dan smoke native kedua countdown lulus; lihat catatan navigasi di `progress/02-time.md`.

**Settings terbaru:** `oryn.settings` menjalankan filesystem/SQLite pada worker dengan satu latest-save slot dan drain/join RAII. Theme/reduced motion, topmost, compact startup, posisi, serta durasi Pomodoro persist restart; Save dari Time menunggu acceptance timer. UI memakai `SettingsData`/`SettingsActions` dan draft yang tidak membuang edit saat navigasi. Corrupt/future DB dipertahankan dengan error, bukan di-reset otomatis. Default `%LOCALAPPDATA%/Oryn/settings.sqlite3`; `ORYN_DATA_DIR` absolute untuk diagnosis. Secret memakai adapter Windows Credential Manager terpisah, tidak masuk tabel Settings; logging Quill hanya label/kode nonsensitif.

**Verifikasi tahap 03:** 8/8 target normal dan 8/8 MSVC ASan lulus saat tahap 03. Smoke membuktikan preferensi persisten, WAL/schema v1, Pomodoro saat pindah tab, dan hide/restore tray. Tahap 04 menaikkan schema ke v2 tanpa mengganti preferensi; spesifikasi Settings ada di `features/07-settings-ai-usage.md`.

**Calendar terbaru:** `oryn.calendar` memiliki worker/SQLite/EnTT sendiri dan bounded FIFO 64 yang menguras mutation saat shutdown. UI `CalendarData`/`CalendarActions` + view-owned draft mengikuti Pencil Today/New task, memakai ID string int64, filter Today/Upcoming/Completed, grouping tanggal, scrolling serta page50. Today juga mencakup overdue/tanpa tanggal; tanggal adalah local civil day, timestamp UTC. Error tidak membuang draft, Save menutup setelah revision commit, delete perlu konfirmasi. Home/compact memakai data Calendar nyata; timer aktif tetap prioritas compact.

**Verifikasi tahap 04:** suite normal 10/10 target lulus; UI menjangkau task ke-50 via wheel dan clock animasi deterministik. Smoke native CRUD, multiline notes, invalid-date draft retention, filter/status/reopen/delete confirmation, restart persistence, timer/tray, serta exit kode 0 lulus pada data folder terisolasi. Migration aplikasi v1 → v2 mempertahankan durasi 27/6/13 dan WAL. Berikutnya **tahap 05 Music Windows**; OAuth/Teams/GitLab/timesheet belum termasuk.

## Keputusan yang sudah dipilih (jangan ganti diam-diam)

- Bahasa **C++23**; UI **Slint** AOT; window backend **Winit**; renderer default **software** (`winit-software`), dipilih sebelum backend dibuat. `SLINT_BACKEND` eksplisit tetap tersedia untuk diagnosis. Skia bukan renderer produk; SDK lokal masih memuat dukungannya. D-003 menggantikan target FemtoVG/WGPU setelah persetujuan pengguna.
- Seluruh interface C++ internal memakai **named modules `.cppm`**, bukan `.hpp`; caller memakai `import`. Header standard library/dependency/generated Slint tetap lewat `#include` pada global module fragment. Implementasi dan entry point tetap boleh `.cpp`.
- Build/dependencies **Bazel + Bzlmod + Bazelisk**.
- Slint memakai global domain `TimeData`/`TimeActions`, `SettingsData`/`SettingsActions`, `CalendarData`/`CalendarActions`, enum status/filter, dan global `Theme`; native Palette mengikuti tema yang sama. Draft Settings/Calendar dimiliki view; state window/navigation tetap pada root.
- Build default opt; `--config=asan` menyediakan MSVC AddressSanitizer opt-in dan job CI Windows terpisah. SDK Slint prebuilt tidak ikut terinstrumentasi; menjalankan konfigurasi sanitizer tidak membuktikan seluruh DLL aman.
- Arsitektur **Feature-Based Modular Monolith + Event-Driven + Platform Abstraction Layer (PAL)**.
- **EnTT Dispatcher** untuk notifikasi event antarmodul; bukan untuk semua pemanggilan method.
- Async **NVIDIA stdexec + Boost.Asio + Boost.Process V2**; `std::jthread`, `std::stop_token`, RAII. **Tidak menggunakan libuv.** Adapter stdexec–Asio perlu validasi.
- HTTP **libcurl** (dipertahankan; tidak diganti Boost.Beast); JSON **Glaze**; local DB **SQLite + WAL**; logging **Quill**; unit test **doctest**.
- Kredensial disimpan di **OS credential vault/keychain**, bukan SQLite plaintext.

## Enam menu utama + Settings

1. **Mirror:** webcam preview sederhana saja, flip, pilih kamera; nonaktifkan capture saat ditutup; tidak merekam.
2. **Ask AI:** chat general-purpose, mulai OpenAI kemudian Claude/Gemini via interface provider; SSE stream; SQLite history; bukan coding agent.
3. **Music:** kontrol player eksternal, Windows GSMTC, Linux MPRIS, adapter macOS menunggu validasi; bukan layanan streaming.
4. **Calendar:** task harian manual + sinkronisasi Microsoft Teams/Graph dan GitLab enterprise melalui polling; hubungan aktivitas dengan task; **timesheet bukan MVP**.
5. **Time:** timer, countdown, Pomodoro dengan event/timer hemat daya.
6. **Agents:** **monitor Codex yang berjalan di Zed melalui ACP**. UI fokus pada status agent, tugas, aktivitas terakhir, file aktif. Ringkasan angka file/`+/-` opsional; **tidak ada editor/diff viewer/terminal ala IDE**. Open in Zed untuk detail. Perolehan ACP events lewat proxy perlu POC.
7. **Settings:** theme, akun/integrasi, preferensi window, keamanan, AI Usage sebagai subhalaman.

## UI Agents yang sudah dipilih

- **Collapsed:** `Codex · Editing service.cpp` (atau status alternatif ketika data file tidak ada).
- **Expanded:** header `Codex / Zed / project / Working`, `Current Task`, `Editing file`, statistik perubahan opsional, tautan `Open in Zed`.
- **JANGAN** memakai daftar tetap `Read → Edit → Bash → Done` sebagai tampilan utama. Status harus diambil dari event, tidak direkayasa.
- Hitungan Git working tree tidak otomatis berarti perubahan Codex; beri label yang jujur.

## Urutan implementasi (mudah → sulit)

Ikuti [docs/09-roadmap.md](docs/09-roadmap.md): **bootstrap/POC → floating UI → Time → Settings dasar → Calendar manual → Music Windows → Ask AI/OpenAI → Mirror Windows → GitLab sync → Teams/Graph sync → Agents Zed/Codex → cross-platform/release**. Jangan menganggap urutan nomor file spesifikasi sebagai urutan implementasi. ACP dan Slint/Bazel tetap membutuhkan *feasibility spike* awal meskipun Agents dikerjakan belakangan.

## Prioritas dan risiko

1. Paket Slint lokal 1.18.1 sudah dipakai untuk codegen/header/runtime Windows, tetapi path SDK machine-local belum menjadi distribusi dependency lintas mesin yang hermetic.
2. Buktikan transparansi/floating window/idle rendering pada Windows dan batasan Wayland.
3. Buktikan Boost.Asio + Boost.Process + stdexec adapter dan shutdown bersih.
4. Buktikan ACP proxy untuk Zed–Codex dapat menerima `session/update`; jangan mengasumsikan semua file/task bisa diketahui.
5. Ukur RAM/CPU idle/startup; angka performa adalah **target**, bukan janji.

**Baca berikutnya:** [AGENTS.md](AGENTS.md), [stack](docs/02-tech-stack.md), [architecture](docs/03-architecture.md), [folder structure](docs/04-repository-structure.md), [Agents feature](features/06-agents.md), [decisions](docs/10-decisions.md).
