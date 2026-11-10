# 12 — Rujukan Resmi Teknis

Diperiksa sebagai referensi awal **24 Oktober 2026**. Referensi bukan bukti bahwa seluruh kombinasi build telah berhasil di proyek ini.

## UI dan build

- Slint C++ AOT API: https://docs.slint.dev/latest/docs/cpp/
- Slint Winit backend & renderer list (default Oryn: `winit-software`): https://docs.slint.dev/latest/docs/slint/guide/backends-and-renderers/backend_winit/
- Slint renderer overview: https://docs.slint.dev/latest/docs/slint/guide/backends-and-renderers/backends_and_renderers/
- Slint desktop royalty-free license FAQ (atribusi): https://github.com/slint-ui/slint/blob/master/FAQ.md
- Bazel: https://bazel.build/
- Bazel Central Registry: https://registry.bazel.build/

## Modern C++ async / HTTP / DB

- NVIDIA stdexec: https://github.com/NVIDIA/stdexec
- Boost.Asio: https://www.boost.org/doc/libs/latest/doc/html/boost_asio.html
- Boost.Process V2: https://www.boost.org/doc/libs/latest/libs/process/doc/html/index.html
- libcurl: https://curl.se/libcurl/
- SQLite WAL: https://sqlite.org/wal.html
- EnTT: https://github.com/skypjack/entt
- Glaze: https://github.com/stephenberry/glaze
- Quill: https://github.com/odygrd/quill
- doctest: https://github.com/doctest/doctest

## Agent integration & Zed

- Zed External Agents / custom ACP agents: https://zed.dev/docs/ai/external-agents
- Zed CLI open file/project: https://zed.dev/docs/reference/cli
- ACP Tool Calls (`session/update` fields, optional locations): https://github.com/agentclientprotocol/agent-client-protocol/blob/main/docs/protocol/v2/tool-calls.mdx
- ACP full protocol schema: https://github.com/agentclientprotocol/agent-client-protocol/blob/main/docs/protocol/v2/schema.mdx

## Platform & integrations

- Microsoft Graph overview: https://learn.microsoft.com/en-us/graph/overview
- GitLab REST API: https://docs.gitlab.com/api/rest/
- Windows GSMTC: https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssessionmanager
- MPRIS D-Bus: https://specifications.freedesktop.org/mpris-spec/latest/
- Apple AVFoundation: https://developer.apple.com/av-foundation/

## Catatan

- Cek versi dan opsi dependency pin saat implementasi; beberapa tautan `/latest/` berubah dari waktu ke waktu.
- Status ACP/Zed version compatibility harus diuji pada versi aplikasi yang dipakai; jangan langsung menyimpulkan seluruh event tersedia.
- Mac media system-wide integrations belum ada keputusan final; gunakan PAL capability detection.
