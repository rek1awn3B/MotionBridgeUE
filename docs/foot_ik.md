# 运行时 Foot IK

## 今天解决的问题

Root Motion 动画可以推动角色胶囊移动，但动画脚底默认只服从原始动作，无法自动
适应 UE 场景中的平地、斜坡和高低台阶。今天建立一条运行时 Foot IK 数据链：

```text
IK 前 Observe Bone 缓存的 foot_l / foot_r 位置
                 |
             向下射线
                 |
      命中点 + 地面法线 + 支撑脚权重
                 |
       世界空间转换为 Mesh 组件空间
                 |
  pelvis 修正 -> 左腿 Two Bone IK -> 左脚旋转
              -> 右腿 Two Bone IK -> 右脚旋转
                 |
              最终姿势
```

AnimGraph 在 Two Bone IK 之前用两个 `Observe Bone` 缓存原动画脚位置；
`UMotionBridgeFootIKComponent` 在 Mesh 求值完成后读取这份缓存并执行碰撞查询；
`UMotionBridgeFootIKAnimInstance` 再把查询结果转换为 AnimGraph 使用的组件空间。
骨骼修改始终留在 AnimGraph 中，避免网络线程或普通组件直接修改动画姿势。

当前 BVH 动作的根轨迹会在约 2.3 秒内走出约 94 cm，并连续掉头约 185° 后返回；
它适合验证 Root Motion，却会让固定相机难以观察脚部。两个 Foot IK
测试关卡因此使用同一重定向动画的原地模式：AnimInstance 仍正确移除根轨迹，角色
Actor 不再消费该轨迹；Root Motion 专用关卡继续保留真实位移和转向。

不能直接使用 IK 后的 `GetSocketLocation` 作为下一帧输入。那会形成反馈环：上一帧的
IK 输出会被误认为新的原动画位置，本帧身体继续移动后，腿便可能被拉成交叉。当前
实现只从 IK 前的 `Observe Bone` 读取原动画脚位置；支撑脚锁定是一个单独、可进入和
退出的状态，不会把 IK 输出反馈回输入。

同时要注意：原地播放并不会自动把源动作变成直线走路。这段 BVH 在约 2.3 秒内累计
掉头约 185°；实测左右脚相对身体的横向活动范围约为 21--25 cm。关闭 Actor 根运动
后身体留在原地，但摆动腿仍保留掉头动作原有的侧向落脚轨迹。

## 位置、膝盖和骨盆

每只脚使用鞋底后跟和鞋尖两个接触点，从两点上方分别向下进行
Visibility 射线检测。首先验证两个命中点是否属于同一支撑平面：

```text
plane_separation = abs(dot(toe_hit - heel_hit, average_normal))
supported = plane_separation <= 2 cm and normals_are_consistent
```

只有整只鞋底获得同一平面支撑时，目标垂直修正量才是：

```text
heel_offset = heel_ground_z + margin - predicted_heel_z
toe_offset  = toe_ground_z  + margin - predicted_toe_z
foot_vertical_offset = max(heel_offset, toe_offset)
```

接触点在 Manny 参考姿势中对齐鞋底，再转换到 `foot_l / foot_r` 的局部
空间。运行时它们会跟随脚骨的目标旋转，因此脚掌俯仰时仍能分别检查
鞋尖和后跟，不再用一个固定“脚骨到鞋底高度”代替整只脚。仅保留 `0.20 cm`
接触容差避免深度精度造成的闪烁。若前掌在台阶上、后跟在台阶下，则退出
该脚的完整支撑，不再用较高一侧把整只脚抬起并制造后跟悬空。

地面法线、脚掌朝向、锁定释放权重和脚目标的相对垂直偏移使用
与帧率无关的指数平滑：

```text
alpha = 1 - exp(-response_speed * delta_time)
smoothed = lerp(previous, target, alpha)
```

