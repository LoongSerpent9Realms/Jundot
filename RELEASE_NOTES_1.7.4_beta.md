# Jundot Engine 1.7.4 beta — Release Notes

> **发布日期**: 2026-06-08  
> **基于**: Godot Engine 4.6.3-stable  
> **性质**: Beta 预览版  
> **官网**: [https://jundotengine.org](https://jundotengine.org)

---

## 概述

Jundot Engine 1.7.4 beta 是一个以 **AI 辅助自动迭代** 为核心特性的重大版本。本次更新在 Godot 4.6.3 之上新增了三大模块：**AI Chat 模块**（含 Skill 系统、记忆系统、沙箱），**Jundot 自研物理引擎**（2D/3D），以及 **图形化打包工具**（Jundot Package Builder）。同时合并了 Godot 上游从 4.6.3-stable 以来的多项缺陷修复。

---

## 新增模块

### AI Chat 模块 (`modules/ai_ui/`)

全新内置的 AI 交互模块，为引擎引入 AI 辅助开发能力：

| 组件 | 说明 |
|------|------|
| **AIInterface** | AI 对话接口，支持与本地/云端 AI 模型交互，可用于缺陷分析、代码建议、功能评估等场景 |
| **AISandbox** | 沙箱执行环境，在隔离环境中运行 AI 生成的代码片段，不影响主工程 |
| **MarkupUI** | 富文本标记渲染组件，用于在编辑器内展示 AI 输出的结构化内容（代码块、表格、引用等） |

核心能力：

- **Skill 系统** — 可扩展的 AI 技能框架，支持用户自定义技能（编写、审查、优化等），按需加载
- **记忆系统** — 持久化 AI 对话上下文，跨会话保留开发决策与偏好
- **自动编译集成** — 编辑器内一键触发编译，编译结果反馈给 AI 进行迭代修正
- **沙箱接口** — 安全的代码执行隔离，AI 建议的代码在沙箱中预览/验证后方可合入

### Jundot 自研物理引擎

独立的 2D 和 3D 物理引擎实现，与 Godot 内置物理引擎并行存在：

#### Jundot Physics 2D (`modules/jundot_physics_2d/`)

- 完整刚体动力学：`JundotBody2D`、`JundotArea2D`
- SAT 碰撞求解器：`JundotCollisionSolver2DSAT` — 分离轴定理精确碰撞检测
- BVH 宽相碰撞检测：`JundotBroadPhase2DBVH` — 高效空间划分
- 关节系统：支持 Pin、Hinge、Slider、Groove、DampedSpring 等约束类型
- 形状系统：Circle、Rectangle、Capsule、Segment、ConvexPolygon、ConcavePolygon、WorldBoundary
- 空间与步进：独立物理空间管理 (`JundotSpace2D`) 与步进调度 (`JundotStep2D`)

#### Jundot Physics 3D (`modules/jundot_physics_3d/`)

- 与 2D 物理引擎对称的 3D 刚体动力学体系
- 完整的碰撞检测、宽相、关节与形状系统

> 自研物理引擎可在项目设置中独立选择，与 Godot 内置引擎互不冲突。

### Jundot Package Builder (`tools/PackageBuilder/`)

图形化引擎打包工具，替代传统命令行编译流程：

- **C# WinForms GUI** — 可视化构建配置界面
- **多平台支持** — 一键构建 Windows / Linux / macOS / Android / iOS / Web 版本
- **版本管理** — 内置版本号编辑面板，自动更新 `version.py`
- **构建配置面板** — 目标平台、架构、脚本语言、并行任务数、MinGW 路径等可视化设置
- **高级选项** — 自定义 SCons 参数、输出/日志目录、包名前缀
- **构建记录** — 历史构建日志持久化记录
- **更新检查** — 内置更新检测机制
- **国际化** — I18N 模块支持多语言界面

---

## 引擎核心改进

### JundotInstance 扩展 (`core/extension/jundot_instance.*`)

新增 Jundot 实例管理器，为 GDExtension 提供统一的初始化与生命周期管理接口。

### 编辑器自动编译 (`editor/editor_node.cpp`)

编辑器内部集成一键编译能力：
- 菜单触发自动编译，无需退出编辑器
- 编译日志实时回传 AI Chat 模块
- 支持增量编译，加快迭代速度

### 物理引擎改进

- **VoxelGI + Area Light 修复**: 修复 Area Light 图集错误销毁 VoxelGI uniform set 的问题
- **OpenGL EYE_OFFSET 修复**: 修复 Compatibility 渲染器下顶点着色器编译错误
- **自定义物理引擎 2D/3D**: 全新的 Jundot Physics 实现

---

## Godot 上游缺陷修复 (自 4.6.3-stable)

### 编辑器

- 修复 `Move Up/Down` 操作对非场景节点失效的问题
- 修复 `Change Type` 忽略非顶级节点、错误编辑外部节点的问题
- 修复 `Open Documentation` 忽略非顶级节点的问题
- 修复 Blend Space 编辑器动画菜单为空的问题
- 修复桌面平台导航 Gizmo 默认启用的问题（现默认关闭）
- 修复调试器释放后崩溃的问题（`detaching freed debugger`）
- 修复编辑器中树节点拖放 `get_drop_section_at_position` 返回缺失问题

### GUI / 文本

- **RichTextLabel**: 修复使用某些 fallback 字体时末行高度异常的问题
- 修复项目中文本对齐显示问题

### 渲染

- **OpenGL (Compatibility)**: 修复 `EYE_OFFSET` 顶点着色器编译错误
- **VoxelGI**: 修复 Area Light 图集同时破坏 VoxelGI uniform set 的问题

### 构建系统

- 更新 GABE (Godot Android Build Environment) 下载地址

### 文档 / 规范

- GDScript 设计规范迁移至贡献者文档 (`CONTRIBUTING.md`)

---

## 文档更新

- **README 全面改版**: 新增 AI 驱动的引擎迭代流程说明，涵盖缺陷发现 → 补丁生成 → 人工审核的完整闭环
- **贡献指南**: 更新 Jundot 特有模块的代码贡献规范
- **构建文档**: 补充自动编译与沙箱功能的接口文档

---

## 已知问题

> 当前版本为 **beta**，以下为已知问题，将在正式版前修复：

1. AI Chat 模块的 MarkupUI 渲染器在处理极端嵌套格式时可能丢帧
2. Jundot Physics 2D 的 ConcavePolygon 碰撞在某些退化几何形状下精度不足
3. Package Builder 在非 Windows 平台上需配合 Mono 运行（Linux/macOS 支持尚在测试中）
4. AISandbox 沙箱环境对 GDScript 以外的语言（C#、C++）支持有限

---

## 获取方式

### 二进制下载

Jundot 1.7.4 beta 的预编译二进制（含编辑器和导出模板）可从以下渠道获取：

- [GitHub Releases](https://github.com/jundotengine/jundot/releases)（待发布）

### 从源码编译

```bash
git clone https://github.com/jundotengine/jundot.git
cd jundot
git checkout 1.7.4-beta
scons platform=windows target=editor
```

编译细节请参考 [Godot 官方编译文档](https://docs.godotengine.org/en/latest/engine_details/development/compiling.html)。

---

## 贡献者

- **LoongSerpent9Realms** — AI Chat 模块、Jundot Physics、Package Builder 核心开发
- **Godot Engine 贡献者** — 上游引擎及 4.6.3 缺陷修复
- **Jundot 社区** — 测试反馈与文档改进

---

## 许可

Jundot Engine 基于 [MIT License](LICENSE.txt) 开源。Godot Engine 原始代码版权归 Godot Engine 贡献者及 Juan Linietsky、Ariel Manzur 所有。Jundot 特有修改版权归 Jundot 贡献者所有。

---

*Jundot Engine — AI 驱动的自动迭代游戏引擎*
