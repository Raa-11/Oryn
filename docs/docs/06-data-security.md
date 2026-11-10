# 06 — Storage, Data Model & Security

## Database

**SQLite + WAL** untuk seluruh data lokal. Desain default satu database file user-space per profil. Tidak membutuhkan server, tidak menggunakan PostgreSQL cloud.

- `PRAGMA journal_mode=WAL` (persisten di DB setelah disetel); `foreign_keys=ON` per koneksi.
- `busy_timeout` dan transaksi singkat; indeks sesuai query konkret.
- Schema migration berurutan, bisa diuji rollback/backups.
- Query DB dijalankan pada storage worker agar tidak memblok UI.
- Async write tidak berarti SQLite magically non-blocking; tetap worker/queue bounded.
- Tidak perlu FTS5/DB encryption until specified; evaluasi nanti jika kebutuhan nyata.

## Entitas awal (konseptual)

| Tabel | Data utama | Catatan |
|---|---|---|
| `settings` | key, value, updated_at | Tidak menyimpan secret |
| `conversations` | id, provider, title, timestamps | Lokal Ask AI |
| `messages` | id, conversation_id, role, content, tokens?, created_at | Paginasi dan lazy loading |
| `tasks` | id, title, notes?, status, due_at?, source, timestamps | Agenda harian |
| `external_links` | task_id, provider, external_id, url | Relasi Teams/GitLab |
| `sync_cursors` | source, cursor?, etag?, last_sync_at | Polling incremental |
| `agent_sessions` | session_id, agent, project_path?, state, last_seen | Metadata; jangan simpan prompt mentah default |
| `agent_activity` | session_id, kind, file_path?, status, observed_at | Bounded retention |
| `timer_sessions` | id, type, start/end, completed_at? | Opsional history |
| `ai_usage` | provider, conversation/session id?, model?, input/output tokens?, estimated_cost?, source | Null bila data tak tersedia |

Model di atas adalah usulan awal, **bukan migration SQL final**. Time fields UTC; tampilkan menurut local timezone. Hindari menyimpan file path/project private lebih lama dari yang diperlukan.

## Chat berukuran besar

- UI memuat sekitar 30–50 bubble terakhir, older paged upon scroll.
- Bubble panjang >15–30 baris collapsed preview; full content tetap di DB.
- Pasted code panjang sebagai attachment/card ketika sesuai; viewer lazy/virtualized, bukan membuat satu giant `Slint Text` 200.000 karakter.
- Cache UI dibatasi; saat beralih conversation, lepaskan view models lama.
- AI request context ditentukan terpisah dari history storage dan diberi policy window/summary.

## Credential handling

- Simpan API keys/tokens/refresh tokens **hanya** di OS credential vault (Windows, Linux desktop secret service, macOS Keychain) dengan referensi ID nonsecret pada DB.
- Token tidak di-print di Quill, crash dump aplikasi (sebisa mungkin), atau telemetry.
- Gunakan TLS certificate verification default; jangan disable validation.
- Scope permission Graph/GitLab paling minimum; lakukan incremental consent bila tersedia.
- User bisa disconnect/revoke akun, hapus data/riwayat, dan mengetahui data yang dibagikan ke provider.
- HTTPS request body chat jelas dikirim ke provider AI yang dipilih; jangan mengklaim AI memproses semuanya lokal.

## Logging

Quill untuk diagnostik, bukan database aktivitas user. Release default INFO/WARN/ERROR, DEBUG dev-only. Rotasi awal ~5 MB x3 (konfigurasi, diuji). Redact token, auth headers, prompt, private code, diff. Periksa apakah backend async logger menjaga CPU idle dan flush shutdown.

## Data retention dan keamanan Agents

- Default Agents menyimpan metadata sesi/status ringkas saja; content tool/rawOutput tidak disimpan kecuali diperlukan dan diizinkan.
- Path file dapat sensitif; sediakan toggle tampilan nama file saja vs full path.
- Git `+/-` working tree adalah cakupan repository, bukan bukti perubahan oleh Codex. Label eksplisit.
- UI permission hanya ketika protokol dan safety path mendukung; jangan melakukan auto-approve tersembunyi.

## Backup/migration

Sediakan export setting/chat/task pada fase berikutnya. Untuk backup SQLite WAL gunakan SQLite Backup API atau prosedur aman; jangan cuma copy `.db` saat WAL sedang aktif tanpa memastikan konsistensi.
