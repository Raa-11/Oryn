# 01 — Visi Produk, Sasaran, dan Batasan

## Visi

Membangun **native floating desktop companion** yang terasa seperti mini-island: berada di desktop, tampil kecil ketika idle, berkembang saat dipilih, memberi kontrol fungsi harian dan status real-time tanpa mengganggu kerja. Inspirasi interaksi: aplikasi kecil seperti Coucou/Notchy, **bukan tiruan desain 1:1**.

## Platform

- **Target:** Windows, Linux (X11/Wayland dengan pembatasan), macOS.
- **Pengembangan utama:** Windows; WSL2 berguna untuk build/test Linux, tetapi bukan pengganti pengujian compositor desktop Linux sesungguhnya.
- **CI:** Windows + Linux + macOS build/automated test; hardware/GUI macOS dan Wayland perlu tes nyata.
- Satu executable desktop utama; helper executable kecil khusus ACP proxy dapat ditambahkan jika integrasi teknis memerlukan.

## Karakter produk

- UI kecil, kontekstual, dark/light theme, animasi expand/collapse singkat dan tidak boros idle.
- Native APIs untuk media dan kamera; internet hanya saat fitur butuh.
- Fokus privasi lokal: histori chat dan tugas di SQLite; secrets disimpan pada vault OS.
- Graceful degradation: jika suatu OS tidak mendukung window pinning, media session, atau camera source, tampilkan status jujur.

## Fitur yang ditargetkan

| Fitur | Hasil pengguna | Spec |
|---|---|---|
| Mirror | Melihat webcam dan flip preview | [Mirror](../features/01-mirror.md) |
| Ask AI | Chat dengan OpenAI, stream, riwayat | [Ask AI](../features/02-ask-ai.md) |
| Music | Mengontrol musik dari player lain | [Music](../features/03-music.md) |
| Calendar | Agenda task harian + dua jalur sinkronisasi | [Calendar](../features/04-calendar.md) |
| Time | Timer, Pomodoro dan notifikasi | [Time](../features/05-time.md) |
| Agents | Mengetahui apa yang sedang dilakukan Codex di Zed | [Agents](../features/06-agents.md) |
| Settings | Pengaturan, auth, dan AI Usage | [Settings](../features/07-settings-ai-usage.md) |

## Non-goals MVP

- Tidak membuat IDE, terminal terminal-emulator, editor/diff viewer sendiri.
- Tidak membuat recording/webcam broadcaster, sound streaming engine, atau pengganti Spotify.
- Tidak membuat AI coding agent dalam Ask AI; coding agent tetap di Zed.
- Tidak membuat timesheet otomatis dalam MVP Calendar.
- Tidak membuat cloud backend / user accounts server / multi-device sync.
- Tidak ada dukungan penuh semua provider AI pada hari pertama; mulai OpenAI.
- Tidak menjamin kontrol/intersepsi ACP agent Zed tanpa proof-of-concept.

## Target nonfungsional (aspirasi, bukan hasil benchmark)

| Ukuran | Target awal | Cara validasi |
|---|---|---|
| Idle CPU | Mendekati 0% ketika island tidak dianimasikan | Rekam 5–10 menit idle di 3 OS |
| RAM idle | Diusahakan di bawah 50 MB, **belum terbukti** | Resident set/process private bytes |
| Startup | Diusahakan kurang dari 1 detik, **belum terbukti** | Cold/warm start beberapa kali |
| UI responsiveness | Smooth, tanpa blok I/O | Frame timing + interaksi |
| Background tasks | Tidak membuat polling rapat di idle | Wakeup count / CPU profile |
| Privacy | Tidak ada secret plaintext di DB/log | Inspection + test |

## Prioritas prinsip

1. Correctness dan lifecycle (tidak hang, tidak bocor resource).
2. Respons UI dan konsumsi daya idle.
3. Performa diukur di device nyata, bukan klaim library.
4. Portabilitas melalui PAL; UX bisa berbeda antar OS.
5. Fitur tambah bertahap, sesuai roadmap.
