# Cline AI Integration for Jundot

This project now includes support for [Cline](https://cline.bot/), an AI-powered coding assistant that can interact with the Jundot AI system through the MCP (Model Context Protocol).

## What is Cline?

Cline is an AI assistant for VS Code that can use tools through the Model Context Protocol (MCP). This integration allows Cline to access Jundot built-in AI tools for:

- **Project Development**: Working with game project files (scenes, scripts, resources)
- **Engine Development**: Working with engine source code (C++, SCons builds)
- **AI Configuration**: Managing Jundot AI settings

## Quick Start

### 1. Prerequisites

- **Node.js 18+** - Required for the MCP server
- **Jundot Editor** - Must be running with External API enabled
- **Cline for VS Code** - Install from VS Code Marketplace

### 2. Installation

#### Windows
`cmd
.cline\setup.bat
`

#### Linux/macOS
`ash
chmod +x .cline/setup.sh
.cline/setup.sh
`

Or manually:
`ash
cd .cline/jundot-mcp-server
npm install
`

### 3. Configure Jundot Editor

1. Start the Jundot Editor
2. Open the **AI Config** panel
3. Enable **External MCP HTTP API server**
4. Note the port (default: 8080)

### 4. Configure Cline

The MCP configuration is already set up in .cline/mcp.json. Cline will automatically load this configuration when you open the project.

### 5. Start Using Cline

1. Open Cline in VS Code
2. Cline will connect to the Jundot MCP server
3. You can now ask Cline to help with Jundot development!

## Available Tools

### Project Mode Tools
- list_files, read_files, write_file, edit_file, search_files, grep_code
- check_project_scripts, check_ui_layout, build_project, play_scene, package_project

### Engine Mode Tools
- All project tools plus: run_build, check_build_status, read_build_log, restart_engine, return_to_project_mode

### AI Settings Tools
- ai_settings.get_config, ai_settings.update_config, ai_settings.reset_config

## Documentation

- .cline/README.md - Detailed setup and usage guide
- doc/ai_assistant_memory_tools.md - AI memory system
- doc/ai_self_repair_publish_flow.md - AI-driven development workflow

## Resources

- [Jundot Engine Source](https://github.com/LoongSerpent9Realms/Jundot.git)
- [MiMoCode Plugin](https://github.com/LoongSerpent9Realms/MiMo-Code-jundot)
- [Cline Documentation](https://docs.cline.bot/)
