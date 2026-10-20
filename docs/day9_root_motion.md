# Day 9：把整体移动与骨盆动作拆开

## 今天解决的问题

原始 BVH 只有一个顶层 `mixamorig:Hips`。它同时保存：

- 角色在地面上的整体移动和朝向；
- 骨盆相对角色的上下起伏、侧移和身体倾斜。

UE 的 Root Motion 更适合使用一根始终位于地面的独立根骨。今天的处理会生成：

```text
MotionRoot                  地面轨迹与水平朝向
└── mixamorig:Hips          骨盆相对 MotionRoot 的局部动作
    └── 原来的其余骨骼
```

## 拆分公式

原始 `Hips` 的世界变换记作 `(R_source, p_source)`，新根骨变换记作
`(R_root, p_root)`。为了保证拆分前后动画完全相同，必须满足：

```text
R_source = R_root @ R_pelvis
p_source = p_root + R_root @ p_pelvis
```

因此骨盆局部变换不是简单相减，而是：

```text
R_pelvis = transpose(R_root) @ R_source
p_pelvis = transpose(R_root) @ (p_source - p_root)
```

这里的转置等于旋转矩阵的逆。乘逆旋转的作用，是把世界空间中的残差重新表达
到 `MotionRoot` 的局部坐标系里。

当前策略把水平 X/Z 位移和绕竖直 Y 轴的朝向交给 `MotionRoot`，把高度、身体
倾斜和其余局部运动留给 `Hips`。

## 可复现流水线

1. `root_motion.py` 计算根骨与骨盆的局部变换，并逐帧重建检查。
2. `root_motion_bvh.py` 生成带虚拟根骨的标准 BVH。
3. `blender_bvh_to_fbx.py` 将 BVH 的厘米单位换成 Blender 的米，规范为精确
   30 FPS，再导出 FBX。
4. `unreal_import_root_motion_fbx.py` 以相同比例导入骨架和动画，写入独立目录
   `/Game/MotionBridge/RootMotionSource`。
5. `unreal_validate_root_motion_source.py` 直接读取 UE 原始动画姿态进行验证。
6. `unreal_create_root_motion_retarget.py` 创建独立的 Source/Target IK Rig、
   IK Retargeter 和 `RootMotionGeneratorOp`，再烘焙 Manny 动画。
7. `unreal_validate_root_motion_retarget.py` 在 Manny 上逐帧验证 root 与 pelvis。
8. `unreal_create_root_motion_demo.py` 创建 `ACharacter`、地面和固定观察相机组成的
   `/Game/MotionBridge/Maps/L_MotionBridgeRootMotionDemo`。

源 Skeletal Mesh 按 FBX 的实际厘米比例导入，动画导入则使用 `0.01` 的转换比例。
这不是任意补偿：Blender 导出的骨架静态尺寸和动画平移通道采用了不同的量纲，
两处若使用同一比例，就会出现“角色高度正常但位移放大 100 倍”或相反的问题。

重定向时没有再建立单骨骼的 `Hips -> pelvis` 普通 FK 链。`Hips/pelvis` 已经分别
担任源与目标的重定向根，再添加一条 BodyRoot 链会让普通 FK 阶段覆盖 Root Motion
阶段刚计算出的结果。脊柱、颈、双臂和双腿仍由常规链负责。

## 当前验证结果

- 原 BVH：70 个节点、168 个通道、69 个采样点。
- 新 BVH：71 个节点、174 个通道，多出的正好是 `MotionRoot` 的 6 个通道。
- Python 逐帧检查：拆分前后所有原始关节的世界位置与世界旋转一致。
- Blender/FBX：56 根实际骨骼，69 个采样点。
- UE 源 Skeletal Mesh 高度约 `180.03 cm`。
- UE：69 个采样点；根骨水平轨迹约 94 cm；根骨竖直位移约为 0。
- UE 中 `Hips` 仍保留约 4.17 cm 的竖直起伏。
- Manny 烘焙动画：root 水平轨迹跨度 `91.866 cm`，Z 方向跨度为 0；pelvis
  局部仍保留约 `4.064 cm` 的竖直动作。
- 运行时 PIE：角色 Actor/胶囊离起点的最大水平距离为 `91.735 cm`，通过
  `50 cm` 的最低阈值；动作结束后净位移仅 `1.122 cm`，因为这段动作会走出去
  再回到起点附近。

资产级检查和真实 UE 视口播放均已完成。演示角色每帧从同一个 `UAnimSequence`
提取 Root Motion 增量，经 `CharacterMovement` 转换到世界空间并推动胶囊体；网格
播放时忽略内部 Root Motion，避免同一位移被消费两次。

这里还要区分“轨迹跨度”和“首尾净位移”：一次性提取整段 Root Motion 得到的是
首尾变换差，不能用来证明过程中没有移动。对于会返回起点的动作，运行时验收必须
记录整段播放期间的最大距离或累计路径长度。
