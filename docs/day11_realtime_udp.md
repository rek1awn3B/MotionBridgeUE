# Day 11：UDP 实时姿势接收与播放

## 本日目标

将此前只接受函数调用的帧缓冲接到真实 UDP 输入，并让网络帧最终驱动 Manny，
同时保持无网络数据时原动画和 Foot IK 正常工作。

## 数据流与线程边界

```text
外部发送器
    │ UDP JSON
    ▼
FUdpSocketReceiver 后台线程
    │ 只复制文本到 MPSC 队列
    ▼
UMotionBridgeReceiverComponent 游戏线程 Tick
    │ 解析、校验、ConfigureStream / PushFrame
    ▼
排序、有界缓冲与播放时钟
    │ 插值后的 FMotionBridgeSampleResult
    ▼
UMotionBridgeFootIKAnimInstance
    │ 构建本地 Transform 的 FPoseSnapshot
    ▼
Pose Snapshot / 原动画二选一
    ▼
Foot IK → Manny
```

网络线程不直接访问角色、AnimInstance 或骨骼。它只向线程安全队列写入收到的
文本；所有 JSON 解析和 UE 对象修改都发生在游戏线程。

## 最小协议

流定义包先声明骨骼顺序：

```json
{
  "type": "stream",
  "protocol_version": 1,
  "stream_id": "python-local-test",
  "bones": ["root", "pelvis", "spine_01"]
}
```

之后每个帧包沿用相同顺序。四元数排列为 `[x, y, z, w]`：

```json
{
  "type": "frame",
  "frame_index": 0,
  "timestamp": 0.0,
  "root_position": [0.0, 0.0, 0.0],
  "root_rotation": [0.0, 0.0, 0.0, 1.0],
  "bone_translations": [[0.0, 0.0, 0.0]],
  "bone_rotations": [[0.0, 0.0, 0.0, 1.0]]
}
```

当前输入必须已经是 Manny 的父骨骼空间局部旋转。`root_position` 使用 UE 厘米
单位。`bone_translations` 可选；缺失时使用 Manny 参考姿势的局部位移。原始
BVH 到 Manny 的实时重定向不在本日范围内。

当接收组件的 `bConsumeRootMotionAsActorMovement` 开启时，网络帧中的 root
不再直接写入 Pose Snapshot。角色每帧计算相邻 root 样本的增量，将它交给
`CharacterMovement` 移动 Actor/胶囊；快照中的 root 保持参考姿势，骨盆与
其余骨骼仍使用网络数据。这可避免“网格在胶囊里滑动”和 Root Motion 重复消费。

接收组件用平台单调时钟记录最后一个成功接收帧的到达时间。默认
`StreamTimeoutSeconds = 0.5`：超时后 `TryGetFreshPlaybackSample` 返回失败，
AnimInstance 将 `bRealtimePoseValid` 置为 false，Bool 混合节点在 `0.1 s` 内切回
原 AnimSequence；Character 同时清除网络 root 基准。后续重新收到数据时，第一帧
只用于重建基准，从第二帧起恢复位移，不会把断线前后两个 root 强行连接造成瞬移。
`TryGetLatestPlaybackSample` 仍保留最后的样本，专门用于诊断和统计。

## 通信统计

`FMotionBridgeStreamStats` 由接收组件在同一把缓冲锁内维护，可通过
`GetStreamStats` 从 C++ 或蓝图读取：

- UDP 收包数与协议拒绝数；
- 提交、接受、拒绝、重复和乱序帧数；
- 根据当前流最小/最大 `frame_index` 与唯一帧数量估算的缺帧数；
- 播放时钟超过阈值后直接重同步的次数；
- 当前缓冲帧数、当前/最大缓冲延迟和实时流是否仍在线。

唯一帧索引集合随新的 stream definition 或手动 `ResetBuffer` 清空，因此晚到帧
可以填回先前估算的缺口，重复判断也不受 120 帧播放缓冲淘汰影响。

## 屏幕调试 Overlay

接收组件的 `bShowDebugOverlay` 默认关闭，也可以通过启动参数
`-MotionBridgeDebugOverlay` 临时开启。组件每 `0.1 s` 调用
`BuildDebugOverlayText`，使用固定消息键更新同一块屏幕文字，而不是每帧新增一条
消息。显示内容包括：

