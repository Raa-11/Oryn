# Tahap 02 — Time

## Scope dan acceptance criteria

- Countdown custom 00:01–23:59:59 dan preset 1/2/3/5/10/25 menit.
- Start, pause/resume dengan presisi subdetik, reset, cancel, expiry tepat satu kali.
- Pomodoro focus/short/long break, default 25/5/15 menit, konfigurasi sesi 1–180 menit; long break setiap focus keempat.
- Setelah fase selesai, fase berikutnya menunggu Start. Tidak mengarang sesi yang berjalan selama perangkat sleep.
- UI Standard dan Pomodoro mengacu frame `RmveO`/`vhola` pada `design/design.pen`; Home dan compact membaca state timer yang sama, termasuk pause/resume compact.
- Collapse tidak membatalkan timer. Keluar aplikasi membatalkan Asio wait, drain cancellation, lalu join worker. Tidak ada restore setelah keluar; persistence masuk tahap 03.

## Implementasi

- `src/features/time/`: model deadline `steady_clock`, Pomodoro cycle, satu Asio `steady_timer` dan satu worker `jthread`.
- Timestamp model di-inject sehingga tes tidak perlu menunggu menit nyata.
- EnTT dispatcher dimiliki worker I/O; event TimeChanged, TimerStarted, TimerPaused, TimerCompleted, PomodoroPhaseChanged tidak dipicu lintas thread.
- Hasil worker masuk mailbox satu slot: snapshot terbaru mengganti snapshot lama, maksimal satu callback terjadwal di Slint. Semua setter UI dijalankan di Slint event loop.
- Tidak ada timer pembaruan aktif saat idle, paused, atau selesai. Tidak ada thread per session.
- Dependencies: Boost.Asio 1.89.0.bcr.2; EnTT 3.15.0, archive/checksum dipin pada MODULE.bazel. stdexec belum ditambahkan karena belum ada flow orchestration yang membutuhkannya.
- UI smoke memakai testing backend Slint. Metadata debug di-generate pada target test terpisah sehingga tidak menambah metadata debug ke app production.
- CI workflow Windows diperbarui untuk keempat target test; eksekusi runner remote belum diverifikasi.

## Validasi aktual

Perintah yang dijalankan pada Windows/MSVC 14.51, Bazel 9.2.0:

```text
bazel build //src/app:oryn
bazel test //src/core:app_info_test //src/platform:window_test //src/features/time:time_test //ui:time_smoke_test --test_output=errors
```

- Build app berhasil.
- Keempat target test PASSED.
- Unit model: input invalid/zero/negative/oversized, ceil detik, deadline terlambat, pause/resume, reset/cancel, expiry sekali, konfigurasi dan pergantian mode.
- Pomodoro: empat focus, short/long break, nomor sesi, fase berikutnya idle, rekonsiliasi timestamp lompat (simulasi suspend).
- Asio: expiry nyata satu detik, error validation, 100 rangkaian queued start/pause/reset/cancel, penghentian service dengan pending wait, serta beberapa siklus construct/destroy worker.
- UI headless: preset/custom callbacks, pause/resume, reset/cancel, mode Pomodoro, Apply konfigurasi, compact quick control, collapse/reopen. Semua tombol dalam batas window pada state editor durasi paling tinggi.
- Helper Computer Use tidak tersedia (`native pipe ... os error 2`), tetap gagal setelah retry/reset. Karena itu belum ada klaim hasil visual native yang sudah diperiksa pada sesi ini.

## Blocker fondasi yang ditemukan

- Probe sementara self-closing dijalankan dengan `SLINT_BACKEND=winit-femtovg-wgpu`.
- Runtime mencetak `slint winit: unrecognized renderer femtovg-wgpu, falling back to FemtoVG`; probe keluar 0. Exit 0 ini **bukan bukti** target WGPU tersedia.
- Compiler melaporkan 1.18.1; header SDK lokal di folder `Slint-cpp 1.18.1` melaporkan SLINT_VERSION_STRING 1.18.2 dan menyertakan feature Skia. SDK lokal belum membuktikan pin build renderer tanpa Skia sesuai target proyek.
- Pada sesi implementasi Time belum ada perubahan keputusan renderer. Temuan ini kemudian ditindaklanjuti dalam revisi D-003 di bawah: software dipilih pengguna untuk menghemat RAM.
- ACP spike dan runner CI belum diuji. Dokumen ini tidak mengklaim tahap 00 selesai.

## Pemeriksaan manual berikutnya

1. `bazel run //src/app:oryn`, buka Time, pilih preset dan custom 00:05; periksa countdown, pause/resume, reset, serta completion.
2. Saat timer running, collapse, pause/resume dari compact, lalu reopen; waktu harus konsisten dengan Home/Time.
3. Pomodoro: konfigurasi durasi, Start/Pause/Resume/Cancel. Pastikan mode switching membatalkan sesi sebelumnya.
4. Uji sleep/resume perangkat Windows sungguhan dan shutdown saat running.
5. Periksa light/dark, DPI/multi-monitor, keterbacaan SpinBox, clipping editor durasi, dan kelancaran animasi. Tes headless tidak menggantikan pemeriksaan font/rendering native.

