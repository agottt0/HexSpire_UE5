# UI 资产需求清单

第一章「黄泉线」基调：冷调蓝灰 + 暖琥珀高亮，地铁瓷砖与锈蚀钢材，东方怪谈 × 现代城市。
全部 Prompt 都要带这条风格锚点，否则各人生成的资产会拼不到一起。

---

## 命名与目录规范

资产放 `Content/ArtResource/UI/<分类>/`，贴图前缀 `T_`，**文件名一律用英文**。

理由不是洁癖：C++ 侧靠硬编码路径 `LoadObject` 取资产（见 `Source/HexSpire/Private/UI/HexCardArt.cpp`），
**改名/挪目录 = 静默失败** —— 卡面变成纯色块，一行错误都不报。
要改名请连带改 `HexCardArt.cpp`，那里是路径的唯一来源。

### 导出规格

| 项 | 要求 |
|---|---|
| 格式 | **PNG**，带 Alpha |
| 色彩空间 | sRGB（颜色贴图）；数据贴图（遮罩/粗糙度）必须 **关掉 sRGB** |
| 尺寸 | 2 的幂（64 / 128 / 256 / 512） |
| 九宫格资产 | 可拉伸区必须是**纯色**，否则拉伸出来会有渐变断层 |

> **⚠️ 现有 4 张卡牌贴图需要重导。**
> `Card_White` / `Attack` / `Defend` / `Move` 目前是 **JPEG，无 Alpha 通道**（实测确认：151×256 与 167×256，3 通道）。
> 无 Alpha 意味着它们只能当矩形底衬 —— **做不了圆角、描边、异形卡框**，因为那些都需要 Alpha 把边缘抠掉。
> 这是 P0 项：先把这 4 张重导成带 Alpha 的 PNG，卡面才能脱离「白色方块」的观感。

---

## 一、卡牌

| 资产名 | 用途 | 尺寸 | Alpha | 九宫格 | 优先级 |
|---|---|---|---|---|---|
| `T_CardFrame_Common` | 普通卡框 | 512×720 | ✔ | ✘ | **P0** |
| `T_CardFrame_Uncommon` | 精良卡框 | 512×720 | ✔ | ✘ | P1 |
| `T_CardFrame_Rare` | 稀有卡框 | 512×720 | ✔ | ✘ | P1 |
| `T_CardFrame_Epic` | 史诗卡框 | 512×720 | ✔ | ✘ | P2 |
| `T_CardFrame_Legendary` | 传说卡框 | 512×720 | ✔ | ✘ | P2 |
| `T_CardFrame_Cursed` | 诅咒卡框 | 512×720 | ✔ | ✘ | P1 |
| `T_CardIcon_Attack` | 攻击（重导带 Alpha） | 256×256 | ✔ | ✘ | **P0** |
| `T_CardIcon_Guard` | 守备（重导带 Alpha） | 256×256 | ✔ | ✘ | **P0** |
| `T_CardIcon_Move` | 移动（重导带 Alpha） | 256×256 | ✔ | ✘ | **P0** |
| `T_CardIcon_Skill` | 技能 | 256×256 | ✔ | ✘ | **P0** |
| `T_CardIcon_Stance` | 姿态 | 256×256 | ✔ | ✘ | **P0** |
| `T_CardIcon_Derived` | 衍生 | 256×256 | ✔ | ✘ | **P0** |
| `T_CardIcon_Curse` | 诅咒 | 256×256 | ✔ | ✘ | **P0** |
| `T_CostBadge` | 左上角费用圆形底衬 | 128×128 | ✔ | ✘ | **P0** |
| `T_CardCorner_Exhaust` | 「消耗」角标 | 128×64 | ✔ | ✘ | P1 |
| `T_CardCorner_Cornerstone` | 「基石」角标 | 128×64 | ✔ | ✘ | P1 |
| `T_CardMask_Disabled` | 不可用遮罩 | 64×64 | ✔ | ✔ | P1 |
| `T_CardGlow_Selected` | 选中光边 | 256×256 | ✔ | ✔ | P1 |
| `T_CardBack` | 卡背（抽牌堆） | 512×720 | ✘ | ✘ | P1 |

