#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double epsilon = 1e-12;

double RadiansToDegrees(double radians)
{
    return radians * 180.0 / pi;
}
}

struct Vec3
{
    double x{};
    double y{};
    double z{};
};

Vec3 operator/(const Vec3& vector, double scalar)
{
    return {vector.x / scalar, vector.y / scalar, vector.z / scalar};
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

double Length(const Vec3& vector)
{
    return std::sqrt(Dot(vector, vector));
}

Vec3 Normalize(const Vec3& vector)
{
    const double length = Length(vector);
    if (length < epsilon)
    {
        throw std::invalid_argument("Cannot normalize a zero vector.");
    }
    return vector / length;
}

struct Quaternion
{
    double w{1.0};
    double x{};
    double y{};
    double z{};
};

Quaternion operator+(const Quaternion& left, const Quaternion& right)
{
    return {
        left.w + right.w,
        left.x + right.x,
        left.y + right.y,
        left.z + right.z,
    };
}

Quaternion operator-(const Quaternion& quaternion)
{
    return {
        -quaternion.w,
        -quaternion.x,
        -quaternion.y,
        -quaternion.z,
    };
}

Quaternion operator*(double scalar, const Quaternion& quaternion)
{
    return {
        scalar * quaternion.w,
        scalar * quaternion.x,
        scalar * quaternion.y,
        scalar * quaternion.z,
    };
}

Quaternion operator/(const Quaternion& quaternion, double scalar)
{
    return {
        quaternion.w / scalar,
        quaternion.x / scalar,
        quaternion.y / scalar,
        quaternion.z / scalar,
    };
}

Quaternion operator*(const Quaternion& left, const Quaternion& right)
{
    const Vec3 leftVector{left.x, left.y, left.z};
    const Vec3 rightVector{right.x, right.y, right.z};
    const Vec3 cross = Cross(leftVector, rightVector);

    // (w1, v1)(w2, v2)
    // = (w1w2 - v1·v2, w1v2 + w2v1 + v1×v2)。
    return {
        left.w * right.w - Dot(leftVector, rightVector),
        left.w * right.x + right.w * left.x + cross.x,
        left.w * right.y + right.w * left.y + cross.y,
        left.w * right.z + right.w * left.z + cross.z,
    };
}

double Dot(const Quaternion& left, const Quaternion& right)
{
    return left.w * right.w
        + left.x * right.x
        + left.y * right.y
        + left.z * right.z;
}

double NormSquared(const Quaternion& quaternion)
{
    return Dot(quaternion, quaternion);
}

Quaternion Normalize(const Quaternion& quaternion)
{
    const double norm = std::sqrt(NormSquared(quaternion));
    if (norm < epsilon)
    {
        throw std::invalid_argument("Cannot normalize a zero quaternion.");
    }
    return quaternion / norm;
}

Quaternion Conjugate(const Quaternion& quaternion)
{
    return {
        quaternion.w,
        -quaternion.x,
        -quaternion.y,
        -quaternion.z,
    };
}

Quaternion Inverse(const Quaternion& quaternion)
{
    const double normSquared = NormSquared(quaternion);
    if (normSquared < epsilon)
    {
        throw std::invalid_argument("A zero quaternion has no inverse.");
    }
    return Conjugate(quaternion) / normSquared;
}

Quaternion FromAxisAngleRadians(const Vec3& inputAxis, double angleRadians)
{
    const Vec3 axis = Normalize(inputAxis);
    const double halfRadians = angleRadians * 0.5;
    const double sine = std::sin(halfRadians);

    // 旋转四元数使用半角；共轭作用后产生的实际旋转角为完整角度。
    return {
        std::cos(halfRadians),
        axis.x * sine,
        axis.y * sine,
        axis.z * sine,
    };
}

Vec3 RotateVector(const Quaternion& rotation, const Vec3& vector)
{
    const Quaternion unitRotation = Normalize(rotation);
    const Quaternion pureVector{0.0, vector.x, vector.y, vector.z};

    // 单位四元数的逆等于共轭：p' = q p q^-1。
    const Quaternion rotated =
        unitRotation * pureVector * Conjugate(unitRotation);
    return {rotated.x, rotated.y, rotated.z};
}

Quaternion NLerp(Quaternion start, Quaternion end, double t)
{
    start = Normalize(start);
    end = Normalize(end);

    // q 和 -q 表示同一个姿态；取反终点可以选择较短路径。
    if (Dot(start, end) < 0.0)
    {
        end = -end;
    }

    return Normalize((1.0 - t) * start + t * end);
}

Quaternion Slerp(Quaternion start, Quaternion end, double t)
{
    start = Normalize(start);
    end = Normalize(end);

    double dot = Dot(start, end);
    if (dot < 0.0)
    {
        end = -end;
        dot = -dot;
    }

    // 浮点误差可能让点积略微超出 acos 的定义域。
    dot = std::clamp(dot, -1.0, 1.0);

    // 两点非常接近时 sin(omega) 接近 0，NLERP 更稳定且误差极小。
    if (dot > 0.9995)
    {
        return NLerp(start, end, t);
    }

    const double omega = std::acos(dot);
    const double sinOmega = std::sin(omega);
    const double startWeight =
        std::sin((1.0 - t) * omega) / sinOmega;
    const double endWeight = std::sin(t * omega) / sinOmega;

    return Normalize(startWeight * start + endWeight * end);
}

double RotationAngleRadians(const Quaternion& input)
{
    Quaternion quaternion = Normalize(input);

    // q 与 -q 表示同一旋转，这里选择 w >= 0 的表示，返回最短旋转角。
    if (quaternion.w < 0.0)
    {
        quaternion = -quaternion;
    }

    const double cosineHalfAngle = std::clamp(quaternion.w, -1.0, 1.0);
    return 2.0 * std::acos(cosineHalfAngle);
}

double LerpEulerAngle(double startRadians, double endRadians, double t)
{
    // 故意展示最朴素的欧拉角分量插值：它不知道 179° 与 -179° 只差 2°。
    return (1.0 - t) * startRadians + t * endRadians;
}

bool NearlyEqual(double left, double right, double tolerance = 1e-8)
{
    return std::abs(left - right) <= tolerance;
}

bool CheckScalar(
    const char* testName,
    double actual,
    double expected,
    double tolerance = 1e-8)
{
    const bool passed = NearlyEqual(actual, expected, tolerance);
    std::cout << (passed ? "[PASS] " : "[FAIL] ")
              << testName << ": actual=" << actual
              << ", expected=" << expected << '\n';
    return passed;
}

bool CheckVector(
    const char* testName,
    const Vec3& actual,
    const Vec3& expected,
    double tolerance = 1e-8)
{
    const bool passed = NearlyEqual(actual.x, expected.x, tolerance)
        && NearlyEqual(actual.y, expected.y, tolerance)
        && NearlyEqual(actual.z, expected.z, tolerance);

    std::cout << (passed ? "[PASS] " : "[FAIL] ")
              << testName << ": ("
              << actual.x << ", " << actual.y << ", " << actual.z
              << ")\n";
    return passed;
}

int main()
{
    std::cout << std::fixed << std::setprecision(6);
    bool allPassed = true;

    const Vec3 xAxis{1.0, 0.0, 0.0};
    const Vec3 yAxis{0.0, 1.0, 0.0};
    const Vec3 zAxis{0.0, 0.0, 1.0};

    const Quaternion rotateZ90 = FromAxisAngleRadians(zAxis, pi / 2.0);
    allPassed &= CheckVector(
        "q p q^-1 rotates +X to +Y",
        RotateVector(rotateZ90, xAxis),
        yAxis);

    const Quaternion inverseZ90 = Inverse(rotateZ90);
    allPassed &= CheckVector(
        "inverse rotation returns +Y to +X",
        RotateVector(inverseZ90, yAxis),
        xAxis);

    allPassed &= CheckVector(
        "q and -q represent the same rotation",
        RotateVector(-rotateZ90, xAxis),
        yAxis);

    const Quaternion identity{};
    const Quaternion rotateZ180 = FromAxisAngleRadians(zAxis, pi);
    const Quaternion nlerpQuarter = NLerp(identity, rotateZ180, 0.25);
    const Quaternion slerpQuarter = Slerp(identity, rotateZ180, 0.25);

    // NLERP 沿正确圆弧移动，但 t 与圆心角不成正比。
    allPassed &= CheckScalar(
        "NLERP at t=0.25 is not 45 degrees",
        RadiansToDegrees(RotationAngleRadians(nlerpQuarter)),
        36.8698976458,
        1e-6);

    // SLERP 让四维球面夹角线性变化，因此三维旋转角也线性变化。
    allPassed &= CheckScalar(
        "SLERP at t=0.25",
        RadiansToDegrees(RotationAngleRadians(slerpQuarter)),
        45.0);
    allPassed &= CheckScalar(
        "SLERP at t=0.50",
        RadiansToDegrees(
            RotationAngleRadians(Slerp(identity, rotateZ180, 0.50))),
        90.0);
    allPassed &= CheckScalar(
        "SLERP at t=0.75",
        RadiansToDegrees(
            RotationAngleRadians(Slerp(identity, rotateZ180, 0.75))),
        135.0);

    // 170° 到 -170° 的最短路径只有 20°，中点应位于 180°。
    const Quaternion rotateZ170 = FromAxisAngleRadians(zAxis, 17.0 * pi / 18.0);
    const Quaternion rotateZMinus170 =
        FromAxisAngleRadians(zAxis, -17.0 * pi / 18.0);
    const Quaternion shortestMidpoint =
        Slerp(rotateZ170, rotateZMinus170, 0.5);
    allPassed &= CheckVector(
        "SLERP chooses the shortest path across 180 degrees",
        RotateVector(shortestMidpoint, xAxis),
        Vec3{-1.0, 0.0, 0.0});

    allPassed &= CheckScalar(
        "Euler component lerp takes the long path midpoint",
        LerpEulerAngle(
            17.0 * pi / 18.0,
            -17.0 * pi / 18.0,
            0.5),
        0.0);

    // 极小夹角会走 NLERP 回退分支，避免除以接近 0 的 sin(omega)。
    const Quaternion tinyRotation =
        FromAxisAngleRadians(zAxis, pi / 180000.0);
    allPassed &= CheckScalar(
        "near-zero SLERP fallback",
        RadiansToDegrees(
            RotationAngleRadians(Slerp(identity, tinyRotation, 0.5))),
        0.0005,
        1e-8);

    return allPassed ? 0 : 1;
}