## Batasan

- Belum ada notifikasi toast native OS; completion terlihat di panel dan compact saat aplikasi hidup.
- Preferensi tidak disimpan ke SQLite sampai tahap 03; aplikasi berhenti berarti timer berhenti.
- Windows QPC mendukung waktu monotonic termasuk suspend, tetapi perangkat nyata belum diuji; Linux/macOS suspend behavior belum diverifikasi.
- Tidak ada benchmark RAM/CPU/FPS/startup pada sesi ini.


## Revisi visual Time
- Mode menjadi segmented control, preset memiliki border tipis, dan input custom menjadi satu baris dengan field angka tanpa spinner native.
- Semua kontrol custom, progress bar dan edit durasi dipusatkan. Input numeric tetap melewati validasi TimerService; smoke test ditambah untuk 00:05.
- Preview Standard/Pomodoro di-render melalui Slint take_snapshot() dalam harness self-closing dan diperiksa visual. Ini memverifikasi gambar renderer, bukan interaksi pointer pada aplikasi pengguna.
- Build --platform_suffix=time_ui_review berhasil tanpa menutup app yang sedang berjalan; UI smoke dan timer_test PASSED.
- Screenshot Task Manager pengguna menunjukkan oryn.exe250.9MB. Itu observasi pengguna, bukan benchmark terkontrol atau diagnosis leak. RAM/renderer/driver/cache belum diprofil, dan tidak ada klaim targetmemori tercapai.

## Optimasi RAM — software renderer

- Pengguna melaporkan `oryn.exe` sekitar 176,2 MB pada screenshot Task Manager. Ini observasi pengguna, bukan benchmark terkontrol.
- Probe mengungkap default SDK memakai **Skia/WGPU**, bukan target renderer proyek. `winit-femtovg-wgpu` ditolak SDK; meski pesan fallback menyebut FemtoVG, `SLINT_DEBUG_PERFORMANCE=refresh_lazy,console` tetap menunjukkan Skia/WGPU. `winit-femtovg` eksplisit terbukti memakai OpenGL.
- Setelah perbandingan probe, pengguna menyetujui software renderer. `src/app/main.cpp` mengisi `SLINT_BACKEND=winit-software` sebelum Slint/worker dibuat, hanya bila environment belum berisi pilihan eksplisit. Gagal mengisi environment dilaporkan dan startup dibatalkan, bukan diam-diam memakai renderer SDK.

### Pengukuran aplikasi lengkap

Build: `bazel build -c opt --platform_suffix=memory //src/app:oryn` berhasil pada Windows/MSVC dengan SDK Slint lokal. Peringatan compiler: override `/std:c++17` dan perbedaan `NDEBUG` saat import module; dependency Boost.Context juga menghasilkan warning opsi MASM.

Binary opt yang sama dijalankan sebagai dua proses terpisah berurutan, tampilan Home expanded, ukuran native 1064×565 pada DPI aktif, diukur sekitar 5 detik setelah startup. Baseline memakai override `SLINT_BACKEND=winit-skia`; sesudah memakai startup baru tanpa variabel tersebut. psutil membaca resident working set (`rss`), private working set (`uss`), dan private committed bytes (`private`). MiB = 1.048.576 byte; kolom ini tidak boleh disamakan satu sama lain atau langsung disamakan dengan screenshot Task Manager.

| Metrik | Skia/WGPU | Software |
|---|---:|---:|
| Working set | 210,46 MiB | 28,19 MiB |
| Private working set | 169,07 MiB | 6,64 MiB |
| Private committed bytes | 186,66 MiB | 7,83 MiB |

- Working set berkurang sekitar 86,6% pada pengukuran ini. Satu sampel per kondisi, bukan statistik jangka panjang atau bukti tidak ada leak.
- Sampling CPU software selama 3 detik idle menunjukkan delta CPU proses 0 detik pada resolusi counter Windows; performance log melaporkan 0 frame/detik saat tidak ada perubahan. Ini bukan pengukuran FPS animasi.
- Setelah interaksi timer/navigasi, sampel software lain memakai working set 29,20 MiB; bukan kondisi benchmark Home yang sama.
- Runtime log membuktikan `Backend: software` tanpa override, dan `Backend: Skia renderer (skia backend wgpu; surface: 32 bpp)` pada kontrol baseline.

### Smoke native

