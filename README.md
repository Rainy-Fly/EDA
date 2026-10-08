# TinyEDA

一个用 wxWidgets 实现的简易电子设计自动化（EDA）教学项目：在画布上放置逻辑门与电子元件、绘制导线，查看并编辑元件属性。

## 快速开始

### 环境要求

- Windows + Visual Studio 2022（MSVC）
- CMake ≥ 3.24
- 第三方依赖由 CMake 的 `FetchContent` 自动拉取（wxWidgets 3.2.11、jsoncpp 1.9.6），
  首次配置需要能访问 GitHub

### 构建

```powershell
cmake -S . -B build
cmake --build build --config Debug --target TinyEDA
```

产物：`build\Debug\TinyEDA.exe`

### 拉取队友的更新之后

```powershell
git pull
cmake -S . -B build        # 重新配置
cmake --build build --config Debug --target TinyEDA
```

两条 `cmake` 命令各自的作用：

| 命令 | 作用 |
|---|---|
| `cmake -S . -B build` | **配置**：生成/刷新构建系统（VS 的 sln/vcxproj）。它会把 `src` 下当时存在的 `.cpp` 收集成源文件清单 |
| `cmake --build build --config Debug --target TinyEDA` | **编译**：`--config Debug` 是必须的（VS 是多配置生成器）；`--target TinyEDA` 只编主程序，避开 jsoncpp 自带测试工程那个必然失败的步骤（否则整条命令返回 exit code 1，看着像编译失败） |

**为什么拉取后要重新配置**：CMakeLists 用
`file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")` 收集源文件 —— `GLOB` 是在
**配置阶段**扫描目录，而 `CONFIGURE_DEPENDS` 让构建前自动重扫，所以正常情况下**新增文件不需要手动配置**
（已实测：往 `src/` 放一个新 `.cpp`，直接构建就会被收进工程）。

不过这个机制在个别情况下不完全可靠，记住两条经验：

- 构建报 **“无法打开源文件 xxx.cpp”**（通常是**删除**源文件之后）→ 再构建一次即可，或手动 `cmake -S . -B build`
- 链接报 **找不到符号**、或新加的文件看起来没被编译 → 手动 `cmake -S . -B build` 之后再构建

如果 `git pull` 报 `CMakeLists.txt` 冲突（你本地也改过同一个文件），先看 `git diff` 弄清两边各改了什么再合并，
**不要直接丢弃本地改动**；推送后的版本已经包含 `WIN32`、`/utf-8`、`wx::xml` 这些必需项，
如果你本地只是为了这些才改的 CMakeLists，冲突时取远端版本即可。

### 运行

**推荐：双击仓库根目录的 `run_TinyEDA.bat`。** 它会先把工作目录切到仓库根，再启动 exe。
另有两种等价方式：命令行 `cd <仓库根>; .\build\Debug\TinyEDA.exe`，
或建快捷方式并把「起始位置」设为仓库根。

不建议直接在资源管理器里双击 `build\Debug\TinyEDA.exe`，原因有两个：

1. **工作目录必须是仓库根**：程序按「当前目录」或「exe 目录的上一级」查找
   `src/metadata/*.json` 与元件 SVG（`src/Canva/assets/Symbols/`、`src/assets/electronic-symbols/SVG/`）。
   直接双击时工作目录是 `build\Debug`，两处都找不到 —— 界面能开，但**没有元件图标、属性栏也没有元数据**
   （程序对这些缺失是静默容错的，不会报错，不容易发现）。
