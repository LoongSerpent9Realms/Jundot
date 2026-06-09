# AI Assistant Memory and Tool Context

The editor AI assistant stores project-scoped context in the editor project settings directory. These files are not scene resources and are intended for editor-only assistant behavior.

## Files

- `ai_memory.json`: project memories used as optional AI context.
- `ai_tools.json`: skill and MCP server registry entries used as optional AI context.

Both files use a root `schema_version` field. Version `1` is the initial schema.

## Memory Schema

```json
{
  "schema_version": 1,
  "entries": [
    {
      "id": "memory-...",
      "title": "Architecture note",
      "content": "Short project fact or recurring issue.",
      "tags": ["architecture"],
      "enabled": true,
      "created_at": "2026-06-08T12:00:00",
      "updated_at": "2026-06-08T12:00:00"
    }
  ]
}
```

Enabled memory entries are sorted by `updated_at` descending, then by title, before `AIContextBuilder` injects them into the system prompt.

## Tool Registry Schema

```json
{
  "schema_version": 1,
  "skills": [
    {
      "id": "skill-...",
      "name": "Plan Review",
      "description": "Reviews a plan before implementation.",
      "prompt_text": "Use this skill when...",
      "permission_level": "read",
      "enabled": true,
      "writes": false,
      "requires_confirmation": true,
      "read_only_allowed": true,
      "created_at": "2026-06-08T12:00:00",
      "updated_at": "2026-06-08T12:00:00"
    }
  ],
  "mcp_servers": [
    {
      "id": "mcp-...",
      "name": "Local Docs",
      "command": "node",
      "arguments": "server.js",
      "url": "",
      "capabilities_json": "{\"tools\":[]}",
      "enabled": true,
      "requires_confirmation": true,
      "writes": false,
      "read_only_allowed": true,
      "created_at": "2026-06-08T12:00:00",
      "updated_at": "2026-06-08T12:00:00"
    }
  ]
}
```

The first implementation treats skills and MCP servers as context only. The editor does not start MCP processes, call tools, or execute skills from model output. Permission fields are persisted now so a later execution layer can route actions through an explicit confirmation or sandbox boundary.

## AI Auto-Suggest

When the "Allow AI to suggest Skill/MCP/Memory entries" setting is enabled, the system prompt includes instructions for the AI to output structured suggestion blocks in its responses. These blocks are parsed and shown as confirmation cards below the AI message. The user must explicitly accept each suggestion before it is saved.

### Suggestion Block Formats

The AI may output suggestions using HTML comment markers (preferred) or JSON code blocks (fallback).

**Skill suggestion (comment markers):**

```html
<!-- SKILL -->
NAME: Code Review
DESCRIPTION: Reviews code changes before commit.
PROMPT: Use this skill when the user asks to review code...
PERMISSION: read
WRITES: no
CONFIRMATION: required
<!-- END_SKILL -->
```

**MCP server suggestion (comment markers):**

```html
<!-- MCP -->
NAME: Local Docs Server
COMMAND: node
ARGS: docs-server.js
URL:
CAPABILITIES: {"tools":[]}
<!-- END_MCP -->
```

**Memory suggestion (comment markers):**

```html
<!-- MEMORY -->
TITLE: Project Architecture
TAGS: architecture, guidelines
CONTENT: This project uses a layered architecture...
<!-- END_MEMORY -->
```

**JSON fallback format:**

````markdown
```json
{
  "type": "skill",
  "name": "Code Review",
  "description": "Reviews code changes before commit.",
  "prompt_text": "Use this skill when...",
  "permission_level": "read"
}
```
````

The `"type"` field must be `"skill"`, `"mcp"`, or `"memory"`.

### Confirmation Flow

1. The AI response is parsed for suggestion blocks after each chat completion.
2. Parsed suggestions appear as cards below the message with accept and reject buttons.
3. When two or more suggestions are present, "Add All" and "Dismiss All" buttons appear.
4. Accepted suggestions are written to `ai_tools.json` or `ai_memory.json` immediately.
5. Rejected suggestions are discarded.

### Settings

- **Allow AI to suggest Skill/MCP/Memory entries** (default: on) — Controls whether suggestion instructions are injected into the system prompt. When off, the AI will not output suggestion blocks and any blocks that do appear are ignored.

## Local File Import

Skill, MCP, and Memory entries can be imported from local files or directories via the "Import..." button in the Chat, Memory, or Tools panels.

### Supported File Formats

| Format | Detection | Parsed As |
|---|---|---|
| WorkBuddy SKILL.md | Filename contains "skill" and ends `.md` | Skill entry (frontmatter + body) |
| MCP config JSON | Filename contains "mcp" and ends `.json`, or any `.json` | MCP server entries |
| Text/Markdown | Any `.md` or `.txt` not matching above | Memory entry (filename as title) |

### SKILL.md Format

WorkBuddy skill files use YAML frontmatter:

```markdown
---
title: Code Review
summary: Reviews code changes before commit.
---

Use this skill when the user asks to review code...
```

- `title` or `name` → skill name
- `summary` or `description` → skill description
- Body after frontmatter → `prompt_text`
- If no frontmatter is present, the first `# Heading` is used as the name

### MCP JSON Format

Standard MCP configuration files are supported:

```json
{
  "mcpServers": {
    "docs-server": {
      "command": "node",
      "args": ["docs-server.js"],
      "env": { "PORT": "3000" }
    }
  }
}
```

An alternative array format is also supported:

```json
{
  "servers": [
    { "name": "docs-server", "command": "node", "arguments": "docs-server.js" }
  ]
}
```

### Limits

- Maximum single file size: 64 KB
- Maximum entries per import operation: 50
- Directory import scans recursively into subdirectories
