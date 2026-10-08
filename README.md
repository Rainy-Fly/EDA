# TinyEDA

一个用 wxWidgets 实现的简易电子设计自动化（EDA）教学项目：在画布上放置逻辑门与电子元件、绘制导线，查看并编辑元件属性。

## 项目描述文件（.xml）定义

项目以单个 XML 文件保存（如 `project.xml`），同时包含**项目信息**与**电路图各元件信息**，类似 Logisim 的 `.circ` 文件。当前仅完成格式定义，保存/加载尚未实现。

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