“不能穿地”是硬约束：当地面要求脚抬得更高时立即采用新偏移，只在目标下降时
保留平滑，避免射线噪声造成脚掌抖动。当前动画没有脚接触曲线，因此以“整只鞋底离地高度 + 脚骨
沿支撑面法线的速度”估计支撑相：鞋底在 `5--10 cm` 之间淡出，法线速度在
`10--60 cm/s` 之间淡出。不能固定使用世界 Z 速度；角色沿斜坡切线前进时本来就会
持续升高，但这不代表脚正在离开坡面。水平方向也叠加了角色根位移，因此不参与接触
判定。骨盆只参考当前支撑脚并且只向下补偿，避免被摆动脚拖成大幅上下晃动。膝盖
目标位于当前小腿前方，交给 Two Bone IK 决定弯曲平面。

地面检测首先生成“原动画 X/Y + 地面 Z”的基础目标。接触权重达到进入阈值时，
`UpdateFootLock` 只稳定当前支撑面的法线和脚掌朝向；目标 X/Y 每帧仍来自原
动画，Z 则按当前 X/Y 重新投影到该支撑平面。这一点对斜坡必不可少：如果只放开
X/Y 却锁死落脚瞬间的世界 Z，脚水平前进时就会逐渐陷入升高的坡面。完整的世界
坐标锁脚需要 locomotion、根运动和动画相位严格同步，不应塞进这个基础 Foot IK
组件。进入和退出仍采用不同阈值，避免边界附近反复切换。

## 脚掌对齐地面法线

不能把脚骨骼的局部 X/Z 轴直接解释成“脚尖/脚底”后重建绝对旋转。Manny 的骨骼
坐标轴带有绑定姿势偏置，左右脚还可能镜像；覆盖绝对旋转会让鞋底侧翻、脚尖外撇。

实现以动画姿态为基准，先将当前鞋底法线转换到世界空间，再计算它到地面
法线的旋转增量：

```text
animated_sole_normal = animated_foot_rotation * sole_normal_local
ground_tilt = clamp_angle(
    rotation_between(animated_sole_normal, ground_normal),
    25 degrees)
```

用该增量乘上原动画脚旋转，先得到完整的世界空间目标朝向；再把它转换到
Mesh 组件空间，由 Transform (Modify) Bone 的 Replace 模式直接写入。不在
AnimGraph 中使用跨帧的 Additive 增量，是因为 Two Bone IK 已先修改腿链，而组件数据与姿势
求值又相隔一帧，旧参考姿势不再可靠。因此即使平地的法线仍是世界 Up，支撑脚原本残留的
脚尖上翘也会被压平。接触权重进入锁定阈值时，同时捕获脚骨的世界旋转；
支撑期间保持该朝向，抵消原动画在落脚后仍然存在的脚踝摇晃，离地时再平滑释放。
斜坡对齐最大限制为 `25°`，避免台阶边缘或异常法线把脚踝猛然掰转。

## AnimGraph

动画蓝图 `/Game/MotionBridge/FootIK/ABP_MotionBridgeFootIK` 继承
`UMotionBridgeFootIKAnimInstance`，节点链为：

```text
Sequence Player
-> Local To Component
-> Observe Bone foot_l
-> Observe Bone foot_r
-> Transform pelvis
-> Two Bone IK foot_l
-> Transform foot_l rotation
-> Two Bone IK foot_r
-> Transform foot_r rotation
-> Component To Local
-> Output Pose
```

`MotionBridgeEditor` 编辑器模块可以通过 `-MotionBridgeBuildFootIKGraph` 重建、连接、
编译并保存这张图，避免依赖手工创建节点时的菜单歧义。

自然步态演示使用独立动画蓝图
`/Game/MotionBridge/FootIK/ABP_MotionBridgeNaturalWalkFootIK`，姿势与根位移都来自 Manny
自带的同一份 `MM_Walk_Fwd`。AnimInstance 使用 `RootMotionFromEverything`，由
CharacterMovement 消费 AnimGraph 当前帧产生的 Root Motion，不再把 `MM_Walk_InPlace`
与另一份动画的位移手工拼接。动画以 `0.75` 倍速播放，完整周期约 `3.64 s / 660 cm`；
这样腿部节奏和前进距离共享同一播放时钟，也不再使用手填的常速度。

## 验收结果

- 独立数学实验：`FOOT_IK_LAB=PASS`，包含射线目标构造、指数平滑以及 Two Bone IK
  的过远伸直、正常可达和过近折叠边界。
