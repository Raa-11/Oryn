# 09 — Roadmap Implementasi: Dari Mudah ke Sulit

> **Strategi final:** kerjakan fitur bertahap dari yang paling sederhana hingga yang membutuhkan integrasi OS, API eksternal, dan protokol agent. Selesaikan **satu vertical slice yang benar-benar berfungsi** sebelum menambah fitur berikutnya.
>
> **Bukan estimasi durasi.** Tingkat kesulitan merupakan perkiraan relatif untuk proyek ini, bukan hasil benchmark atau jaminan waktu. Setiap tahap punya kriteria selesai (*exit criteria*) yang bisa diuji.

## Aturan pelaksanaan

1. **Mulai dari Windows** sebagai platform development utama. Kompatibilitas Linux/macOS disiapkan lewat PAL sejak awal, tetapi implementasi native lengkap dilakukan bertahap.
2. **Jangan membangun seluruh infrastruktur terlebih dahulu.** Mulai dengan abstraksi minimum; tambahkan SQLite, event bus, stdexec, Process, dan HTTP saat fitur pertama benar-benar memerlukannya.
3. **Tetap pegang stack yang dipilih:** C++23, Slint, Bazel/Bzlmod, EnTT, NVIDIA stdexec, Boost.Asio, Boost.Process V2, libcurl, Glaze, SQLite/WAL, Quill, doctest. Tidak mengganti libuv/Qt/Electron tanpa keputusan baru.
4. **Setiap fitur lulus tes sebelum lanjut.** Minimal build, unit test logic, tidak memblokir Slint UI thread, dan shutdown bersih.
5. **Jangan menunda pemeriksaan risiko yang dapat menggagalkan proyek.** Uji integrasi Slint/Bazel/renderer dari awal; lakukan *feasibility spike* ACP lebih awal walaupun implementasi Agents sengaja belakangan.
6. **Gunakan folder yang sudah ditentukan** dalam [04 — Repository Structure](04-repository-structure.md). Dokumen fitur tetap terpisah di `features/`.

## Peta urutan pengerjaan

| Tahap | Pekerjaan | Kesulitan relatif | Hasil yang terlihat |
|---|---|---|---|
| 00 | Bootstrap dan POC blocker | Wajib, risiko teknis tinggi | App Slint minimal bisa dibangun |
| 01 | Floating island + navigasi UI statis | Mudah | Panel kecil bisa expand/collapse |
| 02 | **Time** — countdown & Pomodoro | Mudah | Timer pertama berfungsi |
| 03 | **Settings dasar** + SQLite | Mudah–sedang | Tema/preferensi tersimpan |
| 04 | **Calendar manual** — CRUD task | Mudah–sedang | Task harian bisa dikelola |
| 05 | **Music** — Windows GSMTC | Sedang | Info lagu dan play/pause |
| 06 | **Ask AI** — OpenAI saja | Sedang | Chat streaming dan history |
| 07 | **Mirror** — kamera Windows | Sedang–sulit | Webcam preview dan flip |
| 08 | **GitLab Sync** pada Calendar | Sulit | Aktivitas repo terbaca dan ditautkan |
| 09 | **Teams/Microsoft Graph Sync** | Sulit | Task/aktivitas Teams yang dipilih tersinkron |
| 10 | **Agents** — Zed + Codex ACP | Paling kompleks | Aktivitas agent dan file terlihat live |
| 11 | Integrasi lintas OS, performance, release | Kompleks lintas fitur | Release candidate yang diuji |

> Urutan ini berdasarkan **kompleksitas fitur untuk diimplementasikan**, bukan nilai bisnis. Tahap 08/09/10 dapat dipertukarkan jika izin API enterprise atau kompatibilitas ACP menjadi blocker; jangan mengklaim fitur selesai hanya dari mock.

---

## Tahap 00 — Bootstrap minimal & pemeriksaan blocker

**Tujuan:** memastikan fondasi benar-benar bisa dibangun sebelum mengembangkan UI dan fitur besar.

