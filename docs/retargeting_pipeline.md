# Blender 转换与 UE 动作重定向

## 数据链路

```text
BVH
  -> Blender 导入与坐标转换
  -> 带骨架和调试网格的 FBX
  -> UE 源 Skeletal Mesh / Skeleton / AnimSequence
  -> Source IK Rig
  -> IK Retargeter
  -> Target IK Rig（UE5 Manny）
  -> Manny AnimSequence
  -> 演示关卡循环播放
```

前半段的 BVH 解析、Euler 通道组合和 FK 是本项目自己实现的；UE IK Retargeter
负责在两个拓扑、命名和比例不同的骨架之间迁移动作。两者不是同一层工作。

## Blender 转换约定

- BVH：Y-up，厘米尺度。
- Blender：Z-up；导入时使用 `axis_forward=-Z`、`axis_up=Y`。
- FBX：`-Z Forward / Y Up`，全局缩放为 1，不额外应用单位缩放。
- 源数据：55 个具名关节、69 个动作采样、约 30 FPS。
- 导出时不增加 Leaf Bones，保留 Root Motion。
- 为保证 UE 创建 Skeletal Mesh，在每个关节头生成一个很小的八面体调试网格，
  并将每个八面体 100% 蒙皮到对应关节。

转换后的骨骼头包围盒约为 `56.32 x 26.61 x 175.94`，符合厘米制人体尺度。

## IK Rig 链

| 语义链 | BVH 源骨架 | UE5 Manny |
|---|---|---|
| Root | Hips -> Hips | pelvis -> pelvis |
| Spine | Spine -> Spine2 | spine_01 -> spine_05 |
| Neck | Neck -> head | neck_01 -> head |
| LeftArm | LeftShoulder -> LeftHand | clavicle_l -> hand_l |
| RightArm | RightShoulder -> RightHand | clavicle_r -> hand_r |
| LeftLeg | LeftUpLeg -> LeftToeBase | thigh_l -> ball_l |
| RightLeg | RightUpLeg -> RightToeBase | thigh_r -> ball_r |

源 Retarget Root 为 `Hips`，目标 Retarget Root 为 `pelvis`。链名相同以后，
Retargeter 可以做精确自动映射，而不要求源/目标骨骼名相同。

## 源根高度校准

这份 BVH 的层级声明把根关节 `Hips` 的静态 `OFFSET` 写成 `(0, 0, 0)`，
但动作第一帧的根位置是 `(-0.2399, 98.2948, -0.5224)` cm。也就是说，
约 98.29 cm 的髋部离地高度保存在动作通道中，而不在不带动画的参考姿势中。

IK Retargeter 会先用参考姿势估计源、目标骨架的根高度和比例。若直接使用
`Default Pose`，它看到的源 `Hips` 在地面，目标 Manny 的 `pelvis` 却在约
95.85 cm 高处，因而会给出根高度警告，并可能把动画位移错误放大。

脚本因此额外创建源重定向姿势 `BVH_Grounded`，只在该校准姿势中给 `Hips`
增加 `98.2948` cm 的 Z 偏移。原始 BVH、源 AnimSequence 以及逐帧 Root Motion
都不修改；`Default Pose` 也保留，便于对照和回退。脚本每次运行都会先重置
`BVH_Grounded` 再写入偏移，避免重复执行时累计高度。

## UE 资产与验证

- 源动画：2.233333 秒，30 FPS，68 个关键采样。
- 重定向动画：同样为 2.233333 秒，并绑定 Manny Skeleton。
- 7 条目标链全部映射到同名源链。
- 68 帧、89 条目标骨骼轨道逐帧检查，平移、旋转和缩放均为有限有效值。
- 演示关卡：`/Game/MotionBridge/Maps/L_MotionBridgeDemo`。

UE 5.5 的命令行 `-nullrhi` 模式不能用于“已注册到世界中的骨骼网格播放”验收；
它会在世界 Tick 时崩溃。资产检查可以使用 Null RHI，最终播放验收使用真实 D3D
渲染路径完成。

## 自动化脚本

- `python/blender_bvh_to_fbx.py`：BVH 导入、调试网格生成与 FBX 导出。
- `python/blender_validate_fbx.py`：在干净 Blender 场景中重新导入并校验 FBX。
- `python/unreal_import_motionbridge_fbx.py`：导入源骨架、网格和动画。
- `python/unreal_create_retarget_assets.py`：创建 IK Rig、Retargeter 并烘焙 Manny 动画。
- `python/unreal_validate_import.py`：检查资产类型、Skeleton、时长与链映射。
- `python/unreal_validate_animation_pose.py`：逐骨逐帧检查重定向姿态。
- `python/unreal_create_demo_level.py`：创建可直接播放的演示关卡。

## 已知视觉检查项

自动化只能证明资产结构、映射和数值有效。打开 UE 后仍要人工观察：

- 初始姿态是否需要额外 A/T Pose 校正；
- 脚底是否贴地、是否有脚滑；
- 肩、肘、膝的弯曲方向是否自然；
- Root Motion 的方向和尺度是否符合预期；
- 手指未单独建链时，继承姿态是否可接受。
