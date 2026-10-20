#include <cmath>
#include <iostream>
#include <stdexcept>

struct Vec3
{
    double x{};
    double y{};
    double z{};
};

struct Vec4
{
    double x{};
    double y{};
    double z{};
    double w{1.0};
};

struct Mat4
{
    double m[4][4]{};
};

Mat4 Identity()
{
    Mat4 result{};
    for (int index = 0; index < 4; ++index)
    {
        result.m[index][index] = 1.0;
    }
    return result;
}

Mat4 Translation(double x, double y, double z)
{
    Mat4 result = Identity();
    result.m[0][3] = x;
    result.m[1][3] = y;
    result.m[2][3] = z;
    return result;
}

Mat4 Scale(double x, double y, double z)
{
    Mat4 result = Identity();
    result.m[0][0] = x;
    result.m[1][1] = y;
    result.m[2][2] = z;
    return result;
}

double Length(const Vec3& vector)
{
    return std::sqrt(
        vector.x * vector.x
        + vector.y * vector.y
        + vector.z * vector.z);
}

Vec3 Normalize(const Vec3& vector)
{
    constexpr double epsilon = 1e-12;
    const double length = Length(vector);
    if (length < epsilon)
    {
        throw std::invalid_argument("Rotation axis must be non-zero.");
    }
    return {vector.x / length, vector.y / length, vector.z / length};
}

Mat4 operator*(const Mat4& left, const Mat4& right);

Mat4 operator+(const Mat4& left, const Mat4& right)
{
    Mat4 result{};
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            result.m[row][column] =
                left.m[row][column] + right.m[row][column];
        }
    }
    return result;
}

Mat4 operator*(const Mat4& matrix, double scalar)
{
    Mat4 result{};
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            result.m[row][column] = matrix.m[row][column] * scalar;
        }
    }
    return result;
}

Vec3 operator+(const Vec3& left, const Vec3& right)
{
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

Vec3 operator*(const Vec3& vector, double scalar)
{
    return {vector.x * scalar, vector.y * scalar, vector.z * scalar};
}

double Dot(const Vec3& left, const Vec3& right)
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

Vec3 Cross(const Vec3& left, const Vec3& right)
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

Vec3 RotateByRodrigues(
    const Vec3& vector,
    const Vec3& unitAxis,
    double radians)
{
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);

    return vector * cosine
        + Cross(unitAxis, vector) * sine
        + unitAxis * (Dot(unitAxis, vector) * (1.0 - cosine));
}

Mat4 RotationAxisAngleDegrees(const Vec3& inputAxis, double degrees)
{
    constexpr double pi = 3.14159265358979323846;
    const Vec3 axis = Normalize(inputAxis);
    const double radians = degrees * pi / 180.0;
    const double sine = std::sin(radians);
    const double cosine = std::cos(radians);

    // K 是旋转轴的反对称矩阵，满足 K * v = axis × v。
    Mat4 skew{};
    skew.m[0][1] = -axis.z;
    skew.m[0][2] = axis.y;
    skew.m[1][0] = axis.z;
    skew.m[1][2] = -axis.x;
    skew.m[2][0] = -axis.y;
    skew.m[2][1] = axis.x;

    // 罗德里格斯公式：R = I + sin(theta)K + (1-cos(theta))K^2。
    return Identity()
        + skew * sine
        + (skew * skew) * (1.0 - cosine);
}

Mat4 operator*(const Mat4& left, const Mat4& right)
{
    Mat4 result{};
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            for (int index = 0; index < 4; ++index)
            {
                result.m[row][column] +=
                    left.m[row][index] * right.m[index][column];
            }
        }
    }
    return result;
}

Vec4 Transform(const Mat4& matrix, const Vec4& vector)
{
    return {
        matrix.m[0][0] * vector.x + matrix.m[0][1] * vector.y
            + matrix.m[0][2] * vector.z + matrix.m[0][3] * vector.w,
        matrix.m[1][0] * vector.x + matrix.m[1][1] * vector.y
            + matrix.m[1][2] * vector.z + matrix.m[1][3] * vector.w,
        matrix.m[2][0] * vector.x + matrix.m[2][1] * vector.y
            + matrix.m[2][2] * vector.z + matrix.m[2][3] * vector.w,
        matrix.m[3][0] * vector.x + matrix.m[3][1] * vector.y
            + matrix.m[3][2] * vector.z + matrix.m[3][3] * vector.w,
    };
}

