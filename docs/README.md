# Desktop Companion — Project Blueprint

> Dokumen sumber kebenaran untuk proyek aplikasi desktop native lintas OS. Terakhir diperbarui: **24 Oktober 2026**. Bahasa dokumen: Indonesia. Status: **rencana arsitektur dan spesifikasi**, belum menyatakan aplikasi sudah dibangun.

## Mulai dari sini

1. **[CONTEXT.md](CONTEXT.md)** — satu halaman untuk mengembalikan konteks saat chat/agent baru.
2. **[AGENTS.md](AGENTS.md)** — aturan bagi AI coding agent ketika mengimplementasikan proyek.
3. **[docs/01-product-scope.md](docs/01-product-scope.md)** — visi produk, batasan MVP dan non-goals.
4. **[docs/02-tech-stack.md](docs/02-tech-stack.md)** — semua keputusan teknologi.
5. **[docs/03-architecture.md](docs/03-architecture.md)** — Modular Monolith + Event-Driven + PAL.
6. **[docs/04-repository-structure.md](docs/04-repository-structure.md)** — rancangan folder source code dan aturan dependensi Bazel.
7. **[docs/07-ui-ux.md](docs/07-ui-ux.md)** — floating island dan desain Agents yang dipilih.
8. **[docs/09-roadmap.md](docs/09-roadmap.md)** — urutan implementasi **dari paling mudah ke paling sulit**, dengan acceptance criteria per tahap.

## Dokumen teknis

| Dokumen | Isi |
|---|---|
| [05 — Async & Events](docs/05-async-event-lifecycle.md) | stdexec, Boost.Asio, Boost.Process, EnTT, shutdown |
| [06 — Data & Security](docs/06-data-security.md) | SQLite/WAL, data model awal, kredensial, privasi |
| [08 — Testing & CI](docs/08-testing-ci.md) | doctest, Bazel, tiga OS, metrik performa |
| [10 — Decisions](docs/10-decisions.md) | keputusan yang sudah dikunci dan alternatif yang ditolak |
| [11 — Open Questions](docs/11-open-questions.md) | risiko dan validasi teknis yang belum selesai |
| [12 — References](docs/12-references.md) | dokumentasi resmi yang menjadi acuan |

## Spesifikasi fitur (pisah per file)

| Fitur | Dokumen |
|---|---|
| Mirror | [features/01-mirror.md](features/01-mirror.md) |
| Ask AI | [features/02-ask-ai.md](features/02-ask-ai.md) |
| Music | [features/03-music.md](features/03-music.md) |
| Calendar | [features/04-calendar.md](features/04-calendar.md) |
| Time | [features/05-time.md](features/05-time.md) |
| Agents | [features/06-agents.md](features/06-agents.md) |
| Settings + AI Usage | [features/07-settings-ai-usage.md](features/07-settings-ai-usage.md) |

## Aturan dokumentasi agar konteks tidak hilang

- **Satu keputusan = satu sumber:** ubah `docs/10-decisions.md` bila memilih/mengganti teknologi, lalu perbarui dokumen teknis terkait.
- **Satu fitur = satu file:** ubah spesifikasi pada `features` sebelum implementasi fitur berubah signifikan.
- **`CONTEXT.md` tetap ringkas** dan berisi hanya keputusan aktif, bukan histori panjang.
- **Jangan menganggap asumsi sebagai fakta:** beri status `TERPILIH`, `TARGET`, `BELUM DIVERIFIKASI`, atau `DITUNDA`.
- **Dokumen ini bukan scaffold source code.** Struktur `../src` pada dokumen adalah blueprint untuk implementasi berikutnya.

## Ringkasan satu kalimat

Desktop Companion berbasis **C++23 + Slint + Bazel**, arsitektur **Modular Monolith + Event-Driven + PAL**, async **NVIDIA stdexec + Boost.Asio + Boost.Process V2**, HTTP **libcurl**, storage **SQLite/WAL**, dan UI floating island kecil yang tidak menduplikasi IDE Zed.
