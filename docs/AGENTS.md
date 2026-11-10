# AGENTS.md — Instruksi untuk Coding Agent

Dokumen ini dirancang agar agen coding baru dapat melanjutkan proyek tanpa mengubah keputusan yang sudah ditetapkan.

## Sebelum mulai mengubah kode

1. Baca `CONTEXT.md`, `docs/10-decisions.md`, `docs/03-architecture.md`, dan spesifikasi fitur yang akan dikerjakan.
2. Tentukan pekerjaan yang diminta termasuk fitur mana, apa acceptance criteria-nya, dan API modul mana yang perlu berubah.
3. Baca `docs/11-open-questions.md`: area belum divalidasi **harus diuji**, bukan dianggap sudah tersedia.
4. Jika keputusan arsitektur perlu diubah, jelaskan alasan, dampak, migrasi, dan tulis usulan ke `docs/10-decisions.md`; jangan mengganti stack diam-diam.

## Prinsip implementasi wajib

- C++23 dengan **named modules untuk seluruh kode internal**: interface `.cppm` memakai `export module`, consumer memakai `import`, implementasi dapat tetap `.cpp` dengan `module nama;`. Jangan menambah header internal `.hpp`/`.h` atau compatibility shim. Header standard library, SDK/dependency, dan generated Slint tetap di-include; pada module unit letakkan di global module fragment (`module;`), bukan dipaksa menjadi header unit. Gunakan RAII, `std::expected` jika bermanfaat, `std::jthread`, move semantics; hindari global mutable state.
- Standar bahasa tetap **C++23**, bukan draft `c++latest`. Gunakan `-std=c++23` pada GCC/Clang dan `/std:c++23preview` pada MSVC lokal 19.51; nama switch MSVC ini sudah diuji melaporkan `_MSVC_LANG=202302L`. Jangan menggantinya dengan `/std:c++23` sebelum compiler benar-benar mendukungnya: compiler lokal mengabaikan switch tersebut dan kembali ke C++14.
- Ownership resource harus eksplisit: raw pointer/handle hanya dipinjam; resource milik aplikasi memakai RAII (`unique_ptr` + deleter atau scope owner kecil). Cleanup tidak boleh melempar. Untuk `SetWindowRgn`, lepaskan ownership C++ hanya setelah Windows menerima region. Selesaikan setup yang dapat gagal sebelum memulai worker.
- Owner RAII non-copyable/non-movable menyatakan keempat operasi copy/move sebagai `= delete`; destructor tetap menjaga cleanup tanpa exception. PAL window mengembalikan `WindowResult` (`std::expected<void, WindowError>`), bukan bool: log nama operasi dan `system_code` yang ditangkap sebelum cleanup. Nilai 0 berarti API tidak menyediakan rincian error terdokumentasi, bukan kode Win32 yang ditebak.
- Setup HWND dilakukan pada UI thread setelah backend benar-benar membuat native window, bersama callback clipping awal. PAL membersihkan `WS_CAPTION` satu kali dan menerapkan `SWP_FRAMECHANGED`; `WS_SYSMENU`, `WS_MINIMIZEBOX`, serta akses taskbar tetap dipertahankan sampai tray berhasil terdaftar. Pada window non-minimized, `WM_NCACTIVATE` memakai `lParam=-1`; saat minimized, pesan diteruskan normal. Transisi `WM_SIZE` minimized → restored/maximized menginvalidasi seluruh client sekali setelah backend memproses ukuran; bukan polling/redraw idle. Subclass dilepas pada `WM_NCDESTROY`.
- Tray Windows dimiliki `platform::TrayIcon` non-copyable/non-movable. Attach hanya setelah HWND/PAL siap; registrasi tray harus sukses sebelum tombol taskbar disembunyikan. `RunUntilQuit` menjaga event loop/timer hidup saat panel hide; Settings/tray Keluar memanggil quit eksplisit. Klik kiri toggle, keyboard selection show/focus, menu native Tampilkan/Keluar. Jangan menganggap minimize/close sama dengan keluar atau memaksa penempatan ikon di overflow/jam.
- Visibility panel tray wajib memakai `slint::Window::hide()/show()`, termasuk hide sementara saat mengganti extended style. Hide/show Win32 langsung melewati invalidasi full software renderer dan dapat meninggalkan hanya strip konten yang dirty. HWND Windows normal dipertahankan oleh Slint; `SLINT_DESTROY_WINDOW_ON_HIDE` adalah override diagnosis yang mengubah asumsi lifetime native ini.
- Ikuti `.clang-format` untuk C++ tulisan tangan: 4 spasi, batas 100 kolom, isi namespace diindentasi, blok kontrol selalu memakai `{}`, fungsi dipisahkan baris kosong, dan fungsi tidak dipadatkan menjadi satu baris. Pisahkan mapping state/UI yang panjang dari callback dan hindari ternary bertingkat yang menyulitkan pembacaan. Jangan format generated Slint/dependency. Urutan include dipertahankan, termasuk `<ostream>` sebelum doctest. Jangan menambah lapisan atau wrapper generik hanya untuk terlihat modern.
- Source Slint mengikuti formatter resmi `slint-lsp format --inline` dan `.editorconfig`: indent 4 spasi, komponen PascalCase, properti/callback kebab-case, braces pembuka di baris deklarasi, tanpa semicolon setelah elemen. Kelompokkan interface properties → callbacks → state privat → binding visual/animasi → child elements. Handler dengan kondisi atau beberapa statement ditulis multiline; forwarding sederhana boleh ringkas. Pisahkan kontrol yang punya tanggung jawab jelas, bukan setiap Rectangle. Hapus komponen yang terbukti tanpa caller, tetapi periksa binding C++ dan aksesibilitas sebelum menghapus callback.
- State/actions Time lintas view melalui global `TimeData`/`TimeActions`, bukan forwarding berlapis. Status memakai `TimerStatus` dan switch eksplisit dari enum domain C++; jangan mengandalkan urutan integer. C++ mengakses global dengan `const auto &` karena wrapper global tidak dapat disalin.
- Warna dan tipografi berada di `ui/themes/theme.slint`. Sinkronkan native `Palette.color-scheme` dari Theme saat root init dan saat scheme berubah; widget native harus terbaca pada surface tema yang sama. Headless test memproses update timer/animation setelah interaksi agar changed handler berjalan, bukan menulis Palette secara manual.
- `Slint` adalah UI. Jangan memblokir UI thread (terutama `sync_wait()`, network, DB, camera read, process wait).
- UI update harus dijadwalkan pada event loop Slint. EnTT Dispatcher punya thread ownership jelas; gunakan queue bounded untuk hasil worker lalu drain pada thread yang sesuai.
- `stdexec` bukan backend socket. Gunakan Boost.Asio/Boost.Process untuk I/O dan processes; adapter `sender` diuji lifecycle dan cancellation.
- Gunakan libcurl sebagai HTTP client; jangan memperkenalkan Boost.Beast, cpr, atau HTTP stack lain tanpa keputusan baru.
- SQLite memakai WAL dan background storage worker untuk kerja blocking. Batasi query, tambahkan indeks, transaksi, pagination.
- Gunakan OS credential store; jangan log token, request prompt, source code, diff sensitif, atau auth headers secara default.
- Fitur independen melalui interface dan event. Jangan membuat referensi silang internal antar fitur.
- PAL mengisolasi native Windows/Linux/macOS. Keanehan Wayland/macOS ditangani di adapter dengan graceful degradation.
- Tidak ada Electron, WebView, Skia, libuv atau editor kode di dalam Agents.