- 自然步态平地最终版本：2 秒统计中左/右脚均实际进入支撑状态，最大接触权重为
  `0.898 / 0.909`；完整周期 Root Motion 前进约 `660 cm`。支撑期相对首次落点的最大
  水平漂移仅为 `2.665 / 2.762 cm`，不再是原地动画配外部平移。最终视频为
  `artifacts/natural_walk_flat/native_root_motion_075.mp4`，近景为
  `artifacts/natural_walk_flat/native_root_motion_075_closeup.mp4`。
- 自然步态 10 度斜坡：完整周期前进约 `660 cm`、上升约 `116.38 cm`，与坡度几何
  关系一致；左右脚检测到的最大地面倾角均为 `10.000°`。接触置信度只负责判断支撑
  相，进入支撑后 IK 解算权重为 `1.0`，退出时再由锁定释放曲线平滑衰减。
- 斜坡穿透的最终原因不是 BVH，也不是左右脚映射：旧实现放开 X/Y 却锁死落脚瞬间的
  世界 Z，脚在坡面支撑期水平前进 `24.116 / 29.809 cm` 时，地面随之升高约
  `4.2 / 5.3 cm`，鞋底却留在旧高度。修正后 Z 每帧按当前 X/Y 投影到支撑平面，
  并改为平滑相对动画的垂直偏移；需要抬高时立即满足最低安全高度，下降时才平滑。
- 最终姿势直接从 Skeletal Mesh 的 IK 后 Socket 计算鞋跟/鞋尖到支撑面的有符号距离，
  左右脚最小间隙为 `+0.194 / +0.200 cm`，防穿透条件已加入运行时 `PASS` 门槛。
  最终关闭/开启 IK 脚部近景对照为
  `artifacts/natural_walk_slope/slope_ab_final_closeup.mp4`，抽帧总览为
  `artifacts/natural_walk_slope/slope_ab_final_closeup_sheet.png`；正式验收日志为
  `artifacts/natural_walk_slope/SlopeFinalAcceptanceRetry.log`。
- 连续小起伏地面由两个约 `4.895 cm` 高/深、`4°` 的短缓坡组成，不包含不可连续
  求解的台阶棱边。完整周期 Root Motion 前进 `660 cm`，最终垂直位移 `0.30 cm`；
  IK 后左右脚最小有符号间隙为 `+0.188 / -0.012 cm`，后者位于 `0.05 cm` 数值容差内，
  运行时验收为 `PASS`。固定相机近景 A/B 为
  `artifacts/natural_walk_uneven/uneven_ab_closeup.mp4`，抽帧总览为
  `artifacts/natural_walk_uneven/uneven_ab_closeup_sheet.png`。
- 旧的 15 cm 左右分割台阶仍只作为无解姿势的压力测试：同一只刚性脚掌跨在上下
  两面时不存在同时贴合的解，不能作为“自然走台阶”的视觉验收。
- 先前“平地 Walk 动画 + 胶囊碰撞顶上 10 cm 台阶”的影像同样撤出验收。Foot IK
  只能贴地，不能凭空生成抬腿轨迹；真正走台阶需要台阶动画或预测摆动脚轨迹，再另行
  验证。旧影像仅保留为失败诊断证据。
- 10 度斜坡和小范围连续起伏地面都已完成数值与 A/B 视觉复验。验收边界仍限于
  连续支撑面，不把结果外推为台阶或任意不连续地形也通过。

测试关卡：

- `/Game/MotionBridge/Maps/L_MotionBridgeRootMotionDemo`
- `/Game/MotionBridge/Maps/L_MotionBridgeFootIKSlopeTest`
- `/Game/MotionBridge/Maps/L_MotionBridgeFootIKStepTest`
- `/Game/MotionBridge/Maps/L_MotionBridgeNaturalWalkFootIK`
- `/Game/MotionBridge/Maps/L_MotionBridgeNaturalWalkFlat`
- `/Game/MotionBridge/Maps/L_MotionBridgeNaturalWalkSlope`
- `/Game/MotionBridge/Maps/L_MotionBridgeNaturalWalkUneven`

无渲染自动验收会强制 Skeletal Mesh 使用
`AlwaysTickPoseAndRefreshBones`，因此即使 NullRHI 下角色不可见，骨骼姿势、射线、
AnimInstance 和 Root Motion 仍会真实逐帧更新。
