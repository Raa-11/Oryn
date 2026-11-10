# 11 — Risiko, Assumptions, dan POC yang Masih Terbuka

Jangan menjawab pertanyaan berikut dengan tebakan. Setiap poin perlu eksperimen atau verifikasi target OS yang sesungguhnya.

| Prioritas | Pertanyaan | Aksi validasi | Dampak jika gagal |
|---|---|---|---|
| Critical | Dapatkah Slint C++ AOT + software renderer dipaketkan reproducible melalui Bazel tanpa renderer yang tidak dipakai? | Build/runtime Windows terbukti; selaraskan versi compiler/header/DLL dan validasi paket lintas OS | SDK lokal masih memuat Skia; ini risiko packaging/pin, bukan alasan memakai renderer default SDK |
| Critical | Apakah custom ACP proxy benar-benar menangkap update Codex dari Zed tanpa mengubah perilaku? | Konfigurasi custom external agent, record JSON-RPC non-sensitive test | Agent activity hanya bisa ditampilkan jika jalur yang didukung tersedia |
| High | Bagaimana menghubungkan Boost.Asio completions ke stdexec sender dengan cancellation aman? | POC timer+pipe, cancel/shutdown 100x | Batasi stdexec ke task orchestration, native Asio tetap untuk I/O |
| High | `libcurl` streaming dan Boost.Asio loop koeksis efisien? | Compare dedicated driver vs adapter multi-socket | Isolasi curl di worker; tak memblok UI |
| High | Always-on-top/transparency/position di Linux Wayland? | Test compositor utama, windowing plugin/PAL | Tampilkan mode window standar bila feature tak didukung |
| High | macOS now-playing control apa yang legal dan stabil untuk external players? | Investigasi APIs/app bridges, permission tests | Capability-scoped feature, bukan janji semua pemain |
| High | Data file aktif/title ACP lengkap pada Codex Zed? | Capture events `session/update`, kind, location, title | UI fallback `Working/Unknown file` |
| Medium | Seberapa akurat +/- diff saat user dan Codex mengedit bersama? | Git worktree snapshot, compare session metadata | Label working tree bukan agent-owned changes |
| Medium | Resource Microsoft Graph Teams mana yang cocok jadi input Calendar? | Tentukan types dan permission tenant, prototype | Batasi sync Teams sesuai API tenant yang diberikan |
| Medium | Apakah RAM idle tetap <50MB setelah fitur dan runtime lengkap ditambahkan? | Software + Time pada Windows opt terukur 28,19 MiB working set; ukur ulang bersama Quill, DB, network, dan fitur lain | Angka saat ini bukan jaminan untuk aplikasi lengkap di masa depan |
| Medium | Apakah credential store Linux selalu ada pada desktop environment? | Test Secret Service present/missing | UI meminta konfigurasi/menonaktifkan koneksi jika tak aman |
| Medium | Apakah window multi-monitor/resizing/minimize konsisten? | Per-OS manual QA | Implement capability detection + fallback |

## Urutan POC

1. Bazel + Slint AOT + target renderer.
2. Floating window di Windows dan Linux X11/Wayland.
3. Boost.Asio/Process V2 stdio + clean process lifecycle.
4. ACP proxy + Codex in Zed tool-call updates.
5. SSE HTTP + stdexec orchestration adapter; stable shutdown.
6. OS media/camera/credentials.

## Aturan fallback produk

- Jika sumber ACP tidak memberi filename, sembunyikan baris file dan tampilkan status unknown.
- Jika musik player tak expose seek, sembunyikan seek control.
- Jika camera permission ditolak, preview tidak dijalankan dan tampilkan permission state.
- Jika Linux compositor menolak always-on-top, app tetap berjalan sebagai jendela normal.
- Jika Graph/GitLab rate limited, hentikan polling sementara dan tampilkan sync status.
- Jika integrasi belum terbukti, tandai `experimental` dalam developer plan, bukan fitur jadi.
