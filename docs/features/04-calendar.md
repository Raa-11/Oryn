# Feature 04 — Calendar (Daily Tasks + External Activity Sync)

**Status:** Calendar manual offline tahap 04 tersedia pada Windows. Sinkronisasi Graph/Teams/GitLab, OAuth, dan timesheet belum diimplementasikan; permission/API POC tetap diperlukan untuk tahap integrasi.

## Implementasi tahap 04 — lokal

- **Create/read/update/delete** task dengan title, notes multiline, status incomplete/completed, dan due date opsional. Edit mempertahankan status/creation timestamp; Complete/Reopen tidak membuang edit draft. Hapus memakai konfirmasi inline yang dapat dibatalkan.
- Title menerima 1–240 byte UTF-8 dan tidak boleh hanya whitespace ASCII; notes maksimal 4096 byte. Teks UTF-8 dan SQL quotes disimpan lewat bound parameters, tidak diinterpolasikan ke query. NUL dan UTF-8 invalid ditolak; data tidak dipangkas diam-diam.
- Due date adalah tanggal sipil `YYYY-MM-DD` atau kosong, Gregorian tahun 0001–9999 termasuk aturan leap-year. Bukan timestamp UTC yang digeser timezone. `created_at`/`updated_at` memakai UTC epoch milliseconds; updated timestamp tidak mundur saat clock sistem mundur.
- **Today** mencakup task incomplete dengan due date hari ini/terlewat serta tanpa due date. **Upcoming** hanya tanggal setelah hari lokal ini. **Completed** mencakup semua task completed. Urutan tanggal menaik, ID sebagai tie-breaker, tanpa tanggal di akhir; pengelompokan Overdue/Today/tanggal/No due date.
- Hari lokal dibaca ulang pada command/Refresh dan saat masuk halaman Calendar. Tidak ada polling idle atau janji refresh otomatis tepat tengah malam tanpa interaksi.
- Tabel `tasks` hidup bersama preferensi pada `%LOCALAPPDATA%/Oryn/settings.sqlite3`. Migration **v1 → v2** transaksi mempertahankan seluruh Settings; database baru menjalankan semua migration yang belum diterapkan secara atomik. Database lebih baru/rusak tidak dihapus/reset otomatis.
- `oryn.calendar` memiliki satu worker `std::jthread`, FIFO terbatas 64 command, dan connection SQLite miliknya. Task mutation yang diterima tidak dikoaleskan/dibuang; shutdown menguras antrean lalu join/close. UI tidak melakukan filesystem/SQL blocking.
- EnTT `TaskCreated`, `TaskUpdated`, `TaskCompleted`, dan `TaskDeleted` diterbitkan pada worker setelah commit sukses untuk refresh snapshot. Revision berubah hanya setelah mutation committed, bukan ketika tombol ditekan; error mempertahankan data terakhir/draft. Fitur lain tidak mengimpor internal Calendar.
- Daftar memakai 50 task per halaman plus lookahead; Previous/Next dan scrolling menjangkau semua task tanpa membesarkan native window. ID melintasi Slint sebagai string untuk menjaga seluruh rentang int64.
- Visual mengacu `design/design.pen`: **18 / Calendar · Today**, **33 / Calendar · Today — Light**, dan **39 / Calendar · New task**. Heading, small filter tabs, separator rows, glyph/property form, input, dan create action memakai Theme Oryn. Filter roadmap Today/Upcoming/Completed menggantikan tab Month yang belum menjadi fitur tahap ini; Connect/Teams/GitLab, Event, Time, dan Project yang belum diimplementasikan tidak diberi kontrol palsu.
- Home memakai ringkasan Calendar aktual dan memberi label filter aktif; compact Calendar memperlihatkan jumlah task Today saat tidak ada selected timer running/paused. Timer aktif tetap memiliki prioritas display/kontrol compact. Draft task bertahan saat pindah halaman; loading/busy/error ditampilkan, dan Save menutup form hanya setelah revision commit.

