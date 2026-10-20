# MotionBridge UE

MotionBridge UE 是一个实时角色动画系统，覆盖 BVH 数据解析、UE5 Manny 动作重定向、Root Motion、Foot IK，以及 Python → Unreal 的 UDP/WebSocket 实时驱动、断线重连、UMG 监控和 Morph Target 控制。

![BVH 动作重定向到 UE5 Manny](artifacts/ue_manny_retarget_contact.png)

English documentation: [README_EN.md](README_EN.md)

## 项目亮点

- 实现 BVH tokenizer/parser、通道解码和 FK，并验证旋转顺序、骨长与逐帧世界姿势。
- 使用 Blender 与 UE IK Retargeter 将公开 BVH 动作重定向到 UE5 Manny，并保留可复现的导入脚本与资产。
- 将 `Hips` 拆成地面 `MotionRoot` 与局部骨盆动作；逐帧重建证明拆分前后世界姿势一致。
- 运行时由 `CharacterMovement` 消费 Root Motion，避免“网格移动、胶囊不动”和 root 双重消费。
- C++ Foot IK 使用脚跟/脚尖双射线、骨盆补偿、Two Bone IK、地面法线对齐和与帧率无关的平滑。
- UDP 与 WebSocket 共用同一套版本化 JSON 解析、质量检查、有界缓冲、插值采样和统计接口。
- 网络回调只入队，UE 对象与动画状态统一在游戏线程修改；支持超时回退、重连和首帧 root 基准重建。
- 通过标量通道实时驱动 `Smile`、`JawOpen`、`BlinkLeft`、`BlinkRight` Morph Target。
- 原生 C++ UMG 面板显示连接状态、输入频率、游戏帧率、端到端延迟、丢帧、乱序、缓冲延迟与重连次数。
- 另有最小第三人称客户端冒烟测试，覆盖输入配置、相机、移动、跳跃、落地和动画实例。

## 系统结构

```text
离线动作链路
BVH -> Parser/Decoder/FK -> 数值与骨长验证 -> 骨架可视化
  \-> Blender/FBX -> UE Source Skeleton -> IK Retargeter -> Manny AnimSequence
                                               \-> Root Motion -> CharacterMovement
                                               \-> Foot IK -> AnimGraph

实时链路
Python UDP / WebSocket Server
        -> 网络线程或 WebSocket 回调
        -> MPSC 原始消息队列
        -> 游戏线程 JSON 校验与时间排序缓冲
        -> 插值采样
           ├─ 骨骼局部姿势 -> Pose Snapshot -> AnimGraph
           ├─ Root Motion -> CharacterMovement
           └─ 标量通道 -> Morph Target Driver
        -> C++ UMG 状态面板与自动验收日志
```

## 已验证结果

| 项目 | 结果 |
| --- | --- |
| Python 自动测试 | 50/50 通过 |
| C++ 动画与实时系统测试 | 7/7 编译并以退出码 0 运行 |
| UE 5.6 Game / Editor Target | 编译通过 |
| UDP 完整 Manny 姿势流 | 89 骨骼、69 帧，旋转/位移误差均为 `0.000` |
| UDP 断流恢复 | 观察到在线→超时→恢复；重连首帧位移 `0.000 cm` |
| WebSocket 稳态 | 约 `27–31 Hz`；拒绝帧 0；本机平均端到端延迟约 `36.76 ms` |
| WebSocket 自动重连 | 服务端每 50 帧主动断开，UE 完成 2 次重连并继续接收 |
| Morph Target | 4/4 通道映射，缺失 0，自动验收 PASS |
| Root Motion | 输入水平跨度 `91.866 cm`；Actor/胶囊实测 `91.736 cm` |
| 10° 斜坡 Foot IK | 双脚倾角 `10.000°`；鞋底最小间隙 `+0.194 / +0.200 cm` |
| 第三人称冒烟测试 | 配置、移动、跳跃、离地和落地全部 PASS |

完整证据与命令见 [最终验收报告](docs/final_acceptance.md)。

## 快速运行

### 1. 环境

- Unreal Engine 5.6
- Visual Studio 2022，安装“使用 C++ 的游戏开发”
- Python 3.12
- Blender 5.x（仅重新生成/导入资产时需要）

安装 Python 依赖：

```powershell
py -3.12 -m pip install -r requirements.txt
```

如果本机没有 UE Third Person 模板资源，先复制 Epic 模板依赖（请按实际安装位置修改 `EngineRoot`）：

```powershell
.\scripts\setup_unreal_demo.ps1 -EngineRoot "C:\Program Files\Epic Games\UE_5.6"
```

仓库保留 `Content/MotionBridge` 下的项目资产；Manny、Quinn 等 Epic 模板资源由安装脚本从本机 UE 安装目录复制。

### 2. 一键实时演示

骨骼实时流：

```powershell
.\scripts\run_websocket_demo.ps1
```

Morph Target 表情流：

```powershell
.\scripts\run_websocket_demo.ps1 -FacialDemo
```

若 UE 不在自动检测路径下，传入 `-EngineRoot`。

### 3. 构建与单元验收

```powershell
.\scripts\run_acceptance.ps1
```

它会从零配置 CMake、运行 50 项 Python 测试、编译并执行 7 个 C++
动画与实时系统测试，同时编译 UE Game 与 Editor Target。没有 UE 时可用
`-SkipUnreal`。

需要连同 WebSocket 稳态、自动重连和 Morph Target 一起启动 UE 做端到端
回归时运行：

```powershell
.\scripts\run_acceptance.ps1 -RunRealtime
```

## 手动运行实时服务

```powershell
py -3.12 tools\motionbridge_websocket_server.py
```

表情通道模式：

```powershell
py -3.12 tools\motionbridge_websocket_server.py --facial-demo
```

默认地址为 `ws://127.0.0.1:8765/`。UE 可通过 `-MotionBridgeWebSocketUrl=ws://host:port/` 覆盖。配置已明确让 localhost 绕过系统 HTTP 代理，避免本机 WebSocket 被代理端口截获。

## 演示素材

- [Manny 重定向视频](artifacts/ue_manny_retarget.mp4)
- [Manny 重定向接触图](artifacts/ue_manny_retarget_contact.png)
- [实时表情与监控截图](artifacts/MotionBridge_RealtimeFacial.png)
- `artifacts/bvh_animation.mp4`：BVH 解析与 FK 输出。

## 文档索引

- [系统架构](docs/architecture.md)
- [Blender / UE 重定向](docs/retargeting_pipeline.md)
- [Root Motion](docs/root_motion.md)
- [Foot IK](docs/foot_ik.md)
- [实时姿势传输](docs/realtime_streaming.md)
- [第三人称客户端](docs/third_person_client.md)
- [UMG、WebSocket 与 Morph Target](docs/realtime_ui_websocket_morph.md)
- [最终验收报告](docs/final_acceptance.md)
- [第三方素材与授权说明](THIRD_PARTY_NOTICES.md)

## 实现范围

- 项目实现 BVH/FK、Root Motion、运行时接收与缓冲、线程交接、Foot IK、状态监控和 Morph 通道驱动。
- 跨骨架动作映射使用 Unreal Engine IK Retargeter。
- 公开动作样本来自 CC BY 4.0 的 100STYLE；延迟数据来自同机回环测试。

## License

项目代码使用 [MIT License](LICENSE)。第三方数据和 Epic Games 资产适用各自
条款，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
