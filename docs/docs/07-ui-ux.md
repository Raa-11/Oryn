# 07 — UI/UX: Floating Island Minimal

## Prinsip utama

- Companion kecil dan tenang, bukan dashboard IDE; melihat kondisi dengan sekilas.
- **Collapsed** = ringkasan satu baris. **Expanded** = konteks tambahan dan aksi utama.
- Dark/light theme, desain konsisten, tipografi jelas, mudah dibaca, warna status tidak menjadi satu-satunya indikator.
- Tanpa animation loop saat idle; animasi state-driven hanya ketika transisi.
- Render perubahan UI dalam batch; tidak perlu redraw saat tak ada perubahan.

## Model window

1. **Collapsed idle:** ukuran kecil; ikon modul aktif, status singkat, kontrol untuk membuka.
2. **Expanded panel:** menu yang dipilih; area konten secukupnya, tanpa panel bersarang berlebihan.
3. **Attention state:** dapat menunjukkan timer selesai atau Agents memerlukan approval jika didukung.
4. **Pin/on-top:** mengikuti kemampuan OS; untuk Wayland transparansi, posisi absolut, click-through, always-on-top bergantung compositor dan perlu fallback.
5. Default posisi, monitor-aware, DPI scaling, multiple displays, drag region harus diuji.

## Desain final fitur Agents

### Collapsed

```text
╭─────────────────────────────────────╮
│  ◉ Codex     ✎ Editing service.cpp │
╰─────────────────────────────────────╯
```

Jika tidak ada file: `Codex · Working` atau `Codex · Reading` berdasarkan event yang tersedia. Jika integrasi putus: `Codex · Disconnected`, bukan `Working` palsu.

### Expanded

```text
╭──────────────────────────────────────────╮
│  ◉ Codex            Working              │
│  Zed · desktop-companion                 │
│──────────────────────────────────────────│
│  CURRENT TASK                            │
│  Implementing authentication             │
│                                          │
│  Editing file                            │
│  src/auth/service.cpp                    │
│                                          │
│  3 files         +82 lines     -17 lines │
│──────────────────────────────────────────│
│  Live activity             Open in Zed ↗ │
╰──────────────────────────────────────────╯
```

**Contoh angka dan tugas hanya mock data**. Ringkasan task hanya jika title/description tersedia. `+/-` opsional dan diberi label sumber perhitungan Git working tree bila tidak dapat diatribusikan. `Open in Zed` membuka path yang aman/tersedia.

### Yang tidak dipakai

- **Bukan** timeline tetap `Read → Edit → Bash → Done` sebagai tampilan utama.
- **Bukan** code editor, diff viewer, terminal, atau clone Agent Panel Zed.
- Jangan membuat persentase progres semu; status `Thinking`, `Reading`, `Editing`, `Executing`, `Completed`, `Failed` bersifat observasi, bukan estimasi persentase selesai.

## Desain fitur lain

- Mirror: preview kamera, toggle flip, camera selector; panel ditutup = capture dihentikan.
- Ask AI: input + riwayat ringan, streamed response, scroll dan pesan panjang virtualized.
- Music: cover art bila tersedia, judul/artist, play/pause/skip dan seek hanya jika didukung.
- Calendar: fokus agenda hari ini, daftar task singkat, sumber sinkronisasi/status.
- Time: countdown besar dan kontrol start/pause/reset; no busy redraw.
- Settings: tema, account, privacy, window, notifications, `AI Usage` sebagai subpage.

## Aksesibilitas & input

- Keyboard navigation dan focus jelas; escape/collapse; screen reader support yang didukung Slint perlu dites.
- `prefers reduced motion` atau toggle animasi minimum bila OS/toolkit tidak menyediakan informasi langsung.
- Jangan animasikan teks per token tanpa throttle.
- Bila data kosong, tampilkan state `No active session`, bukan dummy file atau angka kosong yang menyerupai aktivitas nyata.

## Interaksi dan lifecycle

- Satu sumber state per fitur. UI mendengarkan perubahan, tidak polling setiap render.
- Kamera jangan dihidupkan kecuali panel Mirror benar-benar terbuka.
- Agents monitoring tetap aktif saat panel collapse, bila opt-in aktif.
- Light mode diuji terpisah agar icon, shadow, blur dan text contrast tidak hilang.
