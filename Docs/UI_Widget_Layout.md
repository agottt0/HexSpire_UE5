# 自己改卡面布局（不用碰 C++）

## 现在就能用

`Content/HexSpire/UI/WB_Card` 里已经有完整的控件树了。
在编辑器里双击打开，设计器里就能看到 15 个控件，直接拖动、改边距、改字号、改颜色。
运行游戏会自动用这张蓝图（日志会打 `卡牌控件类：已发现控件蓝图 … WB_Card_C`）。

不需要改任何代码，也不需要在哪里注册。

---

## 为什么之前"改不了、也没用"

两个独立的问题，都已修掉：

**① 设计器是空的。** C++ 的布局是运行时在 `BuildDefaultTree()` 里搭出来的，
资产里没有那棵树，所以没有控件可拖。
现在由 `-run=HexBuildCardWidget` 把布局**实体化**进资产。

**② 蓝图没被使用。** 自动发现原先硬编码找 `WBP_HexCard`，而实际资产叫 `WB_Card`，
永远匹配不上，静默回退到 C++ 版 —— 界面照常显示，改动却不生效，且不报错。
现在按固定候选路径加载，`WB_Card` / `WBP_Card` / `WBP_HexCard` 都认。

> 顺带修了第二版的错误做法：曾改成"扫资产注册表找继承本类的蓝图"。
> 那在编辑器里能用，但 `-game` 下恒失败 —— `WidgetBlueprint` 是**编辑器专属资产类**，
> 独立运行时注册表里一条都没有（实测日志：`注册表里有 0 个 WidgetBlueprint`）。
> 现在加载的是**生成类**（`_C` 后缀），那是运行时资产，两种环境都在。

---

## 控件名不能随便改

| 控件名 | 内容 |
|---|---|
| `Frame` | 卡框贴图 |
| `Art` | 卡面插画 |
| `TypeIcon` | 类型图标 |
| `CostBadge` / `Cost` | 左上角费用底衬 / 数字 |
| `Name` | 卡名 |
| `TypeLabel` | 类型文字（攻击/守备/…） |
| `Desc` | 描述（实算数值） |
| `Range` | 射程 |
| `Tag` | 消耗/基石角标 |
| `Hotkey` | 快捷键提示 |
| `Dim` | 不可用时的浅纱 |

**改名 = 那一项永远不显示。** `BindWidget` 的绑定是**纯按名字匹配**的
（引擎拿控件 FName 去查同名 UPROPERTY），既不看类型也不看位置。
而且因为用的是 `BindWidgetOptional`，名字对不上时**编译期只有一条 Note，运行时完全静默** ——
卡面上那一项就是空白，没有任何报错。

位置、大小、颜色、字号、层级顺序都可以随便改，**只有名字不能动**。
不需要某一项？删掉它就行（Optional，不会崩）。

验证办法：跑一次游戏，看日志里有没有
`[自检]   卡面控件全部绑定成功`。
若打的是 `卡面有 N 项未绑定：…`，那几项就是名字对不上的。

---

## 想重新生成 / 回到 C++ 基线

```powershell
& "E:\UE\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
    "E:\UE_Proj\HexSpire\HexSpire.uproject" -run=HexBuildCardWidget -force
```

> **`-force` 会把你在设计器里的改动全部冲掉，而且不能撤销**（是对资产的批量写入+存盘）。
> 不加 `-force` 时，遇到已有控件树会直接跳过并提示 —— 这就是防手滑用的。
> 正常改布局**永远不需要**跑这个命令。

---

## 布局数值只有一个来源

`Source/HexSpire/Public/UI/HexCardLayout.h` 是唯一数据源，两条路径都只读它：

- 运行时 `UHexCardWidget::BuildDefaultTree()`（没建蓝图时）
- 生成器 `HexBuildCardWidget`（写进蓝图资产）

这样"C++ 版"和"蓝图版"的初始外观必然一致。
若两边各写一份边距，改一处忘一处就会出现"美术看到的和玩家看到的不一样",
而这种不一致不报错、只能靠眼睛发现。


---

## 字体：为什么 FontFace 是 None，现在怎么修的

**根因不是"没配字体"，而是配了一个存不进资产的字体。**

