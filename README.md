# Jundot Engine

<p align="center">
  <a href="https://godotengine.org">
    <img src="misc/logo/logo_outlined.svg" width="400" alt="Jundot Engine logo">
  </a>
</p>

## 一个 AI 辅助自动迭代的游戏引擎（基于 Godot）

**Jundot Engine** 是一个基于 [Godot Engine](https://godotengine.org) 修改的 **AI 辅助自动迭代** 游戏引擎。  
它在继承 Godot 所有特性的基础上，引入了 **AI 驱动的迭代机制**：

- **缺陷发现**：通过 AI 对话（例如与 ChatGPT、Copilot 等交互）自动分析引擎日志、崩溃报告、代码模式，发现潜在缺陷与性能瓶颈。
- **功能扩充**：通过问询 AI 来评估每一个新功能的 **普适性**（是否被多数游戏场景需要）和 **必要性**（是否值得加入核心引擎），从而智能决策是否扩充该功能。
- **混合扩充**：既支持 AI 自动生成并合并补丁，也支持开发者手动添加功能，人工与 AI 协同工作。

所有改动均源自 Godot 原始代码库，并在其之上增加了自动化工作流与 AI 决策层。

## 2D 与 3D 跨平台游戏引擎

**[Godot Engine](https://godotengine.org) 是一个功能丰富、跨平台的游戏引擎，可在统一界面中制作 2D 和 3D 游戏。**  
它提供了一整套[常用工具](https://godotengine.org/features)，让用户专注于制作游戏而无需重复造轮子。游戏可以一键导出到多种平台，包括主流桌面平台（Linux、macOS、Windows）、移动平台（Android、iOS）、Web 平台以及[游戏主机](https://godotengine.org/consoles)。

**Jundot 在 Godot 的基础上增加了 AI 自动迭代能力**，使引擎能够：
- 在开发过程中自动编译、热重载并运行测试
- 通过 AI 对话主动发现引擎缺陷
- 智能评估新功能的普适性与必要性，辅助决策是否合入主线

## AI 驱动的引擎迭代流程

Jundot 将 AI 作为“自动迭代”的核心组件，形成闭环：

1. **缺陷发现**  
   开发者或玩家在使用引擎时产生的错误日志、性能数据、用户反馈，会被结构化后输入 AI 对话系统。AI 负责：
   - 识别错误类型（内存泄漏、渲染错误、脚本异常等）
   - 判断是否为已知缺陷，或新出现的模式
   - 生成缺陷报告，并标注严重等级

2. **补丁与功能建议**  
   对于缺陷，AI 可以尝试生成修复补丁，并自动运行测试套件验证。  
   对于功能扩充，AI 会根据以下维度评估：
   - **普适性**：该功能是否被至少 70% 的游戏类型需要？（示例阈值，可配置）
   - **必要性**：现有工作流能否绕过？绕过成本是否过高？  
   只有同时满足阈值且与 Godot 设计哲学不冲突时，AI 才会将功能标记为“建议加入”。

3. **人工审核与扩充**  
   所有 AI 生成的补丁和新功能代码都必须经过人工审核（Pull Request 机制）。  
   同时，开发者也可以绕过 AI，直接手动提交扩充代码，实现 **人机混合迭代**。

这种流程既保证了迭代速度，又避免了 AI 盲目添加不必要的功能，使引擎始终保持专注和高质量。

## 免费、开源、社区驱动

Jundot 继承自 Godot，因此同样采用非常宽松的 [MIT 协议](https://godotengine.org/license)，完全免费且开源。  
无任何附加条款、版税或隐藏费用。用户的游戏完全属于用户自己，连引擎的最后一行代码都是透明的。  
Jundot 的持续开发保持独立且社区驱动，支持来自 [Godot Foundation](https://godot.foundation/) 的非营利性组织。

在 [2014 年 2 月](https://github.com/godotengine/godot/commit/0b806ee0fc9097fa7bda7ac0109191c9c5e0a1ac) 开源之前，Godot 由 [Juan Linietsky](https://github.com/reduz) 和 [Ariel Manzur](https://github.com/punto-) 作为内部引擎开发多年，并用于发布多款商业作品。  
Jundot 在此基础上增加了 AI 自动迭代特性，使其更适配快速原型开发、实验性项目以及长期维护的大型游戏。

**Jundot 的 AI 迭代模块本身也是开源的**，使用的模型接口可插拔（支持本地模型或云 API），确保用户对数据和决策过程拥有完全控制权。

![Screenshot of a 3D scene in the Godot Engine editor](https://raw.githubusercontent.com/godotengine/godot-design/master/screenshots/editor_tps_demo_1920x1080.jpg)

## 获取引擎

### 二进制下载

Jundot 的官方二进制文件（包含编辑器与导出模板）请关注 [项目的 Releases 页面]（请根据实际情况填写链接）。  
你也可以直接使用 Godot 官方二进制，但 AI 自动迭代功能需要 Jundot 定制版才支持。

### 从源码编译

请参考 [Godot 官方编译文档](https://docs.godotengine.org/en/latest/engine_details/development/compiling)。  
Jundot 的编译步骤与 Godot 完全一致，只需在编译时开启 AI 迭代相关的宏（详见 `docs/auto_iteration.md`）。

## 社区与贡献

Jundot 首先是 Godot 生态的一部分，因此大部分社区资源与 Godot 共用。  
主要社区渠道请查看 [Godot 官网](https://godotengine.org/community)。

如果你想讨论 Jundot 特有的 AI 自动迭代功能，可以加入 [Jundot 讨论区]（链接待补充）。  
有关贡献代码（包括向 Godot 上游贡献以及向 Jundot 特有模块贡献）的说明，请参阅 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 文档与示例

- Godot 官方文档（包含类参考）托管在 [Read the Docs](https://docs.godotengine.org)，由社区维护。
- Jundot AI 自动迭代机制的详细说明请查看 `docs/auto_iteration.md`。
- 官方示例项目请参考 [Godot Demo Projects](https://github.com/godotengine/godot-demo-projects)。

更多的学习资源（视频、文字教程等）可通过 [社区频道](https://godotengine.org/community) 获取。

[![Code Triagers Badge](https://www.codetriage.com/godotengine/godot/badges/users.svg)](https://www.codetriage.com/godotengine/godot)
[![Translate on Weblate](https://hosted.weblate.org/widgets/godot-engine/-/godot/svg-badge.svg)](https://hosted.weblate.org/engage/godot-engine/?utm_source=widget)