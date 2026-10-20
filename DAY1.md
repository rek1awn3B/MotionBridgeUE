# Day 1 Lab：三节点层级变换

## 目标

不使用旧项目代码，也暂时不依赖 GLM。使用标准 C++ 实现一个最小二维齐次坐标变换，弄清动画骨架中父子关节的变换传播。

## 约定

- 使用列向量。
- 点写成 `(x, y, 1)`。
- 角度输入使用 degree，计算时转换为 radian。
- 局部矩阵为 `Local = T * R * S`。
- 世界矩阵为 `World = ParentWorld * Local`。

## 你需要实现

1. `Vec3`：三个 `double` 分量。
2. `Mat3`：保存 3×3 数字。
3. `Mat3 Identity()`。
4. `Mat3 Translation(double x, double y)`。
5. `Mat3 RotationDegrees(double degrees)`。
6. `Mat3 Scale(double x, double y)`。
7. `Mat3 operator*(const Mat3&, const Mat3&)`。
8. `Vec3 TransformPoint(const Mat3&, const Vec3&)`。
9. `Node`：至少包含名称、局部矩阵、父节点指针。
10. `GetWorldTransform(const Node&)`：沿父链组合矩阵。

## 固定测试场景

建立三个节点：

```text
root
└── shoulder
    └── hand
```

- `root.local = Translation(10, 0)`
- `shoulder.local = Translation(2, 0) * RotationDegrees(90)`
- `hand.local = Translation(1, 0)`

将每个节点自己的原点 `(0, 0, 1)` 变换到世界空间。正确结果应为：

```text
root world position     = (10, 0)
shoulder world position = (12, 0)
hand world position     = (12, 1)
```

允许 `1e-9` 左右的浮点误差，不要直接用 `==` 比较浮点数。

## 禁止事项

- 不复制旧 `GameObject.cpp`。
- 不让 AI 一次性生成完整实现。
- 不把三个节点的答案坐标直接写死。
- 测试未通过前不进入四元数或 BVH。

## 完成后必须能回答

1. 为什么 `hand.local` 不是世界坐标？
2. 为什么矩阵乘法顺序不能交换？
3. 如果父节点旋转，子节点为什么会绕父节点运动？
4. `w = 1` 的点和 `w = 0` 的方向向量有什么差别？
5. 如果已知世界变换，怎样还原相对于父节点的局部变换？

