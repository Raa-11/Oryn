# Feature 06 — Agents (Zed + Codex Live Activity)

**Status:** user sudah memilih desain UI ringkas. Jalur pengambilan ACP events masih **POC kritis**.

## Visi fitur

Ini adalah *mini live-activity monitor* untuk **coding agent Codex yang bekerja di Zed**. Tujuannya supaya user sekilas tahu: **Codex sedang apa, file apa yang sedang disentuh, tugasnya apa, dan apakah selesai/menunggu bantuan**. Jangan membuat IDE kedua.

## Desain UI final (dipilih pengguna)

**Collapsed:** `Codex · Editing service.cpp` atau `Codex · Working` bila file tidak diketahui.

**Expanded:** `Codex` + status badge, `Zed · project`, **Current Task**, **Editing/Reading/Executing**, **active file**, opsional `3 files changed / +82 / -17`, `Open in Zed`.

> Semua deskripsi, file, dan angka pada contoh adalah **placeholder desain**. Data real harus datang dari ACP/Git. Bukan daftar tetap `Read → Edit → Bash → Done`.

## Scope MVP

- Monitor satu atau lebih Zed External Agent sessions yang berhasil diobservasi, default Codex.
- Status observasi: `Idle`, `Thinking` (jika report), `Reading`, `Editing`, `Executing`, `Completed`, `Failed`, `Waiting for permission`, `Disconnected`, `Unknown`.
- Last activity title/kind/status dari ACP tool events, dengan time last seen.
- Active file/path opsional, hanya kalau ACP location atau tool metadata dapat dipercaya tersedia.
- Current task title jika ACP session title/prompt metadata yang relevan memang tersedia; jika tidak, tampilkan `Working on current task` tanpa mengarang detail.
- Compact notification saat selesai/perlu perhatian, controlled by settings.
- `Open in Zed` membuka project/path menggunakan Zed CLI jika tersedia.
- Data activities ketika panel collapse tetap diterima bila monitor aktif dan user mengizinkan.
- Statistik Git working tree opsional, bukan blocker MVP.

## Integrasi ACP: arsitektur calon

```text
 Zed (ACP Client)
       ⇅ JSON-RPC/stdio
 ACP Proxy (custom external agent configured in Zed)
       ⇅ JSON-RPC/stdio
 Codex ACP Agent (actual agent process)

 ACP Proxy  --observed session/update--> Agents feature
                                          │
                                  EnTT bounded inbox
                                          │
                                 Slint island view
```

**Syarat penting:** Proxy tidak bisa begitu saja menyadap agent bawaan registry. Perlu konfigurasi Zed agar memakai **custom external agent command** yang meluncurkan proxy, lalu proxy meluncurkan implementasi Codex ACP yang kompatibel. Kompatibilitas protokol, auth, permissions, terminal/stdio dan versi agent harus dibuktikan. Jangan menyebut integrasi selesai sebelum POC berhasil.

## ACP event model

- ACP `session/update` dapat membawa `tool_call` / `tool_call_update` dengan `title`, `kind`, `status`, dan `locations` jika disediakan. Kind dapat termasuk read/edit/execute/think dan lainnya; tool/status berubah sewaktu-waktu.
- Events upsert keyed by toolCallId; tidak boleh menggandakan aktivitas ketika update datang berulang.
- Location optional. Name/path yang kosong jangan ditebak dari pesan AI biasa.
- Alur permission harus **diteruskan tanpa mengubah keputusan Zed/user**; jangan implement auto approve.
- Jika update tidak dikirim karena agent behavior, tampilkan unknown state atau last-seen info.

## Model state domain (konseptual)

```text
AgentSession {
  session_id, agent_name, project_root?, status,
  current_task_title?, active_file?, activity_kind?,
  last_activity_title?, last_observed_at, connected,
  git_worktree_stats?  // OPTIONAL, never assume agent attribution
}
```

Per-session memory bounded; latest activity cache, tidak perlu menyimpan full ACP transcript. Default SQLite hanya metadata ringkas dengan retention policy.

## Statistik file/diff

- Jika Git repository ada, gunakan Git operations/read-only untuk merangkum **working tree**: file changed, additions, deletions.
- Perhatikan staged/unstaged/untracked, rename, binary files dan merge conflicts; Git diff default tidak selalu mencakup untracked files.
- Jangan melabeli hasil sebagai `Codex changed` tanpa bukti per-agent; user dapat mengedit bersamaan.
- Perhitungan dilakukan on-demand/throttled, bukan setiap token.

## Open in Zed

- Gunakan Zed CLI `zed path/to/project` atau `zed path/to/file:line` setelah memastikan path valid.
- CLI dapat berbeda menurut OS, `zed`/`zeditor`/path full; PAL membantu resolve executable.
- Tidak membuat local diff viewer. Detail code review dan editing tetap Zed.

## Error, lifecycle dan performance

- ACP process failure -> session `Disconnected`, relay error ke log sanitized, opsi reconnection sesuai alur Zed.
- Tidak capture source code/rawInput/rawOutput tanpa kebutuhan dan izin.
- Events burst -> queue bounded, per-session coalescing, UI ≤ perubahan yang terlihat.
- Saat app tutup, jangan meninggalkan proxy orphan; tentukan apakah proxy mati menyebabkan Codex juga berhenti (perilaku fatal untuk pengguna). Pastikan lifecycle dipahami sebelum release.
- Jika user tidak mengaktifkan monitoring, jangan intercept ACP.

## Acceptance criteria (harus diuji dengan Codex nyata)

1. User mengaktifkan ACP custom-agent/proxy dan Zed dapat menggunakan Codex seperti biasanya.
2. Panel menampilkan real-time tool activity yang benar (read/edit/execute bila dikirim).
3. Active file hanya tampil jika sumber valid; status fallback benar bila tidak ada.
4. Session selesai/putus terlihat, tidak ada dummy Working yang terus hidup.
5. Klik `Open in Zed` membuka project atau file yang sesuai.
6. Permission handling tidak berubah dan tidak disetujui otomatis.
7. Monitoring tidak memblokir Zed maupun membuat CPU idle tinggi.

## Out of scope

Editor/terminal/diff viewer, full chat assistant untuk coding, agent orchestration multi-model, automated task assignment, persentase kemajuan palsu, atau intercept tanpa jalur ACP yang didukung.

Rujukan: [Zed External Agents](https://zed.dev/docs/ai/external-agents), [ACP tool calls](https://github.com/agentclientprotocol/agent-client-protocol/blob/main/docs/protocol/v2/tool-calls.mdx), [Zed CLI](https://zed.dev/docs/reference/cli).
