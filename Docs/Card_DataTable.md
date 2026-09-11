# 卡牌 DataTable 怎么建、怎么改

结论先说：**表已经建好了**，在 `Content/HexSpire/Data/DT_Cards.uasset`，13 行，
启动时自动加载（日志会打 `卡牌配表已应用：覆写 13 张`）。
日常改数值直接在编辑器里双击这张表改就行，**不需要重新建、不需要编译 C++**。

下面记的是「从零建一张」和「改完为什么没生效」两件事。

---

## 表在整个数据链里的位置

```
HexContentLibrary.cpp 的 13 张卡   ← 权威基线，纯 C++，headless 可用
        ↓  启动时
DT_Cards 同 RowName 的行            ← 覆写该卡字段
DT_Cards 新 RowName 的行            ← 追加为新卡
        ↓
FHexContentLibrary::FindCard()      ← 游戏与验证器都从这里取
```

**代码内建不是"旧数据"，是回退保障。** 两个原因：

- 验证器与批量模拟（10 万场）跑在 headless 下不加载任何资产。卡池只存在 `.uasset` 里的话，
  那套验证就得先启动完整引擎 —— 一轮从秒级变成分钟级，而它的价值恰恰是"改完立刻能跑"。
- 表配错了、表丢了，游戏照样可玩，只是回到实测过的数值。比"启动就崩"好得多。

覆写是**整行替换**，不是字段级合并。CSV 导入时空单元格会填成类型默认值，
做字段级 diff 就得区分"没填"和"填了 0" —— DataTable 不保留这个信息，
于是"把费用改成 0"会被当成"没填"而失效。整行替换语义可预测：表里那行就是最终结果。

---

## 从零建一张表

### 1. 导出当前卡池为 CSV

```powershell
& "E:\UE\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
    "E:\UE_Proj\HexSpire\HexSpire.uproject" -run=HexExportCards
```

产出 `Saved/Export/Cards.csv`，13 行，列名与 `FHexCardTableRow` 的 UPROPERTY 一一对应。

### 2. 导入成 DataTable

自动（推荐，首次建表用）：

```powershell
& "E:\UE\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
    "E:\UE_Proj\HexSpire\HexSpire.uproject" -run=pythonscript `
    -script="E:\UE_Proj\HexSpire\Tools\make_card_datatable.py"
```

脚本是**幂等**的：表已存在时只报告、不覆盖。
覆盖会把美术已配好的 `Visual` 字段全部冲掉 —— 那些字段 CSV 里是空的，重新导入等于清空。

手动（编辑器里）：右键 `Cards.csv` → Import → 行结构选 **`HexCardTableRow`** → 存到 `/Game/HexSpire/Data/DT_Cards`。

> **行结构一定要选对。** 选错时 `FindRow<FHexCardTableRow>` **仍然返回一个指针** ——
> 指向按错误布局解读的字节，于是费用/伤害变成垃圾值，不崩溃也不报错。
> `FHexCardTableLoader::Apply` 会校验 `GetRowStruct` 并拒绝整张表，日志里能看到。

### 3. 路径与命名不能随便改

`FHexCardTableLoader::DefaultTablePath()` 里硬编码了 `/Game/HexSpire/Data/DT_Cards.DT_Cards`。
挪目录或改名会让加载**静默失败** —— 不报错，只是全部回退到代码内建，
表现是"我明明改了表，游戏里纹丝不动"。要改请连带改那个函数。

---

## 配表时最容易踩的四个坑

**① RowName 必须等于卡牌 Id**（如 `atk_basic`，不是"攻击"）。
Id 是逻辑层认卡的唯一依据。写成中文显示名的话覆写会静默落空。

**② 数值要写成系数化三件套，别硬编码**（§7.5）：

```
最终值 = FlatValue + Stats[StatRef] × StatRatio
```

`Damage` 直接填 12 的卡，在 ATK 从 10 涨到 200 时会被数值冲垮，策略层失效。
例外是**离散量**（不随属性缩放）：状态层数、位移格数、抽牌数、体力数 —— 这些是策略锚点，
填固定值才对。

**③ 枚举填标识名，不填中文。** `Attack` / `Guard` / `Move`，不是"攻击"。
CSV 导入按标识名匹配，填中文会**静默填成枚举第 0 项** —— 所有卡都变成"攻击"类型，不报错。

**④ 空 FName 留空单元格，别写 `None`。**
`FName().ToString()` 返回的字面量就是 `"None"`，写进 CSV 再导回来会得到一个
**名字叫 None** 的 FName —— `StatusId` 于是非空，`ApplyStatus` 去找一个叫 "None" 的状态，
查不到就静默什么都不做。卡看起来正常，效果凭空消失。

---

## 美术字段（`Visual` 组）

只有表现层读，逻辑层的 `FHexCardData` 里没有对应字段（纪律 3）。逐行配：

| 字段 | 说明 |
|---|---|
| `CardFrame` | 卡框。留空 → 按稀有度取默认框 |
| `TypeIcon` | 类型图标。留空 → 按 CardType 取默认图标 |
| `Artwork` | 卡面插画 |
| `FrameTint` | **留白色 = 不染色**（现在的默认行为） |
| `RarityGlow` | 0 = 不发光 |

三级回退链：表里配的 → `HexCardArt.cpp` 里按类型/稀有度的默认资产 → `nullptr`（不画那一层）。
这让"美术还没交图"和"交了图"可以共存，灰盒期不必等资产齐全。

> `FrameTint` 现在**默认不用**。之前按 CardType 把整张卡框乘成红/蓝/绿，
> 观感是一排彩色方块，而且插画挂上来会被整体偏色 —— 美术永远调不准，
> 因为他看到的图和游戏里不是一个颜色。类型区分改由**图标 + 类型文字**承担，
> 这也符合 §13.2「不能只靠颜色传信息」。
> 想让一张灰度卡框复用出多种配色时，才显式填这个字段。

---

## 改完没生效？按这个顺序查

1. 日志有没有 `卡牌配表已应用：覆写 N 张`？没有 → 表没被加载，查路径。
2. 有 `行结构不是 FHexCardTableRow` → 导入时选错了行结构，重导。
3. `Overridden` 数比预期少 → 有 RowName 和卡 Id 不一致，那些行被当成新卡追加了。
4. 数值对但描述里的数字不对 → 描述模板的占位符写错。可用：
   `{dmg}` `{block}` `{kb}` `{move}` `{stacks}` `{draw}` `{hits}`。
