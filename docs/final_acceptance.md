# MotionBridge UE 最终验收报告

验收日期：2026-10-27（Asia/Shanghai）

## 结论

当前主链路已从“预先导出的 Manny 姿势流”改为“原始 BVH 在服务启动时转换为 Manny 姿势，再实时发送”。Python 转换、UE WebSocket 稳态、强制断线重连、Pose Snapshot、角色 Root Motion、Foot IK 和 Morph Target 均有可重复执行的自动验收。

## 被验收的真实数据路径

```text
raw BVH
 -> parser / channel decoder / FK
 -> source calibration
 -> source-global to Manny-global fixed rotation maps
 -> Manny local pose (89 bones)
 -> WebSocket
 -> UE queue / validation / interpolation
 -> Pose Snapshot + Actor root motion + Foot IK
```

实时服务显式接收 `--bvh-file`、`--calibration-file` 和 `--manny-calibration-file`。两份参考姿势流只用于重新生成校准，不是运行时输入。

## 自动化验收矩阵

| 层级 | 验收内容 | 结果 |
| --- | --- | --- |
| Python | `unittest discover` | PASS，57/57 |
| Python | 原始 BVH 到规范源姿势 | PASS，165 帧、25 源骨 |
| Python | 规范源姿势到 Manny | PASS，89 目标骨、26 个动态映射 |
| Python | 校准数值 | PASS，最大旋转误差 `7.157°`，最差 `foot_l` |
| UE | `MBEditor` Win64 Development | Succeeded |
| WebSocket | 原始 BVH 转换后的 89 骨姿势 | PASS，拒绝帧 0，直接 Manny Pose Snapshot |
| Foot IK | 平地运行时后处理 | PASS，左右鞋底最小间隙均为正；恢复角色网格高度并将接触余量校准为 `0.2 cm`，避免持续屈膝 |
| WebSocket | 每 50 帧强制断线 | PASS，自动重连 3 次并恢复姿势流 |
| Morph | 四通道实时驱动 | PASS，映射 4、缺失 0 |

## 执行命令

Python 转换测试：

```powershell
py -3.12 -m unittest discover -s python -p "test_bvh_stream.py" -v
```

UE Game/Editor 构建：

```powershell
Build.bat TP_ThirdPerson Win64 Development MotionBridgeDemo.uproject -WaitMutex -NoHotReloadFromIDE
Build.bat MBEditor Win64 Development MotionBridgeDemo.uproject -WaitMutex -NoHotReloadFromIDE
```

端到端实时验收：

```powershell
.\scripts\run_realtime_acceptance.ps1
```

该脚本依次启动真实 Python WebSocket 服务和 UE `NullRHI` 实例，执行稳态、断线重连和 Morph 三组测试，并检查明确的 PASS 标记。

## 关键验收字段

### WebSocket 稳态与 Foot IK

```text
MOTIONBRIDGE_WEBSOCKET_TEST=PASS
messages=162
rejected=0
accepted=161
input_hz=59.46
game_fps=48.56
pose_applied=1
direct_manny_pose=1
runtime_retarget=0
stream_bones=89
snapshot_bones=89
spine_error_deg=0.000

MOTIONBRIDGE_RUNTIME_FOOT_IK=PASS
min_left_post_ik_clearance=0.193
min_right_post_ik_clearance=0.207
```

`runtime_retarget=0` 不是省略重定向：它表示 UE 没有再运行另一套跨组件 Retarget 节点；骨架对映射已由 Python 根据当前原始 BVH 帧计算完成。计算方法和校准边界见 [原始 BVH 到 Manny 的运行时骨架映射](runtime_bvh_to_manny.md)。

### WebSocket 重连

```text
MOTIONBRIDGE_WEBSOCKET_RECONNECT_TEST=PASS
reconnects=3
connected=1
direct_manny_pose=1
stream_bones=89
snapshot_bones=89
```

### Morph Target

```text
MOTIONBRIDGE_MORPH_TARGET_TEST=PASS
mapped=4
missing=0
```

![实时表情验收](../artifacts/MotionBridge_RealtimeFacial.png)

## 校准复现

```powershell
py -3.12 python\build_bvh_source_calibration.py
py -3.12 python\build_manny_retarget_calibration.py
```

第二个脚本读取时间对齐的源/目标参考流并输出 `100style_to_manny_calibration.json`。运行时只使用这份参数文件，不读取目标参考帧。

## 已知限制

- 当前输入是有限 BVH 片段循环；片段边界会瞬间回到首帧，这不是网络断线。
- 当前映射只针对 100STYLE 源骨架与 UE5 Manny。新骨架需要重新对应与校准。
- 26 个主骨动态驱动；Manny 特有的扭转骨、手指和 IK 辅助骨保持参考姿势。
- `7.157°` 是校准片段内的最大拟合误差，不是任意动作的泛化保证。
- 表情测试波形用于接口验收，不代表面捕或语音驱动模型。
- 延迟测量来自同机回环；跨机器部署需要时钟同步后重新测量。