2. **DLL 要在 exe 旁边**：wxWidgets 与 jsoncpp 是动态库。仓库的 `build\Debug\` 里已经放过一份，
   但**重新 clone 或清理构建目录后需要再复制一次**：

```powershell
Copy-Item build\_deps\wxwidgets-build\lib\vc_x64_dll\*.dll build\Debug\ -Force
Copy-Item build\_deps\jsoncpp-build\src\lib_json\Debug\jsoncpp.dll build\Debug\ -Force
```

### MSVC 上必须注意的几点（已写进 CMakeLists）

| 项 | 不这么做会怎样 |
|---|---|
| `add_executable(... WIN32 ...)` | wxWidgets 在 Windows 下的入口是 `WinMain`；不加会 `LNK2019 无法解析的外部符号 main`，运行时还会多弹一个控制台窗口 |
| `if(MSVC) add_compile_options(/utf-8)` | 源码是 UTF-8 无 BOM 且含大量中文字面量，MSVC 默认按本地代码页（GBK）解析 → 乱码/警告 |
| 链接 `wx::xml` | `wxXmlDocument` 在独立的 xml 组件里，不链会有一堆 `LNK2019` |
| 枚举名用 `ERASE` 而不是 `DELETE` | Windows 的 `winnt.h` 把 `DELETE` 定义成访问权限宏（`0x00010000L`），MSVC 下会报语法错误 |
| `DealProjectXML.cpp` 里的 `localtime_r` 兼容宏 | MSVC 没有 POSIX 的 `localtime_r`（只有参数顺序相反的 `localtime_s`） |

另外：不带 `--target` 直接跑 `cmake --build build`，会因为 jsoncpp 自带的测试工程
`jsoncpp_test.exe` 找不到 `jsoncpp.dll` 而返回 exit code 1 —— 这与本项目无关。
只编主程序请加 `--target TinyEDA`；想彻底消掉噪音可以在 CMakeLists 里加
`set(JSONCPP_WITH_TESTS OFF CACHE BOOL "" FORCE)`。

## 功能

### 文件菜单

| 菜单项 | 快捷键 | 行为 |
|---|---|---|
| 新建 | Ctrl+N | 若有未保存修改先询问；然后清空画布、视角复位、清空当前路径 |
| 打开... | Ctrl+O | 先选起始位置（见下），再选文件；**解析失败时画布保持原样** |
| 保存 | Ctrl+S | 没有路径时自动转为「另存为」 |
| 另存为... | Ctrl+Shift+S | 默认文件名 `project.xml`；没写扩展名会自动补 `.xml` |
| 设置默认目录... | — | 选择默认目录并写入用户配置文件 |
| 退出 | Ctrl+Q | 走窗口关闭流程（含未保存确认），退出前把配置落盘 |

其它约定：

- 标题栏显示 `TinyEDA - <文件名>`；有未保存修改时前面加 `*`；新项目还没保存过时显示 `未命名`
- 关闭窗口（右上角 ×）同样会问是否保存
- 「有未保存修改」的判定：放置/删除/移动元件、画完导线、在属性栏改属性都会标记；
  单纯点击选中不算修改（除非「选择」工具确实把元件挪动了）

### 打开项目的三种起始位置

点「打开」后先弹一个对话框选择从哪里开始找：

1. **从最近位置打开** —— 上次打开/保存的项目所在目录（首次运行时不可选）
2. **从根目录打开** —— 项目根目录（当前目录含 `src/`，否则从 exe 位置向上找含 `src/` 的目录）
3. **从默认目录打开** —— 在「设置默认目录...」里设定的目录（未设置时不可选）

目录不存在或还没有记录的选项会自动置灰，默认选中第一个可用项；取消则不打开。

### 用户配置文件

- 位置：`%APPDATA%\TinyEDA\settings.xml`（如 `C:\Users\<你>\AppData\Roaming\TinyEDA\settings.xml`）
- 该位置不可写时（受限环境、便携部署）自动回退到**可执行文件旁边**的 `tinyeda-settings.xml`
- 主程序启动时加载；打开/保存项目、设置默认目录后立即写回；退出前再落盘一次
- 读写失败不会弹错误框（只是设置没保存），也不会影响项目文件的操作

```xml
<?xml version="1.0" encoding="UTF-8"?>
<tinyeda-settings version="1">
  <recent file="C:\work\a.xml" dir="C:\work"/>
  <default-dir path="D:\EDA\projects"/>