- [ ] Buat `../../MODULE.bazel`, `.bazelrc`, `.bazelversion`, `BUILD.bazel` minimal, dan target C++23.
- [ ] Buat *hello world* Slint `.slint` → generated C++ melalui Bazel; jalankan di Windows.
- [ ] Verifikasi backend Slint Winit + software renderer yang dipilih (D-003 revisi) **benar-benar dapat di-build** dan dipaketkan lintas OS. Windows build/runtime sudah diuji; pin runtime minimal dan platform lain masih perlu validasi.
- [ ] Tambahkan satu unit test doctest dan satu job CI build dasar.
- [ ] Buat *feasibility note* singkat untuk Zed ↔ Codex ACP: apakah jalur external agent/proxy bisa digunakan untuk melihat `session/update`? **Ini hanya spike risiko**, bukan mulai membangun modul Agents.
- [ ] Catat lingkungan build, compiler, dependency pin, error, dan hasil POC di dokumen pengujian.

**Selesai ketika:** executable Slint minimal dapat berjalan di Windows, tes dasar lulus, dan risiko terbesar sudah mempunyai bukti atau blocker yang dicatat. Tidak perlu menyelesaikan semua adapter OS.

## Tahap 01 — Floating island dan navigasi UI (mudah)

**Tujuan:** mendapatkan aplikasi yang sudah terasa seperti Desktop Companion walaupun belum terhubung dengan layanan apa pun.

- [ ] Buat `ui/main.slint`, `ui/components/floating_island.slint`, tema dark/light, dan layout menu.
- [ ] Tampilkan collapsed state dan expanded state; klik untuk membuka/menutup.
- [ ] Buat menu dummy **Mirror, Ask AI, Music, Calendar, Time, Agents, Settings** memakai placeholder yang diberi label dengan jelas.
- [ ] Terapkan window capability PAL minimal untuk Windows (transparansi, window placement, fokus dan on-top bila tersedia).
- [ ] Batasi animasi ke perubahan state, tidak menjalankan redraw terus-menerus saat idle.
- [ ] Tambahkan smoke test buka/tutup window dan shutdown.

**Selesai ketika:** UI dapat dinavigasi, expand/collapse stabil, dan setiap layar placeholder tidak mengklaim integrasi sudah aktif.

## Tahap 02 — Time: countdown & Pomodoro (mudah)

**Tujuan:** fitur pertama end-to-end; menguji event-driven UI dan lifecycle dengan risiko integrasi terendah.

- [ ] Buat `features/time/` berisi timer service, state, dan UI Slint.
- [ ] Gunakan `std::chrono::steady_clock` sebagai sumber waktu; tampilkan hitungan mundur, pause/resume/reset.
- [ ] Pakai Boost.Asio timer / scheduling seperlunya, bukan thread busy-loop.
- [ ] Tambahkan Pomodoro dengan durasi yang bisa dikonfigurasi; tangani sleep/resume perangkat.
- [ ] Publikasikan event terpilih melalui EnTT, lalu jadwalkan update UI di Slint event loop.
- [ ] Test start, pause, cancel, expiry, shutdown tanpa worker/thread tertinggal.

**Selesai ketika:** countdown dan Pomodoro bekerja, UI tetap responsif, dan penggunaan CPU idle tidak menunjukkan loop aktif tanpa alasan.

**Spesifikasi:** [Time](../features/05-time.md).

## Tahap 03 — Settings dasar dan penyimpanan lokal (mudah–sedang)

**Tujuan:** membuat preferensi aplikasi persisten tanpa menambahkan auth eksternal terlebih dahulu.

- [x] Implement SQLite connection, migration pertama, dan WAL pada `infrastructure/database/`.
- [x] Simpan preferensi theme, posisi/jendela, timer, dan pengaturan tampilan.
- [x] Jalankan operasi DB blocking pada worker agar tidak menahan Slint UI.
- [x] Buat `features/settings/`: theme, window preference, menu pengaturan dasar.
- [x] Siapkan interface penyimpanan token melalui OS credential vault; **jangan simpan token di SQLite plaintext**.
- [x] Tambahkan Quill dengan logging yang tidak mengandung kredensial, prompt, atau isi kode.

**Selesai ketika:** pengaturan tetap ada setelah restart, migration dapat diuji dan tidak ada operasi DB yang membekukan UI.