**卡类型图标现在只有 3 张。** 技能/姿态/衍生/诅咒会回退到不画图标（`HexCardArt::GetTypeIcon` 返回 `nullptr`）——
刻意不拿「攻击」图标顶替，因为错的图标比没有图标更糟：玩家会把技能卡误读成攻击卡。

### Prompt

**卡框（按稀有度换材质与光泽，构图保持一致）**
```
ornate painted trading card frame, vertical portrait orientation, east-asian folk horror meets modern metro,
subway tile and rusted steel motifs, cold desaturated blue-grey palette with warm amber accents,
empty center area left blank for artwork and text, low-contrast region in upper-left for a number overlay,
thin engraved border, subtle paper grain, transparent background, no text, no numbers, game UI asset
```
稀有度差异追加词：
- Common → `plain riveted steel border, matte finish`
- Uncommon → `brushed brass inlay, faint verdigris patina`
- Rare → `deep indigo enamel with silver filigree`
- Epic → `carved obsidian with violet inner glow`
- Legendary → `gilded talisman frame, paper charm strips, faint ember glow`
- Cursed → `cracked lacquer, black ichor seeping from the edges, off-kilter asymmetry`

**卡类型图标（7 种，剪影必须两两可区分）**
```
flat vector icon, single centered silhouette, readable at 64px, no text, transparent background,
east-asian folk horror meets modern metro, cold blue-grey with warm amber accent,
bold simple shape, high contrast, thick even line weight, game UI icon
```
各类型主体：
- 攻击 → `a downward slashing cleaver blade`
- 守备 → `a hexagonal riot shield with a talisman seal`
- 移动 → `a footprint inside a hexagon with motion arcs`
- 技能 → `an open palm emitting three concentric ripples`
- 姿态 → `a standing figure silhouette inside a stance circle`
- 衍生 → `two nested hexagons splitting apart`
- 诅咒 → `a broken paper talisman with a weeping eye`

**费用底衬**
```
circular UI badge base, dark recessed metal disc with thin amber rim light,
low-contrast flat center for a bold number overlay, subway steel and enamel,
transparent background, no text, no numbers, game UI asset, readable at 48px
```

---

## 二、战场

| 资产名 | 用途 | 尺寸 | Alpha | 九宫格 | 优先级 |
|---|---|---|---|---|---|
| `T_HexHighlight_Legal` | 合法目标高亮 | 256×256 | ✔ | ✘ | **P0** |
| `T_HexHighlight_MoveDest` | 移动落点 | 256×256 | ✔ | ✘ | **P0** |
| `T_HexHighlight_Danger` | 敌人威胁区 | 256×256 | ✔ | ✘ | **P0** |
| `T_HexOutline_Footprint` | footprint 描边 | 256×256 | ✔ | ✘ | **P0** |
| `T_Terrain_Wall` | 墙 | 256×256 | ✔ | ✘ | P1 |
| `T_Terrain_Pit` | 深坑 | 256×256 | ✔ | ✘ | P1 |
| `T_Terrain_Rubble` | 碎石 | 256×256 | ✔ | ✘ | P1 |
| `T_Terrain_ExitGate` | 出口 | 256×256 | ✔ | ✘ | P1 |
| `T_Hazard_Spikes` | 尖刺 | 128×128 | ✔ | ✘ | P1 |
| `T_Hazard_Fire` | 火焰 | 128×128 | ✔ | ✘ | P1 |
| `T_Hazard_Acid` | 腐蚀 | 128×128 | ✔ | ✘ | P1 |
| `T_Hazard_Ash` | 香灰 | 128×128 | ✔ | ✘ | P1 |
| `T_Ring_Player` | 玩家脚下光圈 | 256×256 | ✔ | ✘ | **P0** |
| `T_Ring_Minion` | 杂兵光圈 | 256×256 | ✔ | ✘ | **P0** |
| `T_Ring_Elite` | 精英光圈 | 256×256 | ✔ | ✘ | **P0** |
| `T_Ring_Boss` | Boss 光圈 | 256×256 | ✔ | ✘ | **P0** |

脚下光圈是 **P0 且不可省**：目前敌人之间没有专属模型（共用模板 Mannequin），
威胁等级只能靠光圈颜色 + 体型缩放区分（见 `HexUnitAppearance.h`）。

### Prompt