之前代码用 `GEngine->GetMediumFont()`。项目没有配 `MediumFontName` 时，
它返回的是 `/Engine/Transient.DefaultRegularFont` —— 一个**运行时动态创建的瞬态 UFont**
（`UnrealEngine.cpp` 的 `CreateFontObjectFromDefaultFont`）。两个后果：

1. **瞬态对象无法序列化进资产。** 生成器把它写进控件蓝图时存下来的是空 →
   编辑器里 `FontFace` 显示 `None`，运行时回退到 Roboto 单体（**不含中文字形**）。
2. **它的 typeface 只有 `Regular` 一项。** 而代码按 `"Bold"` 去取，查不到就静默回退 ——
   粗体从来没生效过。

现在改用真实资产 `/Engine/EngineFonts/Roboto.Roboto`：它带
`CompositeFallbackFont`（`DroidSansFallback`），中文走回退子字体，
typeface 里确实有 `Regular` / `Bold` / `Italic` / `Light`。

验证（日志）：

```
[自检]   卡名字体：对象=/Engine/EngineFonts/Roboto.Roboto 字族=Bold 字号=15
```

若这里显示 `对象=【空→回退 Roboto，无中文】`，就是字体没配上。

> ⚠️ 中文字形来自 `DroidSansFallback`，是思源之前的老字体，字形偏瘦、
> 标点位置与中文排版惯例有差异，**只能算"能读"**。
> 正式版仍需换一套授权可商用的中文字体（见 `UI_Asset_Checklist.md` 字体一节）。

字体统一由 `HexCardLayout::GetCardFont()` 提供，运行时控件与生成器共用同一个函数 ——
两边各写一套的话，"没建蓝图"和"建了蓝图"的字体会不一样，且不报错。

---

## 控件里显示白色 —— 不需要去配 DataTable

那是**设计器预览没有贴图**，不是数据问题。

卡框贴图原先只在运行时由 `ApplyView()` 按稀有度赋值，资产里的 `Frame` 控件没有笔刷，
所以设计器里就是一个纯白方块 —— 而游戏里是正常的（运行时赋上了）。
这正是你看到的"游戏内有卡牌样式，控件里是白色"。

现在生成器会把默认贴图和占位文字一起写进去，设计器里所见即所得：

- `Frame` → `Card_White` 贴图
- `TypeIcon` → `Attack` 图标（运行时按卡类型替换）
- `Name` / `Desc` / `Cost` / `Range` / `Tag` / `Hotkey` → 占位文字（如"盾击"、"射程 1-1"）
- `Dim` → 设计器里**默认隐藏**（它是"打不出"时的浅纱，默认可见会让整张卡发白，
  容易误以为颜色配错）

占位文字不只是好看：**空的 TextBlock 在设计器里高度为 0**，
不给占位的话美术会以为"这一行不存在"，从而把间距调错。
运行时 `SetCardView()` 会覆盖这些占位内容。

**DataTable 的 `Visual` 组是另一回事** —— 那是给"某张卡要用专属卡框/插画"用的，
留空就走默认资产。想统一换所有卡的卡框，改
`Source/HexSpire/Private/UI/HexCardArt.cpp` 里的路径；
只想给某张卡换，才去 DataTable 配。

---

## 手牌区布局：卡牌是怎么排列的

`WB_HandPanel` **现在已经在用了**（之前生成了但没接上，是我漏的一步）。
日志会打 `手牌区控件类：已发现控件蓝图 … WB_HandPanel_C`。

结构：

```
PanelRoot (CanvasPanel)
├─ HandBox   (HorizontalBox)  锚在底边中点，自动居中，底部留 16px
└─ LeftCol   (VerticalBox)    锚在左侧 52% 高度，左边距 14px
   ├─ FixedTitle (TextBlock)  "固定卡 · 常驻"
   └─ FixedBox   (VerticalBox) 固定卡竖排
```

手牌是 `HorizontalBox` 横排，每张卡左右各 4px 间距、底端对齐。
张数变化时容器自己重新居中，不需要算坐标。

**排列相关的东西现在都能在蓝图里改**：间距（改卡的 slot padding）、
手牌居中还是靠右（改 `HandBox` 的锚点）、固定卡放左边还是下边（改 `LeftCol` 锚点）。