```text
MotionBridge / LIVE
UDP       packets 70   rejected 0
Frames    accepted 69   rejected 0
Quality   lost 0   duplicate 0   out-of-order 0
Buffer    69/120 frames   delay 0.0 ms   max 100.0 ms
Recovery  hard resync 0
```

状态颜色为：等待首帧时黄色、实时在线时绿色、收到过数据但已经超时时红色。
关闭 Overlay 或组件结束运行时会主动删除固定消息，避免残留到下一次 PIE。
文本生成函数是独立的 BlueprintPure 接口，因此除实际屏幕显示外也能自动验证内容。

## 运行方式

接收器默认关闭并只允许绑定本机回环地址。可在组件上启用，或启动 UE 时加入：

```text
-MotionBridgeEnableUdp
```

本机协议发送器：

```powershell
python tools/send_motionbridge_udp.py --frames 30 --fps 30
```

从已有 Manny 动画导出真实姿势流后回放：

```powershell
# 在 UnrealEditor-Cmd 中执行导出脚本
python/unreal_export_manny_pose_stream.py

python tools/send_motionbridge_udp.py `
  --stream-file artifacts/realtime/manny_root_motion_stream.json
```

模拟一次“发送—断流—重连”：

```powershell
python tools/send_motionbridge_udp.py `
  --stream-file artifacts/realtime/manny_root_motion_stream.json `
  --first-burst-frames 15 `
  --reconnect-gap 0.8
```

发送器先发 15 帧，暂停 `0.8 s`（超过 UE 默认 `0.5 s` 超时阈值），随后重新发送
stream definition，并从时间戳 0 回放完整动作。这同时覆盖真实 UDP 断流、会话状态清空、
时间戳重启和 Root Motion 基准重建。

## 验收结果

- UE 编辑器目标编译通过；
- UDP 绑定 `127.0.0.1:7001` 成功；
- 流定义和 30 帧数据成功进入缓冲；
- 旋转-only 回归测试的 26 根 Manny 主体骨骼通过 Pose Snapshot 驱动；
- 真实动画导出为 69 帧、89 根骨骼，并以 30 FPS 经 UDP 回放；
- UE 播放端观察到 69 个不同帧，Root 水平轨迹为 `91.866 cm`；
- 网络 Root Motion 驱动 Actor/胶囊的峰值水平移动为 `91.736 cm`，播放结束时
  距起点 `1.122 cm`，与原动作“走出再掉头返回”的轨迹一致；
- 最大骨骼运动为 `179.969°`，证明播放内容不是静态姿势；
- 最终逐骨骼最大局部旋转误差 `0.000°`，最大局部位移误差 `0.000 cm`；
- 停止发送后，实时流、网络姿势和 root 基准均自动失效，回退动画继续产生
  `46.616°` 的骨骼运动，`MOTIONBRIDGE_UDP_FALLBACK=PASS`；
- 人工注入缺帧、晚到、重复和时间戳跳变后，统计得到提交 `6`、接受 `5`、
  拒绝/重复/乱序/估算丢帧/硬重同步各 `1`，缓冲延迟 `0.100 s`，
  Overlay 同步包含这些数值，`MOTIONBRIDGE_STREAM_STATS=PASS`；
- 真实 UDP 回放收到 `70` 个包（1 个流定义 + 69 帧），接受 `69` 帧，协议拒绝、
  估算丢帧和乱序均为 `0`；
- 真实 UDP 重连测试依次观察到首次在线、超时和再次在线；重连首帧角色位移
  `0.000 cm`，证明没有把断线前后的 root 强行相连，随后 20 帧推动角色继续移动
  `69.585 cm`，`MOTIONBRIDGE_UDP_RECONNECT=PASS`；
- 不发送网络帧时，AnimSequence 回退和原 Foot IK 运行时验收仍为 `PASS`。

当前动态输入来自已经离线重定向完成的 Manny AnimSequence，不是动捕设备直接
输出，也不是原始 BVH 在 UE 中实时重定向。持续输入、超时回退、通信统计和断流
重连已经完成；后续优先补端到端延迟与性能采样，再决定是否实现原始骨架的在线重定向。