**格子高亮（六边形，中空，边缘发光）**
```
hexagonal tile highlight overlay, top-down view, hollow center, glowing beveled edge,
thin inner gradient falloff, transparent background, no text,
cold blue-grey base with a single accent hue, subtle scanline texture, game UI overlay, seamless flat lighting
```
用途差异：合法目标 `warm amber accent, steady solid edge` / 移动落点 `pale cyan accent, dashed edge segments` /
威胁区 `crimson accent, jagged inner hatching` / footprint 描边 `white accent, thick continuous outline only, fully transparent interior`

**地形与危害图标**
```
flat vector icon, single centered silhouette, readable at 64px, no text, transparent background,
top-down tabletop game tile marker, east-asian folk horror meets modern metro,
cold desaturated blue-grey with warm amber accent, bold readable shape
```
主体：墙 `a solid brick barrier segment` / 深坑 `a dark jagged hole with depth rings` /
碎石 `scattered angular debris chunks` / 出口 `a subway turnstile arch with an exit arrow` /
尖刺 `upward metal spikes` / 火焰 `a stylized flame tongue` /
腐蚀 `a dripping corrosive splash` / 香灰 `drifting incense ash flakes with a smoldering joss stick`

**脚下光圈**
```
circular ground ring decal, top-down view, hollow center, soft outer glow, thin bright rim,
transparent background, no text, subtle rotating rune engraving,
east-asian talisman circle, flat even lighting, game VFX decal
```
等级差异：玩家 `calm cyan-white glow, clean single ring` / 杂兵 `dim grey-blue glow, plain thin ring` /
精英 `amber glow, double ring with short tick marks` / Boss `deep crimson glow, triple ring with dense seal script`

---

## 三、单位浮层

| 资产名 | 用途 | 尺寸 | Alpha | 九宫格 | 优先级 |
|---|---|---|---|---|---|
| `T_Bar_HealthFill` | 生命条填充 | 128×32 | ✘ | ✔ | **P0** |
| `T_Bar_BlockFill` | 格挡条填充 | 128×32 | ✔ | ✔ | **P0** |
| `T_Bar_Back` | 血条底衬 | 128×32 | ✔ | ✔ | **P0** |
| `T_Intent_Attack` | 意图·攻击 | 128×128 | ✔ | ✘ | **P0** |
| `T_Intent_MultiAttack` | 意图·多段 | 128×128 | ✔ | ✘ | **P0** |
| `T_Intent_Buff` | 意图·强化 | 128×128 | ✔ | ✘ | **P0** |
| `T_Intent_Debuff` | 意图·削弱 | 128×128 | ✔ | ✘ | **P0** |
| `T_Intent_Summon` | 意图·召唤 | 128×128 | ✔ | ✘ | P1 |
| `T_Intent_Move` | 意图·移动 | 128×128 | ✔ | ✘ | **P0** |
| `T_Intent_Rotate` | 意图·转向 | 128×128 | ✔ | ✘ | P1 |
| `T_Intent_Special` | 意图·特殊 | 128×128 | ✔ | ✘ | P1 |
| `T_Intent_Sleep` | 意图·休眠 | 128×128 | ✔ | ✘ | P1 |
| `T_Brush_IntentDashed` | 意图连线·虚线笔刷 | 64×16 | ✔ | ✔ | **P0** |
| `T_Brush_IntentArrow` | 意图连线·箭头端点 | 64×64 | ✔ | ✘ | **P0** |
| `T_Status_<StatusId>` | 状态图标（逐个） | 128×128 | ✔ | ✘ | P1 |

**意图图标是 §13.2 点名的硬需求**，且实线 vs 虚线的区分（锁定格子=可躲 / 锁定单位=追踪）
必须靠**形状**而非仅靠颜色 —— 这是为色弱玩家考虑的硬约束。

**状态图标先别开工。** 它按 `StatusId` 一对一配，
需要策划先冻结状态表（当前状态定义在 `HexStatusData.cpp`，仍在增删）。
表没冻结就画，返工率极高。

### Prompt

**意图图标（画在敌人头顶，必须在小尺寸下可读）**
```
flat vector icon, single centered silhouette, readable at 64px, no text, transparent background,
enemy intent indicator for a tactics game, bold aggressive shape, thick even line weight,
cold blue-grey with a strong warm accent, high contrast against dark backgrounds
```
主体：攻击 `a single downward dagger` / 多段 `three stacked chevron slashes` /
强化 `an upward arrow inside a shield` / 削弱 `a downward arrow over a cracked circle` /
召唤 `a summoning circle with two rising wisps` / 移动 `a curved directional arrow with motion trail` /
转向 `a circular rotation arrow around a pivot dot` / 特殊 `an eight-pointed starburst sigil` /
休眠 `three rising sleep glyphs over a closed eye`

