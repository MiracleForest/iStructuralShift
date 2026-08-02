# iStructuralShift

[English README](README.md)

一个给 LeviLamina 服务端用的 Minecraft 结构导入导出模组。

## 简介

`iStructuralShift` 可以在游戏里的结构数据和 `.mcstructure` 文件之间互相导入导出，也能列出和删除结构。

导出时，模组会优先使用当前已经加载到内存里的结构，所以刚加载过或者刚改过的结构也能直接导出。  
导入和删除时，模组也会顺手把对应的内存缓存清掉，避免游戏里和存档里的内容对不上。

## 功能

- 把游戏里的结构导出成 `.mcstructure` 文件
- 把 `.mcstructure` 文件重新导回游戏
- 同时列出存档里和内存里的全部结构
- 同时删除存档里和内存里的单个或全部结构
- 结构发生变化后自动刷新命令补全
- 可以去掉 MC 原版结构命令的范围限制
- 内置 `en-us`、`zh-cn`、`zh-tw` 语言资源

## 用法

命令别名：`/iss`

### 命令

```text
/structural_shift update_enum
/structural_shift list
/structural_shift export <name>
/structural_shift export_all
/structural_shift import <name>
/structural_shift import_all
/structural_shift remove <name>
/structural_shift remove_all
/structural_shift bypass_limit [true|false]
```

### 命令说明

- `export <name>` 会把单个结构导出到 `plugins/iStructuralShift/data/structures/<name>.mcstructure`
- `export_all` 会导出所有已知结构，包括当前只存在于内存里的结构
- `import <name>` 会把 `plugins/iStructuralShift/data/structures/<name>.mcstructure` 导回游戏，并清掉这个结构在内存里的缓存
- `import_all` 会导入结构目录下的全部 `.mcstructure` 文件
- `remove <name>` 会同时删掉这个结构在存档里和内存里的数据
- `remove_all` 会同时删掉全部已知结构在存档里和内存里的数据
- `bypass_limit` 用来开关“去掉 MC 原版结构命令范围限制”这个功能

## 文件位置

导出和导入用到的结构文件都在这里：

```text
plugins/iStructuralShift/data/structures/
```

## 安装

> [!CAUTION]
> 以下安装说明仅供参考，请以你的实际服务端环境和发布包为准。

### 服务端

#### 使用 Lip（推荐）

1. 进入你的 Minecraft 服务端目录
2. 执行以下命令：
   ```bash
   lip install github.com/MiracleForest/iStructuralShift#server
   ```

#### 手动安装

1. 从 [Releases 页面](https://github.com/MiracleForest/iStructuralShift/releases) 下载最新发布包
2. 把压缩包内容解压到服务端的 `./plugins` 目录