Mat4 InverseRigidTransform(const Mat4& transform)
{
    Mat4 inverse = Identity();

    // 对于正交旋转矩阵，R 的逆矩阵等于它的转置矩阵。
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            inverse.m[row][column] = transform.m[column][row];
        }
    }

    // 若 M = [R t; 0 1]，则 M^-1 = [R^T -R^T t; 0 1]。
    const Vec4 translation{
        transform.m[0][3],
        transform.m[1][3],
        transform.m[2][3],
        0.0,
    };
    const Vec4 inverseTranslation = Transform(inverse, translation);
    inverse.m[0][3] = -inverseTranslation.x;
    inverse.m[1][3] = -inverseTranslation.y;
    inverse.m[2][3] = -inverseTranslation.z;
    return inverse;
}

struct Node
{
    const char* name{};
    Mat4 localTransform{};
    const Node* parent{};
};

Mat4 GetWorldTransform(const Node& node)
{
    if (node.parent == nullptr)
    {
        return node.localTransform;
    }
    return GetWorldTransform(*node.parent) * node.localTransform;
}

bool NearlyEqual(double left, double right)
{
    constexpr double epsilon = 1e-9;
    return std::abs(left - right) < epsilon;
}

bool CheckVector(
    const char* testName,
    const Vec4& actual,
    const Vec4& expected)
{
    const bool passed = NearlyEqual(actual.x, expected.x)
        && NearlyEqual(actual.y, expected.y)
        && NearlyEqual(actual.z, expected.z)
        && NearlyEqual(actual.w, expected.w);

    std::cout << (passed ? "[PASS] " : "[FAIL] ") << testName
              << " = (" << actual.x << ", " << actual.y << ", "
              << actual.z << ", " << actual.w << ")\n";
    return passed;
}

int main()
{
    bool allPassed = true;

    // 点的 w=1，会受到平移影响；方向的 w=0，不受平移影响。
    const Mat4 translation = Translation(5.0, 6.0, 7.0);
    allPassed &= CheckVector(
        "translated point",
        Transform(translation, Vec4{1.0, 2.0, 3.0, 1.0}),
        Vec4{6.0, 8.0, 10.0, 1.0});
    allPassed &= CheckVector(
        "translated direction",
        Transform(translation, Vec4{1.0, 2.0, 3.0, 0.0}),
        Vec4{1.0, 2.0, 3.0, 0.0});

    // 罗德里格斯：将 +X 绕 +Z 旋转 90 度，结果应为 +Y。
    const Mat4 rotateAroundZ =
        RotationAxisAngleDegrees(Vec3{0.0, 0.0, 1.0}, 90.0);
    allPassed &= CheckVector(
        "Rodrigues Z 90",
        Transform(rotateAroundZ, Vec4{1.0, 0.0, 0.0, 0.0}),
        Vec4{0.0, 1.0, 0.0, 0.0});

    const Node root{"root", Translation(10.0, 0.0, 0.0), nullptr};
    const Node shoulder{
        "shoulder",
        Translation(2.0, 0.0, 0.0) * rotateAroundZ,
        &root,
    };
    const Node hand{"hand", Translation(1.0, 0.0, 0.0), &shoulder};

    allPassed &= CheckVector(
        "hand world position",
        Transform(GetWorldTransform(hand), Vec4{0.0, 0.0, 0.0, 1.0}),
        Vec4{12.0, 1.0, 0.0, 1.0});

    const Mat4 recoveredHandLocal =
        InverseRigidTransform(GetWorldTransform(shoulder))
        * GetWorldTransform(hand);
    allPassed &= CheckVector(
        "world back to hand local",
        Transform(recoveredHandLocal, Vec4{0.0, 0.0, 0.0, 1.0}),
        Vec4{1.0, 0.0, 0.0, 1.0});

    return allPassed ? 0 : 1;
}
