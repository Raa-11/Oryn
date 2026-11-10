# Feature 03 — Music (Now Playing Controller)

**Status:** scope dipilih; API macOS masih terbuka.

## Goal

Menampilkan lagu yang sedang dimainkan **oleh aplikasi lain** dan mengontrol player tersebut dari floating island. Bukan aplikasi streaming musik.

## MVP

- Deteksi media sessions/player yang OS expose; pilih active session sesuai metadata/status.
- Tampilkan track title, artist, app/player, playback state, album art bila disediakan.
- Play/pause, next, previous jika didukung.
- Seek/progress/volume hanya jika capability nyata tersedia, bukan selalu.
- Saat tidak ada sesi: tampilkan `No media playing` dan kosongkan metadata lama.

## PAL per OS

| OS | Adapter | Catatan |
|---|---|---|
| Windows | `GlobalSystemMediaTransportControlsSessionManager` (GSMTC) | Native now-playing session controls |
| Linux | MPRIS via D-Bus | Beberapa player tidak implement seluruh capability |
| macOS | Native/platform-supported adapter, **belum diputuskan** | Tidak diasumsikan punya sistem GSMTC universal; perlu POC |

## Architecture

- `features/music` memegang `MusicState` dan controller.
- `platform/interfaces/media.hpp` mendefinisikan `list_sessions/current_session/play_pause/next/previous/seek_if_supported`.
- Subscribe perubahan OS native bila tersedia; fallback polling jarang dengan backoff bila API OS memang memerlukan.
- GPU frame hanya redraw saat metadata/state berubah; album art cache dibatasi ukuran.

## UI

Collapsed: ikon play + potongan judul/artis, atau musik pause. Expanded: cover art kecil (opsional), judul, artis, player, controls. Jangan membuat audio decoder atau media streaming engine.

## Event & persistence

`MediaSessionChanged`, `PlaybackStateChanged`, `TrackChanged`, `MediaCommandFailed`. SQLite hanya untuk preferred session/UI preferences bila berguna; **tidak** otomatis mencatat histori semua lagu.

## Edge cases

- Banyak sesi aktif: tentukan aturan memilih saat user menentukan manual/terakhir aktif.
- Unsupported commands: disable tombol, tampilkan capability-aware UI.
- Metadata missing/album art unavailable: fallback icon dan title.
- Session hilang akibat app ditutup: jangan tampilkan metadata stale.
- Lockscreen/sleep/resume: reattach session.

## Acceptance criteria

- Play/pause Windows berjalan pada player yang expose GSMTC.
- MPRIS Linux diuji pada beberapa player sesuai capability.
- Tidak ada CPU polling agresif saat idle.
- macOS dapat menunjukkan `Unsupported/Not configured` secara jujur sambil menunggu adapter nyata.
