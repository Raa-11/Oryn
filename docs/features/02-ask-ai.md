# Feature 02 — Ask AI (General Chat)

**Status:** fitur produk dipilih; OpenAI provider pertama; Claude/Gemini nanti.

## Goal

Chat assistant umum dari floating panel kecil dengan respons streaming dan history lokal. Ini **bukan** coding agent, dan tidak terhubung otomatis dengan Zed.

## MVP user flow

1. User buka Ask AI, buat atau pilih conversation.
2. User ketik pesan, klik Send.
3. UI menampilkan streaming text dari OpenAI API secara bertahap.
4. User dapat Stop ketika sedang menerima respons.
5. History tetap tersimpan di SQLite. User dapat buka ulang, rename/delete conversation.
6. Kesalahan authentication, network, rate limit, dan quota muncul dengan pesan yang tidak membocorkan API key.

## Scope provider

- Interface `IAIProvider`: `send/stream`, cancel, model info, usage metadata bila tersedia.
- Implementasi MVP `OpenAIProvider` memakai HTTP libcurl + SSE decoder + Glaze.
- Arsitektur disiapkan untuk `AnthropicProvider` dan `GeminiProvider`, tetapi **tidak perlu diimplementasikan sebelum diminta**.
- API credential/OpenAI Billing bisa terpisah dari ChatGPT Plus; auth mode dikonfirmasi sesuai provider/docs, jangan mengasumsikan akun Plus otomatis memberi API gratis.

## UI / large content

- View chat ringan; messages terakhir paginated (~30–50 bubble).
- Tidak render 200.000 karakter dalam satu Slint Text.
- Pesan >15–30 baris dapat collapsed preview; full content tetap disimpan.
- Long pasted code boleh sebagai attachment/card dengan viewer lazy; virtualized line display bila dibutuhkan.
- Markdown render subset awal sesuai kemampuan Slint, bukan browser HTML/WebView; prioritaskan readability.
- Streaming deltas dibatch agar UI tidak diupdate tiap token.

## Storage

- `conversations`, `messages` SQLite dengan urutan timestamp + id untuk pagination.
- `ai_usage` menyimpan token reported jika tersedia, atau `null` bila tidak.
- Rahasia provider disimpan OS credential vault, bukan di DB.
- Chat history dan konteks API dipisah: policy truncation/summarization boleh menjadi fase lanjut.

## Async and errors

- `ChatController` memanggil provider; `HttpClient` libcurl multi/worker handle HTTPS; `SseDecoder` mem-parsing frame lengkap; Glaze memetakan typed fields.
- User cancel harus menghentikan transfer; partial message ditandai interrupted.
- UI thread tidak menunggu network/SQL.
- Retry request non-idempotent harus mempertimbangkan risiko jawaban terduplikasi.

## Domain events (contoh)

`ChatStarted`, `ChatDeltaBuffered`, `ChatCompleted`, `ChatFailed`, `ConversationDeleted`, `UsageReceived`. Payload ringkas dan no auth/raw secret.

## Acceptance criteria

- OpenAI stream berhasil; cancel bekerja tanpa hang.
- History persisten setelah restart dan dapat dihapus.
- Chat panjang tidak menyebabkan RAM meningkat tak terkendali hanya karena seluruh bubble dirender.
- 401/429/offline/error jelas; key tak ada pada Quill logs.
- Integrasi Ask AI tidak mengubah state Agents/Zed.

## Non-goals

Coding agent, shell execution, filesystem code edits, embeddings/vector DB, multi-provider launch serentak, browser-like Markdown renderer.
