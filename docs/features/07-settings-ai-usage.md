# Feature 07 — Settings + AI Usage (Subpage)

**Status:** Settings dasar tahap 03 tersedia pada Windows. Accounts, integrasi, Privacy tools, AI Usage, dan About lengkap tetap scope tahap fitur/release berikutnya, bukan fitur yang sudah tersedia.

## Implementasi tahap 03

- Halaman nyata memiliki bagian **Appearance / Window / Timer**, tombol **Save settings**, **Restore defaults**, **Use current position**, dan **Exit Oryn**.
- Visual mengikuti pola `design/design.pen` Settings Appearance dark/light: tab selebar konten, label/deskripsi di kiri, kontrol di kanan, separator antarbaris, pilihan tema segmented berikon, dan switch reduced motion. Footer Save/defaults/Exit serta section Window/Timer tetap tersedia untuk preferensi nyata tahap 03.
- Kontrol Slint memakai token Theme, bukan styling/accent widget bawaan OS. Switch mempertahankan semantic checkbox, enabled/checked accessibility, focus ring, aktivasi Space/Return, dan pointer focus; input memakai text editing/selection native Slint. Pilihan tema hanya Dark/Light; bentuk Notch ditampilkan read-only. System theme/Pill dan halaman integrasi yang belum tersedia tidak diberi tombol palsu.
- Preferensi persisten: tema gelap/terang, reduced motion, always-on-top, startup expanded/compact, remembered physical window coordinates, serta durasi focus/short break/long break Pomodoro.
- Form memakai draft yang bertahan saat navigasi; perubahan tidak diterapkan sebelum Save. Snapshot host tidak membuang edit yang belum disimpan. Preferensi dari Time menyegarkan draft Settings yang belum diedit.
- Durasi menerima bilangan bulat 1–180 menit; posisi menerima piksel bertanda ±1.000.000. Posisi restore di-clamp ke work area monitor yang tersedia. **Remember position** menyimpan koordinat form; **Use current position** mengambil posisi panel sebelum Save, bukan autosave setiap pergerakan.
- Startup expanded/compact berlaku pada peluncuran berikutnya; collapse/hide saat ini tetap aksi sementara. Always-on-top tetap konsisten setelah hide/show melalui tray.
- Konfigurasi durasi dari Settings maupun Time disimpan hanya setelah TimerService menerima perubahan. Pomodoro running/paused menolak penggantian durasi; tidak mereset sesi diam-diam. Sesi/deadline aktif sendiri tidak dipersist setelah aplikasi keluar.
- Database default `%LOCALAPPDATA%/Oryn/settings.sqlite3`; override diagnostik `ORYN_DATA_DIR` harus absolute. `settings` adalah singleton bertipe ketat yang berasal dari migration v1; tahap 04 menambah tabel Calendar dan menaikkan schema ke **v2** tanpa mengganti preferensi. Journal tetap WAL. Filesystem, open, migration, read/write, dan close Settings berjalan pada worker `SettingsService`, bukan thread UI; Calendar memiliki connection/worker terpisah.
- Save dikoaleskan ke satu slot terbaru; shutdown menguras save yang diterima lalu join. UI menampilkan loading/saving/error. Database rusak atau versi lebih baru tidak dihapus/reset otomatis: default aman tersedia dengan error, penyimpanan tetap ditolak sampai data diperbaiki dan aplikasi direstart.
- Modul `oryn.platform.credentials` menyediakan adapter Windows Credential Manager nyata untuk store/read/erase dan move-only buffer yang dihapus aman; secret tidak masuk SQLite. Belum ada provider/auth UI yang memakai vault pada tahap ini. Platform lain mengembalikan unsupported tanpa fallback plaintext.
- `oryn.logging` memakai Quill 13.1.0: queue bounded-dropping, stderr, lifecycle RAII, flush saat keluar. Hanya label lifecycle/operasi dan kode sistem dicatat; tidak ada nilai preferensi, path lengkap, kredensial, prompt, atau isi kode. Tidak ada file log blocking di UI.

**Verifikasi lokal:** 8/8 target tes normal dan 8/8 MSVC ASan lulus, termasuk migration/corruption/future-version, coalescing/drain, vault Windows terisolasi, serta draft/validation/contrast UI. Smoke desktop pada data folder terisolasi mengubah preferensi, menyimpan, keluar dengan kode 0, dan meluncurkan ulang: tema, reduced motion, topmost, compact startup, posisi, dan durasi 30/6/12 kembali benar. Database nyata menunjukkan WAL dan schema version 1. Pomodoro tetap berjalan setelah pindah tab serta hide/restore tray; topmost false dipertahankan. CI remote, OS lain, dan sleep/resume perangkat nyata belum diverifikasi.

Smoke tambahan mengubah durasi dari halaman **Time** menjadi 35/7/14, memeriksa draft Settings yang belum diedit ikut diperbarui, lalu melakukan restart ketiga dan memastikan nilai baru kembali. Ketiga peluncuran keluar kode 0; output Quill menunjukkan start/load/stop tanpa nilai preferensi atau secret.