</tinyeda-settings>
```

| 元素 | 含义 |
|---|---|
| `recent@file` | 上次打开/保存的项目文件（完整路径） |
| `recent@dir` | 上次打开/保存所在目录（「从最近位置打开」用） |
| `default-dir@path` | 用户设置的默认目录 |

读取容错：文件不存在/损坏/字段缺失一律按默认值处理；记录里的目录已被删除时视为未设置。

## 代码结构

| 目录/文件 | 职责 |
|---|---|
| `src/DealProjectXML/` | **项目文件（.xml）读写**。上层统一调用这里的 `SaveProject` / `LoadProject` / `CollectFromCanvas` / `ApplyToCanvas` / `ReadXml` / `WriteXml`，格式见下节 |
| `src/Settings/` | 用户配置文件（settings.xml）的读写、项目根目录识别 |
| `src/MenuBar/` | `MenuBar` 负责菜单结构；`FileActions` 实现新建/打开/保存/另存为/设置默认目录/退出与未保存确认；`OpenLocationDialog` 是「从哪开始找」的三选一对话框 |
| `src/Canva/` | 画布：逻辑坐标系与缩放/平移，元件与导线的放置、选中、拖动、删除、克隆、网格与导线绘制；`CanvasItem`/`Symbol`/`Linking` 数据模型；`ItemSVG` 按类型串加载 SVG 与元数据（共享缓存） |
| `src/metadata/` | 元件元数据 JSON（每个元件一条定义，`type` 是主键）+ JSON 读取器 |
| `src/Explorer/` | 左侧资源树：按类别列出所有元件，点击后进入放置模式 |
| `src/AttributeBar/` | 右下属性栏：显示并编辑当前元件的元数据 |
| `src/demoApp.cpp`、`src/demoMainFrame.*` | 演示用程序入口与主窗口（把上面几块拼起来） |

> 约定：项目文件（`.xml`）的读写**只有 `src/DealProjectXML` 一处实现**，其它模块不要再各写一套；
> 以后要保存新的内容，改那一处即可。用户配置文件的读写同理，只在 `src/Settings`。
>
> 新增源文件直接放进 `src/` 即可，CMake 会自动收进工程（靠
> `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)`）；**删除**源文件后第一次构建可能报
> “无法打开源文件 xxx.cpp”，再构建一次即可（或手动 `cmake -S . -B build`）。
> 新增文件若没被编译、或链接时报找不到符号，也手动跑一次 configure。

## 项目描述文件（.xml）定义

项目以单个 XML 文件保存（如 `project.xml`），同时包含**项目信息**与**电路图各元件信息**，类似 Logisim 的 `.circ` 文件。读写由 `src/DealProjectXML` 实现。

### 总体结构

```
teda-project           根元素（schema-version 属性标明格式版本）
├── info               项目信息
│   ├── name           项目名
│   ├── author         作者
│   ├── created        创建时间（ISO 8601）
│   ├── modified       最近修改时间（ISO 8601）
│   ├── description    项目描述
│   └── view           画布视口（offset-x/offset-y 逻辑原点、scale 缩放、grid-step 网格步长）
└── circuit            电路图
    ├── component      元件（门类/通用元件，Symbol）：位置 + 元数据
    │   ├── description  元件描述（可覆盖元数据默认值）
    │   └── param        通用参数（DataBits/resistance/... 键值对，可多个）
    └── wire           导线/总线（Linking）：折点序列
        ├── param      通用参数（可多个）
        └── point      折点（x/y，至少 2 个）
```

### 根元素

```xml
<teda-project schema-version="1.0">
```

| 属性 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `schema-version` | 字符串 | 是 | 格式版本号，当前为 `1.0`。加载时按版本做兼容处理 |

### 项目信息 `<info>`

```xml
<info>
  <name>我的第一个电路</name>
  <author>nokna</author>
  <created>2025-10-08T15:30:00+08:00</created>
  <modified>2025-10-08T15:31:00+08:00</modified>
  <description>与非门驱动 LED 示例</description>
  <view offset-x="-40" offset-y="-30" scale="1.0" grid-step="10"/>
