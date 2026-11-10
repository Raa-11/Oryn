# 10 — Decision Log (Single Source of Truth)

> Keputusan desain aktif; D-003 direvisi **8 November 2026** dengan persetujuan pengguna setelah pengukuran RAM aplikasi native.

| ID | Keputusan | Status | Rationale singkat |
|---|---|---|---|
| D-001 | C++23 + named modules internal | TERPILIH | Pengguna meminta cutover penuh dari header internal; interface `.cppm`, consumer `import`, implementasi/entry point tetap `.cpp`; dependency/generated tetap header |
| D-002 | Slint untuk UI | TERPILIH | Native custom floating UI, AOT |
| D-003 | Winit window backend + Slint software renderer | TERPILIH | Pengguna memilih RAM minimum; menggantikan target FemtoVG/WGPU. Windows native terukur, Linux/macOS belum diuji |
| D-004 | Bazel + Bzlmod + Bazelisk | TERPILIH | Modular build, pin dependency/toolchain |
| D-005 | Modular Monolith + Event-Driven + PAL | TERPILIH | Isolasi fitur, satu aplikasi, 3 OS |
| D-006 | EnTT Dispatcher | TERPILIH | Typed change notifications, event ownership |
| D-007 | NVIDIA stdexec + Boost.Asio + Boost.Process V2 | TERPILIH / POC | Modern async, process/IPC, ganti libuv |
| D-008 | Tidak memakai libuv | TERPILIH | Menggunakan opsi A C++ native |
| D-009 | libcurl tetap HTTP client | TERPILIH | HTTP/SSE mature, jangan ganti Beast untuk MVP |
| D-010 | SQLite + WAL | TERPILIH | Embedded relational local persistence |
| D-011 | Glaze untuk JSON | TERPILIH | Typed C++ JSON parsing |
| D-012 | Quill untuk logging | TERPILIH | Diagnostics asynchronous; wrapper internal |
| D-013 | doctest untuk testing | TERPILIH | Lean test dependency |
| D-014 | Ask AI = general assistant, OpenAI first | TERPILIH | Provider adapter; Claude/Gemini nanti |
| D-015 | Agents = monitoring Codex yang dipakai di Zed melalui ACP | TERPILIH / POC | No IDE duplication; ACP proxy feasibility perlu dibuktikan |
| D-016 | Agents UI = compact status + task + active file; optional +/- stats | TERPILIH | User memilih desain ringkasan, **bukan** daftar Read/Edit/Bash/Done |
| D-017 | Calendar = manual tasks + Teams/Graph + GitLab polling | TERPILIH / API POC | Read external activity; timesheet ditunda |
| D-018 | Music = control third-party player, tidak stream musik sendiri | TERPILIH | Windows GSMTC, Linux MPRIS, macOS R&D |
| D-019 | Mirror hanya preview tanpa recording | TERPILIH | Minimal privacy/performance |
| D-020 | AI Usage berada di Settings | TERPILIH | Tidak menambah primary menu |
| D-021 | Secrets OS vault; SQLite bukan secret store | TERPILIH | Security & privacy |
| D-022 | No Electron/WebView/Skia | TERPILIH | Native/lightweight target |

## Status terms

- `TERPILIH`: keputusan desain aktif; jangan mengganti tanpa pembicaraan/ADR.
- `TERPILIH / POC`: arah sudah dipilih, integrasi/runtime belum terbukti.
- `DITUNDA`: di luar MVP; boleh dibahas jika scope bertambah.
- `DITOLAK`: bukan pilihan saat ini, dapat dipertimbangkan lagi dengan bukti.

## Alternatif yang pernah dibandingkan

- UI: Qt Quick, Dear ImGui, Flutter, WebView, Electron → Slint menang untuk scope companion kecil.
- Runtime: libuv, Standalone Asio, uvent, dll. → dipilih Boost.Asio + Boost.Process + stdexec.
- HTTP: Boost.Beast, CPR, cpp-httplib → libcurl dipertahankan.
- DB: LevelDB, RocksDB, DuckDB, libmdbx, Turso, PGlite → SQLite/WAL.
- JSON: nlohmann/json → Glaze.
- Logging: spdlog → Quill.
- Tests: Catch2 → doctest.

## Saat ingin mengubah keputusan

Gunakan template ringkas:

```text
Decision ID:
Date:
Old -> New:
Reason / evidence:
Affected folders/modules:
Migration path:
Benchmarks/tests that justify it:
Status: proposed | accepted | rejected
```

Pastikan `CONTEXT.md`, file stack, arsitektur dan fitur relevan tetap konsisten setelah perubahan.

## Revisi D-003 — renderer hemat RAM

- **Old → New:** target FemtoVG/WGPU → software renderer sebagai default produk, tetap Slint C++ AOT dan Winit.
- **Persetujuan:** pengguna memilih “Software — RAM paling kecil” setelah melihat perbandingan probe dan tradeoff CPU/scaling gambar.
- **Bukti:** binary aplikasi opt yang sama, Home expanded idle sekitar 5 detik: working set Skia/WGPU 210,46 MiB versus software 28,19 MiB. Detail metodologi dan keterbatasan di `../progress/02-time.md`.
- **Migrasi:** `src/app/main.cpp` memilih software sebelum inisialisasi Slint dan worker; environment override eksplisit tetap tersedia untuk diagnosis. Tidak ada perubahan service timer atau kontrak UI.
- **Tradeoff:** rasterisasi memakai CPU; tidak ada klaim FPS/kelancaran identik atau performa kamera/video masa depan. Software sudah diperiksa visual dan interaksi pada Windows; platform lain belum.
- **Packaging:** DLL SDK lokal masih berisi dukungan Skia. Rebuild/pin runtime minimal yang reproducible belum diselesaikan; renderer default produk tidak memakai Skia.

