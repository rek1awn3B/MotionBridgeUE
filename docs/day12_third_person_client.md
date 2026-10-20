# Day 12：第三人称客户端框架与自动验收

## 目标

本日补齐第三人称客户端基础，重点不是重新制作 UE 模板，而是理解模板实际运行链路，并建立可重复执行的自动验收：

- Gameplay Framework 的生成与控制关系；
- Enhanced Input、相机相对移动和角色朝向；
- Idle/Walk/Run/Jump 动画实例的运行时归属；
- 独立测试 GameMode 的职责与生命周期；
- 一道覆盖 C++ 容器和迭代器的 LRU 算法题。

## 正式运行链路

```text
ATP_ThirdPersonGameMode
  -> DefaultPawnClass = BP_ThirdPersonCharacter
  -> PlayerController Possess Pawn
  -> NotifyControllerChanged 添加 DefaultMappingContext
  -> SetupPlayerInputComponent 绑定 Jump / Move / Look
  -> CharacterMovement 执行移动、碰撞、重力与朝向
  -> ABP_Quinn_C 驱动角色动画
```

运行时动画实例是 `ABP_Quinn_C`。它继承 `ABP_Manny`，复用父动画蓝图的移动状态机逻辑，并覆盖为 Quinn 对应的动画资源。

## 自动冒烟测试

测试实现：

- `Source/TP_ThirdPerson/MotionBridgeThirdPersonSmokeTestGameMode.h`
- `Source/TP_ThirdPerson/MotionBridgeThirdPersonSmokeTestGameMode.cpp`

测试 GameMode 继承正式 `ATP_ThirdPersonGameMode`，因此复用同一个 `DefaultPawnClass`，并通过 PlayerController 当前 Possess 的 Pawn 取得实际生成角色。测试没有自行生成替代角色。

初始化阶段：

1. 等待 World、PlayerController 和 Character 就绪；
2. 使用引擎自带 Cube 在角色胶囊脚底创建碰撞地面；
3. 等待 `CharacterMovement::IsMovingOnGround()`，排除出生下落对跳跃观测的干扰；
4. 检查输入资源、Enhanced Input、相机、移动、网格与动画实例配置。

行为阶段：

1. 按 ControlRotation 的水平 Yaw 计算相机前方；
2. 持续调用 `AddMovementInput` 1.5 秒；
3. 调用一次 `Jump()`；
4. 观察 `IsFalling()` 从 false 变为 true，再回到 false；
5. 检查水平位移、离地和落地，输出结构化 PASS/FAIL 日志并正常退出。

测试逻辑使用 `TWeakObjectPtr` 观察角色，不取得所有权；每帧访问前重新检查有效性。蓝图私有输入资源通过 UE 反射系统只读检查，避免为测试增加正式角色并不需要的 getter。

## 验收结果

UE 5.6 Editor Target 编译通过。命令行自动验收结果：

```text
MOTIONBRIDGE_THIRD_PERSON_CONFIG=PASS
class=BP_ThirdPersonCharacter_C
anim_class=ABP_Quinn_C
input_assets=1
enhanced_input=1
camera=1
movement=1
mesh_and_anim=1

MOTIONBRIDGE_THIRD_PERSON=PASS
reason=completed
max_distance=749.839
jump_issued=1
observed_falling=1
observed_landing=1
```

## LRU 缓存练习

实现位于 `src/lru_cache_lab.cpp`：

```text
std::list<pair<key, value>>
    维护最近使用到最久未使用的顺序

unordered_map<key, list::iterator>
    平均 O(1) 定位链表节点
```

`Get` 和已有键的 `Put` 使用 `list::splice` 将原节点移动到表头；插入超容量时先用尾节点 key 清理哈希表，再删除尾节点。普通读取、顺序刷新、更新、淘汰、容量 1 和容量 0 共 10 项测试通过。

## 已知边界

- 当前自动测试没有注入真实键盘按键，因此没有端到端覆盖 `IA_Move -> BindAction -> Move()` 的事件路线。
- 原 `ThirdPersonMap` 缺少部分 `LevelPrototyping` 资源；本测试使用引擎空白地图和临时地面隔离该问题。
- UMG 状态面板留到下一学习日完成。
