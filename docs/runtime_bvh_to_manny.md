# 原始 BVH 到 Manny 的运行时骨架映射

## 目标

实时服务必须从当前 BVH 帧计算 Manny 姿势，而不是先导出整段 Manny 动画再把它包装成网络流。当前实现将“离线求校准参数”和“运行时求每一帧姿势”分开：离线阶段只生成固定映射；运行时阶段只读取原始 BVH 和固定映射。

## 第一级：BVH 到规范源骨架

`python/bvh_stream.py` 使用项目自己的 BVH parser 读取层级、`OFFSET`、通道顺序和每帧数值。Euler 通道按文件声明顺序组合，FK 得到每个源关节的局部/全局姿势。

不同格式对前向轴、上方向和关节局部坐标系的约定不同。`100style_source_calibration.json` 保存 100STYLE BVH 到规范源骨架的静态坐标变换。该阶段输出 25 个源骨骼以及独立的角色根轨迹。

## 第二级：规范源骨架到 Manny

`python/build_manny_retarget_calibration.py` 读取一组时间对齐的源姿势和 Manny 参考结果，为每个有语义对应的目标骨骼拟合两个常量旋转矩阵：

```text
R_target_global(j, f) = A_j * R_source_global(j, f) * B_j
```

其中 `j` 是骨骼，`f` 是帧；`A_j` 与 `B_j` 在校准完成后固定。运行时先用当前 BVH 帧算出源骨全局旋转，再代入上式。得到目标全局旋转后，按 Manny 父子关系还原局部旋转：

```text
R_target_local(j, f)
  = inverse(R_target_global(parent(j), f)) * R_target_global(j, f)
```

最终输出 Manny 的 89 骨数组。26 个主要关节由 BVH 动态驱动；没有 BVH 通道的 Manny 扭转骨、手指和 IK 辅助骨保持目标参考姿势。

## Root Motion

根轨迹不塞进骨架 `root` 后再让网格自行离开胶囊。服务将水平位移和朝向作为帧级 `root_position` / `root_rotation` 发送；UE 角色在游戏线程建立首帧基准，并通过 Actor/CharacterMovement 消费增量。骨架内 `root` 保持原点，`pelvis` 保留身体局部起伏，从而避免两次应用同一位移。

## 离线文件和运行时文件的边界

校准输入：

- `artifacts/realtime/source_reference_stream.json`
- `artifacts/realtime/manny_root_motion_stream.json`

运行时输入：

- `data/bvh/100style/Neutral_FW_frames_470_694.bvh`（默认公开演示）
- `data/calibration/100style_source_calibration.json`
- `data/calibration/100style_to_manny_calibration.json`

`tools/motionbridge_websocket_server.py` 的默认 BVH 路径会进入 `build_manny_stream_from_bvh()`。这条函数先从原始 BVH 创建规范源姿势，再应用固定 Manny 校准；它不打开两份参考姿势流。

两级校准和自动验收继续使用其原始对齐窗口
`Neutral_FW_frames_530_694.bvh`，以保证保存的校准误差可复现；默认演示只是在
同一原始文件中向前扩展到第 470 帧，不改变骨架、通道顺序或采样率。

## 可复现验证

```powershell
py -3.12 -m unittest discover -s python -p "test_bvh_stream.py" -v
.\scripts\run_realtime_acceptance.ps1
```

Python 测试验证原始 BVH 帧数、源骨输出、89 骨 Manny 输出、有限数值和 Root Motion。UE 端到端验收额外要求：

- `direct_manny_pose=1`
- `runtime_retarget=0`
- `stream_bones=89`
- `snapshot_bones=89`
- 拒绝帧为 0
- Foot IK 后两脚最小鞋底间隙为正
- 强制断线后完成三次重连

当前校准对这段 100STYLE 动作的最大旋转误差为 `7.157°`，最差骨骼为 `foot_l`。该数字写入校准 JSON 并由生成脚本重新计算，而不是人工填写。

## 限制

- 这是固定源/目标骨架对的标定映射，不是任意骨架自动重定向。
- 校准使用了同一段对齐动作，因此误差数字描述该验证片段，不代表未见动作上的泛化上界。
- 手指和 Manny 特有辅助骨没有原始 BVH 观测，不能声称被实时捕捉。
- Foot IK 负责环境接触修正，不改变上游骨架映射本身。