- Home, Time Standard dan Pomodoro diperiksa dari screenshot aplikasi sebenarnya, bukan testing backend.
- Klik preset 1 menit → countdown terlihat berjalan; Pause → waktu berhenti; collapse → compact mempertahankan waktu; Resume dari compact → waktu berkurang; reopen → state running tetap sama.
- Cancel dan pilih Pomodoro menampilkan Focus 25:00 / 1 of 4; Start Focus dan navigasi Settings dijalankan.
- Tema terang diterapkan dan diperiksa; tombol keluar Settings menghasilkan exit 0.
- Baseline Skia terkontrol sempat tidak berhenti setelah `WM_CLOSE`; proses uji milik sesi dihentikan. Tidak ada klaim clean shutdown baseline. Jalur keluar aplikasi software melalui tombol UI berhasil.
- Tidak ada perubahan kontrak timer/UI; suite headless tidak dijalankan ulang untuk perubahan pemilihan renderer. Smoke native di atas menguji jalur yang berubah.
- Tradeoff yang disetujui: rasterisasi CPU dan kemungkinan perbedaan scaling gambar. Tidak ada klaim kelancaran animasi identik, uji kamera/video, soak memory, suspend nyata, multi-monitor, Linux/macOS, atau penghapusan Skia dari DLL SDK.


## Cutover header internal ke C++ named modules

- Atas permintaan pengguna, empat header internal dihapus jalurnya melalui rename ke `.cppm`: `pomodoro_service`, `timer_state`, `timer_service`, dan PAL `window`. Tidak ada header wrapper/shim tersisa di `src`.
- Modul baru: `oryn.time.pomodoro`, `oryn.time.state`, `oryn.time`, dan `oryn.platform.window`. Interface state/Pomodoro diekspor transitif oleh `oryn.time` karena dipakai pada kontrak `Snapshot` dan service. `format_remaining` dipindahkan deklarasinya ke interface `oryn.time`, sesuai pemilik implementation unit.
- App, timer test, dan window test memakai `import`. Standard library, Win32, Slint generated, Boost, dan EnTT tetap header dependency pada global module fragment bila merupakan module unit. Tidak memaksakan header units atau `import std`.
- Target Bazel time/window memakai `module_interfaces`, `features = ["cpp_modules"]`, C++23, serta `textual_hdrs` untuk pola integrasi CLion yang sudah ada; test consumer juga memakai modules. Tidak ada perubahan logika timer atau renderer.
- Verifikasi: `bazel test -c opt --platform_suffix=modules //src/app:oryn //src/core:app_info_test //src/platform:window_test //src/features/time:time_test //ui:time_smoke_test --test_output=errors` membangun app dan meluluskan seluruh empat test (semuanya dieksekusi, bukan hasil cache).
- Smoke aplikasi native dari binary `x64_windows-opt-modules`: Home terlihat, klik preset 1 menit, Pause menampilkan 00:59, collapse, Resume dari compact menampilkan 00:57, reopen mempertahankan running 00:56, navigasi Settings, keluar melalui tombol UI dengan exit 0.
- Log runtime tetap `Backend: software`. Sampel setelah interaksi: working set 31.297.536 byte (29,85 MiB), private working set 7.405.568 byte; bukan benchmark idle terkontrol atau klaim modules menghemat RAM.
- Warning build yang masih muncul: opsi MASM Boost.Context, override `/std:c++17`, dan MSVC C5050 tentang perbedaan `NDEBUG` antara interface module dan consumer. Tidak disembunyikan; tidak ada klaim build tanpa warning.
- Build/runtime Linux/macOS, rebuild penuh tanpa cache, dan sinkronisasi CLion terbaru belum diuji.


## Penyelarasan standar C++23

- Target C++ sudah memakai `/std:c++23preview` pada Windows dan `-std=c++23` pada platform lain; tidak ada perubahan semantik kode atau API modul.
- `.clwb/.bazelproject` sebelumnya memakai `/std:c++latest`; diganti `/std:c++23preview` agar konfigurasi build IDE tidak memilih draft standar yang lebih baru.
- MSVC lokal 19.51.36248 diuji melalui compile probe sementara: `/std:c++23` menghasilkan D9002 (diabaikan), `_MSVC_LANG=201402L`, dan static_assert C++23 gagal. `/std:c++23preview` lulus dengan `_MSVC_LANG=202302L`. Probe memakai `/Zc:__cplusplus` untuk memverifikasi macro standar juga 202302L; ini bukan flag tambahan proyek.
- Verifikasi sesuai flag build IDE: `bazel test --compilation_mode=dbg --cxxopt=/std:c++23preview --platform_suffix=cpp23 //src/app:oryn //src/core:app_info_test //src/platform:window_test //src/features/time:time_test //ui:time_smoke_test --test_output=errors` berhasil; empat test dieksekusi dan lulus.
- Smoke binary `x64_windows-dbg-cpp23`: aplikasi native dibuka, Time preset 1 menit berjalan hingga terlihat 00:58, Settings dibuka, dan tombol keluar menghasilkan exit 0. Renderer tetap software.
- Warning override `/std:c++17` dan opsi MASM tetap muncul. Tidak ada klaim sinkronisasi/indexing CLion telah diperiksa melalui UI, dukungan C++23 compiler sudah 100%, atau hasil RAM debug setara benchmark opt.


## Modern C++23 / RAII dan kerapian struktur

