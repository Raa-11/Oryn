# 08 — Testing, CI, Performance, dan Release

## Lapisan pengujian

| Level | Framework / alat | Cakupan |
|---|---|---|
| Unit | doctest + Bazel `cc_test` | Event models, SSE decoder, JSON, tasks, timer, git parser |
| Integration | doctest + subprocess mocks | SQLite WAL/migrations, HTTP stream, ACP process, Boost.Asio cancellation |
| UI smoke | App + Slint test harness/manual | Expand/collapse, focus, rendering dan input |
| Platform | Device nyata / native runner | GSMTC, MPRIS, macOS adapter, webcam, vault, window capability |
| Performance | profiler OS + benchmarks internal | Idle CPU/RAM, cold start, leak, animation latency |

## CI yang tersedia

Workflow saat ini memiliki dua job pada runner Windows self-hosted berlabel `oryn-build`: build/test opt biasa dan MSVC AddressSanitizer. Runner memerlukan VS 2026 MSVC 14.51 x64, komponen ASan yang cocok, Windows SDK, Bazelisk, Git Bash, serta Slint 1.18.1 pada path di `MODULE.bazel`.

```bat
bazel test //src/app:oryn //src/core:app_info_test //src/platform:window_test //src/platform:paths_test //src/platform:credentials_test //src/infrastructure/database:database_test //src/features/settings:settings_test //src/features/calendar:calendar_test //src/features/time:time_test //ui:time_smoke_test //ui:calendar_smoke_test --test_output=errors

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.51
set "PATH=%VCToolsInstallDir%bin\Hostx64\x64;C:\Program Files\Slint-cpp 1.18.1\lib;%PATH%"
bazel test --config=asan --nocache_test_results //src/app:oryn //src/core:app_info_test //src/platform:window_test //src/platform:paths_test //src/platform:credentials_test //src/infrastructure/database:database_test //src/features/settings:settings_test //src/features/calendar:calendar_test //src/features/time:time_test //ui:time_smoke_test //ui:calendar_smoke_test --test_output=errors
```

Mode normal sudah opt secara default. `--config=asan` menambahkan instrumentasi C++ `/fsanitize=address`, debug information tanpa Edit-and-Continue, serta linker/runtime MSVC yang cocok. Konfigurasi ini khusus diagnosis Windows x64, bukan paket deployment. DLL SDK Slint prebuilt tidak terinstrumentasi; tidak ada klaim UBSan/TSan MSVC. Workflow tidak menggantikan smoke desktop interaktif dan eksekusi lokal tidak membuktikan runner remote telah berjalan.

UI theme regression memilih tombol segmented Dark/Light Settings, memastikan draft belum mengubah host/theme, lalu memeriksa hasil setelah host acceptance dan kontras Palette/custom text dalam dua arah. Switch kustom diuji untuk pointer focus, Space/Return, serta larangan mutasi/Save saat loading atau saving melalui accessibility. Draft/navigation, invalid duration/negative position, retry feedback, dan geometry seluruh section ikut diuji. Testing backend diinisialisasi sekali per proses; `slint::platform::update_timers_and_animations()` memproses changed handler.

Smoke desktop Windows memakai data folder **terisolasi**, bukan profile pengguna: set `ORYN_DATA_DIR` ke absolute path, ubah preferensi melalui UI nyata, Save, Exit, lalu restart dengan folder yang sama. Periksa theme/reduced motion, topmost, startup compact, koordinat physical, durasi, WAL dan `PRAGMA user_version=2` setelah tahap 04. Smoke historis tahap 03 memeriksa schema v1 dan durasi 30/6/12; migration tahap 04 wajib mempertahankan preferensi v1. Tidak ada klaim restore sesi timer aktif setelah exit.

Jalur konfigurasi **Time → Apply** juga diuji pada aplikasi nyata: 35/7/14 muncul pada Settings tanpa stale draft dan tetap tersimpan setelah restart berikutnya. Ketiga proses smoke keluar kode 0; Quill menguras event start/load/stop pada stderr.

Storage integration menguji migration/roundtrip, nilai invalid, corruption/future-version tanpa destructive reset, write failure, worker callback thread, latest-save coalescing, pre-load ordering, dan shutdown drain. Vault test memakai service/account GUID terisolasi untuk CredWrite/Read/Delete binary/empty-secret, move semantics, serta cleanup; tidak menyentuh akun aplikasi nyata. Resolver test memakai override terisolasi tanpa membuat folder pada UI.

