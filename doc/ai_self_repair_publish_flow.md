# AI 对话驱动的引擎修复、热更新编译与发布上传闭环

## 概述

Jundot 编辑器内置 AI 助手不再是简单的问答工具 — 它已升级为一条**受控工程闭环**：从 AI 发现引擎缺陷/性能瓶颈/功能缺失开始，到代码定位、补丁生成、自动测试验证、增量编译打包、变更评估、直至 GitHub Releases 发布上传。

关键约束：高风险动作必须可追踪、可回滚、可确认。

## 快速开始

### 前置条件

1. 在编辑器 `AI Config` 面板配置 API key（OpenAI-compatible 服务）
2. 首次发送 AI 消息时，同意 AI 使用协议（含 token 成本说明）
3. 确保项目根目录有 `version.py` 和 git 仓库

### 缺陷修复闭环

```
用户描述问题 → AI 识别为缺陷
                │
                ▼
         解析 REPAIR_TASK 块
                │
                ▼
         展示修复任务卡片
       (候选文件 / 补丁摘要 / 测试命令 / 风险)
                │
        ┌───────┴────────┐
        ▼                 ▼
   [Apply Patch]      [Skip]
        │
        ▼
   Dirty worktree 检查
   (保护未提交文件)
        │
        ▼
   记录补丁前快照
        │
        ▼
   [Run Tests]
   (AI 建议或默认测试)
        │
   ┌────┴────┐
   ▼          ▼
 通过        失败
   │          │
   │          ▼
   │     [Ask AI]
   │     (回传错误到对话)
   │
   ▼
 [Build] → 启动 PackageBuilder
            │
            ▼
      增量 SCons 编译 + 打包
            │
            ▼
   生成 zip + update-manifest.json
            │
            ▼
 [Publish] → 上传 GitHub Releases
             (需 GITHUB_TOKEN 环境变量)
```

### 功能扩充准入

```
用户提出功能需求 → AI 识别为功能扩充
                     │
                     ▼
              FEATURE_GATE 评估:
              1. 普适性 ≥ 70%?
              2. 必要性 ≥ 0.7?
              3. 是否违反 Jundot 设计哲学?
                     │
              ┌──────┴──────┐
              ▼              ▼
           通过             不通过
              │              │
              ▼              ▼
        "建议加入"      "不建议加入"
                        + 替代工作流建议
```

## 文件清单

### 新增 C++ 文件 (editor/ai/)

| 文件 | 职责 |
|------|------|
| `ai_repair_workflow.h/cpp` | 修复任务状态机、持久化、dirty worktree 保护、测试运行 |
| `ai_repair_card.h/cpp` | Chat 中的修复任务 UI 卡片（定位文件/打补丁/测试/发布） |
| `ai_build_bridge.h/cpp` | AI 面板 ↔ PackageBuilder 构建桥接（触发构建、读取结果） |

### 新增 C# 文件 (tools/PackageBuilder/)

| 文件 | 职责 |
|------|------|
| `ChangeEvaluator.cs` | 双报告：源码变更 (git diff) + 发布包文件 (manifest) |
| `GitHubReleasePublisher.cs` | GitHub Releases API 上传 zip/manifest，回填 download_url |
| `PublishConfig.cs` | 发布配置（仓库、token 环境变量、灰度比例、draft/正式） |

### 修改文件

| 文件 | 改动 |
|------|------|
| `editor/ai/ai_chat_panel.h/cpp` | 集成修复卡、dirty worktree 检查、测试运行、AI 重试、构建触发 |
| `editor/ai/ai_repair_workflow.h/cpp` | 添加 dirty worktree 保护、预补丁快照、默认测试建议、测试运行器 |
| `tools/Launcher/UpdateOrchestrator.cs` | 默认 GitHub Releases manifest URL |

## 安全边界

### 不自动做的事情

- **不自动发布到公网**：即使所有测试通过，上传也需要用户明确点击 [Publish]
- **不绕过 dirty worktree 保护**：补丁只会触碰任务声明的文件，非任务 dirty 文件会被警告
- **不跳过功能准入**：任何功能扩充必须先通过普适性/必要性/设计哲学三重检查
- **不存储 token**：GitHub token 只从环境变量 `GITHUB_TOKEN` 读取，不写入配置文件或日志

### Token 成本说明

- 每次 AI 对话都会消耗 API token
- 附加上下文（项目文件、日志、测试输出）会增加 token 消耗
- 修复分析可能触发多轮请求
- 可在 Config 面板随时关闭自动建议/自动修复分析

### AI 使用协议

首次使用 AI 功能时会弹出《AI 使用协议》，说明：
1. AI 对话会消耗 API token
2. 上下文注入、多轮分析会增加消耗
3. 可以随时关闭相关能力

协议版本升级时，会重新征询同意。

## 故障排查

| 问题 | 可能原因 | 解决方案 |
|------|----------|----------|
| AI 未生成修复任务 | 对话内容未被分类为缺陷/性能瓶颈 | 明确描述问题、复现步骤、候选影响范围 |
| "Dirty worktree detected" | 有其他文件未提交 | 先提交或暂存不相关文件 |
| 测试无命令 | AI 未指定测试命令 | 系统根据文件类型自动选择默认测试 |
| PackageBuilder 启动失败 | 未编译或路径不匹配 | 手动运行工具：`tools/PackageBuilder/bin/Release/net8.0-windows/JundotPackageBuilder.exe` |
| 发布失败 | 缺少 GITHUB_TOKEN | 设置环境变量或在发布前使用 dry-run 模式 |
| Launcher 无法获取更新 | 远程 manifest URL 不可达 | 使用 `--manifest-url` 手动指定 manifest |

## 开发环境

- **C++ 编译**: `scons platform=windows target=editor dev_build=yes accesskit=no d3d12=no angle=no -j4`
- **C# PackageBuilder**: `dotnet build tools/PackageBuilder/PackageBuilder.csproj -c Release`
- **C# Launcher**: `dotnet build tools/Launcher/Launcher.csproj -c Release`