**Implementasi Windows terverifikasi:** 8/8 target tes normal dan MSVC ASan; smoke aplikasi nyata Save → Exit → restart membuktikan theme/reduced motion, topmost, compact startup, posisi, dan durasi Pomodoro persisten. WAL/schema v1 diperiksa pada database hasil UI. Hide/restore tray serta sesi Time tetap berjalan. Akun, AI Usage, restore sesi timer setelah exit, dan platform lain tidak termasuk tahap ini; detail di spesifikasi Settings.

**Spesifikasi:** [Settings](../features/07-settings-ai-usage.md).

## Tahap 04 — Calendar manual: task harian (mudah–sedang)

**Tujuan:** punya Calendar yang berguna bahkan saat offline dan belum mendapat akses Teams/GitLab.

- [ ] Buat task manual: create/read/update/delete, status, due date, dan pengelompokan harian.
- [ ] Simpan tasks di SQLite melalui modul Calendar sendiri.
- [ ] Tambahkan filter sederhana `Today / Upcoming / Completed` jika diperlukan UX.
- [ ] Gunakan event `TaskCreated`/`TaskUpdated`/`TaskCompleted` untuk refresh state, bukan direct dependency antar fitur.
- [ ] Uji tanggal, urutan, edit, penghapusan, persistence dan data kosong.
- [ ] **Belum** membuat Teams/GitLab sync, OAuth, maupun timesheet.

**Selesai ketika:** daily tasks bisa digunakan sepenuhnya secara lokal setelah restart.

**Spesifikasi:** [Calendar](../features/04-calendar.md).

## Tahap 05 — Music: now playing Windows (sedang)

**Tujuan:** integrasi native OS pertama yang nyata dan terisolasi lewat PAL.

- [ ] Buat interface `IMediaSession` dan adapter **Windows GSMTC**.
- [ ] Tampilkan judul, artis, playback state, dan artwork jika tersedia.
- [ ] Implement Play/Pause/Next/Previous hanya jika session mendukung.
- [ ] Gunakan callback/notifikasi OS; hindari polling rapat.
- [ ] Tampilkan empty/unavailable state saat tidak ada session.
- [ ] Uji player dibuka/ditutup, sesi berganti, media berhenti, dan shutdown.

**Selesai ketika:** informasi media dan kontrol dasar dapat digunakan pada player yang mendukung GSMTC di Windows.

**Spesifikasi:** [Music](../features/03-music.md).

## Tahap 06 — Ask AI: OpenAI lebih dahulu (sedang)

**Tujuan:** chat AI sederhana, aman, dan tidak membuat Slint macet ketika menerima output panjang.

- [ ] Implement `IAIProvider` dan satu provider **OpenAI** saja untuk MVP; Claude/Gemini ditunda.
- [ ] Buat `infrastructure/http/` dengan **libcurl**, streaming SSE, cancellation, timeout, dan error mapping.
- [ ] Gunakan Glaze untuk JSON; buffer SSE hanya per frame, bukan seluruh respons.
- [ ] Implement input chat, streaming incremental, tombol cancel, dan error state.
- [ ] Simpan conversation/message ke SQLite; history memakai pagination.
- [ ] Simpan API credential pada OS credential vault; jangan menganggap ChatGPT Plus otomatis memberi API quota.
- [ ] Uji streaming lama, disconnect, cancel, percakapan panjang, dan restart aplikasi.
- [ ] Tambahkan **AI Usage (Ask AI)** di Settings apabila penggunaan token/cost tersedia; tampilkan `unknown` bila tidak ada datanya.

**Selesai ketika:** OpenAI chat bisa streaming, histori tersimpan, cancellation aman, dan UI tetap lancar.

**Spesifikasi:** [Ask AI](../features/02-ask-ai.md).

## Tahap 07 — Mirror: webcam preview Windows (sedang–sulit)

**Tujuan:** integrasi native camera dengan pipeline frame yang hemat resource.

