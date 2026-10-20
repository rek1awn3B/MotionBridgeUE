# MotionBridge UE

面向动捕动画、AI 虚拟人系统与游戏客户端岗位的公开求职项目。项目覆盖从 BVH 数据理解、离线重定向、Root Motion 与 Foot IK，到 Python → Unreal 的 UDP/WebSocket 实时驱动、断线重连、UMG 监控和 Morph Target 表情驱动。

> 当前状态：核心作品闭环已完成。开发与最终验证环境为 Unreal Engine 5.6、Visual Studio 2022、Python 3.12。

![BVH 动作重定向到 UE5 Manny](artifacts/ue_manny_retarget_contact.png)

项目主线是 Manny 全身动作的解析、重定向与实时驱动。简化头像仅用于验证
标量通道到 Morph Target 的接口，不作为数字人美术成品或项目封面。

English documentation: [README_EN.md](README_EN.md)

## 项目亮点

- 自研 BVH tokenizer/parser、通道解码和 FK，不依赖现成 BVH 解析库。
- 使用 Blender 与 UE IK Retargeter 将公开 BVH 动作重定向到 UE5 Manny，并保留可复现的导入脚本与资产。
- 将 `Hips` 拆成地面 `MotionRoot` 与局部骨盆动作；逐帧重建证明拆分前后世界姿势一致。
- 运行时由 `CharacterMovement` 消费 Root Motion，避免“网格移动、胶囊不动”和 root 双重消费。
- C++ Foot IK 使用脚跟/脚尖双射线、骨盆补偿、Two Bone IK、地面法线对齐和与帧率无关的平滑。
- UDP 与 WebSocket 共用同一套版本化 JSON 解析、质量检查、有界缓冲、插值采样和统计接口。
- 网络回调只入队，UE 对象与动画状态统一在游戏线程修改；支持超时回退、重连和首帧 root 基准重建。
- 通过标量通道实时驱动原创测试头像的 `Smile`、`JawOpen`、`BlinkLeft`、`BlinkRight` Morph Target。
- 原生 C++ UMG 面板显示连接状态、输入频率、游戏帧率、端到端延迟、丢帧、乱序、缓冲延迟与重连次数。
- 另有最小第三人称客户端冒烟测试，覆盖输入配置、相机、移动、跳跃、落地和动画实例。

## 系统结构

```text
离线动作链路
BVH -> 自研 Parser/Decoder/FK -> 数值与骨长验证 -> 骨架可视化
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
| C++ 实验程序 | 16/16 编译并以退出码 0 运行 |
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

仓库已保留 `Content/MotionBridge` 下的自研/生成资产；Epic 模板中的 Manny、Quinn 等资源不会提交。

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

它会从零配置 CMake、运行 50 项 Python 测试、编译并执行 16 个 C++
实验程序，同时编译 UE Game 与 Editor Target。没有 UE 时可用
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
- [实时表情与监控截图](artifacts/candidates/MotionBridge_RealtimeFacial.png)
- `artifacts/bvh_animation.mp4`：自研解析与 FK 输出。
- `artifacts/ue_manny_retarget.mp4`：第一版 Manny 重定向结果。

## 文档索引

- [第一周架构](docs/week1_architecture.md)
- [Day 6：Blender / UE 重定向](docs/day6_blender_unreal_retargeting.md)
- [Day 9：Root Motion](docs/day9_root_motion.md)
- [Day 10：Foot IK](docs/day10_foot_ik.md)
- [Day 11：UDP 实时系统](docs/day11_realtime_udp.md)
- [Day 12：第三人称客户端](docs/day12_third_person_client.md)
- [Day 13–15：UMG、WebSocket 与 Morph Target](docs/day13_15_realtime_ui_websocket_morph.md)
- [最终验收报告](docs/final_acceptance.md)
- [简历与项目讲解材料](docs/portfolio_resume_materials.md)
- [第三方素材与授权说明](THIRD_PARTY_NOTICES.md)

## 项目边界

- 不包含既有科研项目的保密代码或资产。
- 自研内容是 BVH/FK、Root Motion 处理、运行时接收缓冲、线程交接、Foot IK、监控和 Morph 通道驱动；骨架间重定向使用 UE IK Retargeter，不声称自研完整重定向算法。
- 原创头像只用于证明标量通道到 Morph Target 的实时链路，不等同于生产级 MetaHuman 面部解算。
- 公开动作主线使用 CC BY 4.0 的 100STYLE；研究用途的 SFU 迭代成果只保留在被忽略的本地归档目录。
- 当前延迟数据来自同机回环测试，不能代表跨网络部署。
- 开发过程中使用 AI 辅助工具进行实现与审计；公开仓库保留可复现测试与验收记录，核心架构、数据流和验证结论由作者负责。

## License

项目自研代码使用 [MIT License](LICENSE)。第三方数据和 Epic Games 资产适用各自
条款，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