⚠️ 但**卡的增删仍在 C++**（`RefreshFromGameMode` 按手牌数量增删控件）——
排列规则可以在蓝图里调，"有几张卡"由逻辑层决定，这条不能反过来（纪律 3）。

---

## 排列不整齐（一大一小）——已修

真正的原因不是间距，也不是缩放，而是**卡的宽度根本没被约束住**。

实测三张手牌的绝对尺寸是 `129 / 384 / 184` px —— 第二张宽到 384。
根因：`SizeBox` 的 `WidthOverride` **存不进资产**。
资产里数值 168 在、`bOverride_HeightOverride` 也在，但
**`bOverride_WidthOverride` 这一位没有被序列化**，于是运行时宽度不受限制；
而 `Desc` 开了自动换行，文字越长卡就被撑得越宽。

> C++ 路径没这个问题：它每次运行都重新调一次 `SetWidthOverride`，
> 不经过序列化，所以一直是整齐的 112px。
> 这也是"游戏内看着有样式、蓝图里却不对"的同一类原因——
> **凡是只在运行时设、没存进资产的属性，蓝图路径都会丢。**

修法：改用 `Min/MaxDesiredWidth`（这几个属性能正常存盘），
把宽高都夹死。现在是 `116 / 112 / 112`，间隔 117px 均匀
（116 是选中卡的 4% 放大，符合预期）。

---

## 卡面文字确实来自 DataTable

这一点现在已经是这样了，不需要改。链路：

```
DT_Cards 的 DisplayName / DescriptionTemplate
  → 启动时 FHexCardTableLoader 覆写进 FHexContentLibrary
  → FHexCardView::Make() 取 Card.DisplayName / RenderDescription(Hero)
  → Name->SetText() / Desc->SetText()
```

我实测验证过：把表里 `shield_bash` 的名字改成"盾击改名测试"、
描述改成"表里改的文字 {dmg} 点。"，重新导入后游戏里立刻变成：

```
固定卡《盾击改名测试》… 描述="表里改的文字 12 点。"
```

（`{dmg}` 按英雄当前 ATK 实算成 12，验证完已改回。）

**描述里的数值不要写死**，用占位符：
`{dmg}` `{block}` `{kb}` `{move}` `{stacks}` `{draw}` `{hits}`。
写死 12 的话，ATK 成长后卡面还显示 12，而实际伤害早就变了。

⚠️ 蓝图里那些占位文字（"盾击"、"射程 1-1"）**只在设计器里可见**，
运行时 `SetCardView()` 一定会覆盖它们。
所以不要在蓝图里改文字内容来改卡名——那是改不动的，要改表。
蓝图里该调的是文字的**位置、字号、颜色、对齐**。

---

## 「我改了蓝图布局，还是没用」——真正的原因

**你的改动确实存过（资产从 49890 变成 55595 字节），但被每帧覆盖掉了。**

`ApplyView()` 由 `RefreshFromGameMode()` 调用，而后者在 HUD 的 `DrawHUD()` 里
**每帧都跑一次**。它无条件地写颜色、显隐、贴图 ——
于是你在设计器里设的颜色，运行第一帧就被 C++ 刷回去了。
没有报错，日志也全是正常的，所以看起来就是"改了完全没用"。

### 两个开关

在控件蓝图的 **Class Defaults** 里（不是设计器面板，是类默认值）：

| 属性 | 关掉之后 |
|---|---|
| `bCppDrivesAppearance` | C++ 不再写颜色/显隐/贴图，**外观完全归蓝图** |
| `bCppDrivesText` | C++ 不再 `SetText`，文字改由蓝图的属性绑定提供 |

**想在蓝图里自由调颜色，就把 `bCppDrivesAppearance` 关掉。**
我实测验证过：关掉之后卡面照常渲染（文字像素 16008、卡面完好），
用的是蓝图自己的颜色和贴图，尺寸依然是整齐的 112px。

⚠️ 关掉 `bCppDrivesAppearance` 后，这些**动态表现也一起归你**了：
打不出时变淡（`Dim` 的显隐）、悬停提亮、费用不足时数字变红。
只想调静态配色、保留这些动态反馈的话，**别关它**，
而是直接改 `HexCardLayout.h` 里的颜色常量（那是 C++ 和生成器的共同数据源）。

---

## 卡面文字用变量绑定 DataTable（按你的要求）

