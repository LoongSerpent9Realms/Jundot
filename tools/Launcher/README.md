# JundotLauncher — 线上热更新系统

Jundot Engine 的独立启动器，负责启动前版本检查、远程下载、完整性校验与安装。

## 构建

```bash
cd tools/Launcher
dotnet build -c Release
```

输出：`bin/Release/net8.0/JundotLauncher.exe`

发布到引擎目录：复制 `JundotLauncher.exe` 到引擎 bin/ 目录下即可启用热更新。

## 用法

```
JundotLauncher.exe start      启动引擎（检查更新→可选安装→启动）
JundotLauncher.exe check-only 仅检查更新
JundotLauncher.exe update     执行完整更新流程
JundotLauncher.exe rollback   回滚到上一个版本
JundotLauncher.exe --version  显示版本信息
JundotLauncher.exe --help     显示帮助
```

### 选项

| 参数 | 说明 |
|---|---|
| `--engine-path <dir>` | 引擎安装目录（默认：启动器所在目录） |
| `--channel <stable\|beta\|dev>` | 更新通道 |
| `--manifest-url <url>` | 直接指定 manifest URL |

### 退出码

| 码 | 含义 |
|---|---|
| 0 | 成功 |
| 1 | 错误 |
| 2 | 有可用更新但未应用 |

## 更新流程

```
启动器启动
  ├─ 检查 manifest 是否存在
  ├─ 版本比对（current vs manifest.version）
  ├─ 灰度评估（百分比 + 白名单）
  ├─ [有更新] → 用户确认 → 下载（断点续传）
  │                         → SHA256 校验
  │                         → 备份当前版本
  │                         → 安装新版本
  │                         → 更新状态文件
  └─ [无更新] → 跳过
  ↓
启动 Jundot 引擎
```

## 数据结构

### 版本清单 (`update-manifest.json`)

由 `JundotPackageBuilder` 构建时自动生成，Schema 定义见 `scripts/update-manifest-schema.json`。

### 本地状态 (`.jundot-update-state.json`)

存储在引擎目录下，记录当前版本、机器标识、下载进度和备份列表。

## 目录结构

```
引擎目录/
├── jundot.*.exe                  # 引擎可执行文件
├── JundotLauncher.exe            # 启动器
├── .jundot-update-state.json     # 更新状态
├── .backup/                      # 版本备份
│   └── 1.7.2-beta/
├── .update-staging/              # 安装临时目录
│   └── (extracted files)
└── .update-staging-download/     # 下载临时目录
    └── package.zip.download      # 断点续传临时文件
```

## 灰度发布

支持两种模式（可同时启用）：

1. **百分比灰度**：基于 `hash(machine_id + seed) % 100 < percentage` 判断
2. **白名单灰度**：指定机器 ID 列表，白名单内用户始终可见

白名单优先于百分比。在 manifest 中配置：

```json
{
  "grayscale": {
    "enabled": true,
    "percentage": 10,
    "whitelist": ["machine-abc123", "machine-def456"],
    "machine_id_hash_seed": "jundot-grayscale-v1"
  }
}
```
