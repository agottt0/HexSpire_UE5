// Copyright Hex Spire. All Rights Reserved.
//
// 单位外观的资产映射表 —— 表现层
//
// ══════════════════════════════════════════════════════════════════
// 为什么资产路径要集中在这一个文件里
// ══════════════════════════════════════════════════════════════════
// 我们现在借用的是模板目录（/Game/TurnBasedStrategyRPGTemplate/...）
// 里的动画。但按既定方针，模板最终是要【整个删掉】的。
//
// 如果把路径散在各个 .cpp 里，删模板那天会得到一堆"资产找不到"的
// 静默失败：单位变成 T-pose，但代码一行不报错，很难定位。
// 集中在这里之后，替换美术只需要改这一个文件。
//
// ══════════════════════════════════════════════════════════════════
// 为什么不用模板的 AnimBlueprint（ABP_BattlePawn）
// ══════════════════════════════════════════════════════════════════
// ABP_BattlePawn 的 AnimGraph 里，每个状态转移条件都从
// BP_CharacterBase（模板的角色基类）上读变量。它的取值方式是
// 对 TryGetPawnOwner() 做 Cast<BP_CharacterBase>。
//
// 我们的 AHexUnitVisual 是普通 AActor，不是那个类，
// 也不是 Pawn —— Cast 必然失败，所有变量取默认值，
// 结果是角色永远卡在 Idle（或 T-pose），而且【不报任何错】。
//
// 要用那个 ABP，就得让我们的单位继承模板的 Character，
// 那等于把模板的战斗逻辑一起拖进来，与"逻辑层纯 C++"直接冲突。
//
// 所以这里改用 USkeletalMeshComponent::PlayAnimation()
// （SingleNodeInstance 模式）：纯 C++ 驱动，不需要任何蓝图资产。
// 回合制战棋不需要 aim offset / 多层混合，单节点完全够用。

#pragma once

#include "CoreMinimal.h"
#include "Core/HexSpireEnums.h"

class USkeletalMesh;
class UAnimSequence;

// ══════════════════════════════════════════════════════════════════
// 引擎自带资产的路径 —— 表现层共用
// ══════════════════════════════════════════════════════════════════
// ⚠️ 为什么这几条路径必须放在头文件里，而不是各 .cpp 的匿名命名空间里：
//
//    UE 的 unity build 会把整个模块的 .cpp 【拼进同一个编译单元】
//    （见 Intermediate/Build/.../Module.HexSpire.cpp）。
//    拼接是文本级的 #include，所以两个 .cpp 里各自的
//    `namespace { ... }` 会【合并成同一个匿名命名空间】——
//    此时同名变量就是实打实的重定义：
//        error C2374: BasicMatPath: 重定义；多次初始化
//
//    加 `static` 不能解决。static 给的是内部链接，而这里的冲突发生在
//    【同一个编译单元内的同一个作用域】，属于名字重复声明，
//    和链接性无关 —— 两个 static 同名变量照样报 C2374/C2086。
//
//    这类 bug 是潜伏的：unity 的分组随文件数量变化，
//    新增一个无关的 .cpp 就可能把两个冲突文件分到一组从而引爆。
//    因此正确修法是【只保留一处定义】，用 inline 放进共用头文件。

namespace HexEngineAssets
{
	/** 带 Color 向量参数的基础材质，引擎必有 —— 可做成动态实例改颜色 */
	inline const TCHAR* BasicMaterial =
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

	/** 基础形状：默认均为 100×100×100，中心在原点 */
	inline const TCHAR* Cylinder = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	inline const TCHAR* Cube     = TEXT("/Engine/BasicShapes/Cube.Cube");
}

/** 单位的动画状态。刻意做得很小 —— 回合制只需要这几个。 */
enum class EHexUnitAnim : uint8
{
	Idle,
	Walk,
	Attack,
	Cast,
	Shoot,
	GetHit,
	Die,
};

/**
 * 一套角色外观（模型 + 动画）。
 *
 * 之所以把动画也放进"外观"里而不是全局共用一套：
 * 不同敌人将来要有各自的攻击动作（§13.2 要求玩家能从动作预判意图），
 * 现在虽然全指向同一批模板动画，但结构上已经允许按单位替换。
 */
struct FHexUnitAppearance
{
	/** 骨骼网格体资产路径 */
	const TCHAR* SkeletalMeshPath = nullptr;

	/** 各状态对应的 AnimSequence 路径。为空则该状态回退到 Idle。 */
	const TCHAR* IdlePath = nullptr;
	const TCHAR* WalkPath = nullptr;
	const TCHAR* AttackPath = nullptr;
	const TCHAR* CastPath = nullptr;
	const TCHAR* ShootPath = nullptr;
	const TCHAR* GetHitPath = nullptr;
	const TCHAR* DiePath = nullptr;

	/** 按状态取路径（找不到返回 nullptr） */
	const TCHAR* PathFor(EHexUnitAnim Anim) const;
};

namespace HexAppearance
{
	/**
	 * 按队伍与等级挑一套外观。
	 *
	 * ⚠️ 目前模板只有 2 个人形模型（男/女 Mannequin），没有专属怪物模型。
	 *    所以敌人之间【不能靠模型区分】，只能靠：
	 *      ① 体型缩放（S/M/L，见 HexUnitVisual::SizeForClass）
	 *      ② 脚下光圈颜色（杂兵/精英/Boss）
	 *    这也是为什么光圈不是装饰品而是必需品。
	 */
	HEXSPIRE_API const FHexUnitAppearance& For(EHexTeam Team, bool bIsElite, bool bIsBoss);

	/** 同步加载骨骼网格体，失败返回 nullptr（调用方须回退到灰盒） */
	HEXSPIRE_API USkeletalMesh* LoadMesh(const FHexUnitAppearance& Look);

	/** 同步加载某个状态的动画，失败返回 nullptr */
	HEXSPIRE_API UAnimSequence* LoadAnim(const FHexUnitAppearance& Look, EHexUnitAnim Anim);
}