现在卡面文字可以**完全由蓝图绑定**，不再依赖 C++ 逐个 `SetText`。

做法：设计器里选中 TextBlock → 右侧 **Content** 分类的 `Text` 属性
→ 点 **Bind** → 选对应函数：

| 绑这个函数 | 内容 | 数据来自 |
|---|---|---|
| `GetCardName` | 卡名 | DataTable `DisplayName` |
| `GetCardDescription` | 描述（`{dmg}` 已实算） | DataTable `DescriptionTemplate` |
| `GetCardCostText` | 体力费用 | DataTable `EnergyCost`（含符文增减） |
| `GetCardRangeText` | 射程 | DataTable `TargetSpec` |
| `GetCardTypeText` | 类型名 | DataTable `CardType` |
| `GetCardTagText` | 消耗/基石角标 | DataTable 的两个 bool |
| `GetCardHotkeyText` | 快捷键 | 运行时手牌位置 |

还有状态查询，可绑到颜色/显隐上做条件表现：
`IsCardPlayable` / `IsCardSelected` / `IsCardHovered` / `GetCardTypeAccent`，
以及贴图 `GetCardFrameTexture` / `GetCardTypeIconTexture` / `GetCardArtworkTexture`。

绑好之后建议把 `bCppDrivesText` 关掉，否则 C++ 和绑定各写一次（内容一样，白做两遍）。

⚠️ 这些必须是 **BlueprintPure（纯函数）**。UMG 的属性绑定只接受纯函数 ——
带副作用的函数**在 Bind 下拉里根本不会出现**，而 UI 上不会告诉你为什么，
只是列表里找不到。这十四个已经都是纯函数了。

完整链路（改文案只改表，不用碰蓝图、不用编译）：

```
DT_Cards.DisplayName / DescriptionTemplate
  → FHexCardTableLoader 启动时覆写 FHexContentLibrary
  → FHexCardView::Make()（{dmg} 按当前 ATK 实算）
  → GetCardName() / GetCardDescription()
  → 蓝图的 Text 绑定
```

---

## Panel 蓝图里看不到卡牌——已修

`WB_HandPanel` 的 `HandBox` 在设计器里是空的，因为手牌是**运行时**
按实际手牌数量建出来的。于是你在里面调间距、改锚点全都看不到效果，只能盲摆。

现在加了 `DesignPreviewCardCount`（默认 4，在 Class Defaults 里改），
设计器里会摆出几张占位卡，用的是和运行时**同一套**间距与对齐，
所以预览出来的排列是作数的。假数据故意让描述长短不一，
方便看出"文字长了会不会把卡撑变形"。

⚠️ 只在设计器生效（`NativePreConstruct` 里判 `IsDesignTime`），
运行时一张都不建 —— 否则会多出几张点不动、uid 为 0 的假卡。
实测运行时仍是 3 张真卡，无泄漏。

---

## 横版固定卡（左侧三张）

左侧的攻击/守备/移动改成了**横版**样式，用你新加的
`RCard_Attack` / `RCard_Defend` / `RCard_Move`（256×130，比例约 1.95）。

**要手动调位置就打开 `Content/HexSpire/UI/WB_CardWide`**，
它已经自动建好并填入了完整控件树（12 个控件），可以直接拖。

### 为什么单独一个类，而不是把竖版卡缩小

因为**控件树结构不同**，不只是尺寸不同：

```
竖版 WB_Card        横版 WB_CardWide
图标在上             图标在左
卡名                 ┌ 卡名
类型                 └ 描述      ← 右侧一栏
描述                 快捷键在最右
射程
```

之前固定卡是竖版卡 `SetBaseScale(0.72)` 缩出来的——那只是变小，
比例仍是 168×232（竖）。套上 1.95 的横版贴图会**整张拉伸变形**，
而美术会以为是自己导错了图。现在它本身就是 196×100 的横版控件，
按原尺寸画，那个 `SetBaseScale` 和配套的"负下边距 hack"都删了。

### 控件名（同样不能改）

`Frame` / `TypeIcon` / `Cost` / `CostBadge` / `Name` / `Desc` / `Hotkey` / `Dim`

