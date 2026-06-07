# Jundot Engine

<p align="center">
  
  </a>
</p>

## 一个自动迭代的跨平台游戏引擎（基于 Godot）

**Jundot Engine** 是一个基于 [Godot Engine](https://godotengine.org) 修改的自动迭代游戏引擎。  
它在继承 Godot 所有特性的基础上，引入了 **自动迭代机制** —— 即引擎能够在开发过程中自动进行编译、热重载、测试循环，显著提升迭代效率。

所有改动均源自 Godot 原始代码库，并在其之上增加了自动化工作流支持。

## 2D 与 3D 跨平台游戏引擎

**[Godot Engine](https://godotengine.org) 是一个功能丰富、跨平台的游戏引擎，可在统一界面中制作 2D 和 3D 游戏。**  
它提供了一整套[常用工具](https://godotengine.org/features)，让用户专注于制作游戏而无需重复造轮子。游戏可以一键导出到多种平台，包括主流桌面平台（Linux、macOS、Windows）、移动平台（Android、iOS）、Web 平台以及[游戏主机](https://godotengine.org/consoles)。

**Jundot 在 Godot 的基础上增加了自动迭代能力**，例如：
- 代码修改后自动重新编译并热重载
- 编辑器内自动运行测试用例
- 一键式迭代循环：编辑 → 保存 → 自动运行 → 反馈

## 免费、开源、社区驱动

Jundot 继承自 Godot，因此同样采用非常宽松的 [MIT 协议](https://godotengine.org/license)，完全免费且开源。  
无任何附加条款、版税或隐藏费用。用户的游戏完全属于用户自己，连引擎的最后一行代码都是透明的。  
Jundot 的持续开发保持独立且社区驱动，支持来自 [Godot Foundation](https://godot.foundation/) 的非营利性组织。

在 [2014 年 2 月](https://github.com/godotengine/godot/commit/0b806ee0fc9097fa7bda7ac0109191c9c5e0a1ac) 开源之前，Godot 由 [Juan Linietsky](https://github.com/reduz) 和 [Ariel Manzur](https://github.com/punto-) 作为内部引擎开发多年，并用于发布多款商业作品。  
Jundot 在此基础上增加了自动迭代特性，使其更适配快速原型开发与实验性项目。

![Screenshot of a 3D scene in the Godot Engine editor](https://raw.githubusercontent.com/godotengine/godot-design/master/screenshots/editor_tps_demo_1920x1080.jpg)

## 获取引擎

### 二进制下载

Jundot 的官方二进制文件（包含编辑器与导出模板）可在 [项目的 Releases 页面] 获取（根据你的实际情况填写链接）。  
你也可以直接使用 Godot 官方二进制，但自动迭代功能需要 Jundot 定制版才支持。

### 从源码编译

请参考 [官方编译文档](https://docs.godotengine.org/en/latest/engine_details/development/compiling)。  
Jundot 的编译步骤与 Godot 一致，只需在编译时开启自动迭代相关宏（详见 `docs/auto_iteration.md`）。

## 社区与贡献

Jundot 首先是 Godot 生态的一部分，因此大部分社区资源与 Godot 共用。  
主要社区渠道请查看 [Godot 官网](https://godotengine.org/community)。

如果你想讨论 Jundot 特有的自动迭代功能，可以加入 [Jundot 讨论区]（链接待补充）。  
有关贡献代码（包括向 Godot 上游贡献以及向 Jundot 特有模块贡献）的说明，请参阅 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 文档与示例

- Godot 官方文档（包含类参考）托管在 [Read the Docs](https://docs.godotengine.org)，由社区维护。
- Jundot 自动迭代机制的详细说明请查看 `docs/auto_iteration.md`。
- 官方示例项目请参考 [Godot Demo Projects](https://github.com/godotengine/godot-demo-projects)。

更多的学习资源（视频、文字教程等）可通过 [社区频道](https://godotengine.org/community) 获取。

[![Code Triagers Badge](https://www.codetriage.com/godotengine/godot/badges/users.svg)](https://www.codetriage.com/godotengine/godot)
[![Translate on Weblate](https://hosted.weblate.org/widgets/godot-engine/-/godot/svg-badge.svg)](https://hosted.weblate.org/engage/godot-engine/?utm_source=widget)