**连线笔刷**
```
horizontal dashed line brush strip, seamless tileable left to right, uniform dash spacing,
flat solid color with soft edge antialiasing, transparent background, no text, game UI line brush
```
```
arrowhead cap for a UI connector line, pointing right, solid flat shape, sharp tip,
transparent background, no text, matches a thin dashed line weight, game UI asset
```

**血条 / 格挡条**
```
horizontal bar fill texture for a nine-slice UI meter, flat solid center for clean stretching,
thin top highlight and bottom shadow only, no gradient along the horizontal axis,
transparent background, no text, game UI asset
```
差异：生命 `deep green with a bright top rim` / 格挡 `pale steel-blue, slightly translucent, faint hex hatch` /
底衬 `near-black recessed channel with a thin dark rim`

---

## 四、通用框架

| 资产名 | 用途 | 尺寸 | Alpha | 九宫格 | 优先级 |
|---|---|---|---|---|---|
| `T_Panel_Base` | 面板底衬 | 128×128 | ✔ | ✔ | **P0** |
| `T_Panel_Header` | 面板标题条 | 128×64 | ✔ | ✔ | P1 |
| `T_Button_Normal` | 按钮·常态 | 128×64 | ✔ | ✔ | **P0** |
| `T_Button_Hover` | 按钮·悬停 | 128×64 | ✔ | ✔ | **P0** |
| `T_Button_Pressed` | 按钮·按下 | 128×64 | ✔ | ✔ | **P0** |
| `T_Button_Disabled` | 按钮·禁用 | 128×64 | ✔ | ✔ | P1 |
| `T_EnergyCrystal_Full` | 体力·满 | 128×128 | ✔ | ✘ | **P0** |
| `T_EnergyCrystal_Empty` | 体力·空 | 128×128 | ✔ | ✘ | **P0** |
| `T_Pile_Draw` | 抽牌堆图标 | 128×128 | ✔ | ✘ | P1 |
| `T_Pile_Discard` | 弃牌堆图标 | 128×128 | ✔ | ✘ | P1 |
| `T_Pile_Exhaust` | 消耗区图标 | 128×128 | ✔ | ✘ | P1 |
| `T_Meter_Corruption` | 腐蚀度计量条 | 256×64 | ✔ | ✔ | P1 |
| `T_Glow_Rarity` | 稀有度光晕 | 256×256 | ✔ | ✘ | P2 |

> **体力已换成贴图，不再是文字 `◆◇`。**
> 现在用 `UI_体力槽`（1402×1122，带 Alpha 的蓝色火苗）画在**右下角**，
> 一点体力一个火苗，个数按 `FHexRuleBook::EnergyMax` 运行时决定。
> 顶栏里那行 `体力 ◆◆◇◇◇` 已折叠（控件与 `GetEnergyText()` 都留着做退路）——
> 留着会让同一个读数在屏幕上出现两次。
>
> **仍缺空态图。** 已消耗的体力目前是拿**同一张图染暗**（Alpha 0.28）顶着，
> 保留火苗轮廓好让玩家数得出上限。`T_EnergyCrystal_Empty` 那一格仍然是 P0：
> 染暗只是"能看出区别"，不是"看得清还剩几点"。
>
> `T_Button_*` 三态同理：结束回合按钮现在用 `UI_结束回合_3`
> （1266×1243，印章造型，**文字已烙进贴图**）一张图跑三态，只改染色。
> 它**不是九宫格资产**（边缘的尖角是造型的一部分），所以按 `Image` 画而非 `Box`。

### Prompt

```
nine-slice UI panel background, dark frosted glass over brushed steel, thin amber rim light on the border,
flat untextured center area safe for stretching, subway tile grout lines only near the edges,
transparent background, no text, cold desaturated blue-grey, game UI asset
```
```
nine-slice UI button, horizontal pill shape, brushed steel with enamel inlay,
flat stretchable center, thin beveled edge, transparent background, no text,
cold blue-grey with warm amber accent, game UI asset
```
三态差异：常态 `matte finish, dim rim` / 悬停 `brighter amber rim glow, slight inner lift` /
按下 `recessed inward, darker center, no rim glow` / 禁用 `desaturated grey, flat, no rim`

