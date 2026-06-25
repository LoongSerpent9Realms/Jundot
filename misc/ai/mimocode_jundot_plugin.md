# MiMoCode jundot AI Plugin

This document defines the editor-facing contract for replacing the built-in AI backend with a jundot AI plugin. The default plugin id is `mimocode`.

## Editor Contract

The editor only talks to the jundot AI plugin gateway. MiMoCode may run Bun, a local server, or any other runtime internally, but those details stay behind the plugin boundary.

When MiMoCode is selected in the AI settings panel, the editor exposes a download action that opens the packaged plugin release page:

```text
https://github.com/LoongSerpent9Realms/MiMo-Code-jundot/releases/latest
```

Default gateway:

```text
POST http://127.0.0.1:4096/ai/chat
```

Request body:

```json
{
  "plugin_id": "mimocode",
  "action": "send_message",
  "messages": [
    { "role": "user", "content": "..." }
  ],
  "tools": [],
  "context_mode": "project",
  "output_language": "auto"
}
```

Response body may be minimal:

```json
{
  "content": "Assistant response text",
  "finish_reason": "stop"
}
```

Or OpenAI-compatible for tool calls:

```json
{
  "content": "",
  "openai_compatible": {
    "choices": [
      {
        "finish_reason": "tool_calls",
        "message": {
          "role": "assistant",
          "content": "",
          "tool_calls": []
        }
      }
    ]
  }
}
```

## Runtime Boundary

- The editor does not start `mimo serve` directly.
- The jundot plugin owns MiMoCode process/session lifecycle.
- High-permission operations must be routed back through editor-controlled tool interfaces.
- Provider, model, memory, tasks, subagents, and goals belong to MiMoCode plugin configuration.

## Legacy Path

The old OpenAI-compatible direct backend remains available as `legacy_openai` during migration. It should not be the default path for new AI sessions.