## Batasan fitur Agents

Agents untuk **monitoring Zed+Codex**, bukan membuat IDE, remote code editor atau full transcript viewer. Minimal tampilkan status dan aktivitas agent dari ACP bila tersedia, nama file bila disediakan, dan `Open in Zed`. Git diff statistics opsional dan harus dilabeli sebagai working tree bila tidak bisa diatribusikan.

**Urutan kerja:** ikuti [Roadmap dari mudah ke sulit](docs/09-roadmap.md). Jangan mengerjakan fitur yang lebih sulit hanya karena dokumen fiturnya bernomor lebih awal; pengecualian untuk POC blocker (Slint/Bazel dan ACP).

## Cara menyelesaikan satu task

- Tambahkan atau revisi spec di `features` jika scope berubah.
- Implementasi di modul yang relevan; tambahkan target `cc_library` atau `cc_test` Bazel sekecil mungkin.
- Standar bahasa dan mode opt default dipusatkan di `.bazelrc`; jangan menduplikasi switch standar pada setiap target. Modul Windows opt memakai `//:windows_opt` untuk menyamakan `NDEBUG` interface dan consumer pada rules_cc 0.2.26. Sanitizer hanya melalui `--config=asan`, dengan runtime MSVC yang cocok pada PATH; jangan memasukkannya ke distribusi produk.
- Unit test doctest untuk logic; integration/smoke test untuk I/O, shutdown, dan OS adapters.
- Catat hasil pengukuran bila membuat klaim RAM/CPU/startup; jangan mencantumkan hasil perkiraan sebagai benchmark nyata.
- Bila fitur tidak tersedia di OS tertentu, UI harus menjelaskan `Unavailable`/`Permission required`, bukan menampilkan data palsu.

## Prompt singkat melanjutkan proyek

> "Baca README.md, CONTEXT.md, AGENTS.md, docs/10-decisions.md, dan dokumen fitur relevan. Jangan ubah stack tanpa alasan dan persetujuan. Implementasikan langkah MVP berikutnya dari docs/09-roadmap.md; tuliskan acceptance criteria, test dan risiko lintas OS."