- [ ] Buat interface `ICamera` dan implementasi Windows native camera API.
- [ ] Tampilkan preview live dalam panel Slint melalui mekanisme upload/render frame yang teruji.
- [ ] Tambahkan pilih perangkat dan horizontal flip.
- [ ] Aktifkan kamera **hanya ketika Mirror dibuka**; matikan dan lepaskan resource ketika panel ditutup/berpindah.
- [ ] Tampilkan permission/device-busy/no-device state.
- [ ] Uji camera disconnect/reconnect, rapid expand/collapse, dan shutdown.
- [ ] Jangan menambahkan recording, broadcasting, atau video effects berat.

**Selesai ketika:** preview berjalan stabil di Windows, tidak ada kamera yang tetap aktif setelah Mirror ditutup.

**Spesifikasi:** [Mirror](../features/01-mirror.md).

## Tahap 08 — Calendar: GitLab enterprise sync (sulit)

**Tujuan:** menghubungkan aktivitas GitLab perusahaan ke task tanpa menganggap setiap commit otomatis satu task.

- [ ] Identifikasi host/API GitLab yang dapat diakses, izin token, dan kebijakan perusahaan.
- [ ] Implement adapter API **read-only** GitLab: project, commit, merge request atau activity yang benar-benar dibutuhkan.
- [ ] Simpan sync cursor/watermark; lakukan polling yang wajar, retry/backoff dan deduplication.
- [ ] Tautkan aktivitas GitLab ke task melalui aturan yang eksplisit dan dapat dikoreksi pengguna.
- [ ] Jelaskan di UI perbedaan **observed activity** dan **manual task**.
- [ ] Uji network outage, token kedaluwarsa, pagination, duplicate events, dan data perusahaan yang sensitif.

**Selesai ketika:** aktivitas GitLab dapat disinkron dan dipetakan ke Calendar tanpa menggandakan task dan tanpa write access yang tidak diperlukan.

**Spesifikasi:** [Calendar](../features/04-calendar.md).

## Tahap 09 — Calendar: Microsoft Teams/Graph sync (sulit)

**Tujuan:** menambah sumber tugas/aktivitas Teams setelah mekanisme task, sync, dan error handling terbukti.

- [ ] Tentukan **data konkret** yang akan dibaca: Teams message, meeting, assigned task, atau resource lain yang tenant izinkan. Jangan menganggap semuanya tersedia melalui satu endpoint.
- [ ] Verifikasi permission dan consent dari Microsoft Graph serta kebijakan tenant.
- [ ] Implement authentication, refresh, pagination, delta/sync bila API mendukung, retry/backoff, dan rate limit.
- [ ] Map resource yang benar-benar relevan ke task atau activity tanpa duplikasi.
- [ ] Tampilkan status `connected`, `permission required`, `syncing`, `error` secara jujur.
- [ ] Uji consent denied, API permission mismatch, reconnect, konflik, dan data lama.
- [ ] Timesheet **tetap di luar MVP**.

**Selesai ketika:** setidaknya satu sumber Teams/Graph yang disepakati benar-benar tersinkron ke Calendar memakai akun uji yang berizin.

**Spesifikasi:** [Calendar](../features/04-calendar.md).

## Tahap 10 — Agents: Zed + Codex ACP (paling kompleks)

**Tujuan:** membuat live companion untuk tahu Codex sedang mengerjakan apa, file mana, dan statusnya. **Bukan mini IDE**.

- [ ] Gunakan hasil *ACP feasibility spike* dari tahap 00; konfirmasi apakah jalur integrasi Zed/Codex memberi `session/update` dan lokasi file.
- [ ] Jika diperlukan, buat helper/proxy ACP yang benar-benar kompatibel dan transparan untuk pesan JSON-RPC, permission, serta session lifecycle.
- [ ] Gunakan **Boost.Process V2** untuk child process/stdio dan **Boost.Asio** untuk async pipe I/O; implement cleanup/cancel/error recovery.
- [ ] Tambahkan stdexec adapter **hanya untuk flow orchestration yang benar-benar memerlukannya**, uji sender completion dan cancellation.
- [ ] Parse event ACP dengan Glaze, modelkan status `thinking`, `reading`, `editing`, `executing`, `completed`, `waiting`, `unknown` berdasarkan **event aktual**.
- [ ] Tampilkan UI final: `Codex / Zed / project / status`, current task jika tersedia, aktivitas sekarang, dan nama file bila disediakan ACP.
- [ ] Collapsed state contoh: `Codex · Editing service.cpp`; expanded state memberikan konteks tambahan.
- [ ] Tambahkan tombol **Open in Zed**. Statistik Git file/`+/-` opsional dan **harus dibedakan dari perubahan yang pasti dibuat oleh Codex**.
- [ ] Mulai satu session; tambah multi-session hanya setelah single-session stabil.
- [ ] Test agent restart, proses crash, reconnect, missing filename, waiting permission, stop/cancel dan app shutdown.
- [ ] Jangan membuat diff viewer, code editor, terminal, atau list tetap `Read → Edit → Bash → Done` sebagai UI utama.

