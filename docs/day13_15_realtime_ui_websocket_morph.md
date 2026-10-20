# Day 13–15：UMG、WebSocket 与 Morph Target 实时驱动

## 目标

在已有 UDP 骨骼流基础上补齐原四周计划中尚缺的三部分：正式 UMG 状态面板、WebSocket 传输与断线重连、BlendShape/Morph Target 参数驱动。

## UMG 状态面板

`AMotionBridgeStatusHUD` 创建 `UMotionBridgeStatusWidget`。Widget 树完全由 C++ 构造，避免演示资产依赖额外蓝图。面板以低频刷新显示：

- UDP 收包与拒绝数；
- WebSocket 消息、拒绝、连接状态与重连次数；
- 接受/拒绝帧数；
- 输入频率、游戏线程 FPS；
- 最新、平均、最大端到端延迟；
- 丢帧、重复、乱序；
- 缓冲帧数、播放延迟与硬重同步次数。

Widget 只读取 Receiver 的公开快照，不持有或控制网络组件生命周期。

## WebSocket 传输

`UMotionBridgeWebSocketClientComponent` 使用 UE WebSockets 模块连接 Python 服务。回调可能不在适合修改 UObject 的上下文，因此只把原始字符串写入线程安全队列；组件 Tick 在游戏线程取出消息并调用 Receiver 的统一 JSON 入口。

UDP 与 WebSocket 不维护两份解析逻辑。两者最终都进入：

```text
Raw JSON -> schema/version validation -> stream/frame decode
         -> quality accounting -> bounded time-sorted buffer
```

连接失败或断开后使用定时重连；组件结束生命周期时主动关闭连接并清空回调，避免回调访问已经销毁的对象。

本机曾启用 `127.0.0.1:10551` 系统代理。UE 的 WebSocket 库最初把 localhost 连接送进代理，导致握手失败。工程在 `DefaultEngine.ini` 中明确配置 localhost 绕过代理并关闭域名 allowlist，随后稳定连接 `ws://127.0.0.1:8765/`。

## 端到端时间戳

Python 帧带 `sent_unix_seconds`。UE 在游戏线程接受该帧时计算：

```text
latency = local_unix_seconds - sent_unix_seconds
```

统计最新值、平均值和最大值。该指标包含 Python 生成、序列化、传输、回调排队和游戏线程取包，但不包含显示器扫描延迟。由于发送端与接收端在同一电脑，结果只用于本机回归。

## Morph Target 链路

为避免依赖私有或受限角色资产，Blender 脚本生成一个原创简化头像及四个 Shape Key：

- `Smile`
- `JawOpen`
- `BlinkLeft`
- `BlinkRight`

导入 UE 时启用 Morph Target。`UMotionBridgeMorphTargetDriverComponent` 从 Receiver 读取同名标量通道，进行范围约束与指数平滑，再调用 `SetMorphTarget`。未知通道只记录，不造成崩溃。

## 验收

稳态 WebSocket：

```text
MOTIONBRIDGE_WEBSOCKET_TEST=PASS
messages=72 rejected=0 accepted=71 input_hz=26.99
game_fps=1794.11 latency_average_ms=36.76 reconnects=0 connected=1
```

强制断线重连：

```text
MOTIONBRIDGE_WEBSOCKET_RECONNECT_TEST=PASS
messages=102 rejected=0 reconnects=2 connected=1
```

Morph Target：

```text
MOTIONBRIDGE_MORPH_TARGET_TEST=PASS
mapped=4 missing=0 smile=0.998 jaw_open=0.496
blink_left=0.926 blink_right=0.926
```

最终可视化证据：`artifacts/candidates/MotionBridge_RealtimeFacial.png`。

## 边界

- 当前 Morph 值由确定性测试波形产生，不是语音模型或视频面捕模型的输出。
- 当前只驱动四个通道，重点是证明传输、线程交接、映射、平滑、监控与恢复链路。
- 若接入语音驱动数字人，模型服务只需输出同一协议的标量通道；UE 侧架构不需要重写。
