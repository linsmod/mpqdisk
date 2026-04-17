# MPQDisk

基于 Dokan 文件系统的 MPQ 档案虚拟磁盘挂载工具，可以将多个 MPQ 档案文件叠加挂载为 Windows 虚拟磁盘。

## 项目介绍

MPQDisk 是一个 Windows 平台的命令行工具，使用 Dokan 驱动程序将 Blizzard MPQ 游戏存档文件挂载为系统可见的虚拟磁盘。支持多个 MPQ 文件叠加挂载，后加载的文件优先级更高，覆盖先加载文件中的同名资源。

## 特性

- ✅ 将 MPQ 档案挂载为标准 Windows 磁盘卷
- ✅ 支持多文件叠加挂载 (Overlay 模式)
- ✅ 只读文件系统保护
- ✅ 完整的文件系统浏览支持
- ✅ Unicode 文件名支持
- ✅ 自定义卷标与盘符
- ✅ 调试输出模式
- ✅ 配置文件支持

## 依赖环境

- Windows 7 / 8 / 10 / 11
- [Dokan Library v2.x](https://github.com/dokan-dev/dokany) (需要安装 Dokan 驱动)
- Visual Studio 2019+ / MSVC 编译器
- CMake 3.15+
- [StormLib](https://github.com/ladislav-zezula/StormLib) (MPQ 文件解析库)

## 编译构建

### 目录结构
```
mpqdisk/
├── Dokan/                  # Dokan 库源码目录
├── StormLib/               # StormLib 库源码目录
└── mpqdisk/                # 本项目目录
    ├── CMakeLists.txt
    ├── main.cpp
    ├── mpqfs.cpp
    ├── mpqfs.h
    ├── mpqfs_ops.cpp
    ├── mpqfs_ops.h
    ├── mpqdisk_config.cpp
    ├── mpqdisk_config.h
    └── mpqdisk.conf
```

### 编译步骤

1. 确保 Dokan 与 StormLib 源代码位于上级目录
2. 创建构建目录:
```cmd
mkdir build
cd build
```

3. 运行 CMake 配置:
```cmd
cmake ..
```

4. 编译项目:
```cmd
cmake --build . --config Release
```

## 使用方法

### 命令行语法

```
mpqdisk.exe --mount [OPTIONS]
```

### 可用选项

| 选项 | 说明 |
|------|------|
| `--mount` | **必须** 启动文件系统挂载 |
| `--config <路径>` | 使用配置文件启动 |
| `--files <文件列表>` | 直接指定要挂载的 MPQ 文件列表 (空格分隔) |
| `--volume <盘符> [卷标]` | 指定挂载盘符与可选卷标 (默认自动分配盘符) |
| `--debug` | 启用调试输出 |
| `--help`, `-h`, `/?` | 显示帮助信息 |

### 使用示例

#### 1. 直接挂载多个 MPQ 文件
```cmd
mpqdisk.exe --mount --files war3.mpq War3Local.mpq War3x.mpq War3xlocal.mpq
```

#### 2. 指定盘符与卷标
```cmd
mpqdisk.exe --mount --files war3.mpq --volume Z: Warcraft3
```

#### 3. 使用配置文件
```cmd
mpqdisk.exe --mount --config mpqdisk.conf
```

#### 4. 启用调试模式
```cmd
mpqdisk.exe --mount --config mpqdisk.conf --debug
```

### 卸载磁盘
按 `Ctrl+C` 即可安全卸载虚拟磁盘并退出程序。

## 配置文件格式

可以使用配置文件批量管理挂载选项，`mpqdisk.conf` 示例:

```ini
# MPQDisk 配置文件
volume_letter = Z:
volume_label = Warcraft III
debug = false

# MPQ 文件列表 (按优先级从低到高排列)
mpq_files = [
    "war3.mpq",
    "War3Local.mpq",
    "War3x.mpq",
    "War3xlocal.mpq"
]
```

## 技术实现

- 使用 **Dokan Library** 实现 Windows 用户态文件系统
- 使用 **StormLib** 解析 MPQ 档案格式
- C++17 标准开发
- 只读文件系统实现
- 支持多 MPQ 叠加层系统
- 自动处理文件优先级覆盖

## 注意事项

1. 必须安装 Dokan 驱动程序才能正常运行
2. 
3. 目前仅支持只读访问模式
4. 建议将所有 MPQ 文件放置于同一目录
5. 挂载多个 MPQ 时，后面的文件优先级更高

## 许可证

本项目使用 MIT 许可证发布。

## 作者

linsmod <linsmod@qq.com>

项目地址: https://github.com/linsmod/mpqdisk