```
faceted crystal pip icon for an energy counter, single centered gem silhouette,
readable at 32px, transparent background, no text,
cold cyan inner light with amber edge refraction, game UI icon
```
空态追加：`hollow dark outline only, no inner light, dull grey facets`

---

## 五、符文与装备

| 资产名 | 用途 | 尺寸 | Alpha | 九宫格 | 优先级 |
|---|---|---|---|---|---|
| `T_RuneSlot_Empty` | 符文槽·空 | 256×256 | ✔ | ✘ | P1 |
| `T_RuneSlot_Filled` | 符文槽·已装 | 256×256 | ✔ | ✘ | P1 |
| `T_RuneBorder_<Rarity>` | 符文品质边框（5 档） | 256×256 | ✔ | ✘ | P2 |
| `T_EquipSlot_Weapon` | 武器槽 | 128×128 | ✔ | ✘ | P1 |
| `T_EquipSlot_Armor` | 盔甲槽 | 128×128 | ✔ | ✘ | P1 |
| `T_EquipSlot_Trinket` | 饰品槽 | 128×128 | ✔ | ✘ | P1 |

### Prompt

```
hexagonal socket base for a rune slot, top-down view, recessed dark interior,
thin engraved talisman script around the rim, transparent background, no text,
east-asian folk horror meets modern metro, cold blue-grey with amber rim, game UI asset
```
```
flat vector icon, single centered silhouette, readable at 64px, no text, transparent background,
equipment slot glyph, cold blue-grey with warm amber accent, bold simple shape
```
主体：武器 `a crossed cleaver and baton` / 盔甲 `a segmented riot vest torso` / 饰品 `a hanging talisman charm on a cord`

---

## 六、字体

**这是 P0 的隐藏风险项。**

`HexDemoHUD` 与卡牌控件目前用 `GEngine->GetMediumFont()`。
引擎自带的 Roboto **不含中文字形**，而 UMG 在缺字时画的是**空白**（不是方块）——
所以「卡名不见了」看起来像布局 bug 而非字体问题，排查一定会跑偏。
现在能显示中文只是因为引擎复合字体在中文 Windows 上回退到了系统字体，**不能依赖**。

正式版需要一套**授权可商用**的中文字体，覆盖范围：

| 范围 | 说明 |
|---|---|
| GB2312 一级+二级 | 6763 字，覆盖现代简体中文正文 |
| 标点与全角符号 | 中文引号、书名号《》、破折号、省略号 |
| 拉丁与数字 | 卡面数值会到三位数，数字必须等宽以免跳动 |
| 特殊符号 | `◆◇` 等（若继续用文字画体力条） |

字重至少 Regular + Bold（卡名用 Bold，描述用 Regular）。
建议候选：思源黑体 / 思源宋体（SIL OFL，可商用），衬线用于标题更贴「怪谈」调性。

---

## 验收标准

1. **64px 缩略图测试** —— 把图标缩到 64px，7 种卡类型两两之间仍能分辨。分不出就是剪影不够独特。
2. **灰度测试** —— 整套图标转成灰度后仍可区分。只靠颜色区分的一律打回（色弱玩家 + §13.2 硬要求）。
3. **数字叠加测试** —— 卡框左上角叠一个白色三位数（如 `188`），在任意卡面上都清晰可读。这是「低对比区」是否真的留出来的判据。
4. **Alpha 边缘测试** —— 在纯白和纯黑背景上各放一次，边缘无白边、无黑边、无锯齿硬边。
5. **九宫格拉伸测试** —— 拉到原尺寸 4 倍宽，中间区域无渐变断层、无纹理拉丝。
6. **深色底测试** —— 战场底色是深灰（地面 0.30,0.32,0.35 水泥灰），所有浮层图标在这个底上对比度足够。
7. **同框测试** —— 把同组资产（如 7 个卡类型图标）并排放一起，线宽、留白、视觉重量一致，不能有一张明显更粗或更满。
8. **命名与路径核对** —— 资产名与本清单完全一致；若有改动，`HexCardArt.cpp` 已同步更新（否则静默回退纯色块）。