⚠️ 横版**故意没有** `Range` 和 `TypeLabel`——地方太小放不下，
而固定卡的射程和类型玩家早就记住了（就那三张）。
因为用的是 Optional 绑定，少这两个控件不会崩，`ApplyView` 里会自动跳过。

### 文字区的内边距是量出来的

`HexCardLayout::R::ContentPad` 那几个比例不是猜的：
对 `RCard_Attack` 做亮度采样，中间留白区（亮度约 215）大致占
x 的 17%~92%、y 的 17%~83%，四周是暗色边框。
文字必须落在留白区内——压到暗边框上会读不出来。
你在设计器里挪位置时，这个范围可以参考。

### 顺带修的两件事

**① 生成器现在会自动建缺失的蓝图。** 以前要求你先去编辑器手工新建、
父类选对、名字拼对、目录放对——四个地方能出错，而错了的表现是
静默回退（界面正常、改动不生效、无报错）。上一轮就因为
`WB_Card` vs `WBP_HexCard` 名字对不上白折腾了一轮。能自动做对的不留给手工。

**② 修了一个潜伏的 unity build 编译错误。**
`HexUnitVisual.cpp` 和 `HexBoardVisual.cpp` 各有一个同名的
`BasicMatPath`，都在匿名命名空间里——但 `const TCHAR*` 在命名空间作用域
仍是**外部链接**，unity build 把两个 .cpp 合进同一编译单元时就重定义报错。
加 `static` 修掉。这个 bug 一直潜伏着，我新增一个无关的 .cpp
就把它触发了出来。

---

## 左上角头像/字样 + 顶栏（本次新增）

打开 `Content/HexSpire/UI/WB_HandPanel`，顶栏已经在里面了，直接拖。

结构（字样在头像**左边**，都在顶栏最左）：

```
PanelRoot (CanvasPanel)
├─ TopBar (Border)              锚顶边左右拉伸，高 96px
│  └─ TopRow (HorizontalBox)
│     ├─ NameArtBox (ScaleBox)  ← 字样竖条
│     │  └─ NameArtStack (Overlay)
│     │     ├─ NameArt_Warden      (可见)
│     │     ├─ NameArt_Medium      (Collapsed)
│     │     ├─ NameArt_Scrivener   (Collapsed)
│     │     └─ NameArt_Revenant    (Collapsed)
│     ├─ PortraitBox (SizeBox 76x76) └─ Portrait  ← 头像
│     └─ StatCol (VerticalBox)  HP/血条/腐蚀度/层数/卡组/符文/回合/体力/牌堆
├─ StatusText (TextBlock)       顶栏下方的状态提示
├─ HandBox / LeftCol            手牌与固定卡（原有，未改动）
```

### 换角色只改显隐

四个角色的头像与字样**都已经摆进蓝图**了，三组是 `Collapsed`。
换角色时把要显示的那组改成 Visible、其余 Collapsed 即可，不用重新指定贴图。

⚠️ 用 `Collapsed` 而不是 `Hidden`。Hidden 仍然**占位**，
三张隐藏的字样会把可见那张挤偏，而且挤多少取决于哪张最宽 —— 换角色时位置还会跳。

设计器里想预览别的角色：Class Defaults 里改 `DesignPreviewHeroIndex`（0~3）。

### 字样不要写死宽高

四张字样的比例差异很大，是实测值：

| | 尺寸 | 宽高比 |
|---|---|---|
| Warden | 289×799 | 0.362 |
| Medium | 311×760 | 0.409 |
| Scrivener | 296×566 | 0.523 |
| Revenant | 286×836 | 0.342 |

所以字样套在 `ScaleBox`（ScaleToFit）里按原比例缩放。
**给 Image 设 Desired Size 会把这 0.34~0.52 的差异压成同一个形状**，
三个角色的字样会变形 —— 而那看起来像是美术导错了图。
要调大小请改 `NameArtBox` 的槽位尺寸，不要改 Image。

头像是近正方形（617×611 等，比例≈1.01），所以它反而可以按方形槽位画。

### 资产名有两处不一致（不是笔误）

- 头像叫 `UI_Revanat_头像`（Rev**a**nat）
- 字样叫 `UI_Revenat_字样`（Rev**e**nat）

路径在 `Source/HexSpire/Public/UI/HexTopBarLayout.h` 里按实际名字写死了。
改资产名的话要同步改那里，否则 `LoadObject` 静默失败（回退 nullptr → 不画图层，不报错）。

