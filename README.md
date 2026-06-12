# Jundot Engine

Jundot Engine is a modified distribution of [Godot Engine](https://godotengine.org).
Original Godot Engine code remains copyright (c) Godot Engine contributors and
Juan Linietsky, Ariel Manzur. Jundot-specific modifications are copyright (c)
Jundot contributors. See [LICENSE.txt](LICENSE.txt) and [COPYRIGHT.txt](COPYRIGHT.txt)
for licensing and attribution details.

<p align="center">
  <!-- <a href="https://godotengine.org">
    <img src="misc/logo/logo_outlined.svg" width="400" alt="Jundot Engine logo">
  </a> -->
</p>

## 一个 AI 辅助自动迭代的游戏引擎（基于 Godot）

**Jundot Engine** 是一个基于 [Godot Engine](https://godotengine.org) 修改的
**AI 辅助自动迭代** 游戏引擎。

它在继承 Godot 主要能力的基础上，引入 AI 辅助开发工作流：

- **缺陷发现**：通过 AI 对话分析引擎日志、崩溃报告和代码上下文，辅助定位潜在缺陷与性能问题。
- **补丁建议**：让 AI 基于上下文生成修复建议，并结合构建结果进行迭代。
- **人工审核**：AI 生成的改动仍需开发者审核、测试和合并，避免盲目引入不必要的复杂度。
- **混合扩展**：既支持 AI 辅助生成补丁，也支持开发者手动添加功能。

Jundot 的目标不是替代 Godot 上游，而是在 Godot 的基础上探索更自动化、更可审计的引擎开发流程。

## Godot 基础能力

[Godot Engine](https://godotengine.org) 是一个功能丰富、跨平台的 2D/3D 游戏引擎。
Jundot 保留 Godot 的核心架构、编辑器体验、导出流程和内置物理后端。

其中，内置的 2D/3D 物理后端仍为 Godot 上游的 **Godot Physics**：

- `modules/godot_physics_2d/`
- `modules/godot_physics_3d/`

这些模块不是 Jundot 独立实现；它们作为 Godot 原始代码的一部分继续遵循 Godot Engine 的 MIT/Expat 许可与版权声明。

## AI 驱动的迭代流程

Jundot 将 AI 作为辅助开发组件，形成可人工审核的闭环：

1. **缺陷发现**

   开发者在使用引擎时产生的错误日志、崩溃信息和用户反馈，可以被整理后输入 AI 对话系统。
   AI 负责辅助识别错误类型、提出可能原因，并生成缺陷报告。

2. **补丁与功能建议**

   对于缺陷，AI 可以尝试生成修复建议，并配合本地构建或测试结果继续迭代。
   对于功能扩展，AI 需要结合普适性、必要性、维护成本和 Godot 设计哲学进行评估。

3. **人工审核与合入**

   所有 AI 生成的补丁和新功能代码都必须经过人工审核。开发者也可以绕过 AI，直接提交手写改动。

## 免费、开源、保留归属

Jundot 继承自 Godot，因此同样采用宽松的 [MIT 协议](https://godotengine.org/license)。
用户使用 Jundot 制作的游戏完全属于用户自己。

Jundot 特有修改归 Jundot contributors 所有；Godot 原始代码仍归 Godot Engine contributors
以及 Juan Linietsky、Ariel Manzur 所有。第三方组件与例外许可详见 [COPYRIGHT.txt](COPYRIGHT.txt)。

## 获取引擎

### 二进制下载

Jundot 的官方二进制文件（包含编辑器与导出模板）请关注项目的 Releases 页面。

### 从源码编译

请参考 [Godot 官方编译文档](https://docs.godotengine.org/en/latest/engine_details/development/compiling)。
Jundot 的编译流程整体沿用 Godot 的 SCons 构建系统。

## 社区与贡献

Jundot 首先是 Godot 生态的一个派生项目，因此大部分学习资源、文档和社区知识与 Godot 共用。

有关贡献代码（包括向 Godot 上游贡献以及向 Jundot 特有模块贡献）的说明，请参阅 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 文档与示例

- Godot 官方文档：[docs.godotengine.org](https://docs.godotengine.org)
- Godot 示例项目：[godot-demo-projects](https://github.com/godotengine/godot-demo-projects)
- Jundot 发布说明：[RELEASE_NOTES_1.7.4_beta.md](RELEASE_NOTES_1.7.4_beta.md)

<!-- Godot editor screenshot removed temporarily. -->
