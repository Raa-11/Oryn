# Feature 01 — Mirror (Webcam Preview)

**Status:** dipilih untuk produk; implementasi lintas platform belum dibuat.

## Goal

Menjadi cermin kamera cepat dalam floating panel tanpa membuka kamera full-screen. UI cukup live preview, flip horizontal dan pemilihan perangkat input.

## MVP

- List camera devices dari API OS dan tampilkan state `No camera`, `Permission required`, `Camera busy`.
- Start preview saat panel Mirror dibuka dan permission granted; pilih default camera.
- Switch front/back device bila beberapa camera tersedia.
- Toggle horizontal mirror (hanya tampilan, bukan memodifikasi camera source).
- Stop capture saat panel Mirror ditutup, user mematikan preview atau aplikasi shutdown.
- Preview tidak direkam, tidak dikirim ke internet, tidak disimpan di SQLite.

## Interaksi UI

Collapsed: ikon kamera + `Mirror` atau status minimal. Expanded: preview, flip toggle, dropdown camera. Jika camera tidak aktif, tampilkan placeholder tanpa menghidupkan camera di background.

## Arsitektur teknis

- `src/features/mirror/mirror_controller` mengelola lifecycle dan view state.
- `src/platform/interfaces/camera.hpp`: misalnya `list_devices`, `request_permission`, `start_preview`, `stop_preview`, `select_device`.
- Windows: native media/camera APIs; Linux: V4L2/PipeWire bergantung session; macOS: AVFoundation via `.mm` bridge.
- Transfer frame menuju Slint harus diuji format, resize, zero-copy/copy overhead dan release GPU resource. Jangan lakukan manual RGB conversion besar setiap frame tanpa profiling.
- Event contoh: `CameraStateChanged`, `CameraFrameAvailable` (frame pointer/handle aman dan lifetime jelas).

## Lifecycle & error handling

- Permission ditolak: tidak membuat polling/auto-retry agresif.
- Device dicabut: matikan stream, kosongkan preview, laporkan status.
- Screen collapse: bebaskan kamera dan GPU textures terkait dengan urutan thread-safe.
- Laptop sleep/resume: re-enumerate device bila session sebelumnya invalid.

## Acceptance criteria

- Preview muncul setelah user mengizinkan kamera pada OS yang diuji.
- Flip benar dan bukan webcam recording.
- Kamera benar-benar berhenti ketika panel ditutup (bukan hanya menutupi preview).
- Tidak freeze UI ketika device offline/permission ditolak.
- Tidak ada background upload atau penyimpanan frame.

## Bukan MVP

Recording video/audio, virtual camera, face recognition, filters, streaming, multi-cam composition.