**Verifikasi lokal:** migration v1 → v2 pada aplikasi nyata mempertahankan durasi Settings 27/6/13 dan WAL. Smoke desktop menggunakan folder data terisolasi untuk CRUD, notes multiline, penolakan tanggal invalid tanpa membuang draft, Today/Upcoming/Completed, Complete/Reopen, overdue/tanpa tanggal, pembatalan/konfirmasi delete, restart persistence, serta timer/tray dan exit kode 0. Suite normal meluluskan 10/10 target; Calendar UI menguji task ke-50 melalui wheel dengan clock animasi testing yang deterministik, bukan sleep atau pengurangan jumlah data.


## Goal

Satu panel agenda harian yang menghubungkan task manual dengan aktivitas kerja dari **Microsoft Teams/Graph** dan **GitLab enterprise**. Sync dilakukan di background melalui API polling, bukan membaca lokal Git push secara otomatis.

## Scope fitur lengkap — integrasi bertahap

- Buat, edit, selesai, hapus task manual dengan title/notes/status/due date.
- Lihat task `Today`, `Upcoming`, `Completed` (minimal agenda hari ini paling utama).
- Hubungkan akun Microsoft dan GitLab melalui auth yang diizinkan.
- Sinkronisasi sumber pilihan, tampilkan sumber, waktu last sync, status/galat.
- Hubungkan satu activity GitLab/Teams ke task atau buat task dari aktivitas bila model data dan izin memungkinkan.
- Tidak menduplikasi item saat sync berkali-kali; memungkinkan refresh manual.

## Microsoft Teams / Graph

- Gunakan Microsoft Graph dan OAuth flow yang sesuai untuk tenant/akun.
- **Belum diputuskan objek sinkronisasi persisnya:** Teams messages, assignments/task dari Planner/To Do, meeting atau channel activity memiliki izin/API berbeda. Pilih dari hasil POC dan kebutuhan user, jangan mengklaim semuanya tersedia.
- Token/refresh token disimpan vault OS; tangani expired/revoked consent.
- Hargai pagination, delta query bila endpoint mendukung, throttling dan tenant policies.

## GitLab Enterprise

- Konfigurasi base URL misal `https://gitlab.example.internal` tanpa hardcode perusahaan.
- Gunakan GitLab REST API untuk read-only project activity seperti commits/MRs/push events yang endpoint, role dan token-nya tersedia.
- Polling bertahap dengan timestamps/page cursors, ETag jika memungkinkan.
- Multi-project/filter user/group, kebijakan interval dan backoff.
- **Tidak** mengasumsikan aplikasi membaca push dari lokal Git; sumber resmi adalah GitLab API. Webhook bisa dipertimbangkan jika server/user admin setuju, bukan requirement MVP.

## Data dan konflik

- `tasks` tersedia pada tahap 04. `external_links` dan `sync_cursors` dengan `provider + external_id` unique baru dibuat ketika integrasi provider nyata diimplementasikan.
- Saat remote berubah, jangan menimpa edit lokal tanpa conflict strategy. Simpan `external_updated_at` dan `last_synced_at`.
- Timestamp aktivitas memakai UTC; due date task manual adalah tanggal sipil, sedangkan Today dihitung dari hari lokal sistem.
- Sync masuk sebagai candidate activity; user memutuskan link/create task jika mapping belum pasti.

## UI

Tahap 04: expanded agenda manual/filter/editor, compact jumlah task Today dengan prioritas timer aktif. Status sync Teams/GitLab hanya akan ditambahkan setelah integrasi nyata tersedia, bukan data contoh.

## Async

- Poll via scheduler Boost.Asio; libcurl HTTP Graph/GitLab.
- stdexec bisa menggabungkan sync per provider pada high-level pipeline setelah adapter terbukti.
- Jangan polling ketika akun disconnected; batasi request retry; support manual refresh.
- Tahap 04 memakai EnTT `TaskCreated/Updated/Completed/Deleted` untuk refresh setelah commit. `SyncStarted/Completed/Failed` menunggu implementasi sync.

## Acceptance criteria

- Manual task CRUD tetap berfungsi offline.
- Setelah sync ulang, tidak ada duplikasi; error/network offline tidak menghapus tasks.
- Expired token/403/429 tampil jelas dan tidak loop request tanpa batas.
- Sinkronisasi hanya resource yang user izinkan dan endpoint yang benar-benar tersedia.

## Di luar MVP

Timesheet generation, billing, jadwal kerja otomatis dari local git, meeting transcription, cloud calendar provider lain, automation yang mengirim pesan ke Teams.