**Selesai ketika:** aktivitas nyata Codex di Zed bisa diamati di aplikasi; missing data ditunjukkan jujur; app dan agent tidak terganggu oleh integrasi. Mock UI saja bukan completion.

**Spesifikasi:** [Agents](../features/06-agents.md).

## Tahap 11 — Cross-platform, optimasi, dan release (kompleks lintas fitur)

**Tujuan:** membawa versi Windows yang telah berfungsi ke Linux dan macOS secara bertahap, lalu menyiapkan release candidate.

- [ ] Tambahkan adapter **Linux MPRIS** dan camera Linux; uji X11 serta Wayland dan fallback window capabilities.
- [ ] Tambahkan adapter **macOS** camera dan media sesuai API/permission yang benar-benar tersedia, jangan mengklaim semua player kompatibel.
- [ ] Bangun CI Bazel + doctest pada Windows/Linux/macOS; catat compiler/SDK incompatibility jika ada.
- [ ] Uji native credential store tiap OS; app harus menangani vault yang tidak tersedia.
- [ ] Tambahkan fitur Settings lanjut: disconnect akun, data deletion/export sesuai kebijakan, AI Usage dari sumber yang tersedia.
- [ ] Benchmark **idle CPU, RAM, startup, responsiveness, camera resource, dan activity monitoring** pada release build; target awal bukan hasil yang sudah terbukti.
- [ ] Uji DPI/multi-monitor, device permission, suspend/resume, startup/shutdown, signing dan atribusi lisensi Slint/dependencies.
- [ ] Perbaiki bug berdasarkan pengukuran, bukan asumsi tentang library yang pasti paling cepat.

**Selesai ketika:** release candidate memenuhi kriteria [08 — Testing & CI](08-testing-ci.md), keterbatasan per OS terdokumentasi, dan tidak mengklaim kemampuan yang belum diuji.

---

## Checklist per tahap (wajib untuk coding agent)

Setelah mengerjakan setiap tahap, perbarui catatan hasil di issue/PR atau `docs/progress/` (folder ini dibuat saat implementasi):

- [ ] Scope dan acceptance criteria tercapai.
- [ ] Build release/debug yang relevan berhasil.
- [ ] Unit/integration test relevan lulus; hasil manual test dicatat.
- [ ] Tidak ada UI-thread blocking, resource leak, atau background polling rapat yang tidak diperlukan.
- [ ] Jika ada keputusan berubah, `docs/10-decisions.md` dan `CONTEXT.md` diperbarui.
- [ ] Tahap berikutnya dan blocker dituliskan dengan jelas.

## Kalau ingin MVP lebih cepat

Jangan menghapus arsitektur yang sudah dipilih. **Berhenti pada milestone yang sudah berguna:**

- **MVP 0:** floating island + Time + Settings lokal (tahap 00–03).
- **MVP 1:** tambah Calendar manual + Music (tahap 04–05).
- **MVP 2:** tambah Ask AI + Mirror (tahap 06–07).
- **MVP 3:** integrasi GitLab/Teams + Agents, mengikuti akses dan hasil POC (tahap 08–10).
- **Release multi-platform:** tahap 11.

Urutan dokumentasi fitur (`01-mirror.md`, `02-ask-ai.md`, dst.) **bukan urutan pengerjaan**. Ikuti urutan **roadmap ini** saat memilih task selanjutnya.
