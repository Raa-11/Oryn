# Feature 05 — Time (Countdown / Pomodoro)

**Status:** implementasi Windows tahap 02 tersedia; model, lifecycle Asio, dan kontrak UI diuji otomatis. Validasi visual native serta sleep/resume perangkat nyata masih diperlukan.

## Goal

Timer sehari-hari yang tetap akurat dan tidak memboroskan resource. Jenis utama: countdown custom dan Pomodoro work/break cycles.

## MVP

- Countdown set menit/detik, start, pause, resume, reset.
- Pomodoro default (konfigurasi): work 25 menit / short break 5 menit / long break 15 menit / long break setiap 4 sesi (opsional pengaturan).
- Status simple `Idle`, `Running`, `Paused`, `Finished`.
- Notifikasi ketika sesi selesai jika OS permission diberikan.
- Sisa waktu benar meskipun window collapse, komputer sleep/resume, dan sistem lambat.

## Clock model

- Gunakan `std::chrono::steady_clock` untuk perhitungan elapsed durasi; bukan decrement variabel setiap detik.
- Gunakan `Boost.Asio steady_timer` untuk wake-up/update cadence yang diperlukan, bukan busy wait.
- Saat UI collapsed, update display sesuai cadence seperlunya, misal tiap 1 detik; hitung countdown langsung dari deadline.
- Setelah resume dari sleep, rekonsiliasi state (timer mungkin seharusnya selesai).

## UI

Collapsed: `12:43` / `Focus 12m` dan start/pause quick control. Expanded: big digits, mode selector, progress ringan, reset. No continuous animation in idle.

## Architecture & events

- `timer_service` dan `pomodoro_service` dalam `features/time`.
- Events: `TimerStarted`, `TimerPaused`, `TimerCompleted`, `PomodoroPhaseChanged`.
- Notification menggunakan `platform/interfaces/notifications.hpp` dengan graceful fallback.
- Preferences/default durations SQLite; timer session history opsional.

## Edge cases

- Countdown 0 atau input tidak valid tidak dijalankan.
- Clock timezone berubah tidak merusak monotonic elapsed time.
- Notifikasi ditolak OS: UI masih menunjukkan selesai.
- App exit: kebijakan sesi berjalan harus eksplisit (stop atau persist + restore based on time); jangan menjanjikan background timer bila app benar-benar ditutup.

## Semantik implementasi tahap 02

- Countdown menerima 00:01–23:59:59. Preset langsung memulai countdown; custom memakai menit/detik.
- Pause menyimpan durasi subdetik; tampilan dibulatkan ke atas. Resume membuat deadline baru dari sisa durasi.
- Pomodoro: focus/short break/long break dapat diatur 1–180 menit. Setelah fase selesai, fase berikutnya **menunggu Start**; tidak menjalankan rangkaian sesi yang terlewat selama sleep.
- Long break tersedia setelah setiap empat focus. Reset mengulang fase pada mode yang dipilih; Cancel pada Pomodoro mengembalikan siklus ke focus pertama. Keduanya tidak mengubah sesi mode lain.
- Standard dan Pomodoro memiliki sesi/deadline terpisah. Penggantian tab hanya memilih sesi yang ditampilkan: running tetap berjalan, paused tetap paused, serta fase/session progress tetap tersimpan. Memulai countdown Standard tidak membatalkan Pomodoro dan sebaliknya.
- Konfigurasi durasi tidak menimpa Pomodoro running/paused. Tahap 03 menyimpan focus/short break/long break melalui worker SQLite setelah TimerService menerima perubahan, baik dari Time maupun Settings; nilai kembali setelah restart. Sesi/deadline aktif bukan bagian preferensi persisten.
- Home dan compact membaca snapshot mode yang dipilih; compact memiliki pause/resume. Collapse/hide/tray tidak menghentikan kedua timer. Keluar aplikasi menghentikan worker; belum ada restore sesi setelah app ditutup.
- Satu worker Boost.Asio dan satu steady_timer menjadwalkan deadline terdekat dari kedua sesi, maksimal cadence satu detik. Jika tidak ada sesi running, tidak ada async timer yang terus berdetak. EnTT dispatcher dimiliki worker; hasil UI dikoaleskan ke satu slot lalu diterapkan di event loop Slint.
- Completion ditampilkan di UI dan compact. Notifikasi native OS belum dikonfigurasi; tidak ada klaim toast/background notification ketika aplikasi sudah keluar.
- Windows memakai MSVC steady_clock (QPC) sebagai clock monotonic. Rekonsiliasi deadline diuji dengan timestamp lompat; suspend/resume perangkat nyata serta perilaku clock saat suspend pada Linux/macOS belum diverifikasi.

## Acceptance criteria

- Pause/resume tepat secara semantik.
- Tidak ada thread per timer, tidak busy loop.
- UI tidak freeze dan countdown terukur meski app dibiarkan idle.
- 100x start/reset/cancel tanpa race/leak.
- Pindah Standard ↔ Pomodoro tidak reset/pause sesi; timer pada tab yang tidak dipilih tetap mencapai deadline. Completion Pomodoro hanya berpindah satu fase lalu menunggu Start seperti sebelumnya.
