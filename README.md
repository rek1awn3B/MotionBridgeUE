# MotionBridge UE

MotionBridge UE 是一套从原始 BVH 到 UE5 Manny 的实时动画数据桥。Python 在启动时解析 BVH、计算源骨架局部/全局姿势并映射到 Manny；运行期间按约 60 Hz 经 WebSocket 发送姿势帧。Unreal C++ 负责消息校验、线程交接、缓冲插值、Pose Snapshot、角色 Root Motion、Foot IK、断线恢复和状态监控。

> 验证环境：Unreal Engine 5.6、Visual Studio 2022、Python 3.12。

![BVH 动作驱动 UE5 Manny](artifacts/ue_manny_retarget_contact.png)

English documentation: [README_EN.md](README_EN.md)

## 现在实际运行的链路

```text
Neutral_FW_frames_530_694.bvh
  -> 自研 BVH tokenizer / parser / 通道解码 / FK
  -> 源坐标系校准
  -> 逐帧计算源骨骼全局旋转
  -> 固定骨架对校准：R_target_global = A * R_source_global * B
  -> 按 Manny 父子关系还原局部旋转
  -> 89 骨 Manny 姿势帧 + 独立角色 Root Motion
  -> WebSocket
  -> UE 原始消息队列（网络回调）
  -> 游戏线程校验、按时间排序与插值
  -> Pose Snapshot + CharacterMovement + Foot IK
```

实时服务的默认输入只有：

- 原始 BVH：`data/bvh/100style/Neutral_FW_frames_530_694.bvh`
- 源坐标校准：`data/calibration/100style_source_calibration.json`
- 100STYLE 源骨架到 Manny 的固定校准：`data/calibration/100style_to_manny_calibration.json`

`artifacts/realtime/source_reference_stream.json` 和 `manny_root_motion_stream.json` 只用于离线生成、复核校准参数，实时服务不读取其中任何目标姿势帧。也就是说，运行时发出的每一帧 Manny 动作都由当前 BVH 帧计算得到，不是回放一份预先导出的 Manny 动画。

## 骨架映射如何工作

校准脚本使用一组时间对齐的源/目标参考动作，为 26 个主要目标骨骼拟合固定的双边旋转映射：

```text
R_target_global = A_j * R_source_global * B_j
R_target_local  = inverse(R_target_parent_global) * R_target_global
```

运行时只使用保存下来的常量 `A_j`、`B_j`，不查询目标动画。Manny 的扭转骨、手指和 IK 辅助骨没有对应 BVH 通道，因此保持目标参考姿势；数据包仍包含完整的 89 骨顺序。当前 100STYLE 片段上的最大校准旋转误差为 `7.157°`，最差骨骼为 `foot_l`，随后由 Foot IK 处理地面接触。

这是一套针对“100STYLE 骨架 → UE5 Manny”标定的运行时映射，并非任意骨架自动重定向器。换用新的捕捉骨架或目标角色时，需要重新建立骨骼对应并生成校准文件。算法、映射表、生成脚本和误差数据均保存在仓库中，详见 [运行时骨架映射](docs/runtime_bvh_to_manny.md)。

## 主要实现

- 自研 BVH tokenizer/parser、通道解码和 FK，不依赖现成 BVH 解析库。
- 将 BVH 根轨迹拆为角色水平位移/朝向与骨盆局部动作，避免网格移动而胶囊不动或 root 被重复消费。
- UDP 与 WebSocket 共用版本化 JSON 校验、有界缓冲、时间排序、插值采样和统计接口。
- 网络回调只写入 MPSC 队列，UObject、角色与动画状态只在游戏线程修改。
- C++ Foot IK 使用脚跟/脚尖射线、骨盆补偿、Two Bone IK、地面法线对齐和与帧率无关的平滑。
- 支持超时回退、WebSocket 自动重连以及重连后的 Root Motion 基准重建。
- C++ UMG 面板显示连接状态、输入频率、FPS、延迟、异常帧、缓冲和重连次数。
- 四个标量通道可驱动测试头像的 `Smile`、`JawOpen`、`BlinkLeft`、`BlinkRight` Morph Target；该头像只用于接口验收。
- 仓库还保留 Blender/FBX/UE IK Retargeter 的离线资产制作流程，用于对照验证，不是当前实时服务的运行依赖。