- Panduan yang dibaca: [Trail of Bits modern-cpp](https://skills.sh/trailofbits/skills/modern-cpp) dan [C++ Core Guidelines R.1](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rr-raii). Dipakai selektif untuk C++23; tidak menginstal skill global, menambah dependency runtime, atau memakai fitur C++26.
- PAL sekarang memiliki interface ringkas `window.cppm` dan implementation unit `window.cpp`. Helper HWND serta ownership Win32 privat: device context/region memakai `unique_ptr` dengan custom deleter tanpa alokasi C++ tambahan; saved DC dan drawing path memakai scope owner non-copyable. Early return memulihkan DC, membatalkan path yang belum dikonsumsi, dan melepas resource.
- Region hanya di-`release()` setelah `SetWindowRgn` sukses, sesuai kontrak ownership Windows. Array judul/titik Bezier memakai `std::array`; nilai kurva dan perilaku window dipertahankan.
- Timer menyelesaikan initial post sebelum memulai `jthread`, sehingga unwind setup tidak menunggu worker yang ditahan work guard. Shutdown normal mempertahankan satu owner Asio, FIFO, cancel/drain/join. Jika post atau cancel shutdown gagal, fallback stop/join menjaga lifetime; pekerjaan pending dapat dibuang hanya pada jalur kegagalan tersebut. Observer tetap wajib tidak melempar atau menghancurkan servicenya sendiri.
- App mailbox/weak handle yang sudah aman dipertahankan. Dependency app → core yang tidak dipakai serta salinan/target generated `ui-gen` tanpa caller dihapus. Tidak menambah framework resource atau lapisan fitur baru.
- `.clang-format` menetapkan indent 4 spasi, 100 kolom, namespace tanpa indent, serta mempertahankan include order. Seluruh C++ tulisan tangan dirapikan dengan clang-format 22.1.3; generated/dependency tidak diformat. `<ostream>` tetap mendahului doctest.
- Test native memeriksa 256 pergantian region: silhouette tetap valid dan jumlah GDI objects proses tidak bertambah. Timer test tambahan memeriksa destructor menguras command yang diterima dalam urutan FIFO, termasuk error validasi, tanpa mengandalkan waktu tidur.
- Verifikasi: `bazel test -c opt --platform_suffix=raii //src/app:oryn //src/core:app_info_test //src/platform:window_test //src/features/time:time_test //ui:time_smoke_test --test_output=errors` berhasil membangun app dan meluluskan 4/4 test. Run akhir mengeksekusi tiga test; window_test dari cache run sebelumnya yang juga lulus pada kode RAII yang sama.
- Smoke native `x64_windows-opt-raii`: countdown 1 menit, collapse, pause/resume compact, reopen memperlihatkan running 00:57, lalu keluar dari Settings saat timer aktif dengan exit 0. Region compact terukur `(274, 0, 790, 68)`; renderer tetap software.
- Build pertama menemukan overload `cancel(error_code)` tidak tersedia pada Boost.Asio lokal; diperbaiki memakai API `cancel()` yang didukung dengan cleanup handler `noexcept` dan fallback stop bila cancel melempar. Warning toolchain `/std` override, `NDEBUG` modules, dan opsi MASM masih ada; tidak disembunyikan.
- Fault injection allocation/cancellation failure, sanitizer, soak memory, Linux/macOS, serta sinkronisasi IDE tidak dijalankan. Review tambahan dihentikan setelah pengguna meminta dipercepat; tidak ada klaim audit menyeluruh tanpa sisa risiko.


## Perapihan keterbacaan setelah review pengguna

- Pengguna menilai penulisan masih terlihat acak; format LLVM sebelumnya masih mengizinkan fungsi padat, `if` tanpa braces, namespace tanpa indent, serta definisi yang saling menempel.
- `.clang-format` diperketat: namespace diindentasi, braces kontrol disisipkan, fungsi tidak satu baris, definisi dipisahkan baris kosong, dan access modifier sejajar dengan awal isi class. Seluruh C++ tulisan tangan diformat; generated/dependency tetap tidak disentuh.
- `application.cpp` memisahkan mapping `Snapshot` → setter UI ke helper privat `apply_time_snapshot()`. Callback hanya menguras mailbox dan menerapkan snapshot. Ternary bertingkat untuk label fase/status diganti switch/if yang eksplisit, dengan fallback label dan prioritas completion yang tetap sama. Tidak menambah module, allocation, atau ownership baru.
- Include standard/dependency dan deklarasi module/import pada file yang dirapikan dipisahkan secara visual; komentar status Pomodoro tidak lagi menggantung di samping assignment.
- Verifikasi: clang-format `--dry-run --Werror` lulus; build app dan 4/4 test dieksekusi serta lulus dengan `bazel test -c opt --platform_suffix=raii //src/app:oryn //src/core:app_info_test //src/platform:window_test //src/features/time:time_test //ui:time_smoke_test --test_output=errors`.
- Smoke native: preset satu menit, pause, collapse, resume compact, reopen menunjukkan countdown running 00:57, lalu keluar dari Settings dengan exit 0. Renderer tetap software. Warning toolchain sebelumnya masih muncul; tidak ada klaim semua aspek gaya atau kualitas codebase sudah sempurna.


## Perapihan source Slint dan komponen tidak terpakai

- Seluruh enam file `.slint` aktif diformat dengan formatter resmi `slint-lsp` 1.18.1. Tool Windows diunduh dari release resmi dan SHA-256 dicocokkan dengan release sebelum dipakai. Tidak ada perubahan renderer atau dependency runtime.
- `floating_island.slint` diganti nama menjadi `floating-island.slint`; main import dimigrasikan tanpa alias lama. `NavigationTab` dan `IconButton` dipisah ke file kontrol masing-masing dengan binding, ukuran, animasi, focus, dan accessibility yang dipertahankan.
- Interface window dikelompokkan terpisah dari geometry/animasi dan child tree. State internal ditandai privat; handler dengan kondisi/beberapa statement dibentangkan multiline. `.editorconfig` menyimpan aturan UTF-8/LF/empat spasi untuk Slint.
- `feature_panel.slint` / `FeaturePanel` dihapus setelah pencarian caller tidak menemukan pemakai. Export `TimeButton` yang tidak dipakai lintas-file dihapus, tetapi komponennya tetap privat dan dipakai TimeScreen. Tidak ada fungsi Slint bebas yang ditemukan untuk dihapus; callback timer, exit, shape, keyboard, dan aksesibilitas masih memiliki pemakai sehingga dipertahankan.
- Formatter dijalankan ulang dan hash seluruh enam file tidak berubah: format idempotent.
- `bazel test -c opt --platform_suffix=raii //src/app:oryn //ui:time_smoke_test --test_output=errors` berhasil. Genrule UI berjalan; consumer menghasilkan cache hit. Binary test kemudian dijalankan langsung: 1 case dan 99/99 assertion lulus.
- Aplikasi sebelum/sesudah ditangkap pada ukuran native 1064×565, posisi yang sama, software renderer, Home dan Time Standard idle. Perbandingan crop konten `(80, 0, width - 80, height - 40)` menghasilkan difference bounds `None` untuk keduanya. Ini bukti area yang dibandingkan identik, bukan klaim seluruh state/DPI/platform sudah diuji.
- Smoke native sesudah refactor: countdown satu menit, collapse, pause/resume compact, reopen memperlihatkan running 00:57, navigasi Settings, lalu tombol keluar menghasilkan exit 0.
- Pemeriksaan difokuskan pada struktur/penulisan source dan pemeliharaan perilaku desktop yang ada, bukan redesign atau audit UX lintas OS. Tampilan light mode, multi-monitor, dan DPI lain tidak diulang pada perubahan ini. Binary/tool/archive/capture sementara dihapus setelah verifikasi.


## Penyelarasan dengan skill Slint, modern C++23, dan Bazel

- Skill lokal `slint`, `modern-cpp`, `cpp-coding-standards`, serta `bazel-build-optimization` dibaca. Panduan Bazel dipakai sesuai ukuran repository: Bzlmod/pin tetap dipertahankan, bukan diganti WORKSPACE lama atau remote-cache endpoint fiktif.
- `ui/themes/theme.slint` memusatkan token warna/typography. Native Palette mengikuti Theme melalui init dan changed handler root. Bug checkbox putih pada surface terang diperbaiki; screenshot native Settings memperlihatkan label terbaca pada kedua tema.
- `ui/time-state.slint` mengekspor `TimeData`, `TimeActions`, dan enum `TimerStatus`. Forwarding Time melalui root/shell/screen dihapus; C++ memakai referensi global dan switch enum eksplisit. Mailbox bounded/single-slot serta weak-handle lifecycle dipertahankan.
- Boundary PAL mengembalikan `std::expected<void, WindowError>` dengan label operasi dan kode sistem yang tidak bergantung pada stale last-error. Owner saved DC/drawing path serta TimerService menyatakan copy/move yang dilarang secara eksplisit.
- `.bazelrc` memusatkan C++23, mode opt default, dan konfigurasi ASan terpisah. Feature default C++17 dimatikan pada MSVC; `NDEBUG` interface Windows opt diselaraskan dengan consumer. Build yang dijalankan tidak lagi menampilkan warning override standar atau C5050 tersebut. Warning opsi MASM dari Boost.Context masih muncul dan tidak disembunyikan.
- CI memperoleh job MSVC ASan pada runner Windows self-hosted yang diprovisi. Local `--config=asan --jobs=2 --platform_suffix=asan --nocache_test_results` membangun aplikasi dan meluluskan 4/4 target test; seluruh test dieksekusi. UI mencakup 2 case dan 113/113 assertion, termasuk klik checkbox light/dark dan kontras native/custom text. Headless regression memproses event-loop update, bukan mengubah Palette secara manual.
- Probe ASan terpisah dikompilasi dengan MSVC 19.51.36248 dan `/std:c++23preview /fsanitize=address`: kasus valid keluar 0; deliberate heap-buffer-overflow terdeteksi dengan diagnostic `WRITE of size 1` dan exit 91 yang dikonfigurasi hanya untuk probe. Seluruh file probe dibersihkan.
- Aplikasi instrumented benar-benar memuat `clang_rt.asan_dynamic-x86_64.dll` dari toolset 14.51.36231. Countdown, pause/resume compact, reopen, light Settings, dan exit saat timer aktif dijalankan; exit 0 tanpa diagnostic ASan. Capture ASan sempat tertutup sebagian window lain; bukti visual tema/timer penuh berasal dari smoke opt biasa.
- Smoke opt native memperlihatkan light countdown paused 00:59, compact resumed 00:58, reopen dark running 00:56, Pomodoro Focus berjalan, lalu Settings exit 0. Renderer tetap software. Sampel sesudah interaksi: working set 30.547.968 byte (29,13 MiB), private working set 7.565.312 byte; bukan benchmark idle terkontrol atau klaim refactor menghemat RAM.
- Batasan: workflow remote belum dieksekusi, SDK Slint prebuilt tidak diinstrumentasi, tidak ada klaim UBSan/TSan atau LeakSanitizer Windows, Linux/macOS, soak, suspend nyata, dan sinkronisasi IDE. Pengguna kemudian melaporkan title bar native muncul sesekali; diagnosis/fix dicatat terpisah setelah reproduksi.

## Bug title bar native saat fokus berpindah

- Laporan pengguna direproduksi persis: Settings frameless menerima `WM_NCACTIVATE` ketika fokus pindah ke helper window/aplikasi lain, lalu classic caption Windows menimpa navigasi. Synthetic `FALSE/TRUE, lParam=0` juga menghasilkan 28.928 pixel navigasi berubah; `lParam=-1` pada kontrol bersih tidak mengubah pixel.
- Menghapus `WS_CAPTION` dan memanggil `SWP_FRAMECHANGED` diuji pada proses uji saja dan gagal mengatasi repaint. Style backend tidak diubah pada perbaikan permanen; tidak menonaktifkan Windows ghosting atau memasukkan input injection.
- PAL memasang subclass pada UI thread sekali saat initial clipping, meneruskan `WM_NCACTIVATE` melalui `DefSubclassProc(..., -1)`, dan melepas subclass saat `WM_NCDESTROY`. Aktivasi tetap diproses sesuai [dokumentasi Win32](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-ncactivate); pesan lain tidak diubah. `comctl32.lib` ditambahkan sebagai dependency Win32.
- Percobaan pemasangan langsung sesudah `show()` terbukti terlalu dini (`native_window failed`, HWND belum tersedia). Jalur final `initialize_window` memakai callback clipping awal 50 ms yang sudah ada, setelah backend membuat window pada event loop; tidak ada loop retry/polling tambahan. Error tersebut tidak disembunyikan.
- Fresh binary opt `aafc03fb87b6ba3f4ba965bef7c571ff1540c86477ace82d914b12676a132f33` diuji pada desktop hidup/nonblack: synthetic activate/deactivate dan natural focus away/back menghasilkan zero changed navigation pixels. Foreground helper dan foreground Oryn diperiksa, jadi perpindahan fokus tidak diblokir. Dark/light, collapse/expand, lalu aktivasi ulang tetap bersih; screenshot hasil diperiksa.
- Native/client size tetap 1064×565, style `0x16ca0000`, exstyle `0x40118`. Region expanded tetap 784 byte dengan SHA-256 `a17f97329308fffa443c967b44ac2e2e5b5547a50e3017f27cd05826e89cec39`; collapse/expand mengembalikan hash yang sama. Settings exit menghasilkan 0 dan captured stderr kosong pada probe opt.
- Build final normal dan `--config=asan` masing-masing membangun aplikasi serta mengeksekusi/meluluskan 4/4 target test. Aplikasi ASan final memuat runtime yang cocok; countdown, pause/resume compact, reopen, light theme, dan 40 pesan aktivasi dijalankan. Navigasi tidak berubah, tidak ada native setup error/ASan diagnostic, dan exit saat timer aktif menghasilkan 0.
- Capture sempat seluruhnya hitam karena display idle; perbandingan zero-diff itu dibuang sebagai bukti tidak valid. Setelah one-shot display-idle reset (tanpa mengubah setting atau membuka autentikasi), desktop nonblack dikonfirmasi sebelum seluruh perbandingan final. Tool/harness/screenshot sementara dibersihkan setelah pemeriksaan.
- Regression nonclient paint diverifikasi lewat aplikasi desktop sebenarnya; headless Slint testing backend tidak membuat HWND dan tidak membuktikan paint nonclient Windows. Tidak ada klaim seluruh kemungkinan pesan/DPI/multi-monitor/OS telah diuji.

## Bug minimize/restore melalui taskbar

- Pengguna melaporkan tampilan mengecil/terpotong dan caption muncul setelah minimize lewat taskbar. Baseline direproduksi dengan `SC_MINIMIZE` → `SW_RESTORE`: ukuran client/native kembali 1064×565 dan region expanded tetap 784 byte/hash semula, tetapi software surface rusak. Full client redraw memulihkan isi; geometry yang benar bukan bukti repaint benar.
- Probe taskbar memakai UI Automation untuk memilih tombol berdasarkan executable-path Appid, memastikan hanya satu running window, lalu klik fisik tombol aplikasi uji. Genuine taskbar → taskbar kadang sukses, sedangkan taskbar → `SW_RESTORE` merusak baseline pada geometry identik. Tidak meminimalkan proses pengguna atau memasang callback lintas proses.
- Kandidat full-client invalidation saja melewati 54 siklus normal, tetapi smoke ASan mengungkap caption masih bisa menimpa navigasi. Menahan `WM_NCPAINT` dan menunda redraw melalui private message juga tidak menyelesaikan seluruh jalur. Kandidat/scaffold itu dihapus; hasilnya tidak dipakai sebagai bukti kandidat final.
- Root correction di `initialize_window`: bersihkan `WS_CAPTION`, lalu `SWP_FRAMECHANGED` tanpa memindah/mengubah ukuran/urutan Z. `WS_SYSMENU | WS_MINIMIZEBOX` dipertahankan; eksperimen menghapus system menu mengganggu restore taskbar dan dibatalkan. Caption removal dipadukan dengan invalidasi restore; eksperimen lama hanya menghapus caption tanpa lifecycle repaint tidak cukup. Style non-minimized menjadi `0x160a0000`, ukuran client/native dan silhouette tetap.
- Subclass menyimpan satu bit minimized pada reference data yang sudah tersedia, tanpa heap allocation per message. Setelah native/backend menerima ukuran restored/maximized, invalidasi seluruh client sekali menghindari reuse pixel iconic. `WM_NCACTIVATE` tetap diteruskan normal saat iconic. Tidak ada timer/polling/redraw kontinu atau pembatasan tombol taskbar.
- Build final normal dan ASan masing-masing membangun aplikasi serta mengeksekusi/meluluskan 3 target terkait: window, Time, dan UI smoke. Smoke aplikasi ASan final memverifikasi 5 siklus `SC_MINIMIZE`/`SW_RESTORE`: crop konten identik RGB, metrics/region identik per state; active countdown tetap berjalan dan terlihat 00:57 setelah restore. Shutdown via Settings exit 0 tanpa diagnostic ASan/native setup error.
- Probe kode/render sementara dibersihkan sesudah pemeriksaan. Headless test tidak membuat HWND; permanent test forwarding/pesan tiruan tidak ditambahkan karena tidak membuktikan paint desktop yang rusak. Verifikasi native tetap diperlukan untuk lifecycle ini.

## System tray Windows — cutover dari tombol taskbar

- Atas permintaan pengguna, Oryn menjadi notification-area/tray app. `platform::TrayIcon` non-copyable/non-movable memiliki registrasi `Shell_NotifyIconW` version 4, HICON berglyph panels Oryn dan HMENU native. HWND/Slint Window dipinjam; destructor menghapus registrasi/menu/icon/subclass tanpa menghancurkan borrowed window.
- App mempertahankan owner sepanjang event loop `RunUntilQuit`. Startup panel terlihat; klik kiri ikon toggle, keyboard selection membuka, menu native menyediakan Tampilkan Oryn/Keluar. Minimize/native close hide; Settings/tray Keluar quit. Tidak ada autostart, worker/polling tray, dependency runtime baru, atau pemaksaan pin ikon dekat jam.
- Semua hide/show memakai Slint Window, termasuk transisi extended styles. Backend Slint 1.18.1 menandai full renderer region dirty saat show karena Windows membuang pixel hidden tanpa memperbarui software buffer age. Win32-only hide/show terbukti menyisakan strip timer; kandidat itu diganti, bukan ditambal dengan resize/timer redraw.
- Saat tray terdaftar, `WM_STYLECHANGING` menjaga `WS_EX_TOOLWINDOW` dan menghapus `WS_EX_APPWINDOW`, karena Winit mengaplikasikan ulang cached styles saat show. Ini mencegah tombol taskbar muncul kembali tanpa COM/polling. Gagal registrasi memulihkan akses window biasa; Explorer TaskbarCreated memiliki jalur re-register.
- Build final normal dan ASan membangun aplikasi dan mengeksekusi/meluluskan 3/3 test terkait (window, Time, UI smoke). clang-format dry-run juga lulus pada source C++ yang diubah.
- Smoke Windows nyata dengan koordinat per-monitor-DPI-aware: startup terlihat, klik kiri hide/show, native Tampilkan Oryn, minimize hide non-iconic, serta PID tetap hidup. Final observed states memiliki **0 ordinary taskbar buttons** dan exstyle `0x198` pada seluruh tahap, termasuk setelah SDK show. Full restored panel diperiksa dari capture; countdown berlanjut dari 00:58 ke 00:47 saat tersembunyi.
- Native menu Keluar sebelumnya diklik dan proses beserta ikon hilang; exit code pada harness itu tidak tersedia. Settings Exit pada smoke instrumented sebelumnya menghasilkan 0 tanpa diagnostic ASan. Percobaan GUI ASan tambahan setelah cutover dihentikan karena harness timeout, bukan diklaim lulus. Bukti final GUI berasal dari aplikasi normal; build/test ASan final lulus.
- Batasan: Explorer restart nyata tidak dijalankan agar tidak mengganggu desktop pengguna; jalur recovery bukan klaim smoke teruji. Penempatan overflow/jam tetap kebijakan Windows. Override diagnostik `SLINT_DESTROY_WINDOW_ON_HIDE` mengubah asumsi HWND stabil dan tidak diuji. Scaffold/capture uji dibersihkan; pemeriksaan opsional dihentikan setelah pengguna meminta dipercepat.

## Navigasi Standard/Pomodoro tidak lagi membatalkan sesi

- Penyebab laporan pengguna ada pada `TimerState::select_mode`: setiap perubahan mode mereset siklus dan timer; spesifikasi/test lama memang mengunci perilaku destruktif itu. Semantik tersebut diganti, bukan menyembunyikan reset di UI.
- Standard dan Pomodoro sekarang memiliki dua record sesi berukuran tetap untuk status, durasi tersisa, deadline, dan completion flag. Seleksi hanya mengubah sesi yang ditampilkan. Kedua sesi dapat berjalan bersamaan; paused tetap paused. Fase/session progress Pomodoro tidak direset oleh navigasi atau Cancel/Reset Standard.
- Reset/Cancel hanya berlaku pada mode yang dipilih. Pomodoro yang selesai saat tab lain dipilih tetap berpindah satu fase dan menunggu Start; tidak menjalankan fase-fase yang terlewat. Configuration tetap menolak penggantian Pomodoro running/paused.
- Service tetap satu worker Asio dan satu steady_timer, menjadwalkan `next_deadline()` terdekat dari kedua sesi. Snapshot memperbarui kedua deadline, jadi completion background tidak menunggu pengguna kembali ke tab; jika tidak ada yang running, wake timer berhenti.
- Test lama yang mengharuskan mode switch membatalkan sesi dihapus/diganti dengan regresi state/deadline, dua mode running, fase tersembunyi, pause subdetik, Reset/Cancel terisolasi, serta expiry worker ketika mode yang dipilih idle. `time_test`: 16/16 case dan 151/151 assertion; UI smoke juga dieksekusi dan lulus.
- Build aplikasi normal berhasil. Smoke desktop nyata: Start Pomodoro → Standard → kembali Pomodoro memperlihatkan running Focus 1 of 4 / 24:57; mulai Standard satu menit → kembali Pomodoro tetap running 24:55; kembali Standard memperlihatkan running 00:59. Settings Exit saat kedua timer aktif menghasilkan exit 0, stderr kosong.
- Tidak ada perubahan Slint/layout/tray/renderer atau worker tambahan. Sesi belum dipersist setelah aplikasi benar-benar keluar. ASan tidak diulang untuk koreksi domain ini. Script/capture smoke sementara dibersihkan setelah pemeriksaan.

## Logo resmi Oryn

- Pengguna menetapkan `ui/assets/logo oryn.png` sebagai logo resmi. File RGBA 1254×1254 tetap utuh; turunan `oryn-icon.png` 128×128 berukuran 6.789 byte disiapkan agar icon tidak mendecode aset besar untuk kebutuhan kecil.
- Slint Window.icon memakai turunan embedded tersebut; compact memakai silhouette yang sama dengan tint foreground tema. Tray mengambil HICON window resmi melalui WM_GETICON dan memiliki salinannya lewat CopyIcon. Generator glyph panels sementara serta asset panels.svg tanpa caller dihapus.
- Build app dan 2/2 test terkait (window/UI) dieksekusi serta lulus. Smoke native mengambil icon window aktual 128×128 dan memeriksa hasil visual, Shell_NotifyIconGetRect mengonfirmasi registrasi tray (HRESULT 0), compact benar-benar memperlihatkan logo resmi, dan Settings Exit menghasilkan 0 dengan stderr kosong.
- Tidak mengubah perilaku timer/tray atau menambah dependency runtime. Ini icon window/tray saat aplikasi berjalan; resource icon Explorer pada file PE belum diubah. Script/capture sementara dibersihkan.

- Ukuran logo compact dinaikkan dari 13 ke 16 design px atas permintaan pengguna; panel tetap berukuran sama dan logo tetap center vertikal. Build serta smoke compact/reopen/Settings Exit lulus (exit 0); capture aplikasi nyata diperiksa.