Smoke frameless/tray wajib mencakup native minimize/close → hide, restore melalui ikon, expanded/compact, dua tema, dan topmost true/false. Aplikasi sukses mendaftarkan tray tidak memiliki tombol taskbar biasa. Smoke tahap 03 memverifikasi Pomodoro tetap aktif saat pindah tab dan hide/restore, serta topmost false bertahan. Geometry saja tidak membuktikan pixel software surface benar; review gambar render dan konten setelah restore. Headless UI smoke tidak membuat HWND sehingga tidak mencakup repaint nonclient atau lifecycle shell.

Calendar menambah `//src/features/calendar:calendar_test` dan `//ui:calendar_smoke_test`. Cakupan: Gregorian/leap-year/optional date, v1 → v2 preservation dan rollback, UTF-8/SQL quotes, CRUD/status, filter/urutan/ID, halaman 50 task, event/revision setelah commit, kegagalan tanpa kehilangan draft/data, antrean penuh, worker ownership, serta shutdown drain. UI memakai jam animasi testing Slint yang deterministik untuk menyelesaikan wheel scroll ke task ke-50; tidak memakai sleep untuk menutupi state.

Smoke Calendar aplikasi nyata: create/edit notes multiline, due date invalid tetap draft, complete/reopen, Today/Upcoming/Completed, overdue dan tanpa tanggal, draft lintas halaman, delete confirmation/cancel, serta restart membuktikan penghapusan persisten. Uji minimize/tray bersama timer running dan shutdown kode 0. Profile upgrade terisolasi menunjukkan schema v2/WAL dan durasi lama 27/6/13 tetap ada. GUI/image review tetap diperlukan terpisah dari headless test.

## Target CI lintas platform

- **Windows:** build, doctest, parser/DB/smoke tests; GUI smoke apabila runner memberi desktop session.
- **Linux:** build+test pada CI; X11/Wayland GUI validation membutuhkan session compositor, bukan WSL2 build saja.
- **macOS:** build+unit/integration via GitHub Actions; camera/media/desktop interaction memerlukan entitlement/permission/device nyata bila perlu.
- Compiler support C++23 + stdexec harus dites per versi pin (`MSVC`, `Clang`, `GCC`, `AppleClang`); jangan memaksakan matrix yang tidak bisa compile tanpa mencatat limitation.

## POC teknis wajib sebelum fitur besar

1. Slint `.slint` AOT -> generated C++ header -> Bazel `cc_binary` lintas OS.
2. Renderer default `winit-software` benar-benar aktif untuk target C++ yang dibangun; verifikasi performance log, bukan hanya exit code atau pesan fallback.
3. Floating island transparan dan expand/collapse; Wayland fallback jelas.
4. `Boost.Asio` + `Boost.Process V2` membaca/menulis stdio child process dengan cancellation/shutdown bersih.
5. `stdexec` menyusun satu Asio timer operation, dan operasi cancel tidak use-after-free.
6. `libcurl` SSE transfer berkelanjutan, cancel dan clean shutdown.
7. ACP custom agent/proxy benar-benar menerima `session/update` dari Codex di Zed.

## Benchmark protocol

- Gunakan release build optimizations yang sama, perangkat dan OS dicatat; jangan mengukur RAM debug lalu membandingkannya dengan release.
- CPU idle 5–10 menit dengan window collapsed, semua features off; lalu ukur dengan Agents monitor aktif, music aktif, timer aktif, Mirror aktif terpisah.
- RAM: startup/steady state/peak; camera/GPU resources dipisahkan.
- Startup cold/warm minimal 10 sampel tiap target.
- UI frame timing saat animasi; tidak perlu membuat FPS loop idle.
- Laporkan results dalam Markdown dengan machine/commit/toolchain dan observed values, bukan angka dugaan.

## Release

- Simpan signing/release settings di CI secrets, bukan repo.
- Periksa kewajiban atribusi Slint dan lisensi seluruh dependency.
- User data default di directory standar OS, bukan folder instalasi.
- Semua external process launch harus escape arguments dengan benar, memakai absolute path bila tersedia, dan tidak melewati `shell` bila tidak diperlukan.

## Definition of Done proyek awal

- Aplikasi boot dan shutdown 100x tanpa leak/hang pada Windows.
- UI Slint 2 state collapse/expand bekerja tanpa blocking I/O.
- Satu vertical slice fitur Time atau Music berhasil dari UI sampai PAL/Asio.
- Toolchain, dependensi dan build steps reproducible di README.
- CI unit tests berjalan di ketiga OS (atau limitation dan unblocker tertulis).