## 已验证结果

| 项目 | 结果 |
| --- | --- |
| Python 完整测试 | 55/55 通过 |
| 原始 BVH → Manny 单元测试 | 165 帧；输出 89 骨；运行时不读取目标姿势流 |
| 固定骨架对校准 | 动态映射 26 个主骨；最大旋转误差 `7.157°`（`foot_l`） |
| UE 5.6 Game / Editor Target | 编译通过 |
| WebSocket 实时姿态 | 89 骨；拒绝帧 0；Pose Snapshot 已应用；脊柱误差 `0.000°` |
| WebSocket 自动重连 | 服务端每 50 帧断开；UE 完成 3 次重连并恢复姿势流 |
| 运行时 Foot IK | 左右鞋底最小间隙 `+0.009 / +0.004 cm`；平地步态无持续屈膝 |
| Morph Target | 4/4 通道映射，缺失 0 |
| Root Motion | 由角色 Actor/胶囊消费，不在骨架 root 中重复应用 |

命令、完整日志字段与限制见 [最终验收报告](docs/final_acceptance.md)。

## 快速运行

安装 Python 依赖：

```powershell
py -3.12 -m pip install -r requirements.txt
```

本机若没有 UE Third Person 模板资源，先复制 Epic 模板依赖（这些资源不提交到仓库）：

```powershell
.\scripts\setup_unreal_demo.ps1 -EngineRoot "D:\Program Files\Epic Games\UE_5.6"
```

启动原始 BVH → Manny 实时演示：

```powershell
.\scripts\run_websocket_demo.ps1
```

正面检查左右腿和屈膝情况，或关闭 Foot IK 做 A/B 对照：

```powershell
.\scripts\run_websocket_demo.ps1 -FrontView
.\scripts\run_websocket_demo.ps1 -FrontView -DisableFootIK
```

运行端到端 WebSocket、重连、Foot IK 与 Morph Target 验收：

```powershell
.\scripts\run_realtime_acceptance.ps1
```

仅运行这条实时转换链的 Python 测试：

```powershell
py -3.12 -m unittest discover -s python -p "test_bvh_stream.py" -v
```

重新生成两级校准：

```powershell
py -3.12 python\build_bvh_source_calibration.py
py -3.12 python\build_manny_retarget_calibration.py
```

## 目录定位

- `python/bvh_stream.py`：原始 BVH 到源姿势、再到 Manny 姿势的运行时计算。
- `python/build_bvh_source_calibration.py`：BVH 坐标系和源骨架校准。
- `python/build_manny_retarget_calibration.py`：源/目标固定骨架对映射拟合与误差验证。
- `tools/motionbridge_websocket_server.py`：实时服务、循环时间戳和测试表情通道。
- `Unreal/MotionBridgeDemo/Source/TP_ThirdPerson/`：UE 接收、缓冲、角色驱动、Foot IK、Morph 与监控实现。
- `scripts/run_realtime_acceptance.ps1`：端到端自动验收。
- `docs/`：离线流程、实时协议和实现记录。

## 项目边界

- 当前输入是仓库中的 BVH 片段循环，不是硬件动捕设备；更换为设备输入时可复用转换、传输和 UE 消费层。
- 当前映射只针对 100STYLE 源骨架与 UE5 Manny，不能直接宣称支持任意骨架。
- 26 个主要骨骼由 BVH 动态驱动；Manny 特有的扭转骨、手指和 IK 辅助骨保持参考姿势。
- Morph 通道是确定性测试信号，不包含面捕或语音驱动模型。
- 延迟数据来自同机回环环境，不代表跨机器部署。
- 不包含既有科研项目的保密代码或资产。

## 数据与许可

公开动作使用 CC BY 4.0 的 100STYLE 数据。项目自研代码使用 [MIT License](LICENSE)；第三方数据和 Epic Games 资产适用各自条款，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