</info>
```

| 元素 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `name` | 字符串 | 是 | 项目名 |
| `author` | 字符串 | 否 | 作者 |
| `created` | 时间 | 是 | 创建时间，ISO 8601 格式 |
| `modified` | 时间 | 否 | 最近修改时间 |
| `description` | 字符串 | 否 | 项目描述 |
| `view` | 元素 | 否 | 画布视口；`offset-x/offset-y` 为画布左上角对应的逻辑网格坐标，`scale` 为缩放比例，`grid-step` 为网格步长（默认 10）。加载后恢复画布视角 |

### 电路图 `<circuit>`

电路图包含任意多个 `<component>` 与 `<wire>`，顺序不限。

#### 元件 `<component>`（门类/通用元件）

```xml
<component id="1" type="NAND" name="与非门" category="LogicGate" x="0" y="0">
  <description>与非门：两个输入均为1时输出0，否则输出1</description>
  <param key="DataBits">1</param>
  <param key="inputs">2</param>
  <param key="outputs">1</param>
</component>
```

| 属性 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `id` | 正整数 | 是 | 元件实例 ID，在 `<circuit>` 内唯一（对应 `CanvasItem::id`） |
| `type` | 字符串 | 是 | 元件类型串，与 `src/metadata/*.json` 中元数据的 `type` 字段一致（如 `"NAND"`、`"Resistor-IEC-Standard"`、`"IC-COM-FlipFlop-ClockedD"`），是加载时恢复元数据的**主键** |
| `name` | 字符串 | 是 | 中文显示名（如 `"与非门"`、`"标准电阻(IEC)"`），冗余字段，便于阅读，也可作加载兜底键 |
| `category` | 字符串 | 否 | 类别（如 `"LogicGate"`、`"Resistor"`），冗余字段；用于判定元数据子类（`LogicGate` → `LogicGateMetaData`） |
| `x`、`y` | 整数 | 是 | 元件锚点（图片中心）在逻辑网格上的坐标（对应 `CanvasItem::coords_x/coords_y`，已吸附网格） |

子元素：

| 元素 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `description` | 字符串 | 否 | 元件描述；存在时覆盖元数据默认描述（写入 `MetaData::set_description`） |
| `param` | 键值对 | 否 | 通用参数，可多个；`key` 属性为参数名，文本为值。加载后写入 `MetaData::params`；对逻辑门，`DataBits`/`inputs`/`outputs` 同时写入类型化字段 |

#### 导线/总线 `<wire>`（Linking）

```xml
<wire id="3" type="WIRE" name="导线">
  <param key="DataBits">1</param>
  <point x="10" y="0"/>
  <point x="10" y="60"/>
  <point x="100" y="60"/>
</wire>
```

| 属性 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `id` | 正整数 | 是 | 实例 ID，`<circuit>` 内唯一 |
| `type` | 字符串 | 是 | `"WIRE"`（导线）或 `"BUS"`（总线） |
| `name` | 字符串 | 否 | 中文显示名（`"导线"`/`"总线"`） |

子元素：

| 元素 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `param` | 键值对 | 否 | 通用参数（如 `DataBits`），同 `component/param` |
| `point` | 坐标 | 是（≥2 个） | 折点序列，`x`/`y` 为逻辑网格坐标（对应 `Linking::points`）；导线无 `x/y` 属性，位置由折点决定 |

### 与 C++ 数据模型的对应

| XML | C++ |
|---|---|
| `teda-project@schema-version` | 文件格式版本 |
| `info/*` | 项目元信息（名称/作者/时间/描述） |
| `info/view` | `Canvas::offset_coords`、`Canvas::scale`、`GridStep` |
| `circuit/component` | `CanvasItem`（`Symbol` 子类） |
| `component@id` | `CanvasItem::id` |
| `component@type` | 元数据查找键（`MetaDataJson::load_metadata`） |
| `component@x/@y` | `CanvasItem::coords_x/coords_y` |
| `component/description` | `MetaData::set_description` |
| `component/param` | `MetaData::params` |
| `circuit/wire` | `Linking` 子类 |
| `wire/point` | `Linking::points` |

### 完整示例

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- TinyEDA 项目描述文件 -->
<teda-project schema-version="1.0">

  <!-- 项目信息 -->
  <info>
    <name>与非门驱动 LED</name>
    <author>nokna</author>
    <created>2025-10-08T15:30:00+08:00</created>
    <modified>2025-10-08T15:31:00+08:00</modified>
    <description>示例：与非门输出经限流电阻点亮 LED</description>
    <view offset-x="-40" offset-y="-30" scale="1.0" grid-step="10"/>
  </info>

  <!-- 电路图 -->
  <circuit>

    <!-- 与非门：输出为1时点亮LED -->
    <component id="1" type="NAND" name="与非门" category="LogicGate" x="0" y="0">
      <description>与非门：两个输入均为1时输出0，否则输出1</description>
      <param key="DataBits">1</param>
    </component>

    <!-- 限流电阻（属性栏改过的值会保存在param里） -->
    <component id="2" type="Resistor-IEC-Standard" name="标准电阻(IEC)" category="Resistor" x="200" y="0">
      <param key="resistance">4.7k</param>
      <param key="tolerance">5%</param>
    </component>

    <!-- 发光二极管 -->
    <component id="3" type="Diode-COM-LED" name="发光二极管(LED)" category="Diode" x="320" y="0">
    </component>

    <!-- 导线：从与非门输出到电阻 -->
    <wire id="4" type="WIRE" name="导线">
      <point x="75" y="0"/>
      <point x="75" y="60"/>
      <point x="200" y="60"/>
    </wire>

  </circuit>
</teda-project>
```

### 容错与版本扩展

- **容错原则**：与现有代码"图片缺失/元数据缺失不报错"一致，加载时忽略未知元素、未知属性与无法解析的字段，缺失的 `type` 对应的元件按空元数据创建。
- **版本兼容**：`schema-version` 用于向前兼容。未来新增元素类型（如文本标注 `annotation`、子电路、总线标签等）或字段时递增主版本号，加载器按版本分支处理。
- **坐标单位**：所有坐标均为逻辑网格坐标（整数），网格步长由 `info/view@grid-step` 决定（默认 10），与画布吸附一致。

## 已知问题与约定

- **`src/UI_frame_1.cpp` 目前不参与构建**（见 CMakeLists 的排除规则）。它是分步教程式的独立示例，
  自带 `wxIMPLEMENT_APP(MyApp)` 与 `MyFrame`，与 `demoApp.cpp` 的应用入口重复：一起编会
  `LNK2005`（`WinMain` / `wxCreateApp` / `wxTheAppInitializer` 重复定义）→ `LNK1169`。
  它是否取代 `demoApp`/`demoMainFrame` 需要团队决定，决定前先排除；文件仍保留在版本库
  （它 include 的 `src/mondrian.xpm` 也一并保留）。另外它用的是 `#include "Explorer\ExplorerPane.h"`
  （反斜杠），MSVC 能编过，GCC/MinGW 不行。
- **`<wire>` 没有 `<description>`**：规范里给 `<wire>` 定义的子元素只有 `param`/`point`，
  所以导线上被编辑过的「描述」不会写进文件（而属性栏对导线仍显示可编辑的描述框）。
  要修的话：规范里给 wire 补一个 `<description>`，或者属性栏对导线隐藏它。
- `<modified>` 每次保存都会刷新为当时时间（设计如此）；`<name>` 为空时写成 `Untitled`。
- 本机工作区里保留、但不参与构建、也不进版本库的文件：`src/ExplorerBak/`（备份目录）、
  `src/main.cpp`（本地测试入口）、`src/Explorer.zip`。CMakeLists 里有对应的排除规则。
- 直接 `cmake --build build` 会因 jsoncpp 自带测试工程返回 exit code 1（与本项目无关），
  只编主程序请用 `--target TinyEDA`。
