#include <cmath>
#include <iomanip>
#include <iostream>

struct Vec3
{
    double x{};
    double y{};
    double w{1.0};
};

struct Mat3
{
    double m[3][3]{};
};

Mat3 Identity()
{
    Mat3 result{};
    result.m[0][0] = 1.0;
    result.m[1][1] = 1.0;
    result.m[2][2] = 1.0;
    return result;
}

Mat3 Translation(double x, double y)
{
    Mat3 result = Identity();
    result.m[0][2] = x;
    result.m[1][2] = y;
    return result;
}

Mat3 RotationDegrees(double degrees)
{
    constexpr double pi = 3.14159265358979323846;
    const double radians = degrees * pi / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);

    Mat3 result = Identity();
    result.m[0][0] = cosine;
    result.m[0][1] = -sine;
    result.m[1][0] = sine;
    result.m[1][1] = cosine;
    return result;
}

Mat3 Scale(double x, double y)
{
    Mat3 result = Identity();
    result.m[0][0] = x;
    result.m[1][1] = y;
    return result;
}

Mat3 operator*(const Mat3& left, const Mat3& right)
{
    Mat3 result{};
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            for (int index = 0; index < 3; ++index)
            {
                result.m[row][column] +=
                    left.m[row][index] * right.m[index][column];
            }
        }
    }
    return result;
}

Vec3 TransformPoint(const Mat3& matrix, const Vec3& point)
{
    return {
        matrix.m[0][0] * point.x + matrix.m[0][1] * point.y + matrix.m[0][2] * point.w,
        matrix.m[1][0] * point.x + matrix.m[1][1] * point.y + matrix.m[1][2] * point.w,
        matrix.m[2][0] * point.x + matrix.m[2][1] * point.y + matrix.m[2][2] * point.w,
    };
}

struct Node
{
    const char* name{};
    Mat3 localTransform{};
    const Node* parent{}; // 非拥有指针：父节点的生命周期必须覆盖子节点。
};

Mat3 GetWorldTransform(const Node& node)
{
    if (node.parent == nullptr)
    {
        return node.localTransform;
    }
    return GetWorldTransform(*node.parent) * node.localTransform;
}

Vec3 GetWorldPosition(const Node& node)
{
    return TransformPoint(GetWorldTransform(node), Vec3{0.0, 0.0, 1.0});
}

bool CheckPosition(const Node& node, double expectedX, double expectedY)
{
    constexpr double epsilon = 1e-9;
    const Vec3 actual = GetWorldPosition(node);
    const bool passed = std::abs(actual.x - expectedX) < epsilon
        && std::abs(actual.y - expectedY) < epsilon;

    std::cout << (passed ? "[PASS] " : "[FAIL] ") << node.name
              << " world position = (" << actual.x << ", " << actual.y << ")\n";
    return passed;
}

void PrintMatrix(const char* name, const Mat3& matrix)
{
    std::cout << name << ":\n";
    for (const auto& row : matrix.m)
    {
        for (const double value : row)
        {
            std::cout << std::setw(9) << std::fixed << std::setprecision(3) << value;
        }
        std::cout << '\n';
    }
}

int main()
{
    PrintMatrix("Identity", Identity());
    PrintMatrix("Translation(2, 3)", Translation(2.0, 3.0));
    PrintMatrix("RotationDegrees(90)", RotationDegrees(90.0));
    PrintMatrix("Scale(2, 0.5)", Scale(2.0, 0.5));

    const Node root{"root", Translation(10.0, 0.0), nullptr};
    const Node shoulder{
        "shoulder",
        Translation(2.0, 0.0) * RotationDegrees(90.0),
        &root,
    };
    const Node hand{"hand", Translation(1.0, 0.0), &shoulder};

    const bool allPassed = CheckPosition(root, 10.0, 0.0)
        && CheckPosition(shoulder, 12.0, 0.0)
        && CheckPosition(hand, 12.0, 1.0);

    return allPassed ? 0 : 1;
}
