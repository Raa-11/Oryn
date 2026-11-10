# 05 — Async, Event-Driven, Threading & Lifecycle

## Pilihan final

- **NVIDIA stdexec**: sender/receiver dan orkestrasi task yang membutuhkan komposisi/cancellation.
- **Boost.Asio**: event loop dan I/O (socket, timer, pipe sesuai API yang dipakai).
- **Boost.Process V2**: spawn, stdio, wait dan lifecycle child process ACP.
- **libcurl**: HTTP/SSE; memakai strategi multi interface atau thread khusus yang **tidak** memblokir Slint.
- **EnTT Dispatcher**: event internal bertipe.
- **`std::jthread`**: worker owner dan stop/join.

## Thread model MVP (proposal)

| Thread | Tanggung jawab | Tidak boleh |
|---|---|---|
| Main/UI | Slint event loop, update component/view models | Blocking HTTP/SQLite/wait process |
| I/O worker | Boost.Asio `io_context.run()` | Mengubah properti Slint secara langsung |
| DB worker | SQLite access serialized writes, migrations | Mengirim notifikasi langsung ke UI |
| HTTP worker / driver | libcurl transfer, SSE chunks dan cancellation | Memanggil blocking callbacks di UI |
| Log worker | Quill internal async output | Menyimpan secret/prompt mentah |

Tidak perlu thread per request, per menu, atau per event. Gunakan satu core I/O loop dan worker hanya sesuai profil. Pemilihan strategi libcurl harus di-benchmark dan dapat digabung dengan Asio via socket/timer callbacks dengan adapter yang benar jika memang menguntungkan.

## stdexec + Boost.Asio

stdexec **bukan substitusi** Boost.Asio. Adapter diperlukan untuk mengubah operasi async callback Asio menjadi sender dengan kontrak:

- `connect/start`: operasi belum berjalan sebelum `start` jika sender memang lazy.
- `set_value`: hasil berhasil; `set_error`: error; `set_stopped`: cancelled bila operasi dan stop state mendukung.
- Cancellation dipetakan ke mekanisme Asio yang valid; bukan asumsi bahwa semua operasi mendukung stop seketika.
- Objekt lifetime: operasi tidak boleh mengakses `receiver`, buffer, socket, atau executor setelah dihancurkan.
- Completion tepat satu kali, bebas data races, berfungsi saat app shutdown.
- `sync_wait()` **tidak pernah** digunakan di Slint UI thread.

**Urutan implementasi:** POC satu timer sender dan satu async pipe, test cancel + error + race; lalu gabungkan `when_all` pada satu use case, tidak membungkus semua operasi dahulu.

## EnTT dan event queue

`EnTT Dispatcher` tidak otomatis memberi jaminan multi-thread publish yang aman untuk arsitektur ini. Tetapkan satu thread owner untuk dispatch. Dari I/O/DB/HTTP worker: tulis `DomainEvent` ke **bounded MPSC inbox**, drain dalam owner thread, coalesce status berulang, lalu jadwalkan update Slint di UI loop bila perlu.

Contoh event (nama ilustratif):

```cpp
struct AgentActivityChanged {
    SessionId session_id;
    ActivityKind activity;
    std::optional<std::string> file_path;
};
struct TimerFinished { TimerId timer_id; };
struct ChatDeltaReceived { ConversationId conversation_id; std::string delta; };
struct CalendarSyncCompleted { SourceId source; std::size_t imported; };
```

`ChatDeltaReceived` tidak harus diteruskan satu-per-token ke Slint; buffer lalu flush per frame/batch. Event yang sensitif perlu data paling sedikit; jangan menyebarkan full transcript.

## Timer

Boost.Asio steady timers + `std::chrono::steady_clock` untuk durasi; kalender/jam lokal hanya untuk penjadwalan waktu kalender. Saat sleep/resume/clock berubah, hitung ulang sisa waktu, jangan cuma bergantung pada jumlah tick timer.

## Shutdown order yang aman

1. `app` tandai stopping; matikan aksi baru dari UI dan sync source.
2. Hentikan capture webcam, hentikan timer dan HTTP streaming aktif.
3. Signal stop ke session ACP/proxy; bila perlu terminate process dengan timeout dan cleanup.
4. Batalkan operasi Asio; batasi waiting pending completion; **jangan hentikan event loop sebelum pending handlers/resource cleanup selesai**.
5. Drain queue events yang relevan, flush/commit DB, close SQLite, flush Quill.
6. Stop/join worker RAII; destroy Slint/runtime resources pada owning thread.

## Error policy

- Network: timeout, offline, TLS, rate limit, retry dengan backoff/jitter, idempotency bila diperlukan.
- Agent: disconnected, permission denied, malformed update, partial information.
- DB: busy/corrupt/disk full; pertahankan data dan UI yang aman.
- UI: tampilkan status jujur, fallback atau retry manual.

## Tests penting

- Start/stop berulang tanpa hang; 100x opening/closing island; 100x timer cancellation.
- Streaming response dicancel di tengah dan app shutdown.
- ACP process mati di tengah request.
- Event flood ribuan update dengan bounded queue/coalescing.
- Resource leak sockets/handles, deadlock, use-after-free dan thread sanitizer bila toolchain memungkinkan.