**Koreksi visual Settings:** build aplikasi dan 8/8 target normal lulus; regresi UI mencakup disabled controls yang tidak boleh mengubah draft/mengirim Save, keyboard switch, theme draft/host acceptance, kontras, dan batas kontrol. Aplikasi Windows nyata diuji untuk keyboard Space pada segmented theme, pointer toggle, Save/restart 35/7/14, kedua tema, serta timer dan tray. Screenshot Appearance/Window/Timer ditinjau pada gelap/terang; backend SQLite/PAL dan ukuran shell tidak diubah.

## Scope Settings lanjutan

- **Appearance:** dark/light/system (jika OS signal tersedia), animation/reduced motion, window placement/collapse preferences.
- **Window:** always-on-top bila didukung; monitor position, sizing, shortcuts jika implementasi sudah ada.
- **Accounts:** OpenAI credential untuk Ask AI; Teams/Graph dan GitLab connection state, authorization, disconnect.
- **Integrations:** Zed/ACP setup status dan capability; Music app sessions, camera preference.
- **Notifications:** enable timer finished, agent finished / needs-attention, permission preferences.
- **Privacy:** chat history delete, agent activity retention, redact full paths, clear local data.
- **About:** app version, dependency licenses; atribusi Slint jika opsi lisensi mengharuskan.

## Perilaku desktop Windows yang sudah dipilih

- Oryn menjadi aplikasi **system tray / notification area**, bukan tombol aplikasi biasa di taskbar.
- Startup menampilkan floating panel dan mendaftarkan ikon tray `Oryn`. Windows menentukan apakah ikon langsung terlihat atau masuk overflow `^`; aplikasi tidak memaksa preferensi/pin pengguna.
- Logo resmi sumber: `ui/assets/logo oryn.png`, tidak dimodifikasi. `oryn-icon.png` adalah turunan 128×128 untuk embed icon window/tray dan identitas compact; tray menyalin HICON window yang dipinjam, bukan membuat glyph sementara lain.
- Klik kiri ikon menampilkan/menyembunyikan panel. Aktivasi keyboard menampilkan serta memfokuskan panel.
- Klik kanan menyediakan menu native **Tampilkan Oryn** dan **Keluar**.
- Minimize dan native close menyembunyikan panel tanpa menghentikan timer. Collapse tetap berarti panel compact, bukan hide.
- Keluar dari Settings atau menu tray menghentikan event loop/worker dan menghapus ikon. Menyembunyikan panel tidak mengakhiri aplikasi.
- Jika tray gagal didaftarkan, panel tidak boleh menjadi aplikasi tersembunyi tanpa akses pemulihan. Registrasi/icon/menu dimiliki owner RAII; fallback menjaga window terlihat dan dapat diakses.
- Perilaku ini khusus Windows yang sudah diimplementasikan, bukan klaim tray macOS/Linux.


## AI Usage tracker (di bawah Settings)

Menampilkan usage yang **benar-benar tersedia** dari masing-masing sumber. Jangan menyamakan:

1. **Ask AI API tokens** dari response/provider usage metadata bila tersedia.
2. **Estimasi biaya API** dari pricing table lokal yang harus versioned/timestamped; bukan tagihan resmi provider.
3. **Codex/Zed agent session context usage** jika ACP menyediakan metadata; tidak sama dengan API charges atau subscription remaining credits.
4. **Quota ChatGPT Plus atau subscription third-party** yang tidak selalu diekspos API; tampilkan `Unavailable`, jangan angka perkiraan.

## UI

Settings sidebar sederhana: `Appearance`, `Window`, `AI Providers`, `Integrations`, `Privacy`, `AI Usage`, `About`. Dalam floating small-panel bisa tampil sebagai tabs/list, tanpa dashboard analitik kompleks.

AI Usage cards bisa memperlihatkan `Provider`, `Model`, `Input tokens`, `Output tokens`, `Estimated cost` (bila source cukup), `Data source`, dan periode waktu filter sederhana. Nilai yang tidak tersedia = `—` disertai alasan.

## Storage

- Tabel `settings` SQLite untuk preferensi tahap 03 sudah tersedia. Tabel `ai_usage` untuk metadata nonsecret belum dibuat sampai integrasi usage nyata tersedia.
- Token/re-auth secrets di OS vault. Jangan menyimpan provider password.
- Cost/pricing estimates dipisahkan dari usage records dan diberi timestamp versi harga.

## Edge cases

- App berjalan di Linux tanpa credential vault service -> akun tidak otomatis disimpan plaintext.
- Salah satu source tidak menyediakan usage/cost -> no fake chart.
- User disconnect -> hapus vault item terkait, hentikan sync dan clear stale login state.
- Settings invalid/corrupted -> default aman dan error yang terlihat; database asli dipertahankan, tidak ditimpa otomatis.

## Acceptance criteria

- Theme dan window preferences persist restart.
- Disconnect provider menghapus secret lokal terkait dan menonaktifkan request berikutnya.
- AI Usage bersumber dari data berlabel, unknown tetap unknown.
- No token leak di logs/settings exports.
- About/license info tersedia sesuai kewajiban lisensi release.