### 顶栏文字已经全部 Bind 好了

生成器把 14 条属性绑定一起写进了资产，**不需要手工点 Bind**：

| 控件 | 属性 | 绑定函数 |
|---|---|---|
| `HeroHP` | Text / ColorAndOpacity | `GetHeroHPText` / `GetHeroHPSlateColor` |
| `HPBar` | Percent / FillColorAndOpacity | `GetHeroHPPercent` / `GetHeroHPColor` |
| `Corruption` | Text / ColorAndOpacity | `GetCorruptionText` / `GetCorruptionSlateColor` |
| `CorruptionEffect` | Text | `GetCorruptionEffectText` |
| `FloorInfo` / `DeckInfo` / `RuneInfo` | Text | `GetFloorText` / `GetDeckText` / `GetRuneText` |
| `RoundNum` / `EnergyText` / `PileInfo` | Text | `GetRoundText` / `GetEnergyText` / `GetPileText` |
| `StatusText` | Text | `GetStatusText` |

⚠️ 颜色有 `*Color`（FLinearColor）和 `*SlateColor`（FSlateColor）两个版本，
**不是冗余**：TextBlock 的 `ColorAndOpacity` 委托要 `FSlateColor`，
ProgressBar 的 `FillColorAndOpacity` 要 `FLinearColor`。
绑错类型会在编译蓝图时报 `the sigatnures don't match`。

想让顶栏外观完全归蓝图：Class Defaults 里关掉 `bCppDrivesTopBar`
（同卡面的 `bCppDrivesAppearance`，不关的话 C++ 每帧会把值刷回去）。

### 为什么顶栏从 Canvas 搬过来了

**因为 HUD 的 Canvas 绘制永远画在所有 UMG 之上。**

顶栏原先是 `AHexDemoHUD::DrawTopBar` 画的一条 0,0→全宽×74 的实心面板。
只要它还在，任何摆在左上角的 UMG 控件都会被它整块盖住 —— 而且**不报错**：
控件存在、贴图加载成功、自检全过，屏幕上就是看不见。
所以"头像放左上角"和"顶栏用 Canvas 画"没法共存，`DrawTopBar` 已删除。

⚠️ 顺带搬的还有**状态提示**。它原先画在 `DrawTopBar` 内部（14,80），
是 `GetStatusMessage()` 的**唯一**显示点 —— 漏掉它会让全部操作反馈
（阵亡/胜利/体力不足/目标不合法/房间切换）无声消失。
现在归 `StatusText`，自检里有一条专门盯它的告警。

⚠️ `DrawPileBrowser` 原先写死 `Y=120`（那是"74px 顶栏之下"的隐含依赖），
现在改读 `HexTopBarLayout::PileBrowserTop`。改顶栏高度时两处不会再各自漂移。

### 验证

```powershell
& "E:\UE\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
    "E:\UE_Proj\HexSpire\HexSpire.uproject" -game -HexAutoRoom `
    -unattended -nopause -nosplash -windowed -ResX=1600 -ResY=900 `
    -benchmark -benchmarkseconds=12 `
    -AbsLog="E:\UE_Proj\HexSpire\Saved\Logs\tb.log"
```

日志里应该有（战斗中与地图界面**都要**跑一遍 —— 去掉 `-HexAutoRoom` 就是地图界面）：

```
[自检] 顶栏自检（当前在战斗中）
[自检] 顶栏控件全部绑定成功
[自检] 顶栏贴图：头像=有 字样=有
[自检]   字样原始尺寸 289x799（比例 0.362）
[自检]   顶栏渲染尺寸 1921x96（高度常量 96）
```

⚠️ 顶栏自检故意与卡面自检**分开**：卡面那个在"非战斗直接 return"之后，
只在战斗中跑得到。而"顶栏在地图界面消失"正是这次改动最可能的回归
（顶栏原先跟着整块控件一起折叠），只在战斗里自检的话永远测不出来。

⚠️ **截图验证不了顶栏**。`HighResShot` 只抓场景渲染，不含 Slate 层 ——
顶栏没显示和"截图抓不到顶栏"在图片上完全一样，看图会得出错误结论。
日志自检是唯一可信的判据。
