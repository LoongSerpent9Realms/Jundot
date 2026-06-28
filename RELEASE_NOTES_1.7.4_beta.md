# Jundot Engine 1.7.4 beta - Release Notes

> **发布日期**: 2026-06-08  
> **基于**: Godot Engine 4.6.3-stable  
> **性质**: Beta 预览版  
> **官网**: [https://jundotengine.org](https://jundotengine.org)

---

## 概述

Jundot Engine 1.7.4 beta 是一个以 **AI 辅助自动迭代** 为核心特性的预览版本。本次更新在 Godot 4.6.3 之上新增了 **AI Chat 模块**（含 Skill 系统、记忆系统、沙箱）和 **图形化打包工具**（Jundot Package Builder），并保留 Godot 上游的 Godot Physics 2D/3D 后端。同时合并了 Godot 上游自 4.6.3-stable 以来的多项缺陷修复。

---

## 新增模块

### AI Chat 模块 (`editor/ai/`)

全新内置 AI 交互模块，为引擎引入 AI 辅助开发能力：

| 组件 | 说明 |
|------|------|
| **AI Chat Panel** | 编辑器内 AI 对话入口，支持与本地或云端 AI 模型交互 |
| **Skill 系统** | 可扩展的 AI 技能框架，支持用户自定义技能并按需加载 |
| **记忆系统** | 持久化 AI 对话上下文，跨会话保留开发决策与偏好 |
| **安全检查与确认** | 对 AI 工具调用和代码上传流程加入确认与安全检查 |

核心能力：

- **缺陷分析**: 结合日志、崩溃信息和代码上下文辅助定位问题
- **自动编译集成**: 编辑器内触发构建，并将结果反馈给 AI 进行迭代修正
- **工具调用可视化**: 实时显示 AI 工具执行过程，便于人工审核
- **沙箱接口**: 为后续隔离执行 AI 建议代码预留安全边界

### Godot Physics 2D/3D

本版本恢复并明确标注内置物理后端为 Godot 上游的 Godot Physics，避免误标为 Jundot 独立实现。

#### Godot Physics 2D (`modules/godot_physics_2d/`)

- 保留 Godot 上游 2D 刚体、区域、碰撞、约束和步进实现
- 注册名恢复为 `GodotPhysics2D`
- 源码类型前缀恢复为 `Godot*`

#### Godot Physics 3D (`modules/godot_physics_3d/`)

- 保留 Godot 上游 3D 刚体、区域、碰撞、关节、形状和软体实现
- 注册名恢复为 `GodotPhysics3D`
- 源码类型前缀恢复为 `Godot*`

> 说明：Jundot 对 Godot Physics 的分发和集成仍遵循 Godot Engine 的 MIT/Expat 许可与版权声明。

### Jundot Package Builder (`tools/PackageBuilder/`)

图形化引擎打包工具，替代传统命令行编译流程：

- **C# WinForms GUI**: 可视化构建配置界面
- **多平台支持**: 一键构建 Windows / Linux / macOS / Android / iOS / Web 版本
- **版本管理**: 内置版本号编辑面板，自动更新 `version.py`
- **构建配置面板**: 目标平台、架构、脚本语言、并行任务数、MinGW 路径等可视化设置
- **高级选项**: 自定义 SCons 参数、输出日志目录、包名前缀
- **构建记录**: 历史构建日志持久化记录
- **更新检测**: 内置更新检测机制
- **国际化**: I18N 模块支持多语言界面

---

## 引擎核心改进

### JundotInstance 扩展 (`core/extension/jundot_instance.*`)

新增 Jundot 实例管理器，为 GDExtension 提供统一的初始化与生命周期管理接口。

### 编辑器自动编译 (`editor/editor_node.cpp`)

- 编辑器中新增自动构建入口
- 支持后台异步构建和状态轮询
- 改进构建失败后的提示与恢复流程

### 渲染与资源修复

- **VoxelGI + Area Light 修复**: 修复 Area Light 图集错误销毁 VoxelGI uniform set 的问题
- **资源热重载**: 改进编辑器运行时的资源刷新体验

---

## Godot 上游缺陷修复

### 编辑器

- 修复 `Move Up/Down` 操作对非场景节点失效的问题
- 修复调试器释放后崩溃的问题
- 修复编辑器中树节点拖放区域返回缺失问题
- 修复项目中文本对齐显示问题

### 渲染

- **VoxelGI**: 修复 Area Light 图集同时破坏 VoxelGI uniform set 的问题
- 改进渲染资源释放时序，降低热重载场景中的崩溃风险

### 平台与构建

- 更新 GABE (Godot Android Build Environment) 下载地址
- GDScript 设计规范迁移至贡献者文档 (`CONTRIBUTING.md`)
- 改进 Windows 构建与打包流程

---

## 文档与社区

- **README 全面改版**: 新增 AI 驱动的引擎迭代流程说明，涵盖缺陷发现、补丁生成、人工审核的完整闭环
- **贡献指南**: 更新 Jundot 特有模块的代码贡献规范
- **发布说明**: 明确 Godot Physics 2D/3D 来源，避免将上游物理后端误标为 Jundot 独立实现

---

## 已知问题

> 当前版本为 **beta**，以下为已知问题，将在正式版前修复：

1. AI Chat 模块的富文本渲染器在处理极端嵌套格式时可能丢失部分样式
2. Godot Physics 2D 的 ConcavePolygon 碰撞在某些退化几何形状下精度不足
3. Package Builder 的远程发布流程仍需更多错误恢复测试
4. AI 沙箱环境对 GDScript 以外的语言（C#、C++）支持有限

---

## 获取方式

### 二进制下载

Jundot 1.7.4 beta 的预编译二进制（含编辑器和导出模板）可从以下渠道获取：

- [GitHub Releases](https://github.com/jundotengine/jundot/releases)（待发布）

### 从源码编译

```bash
scons platform=windows target=editor arch=x86_64
```

编译细节请参考 [Godot 官方编译文档](https://docs.godotengine.org/en/latest/engine_details/development/compiling.html)。

---

## 贡献者

- **LoongSerpent9Realms** - AI Chat 模块、Package Builder、Jundot 集成与发布流程
- **Godot Engine 贡献者** - 上游引擎、Godot Physics 2D/3D 及 4.6.3 缺陷修复
- **Jundot 社区** - 测试反馈与文档改进

---

## 许可

Jundot Engine 基于 [MIT License](LICENSE.txt) 开源。Godot Engine 原始代码版权归 Godot Engine 贡献者及 Juan Linietsky、Ariel Manzur 所有。Jundot 特有修改版权归 Jundot 贡献者所有。详见 [COPYRIGHT.txt](COPYRIGHT.txt)。

---

*Jundot Engine - AI 驱动的自动迭代游戏引擎*
