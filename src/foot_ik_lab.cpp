#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

struct Vec3
{
    double x{};
    double y{};
    double z{};
};

Vec3 operator+(const Vec3& left, const Vec3& right)
{
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

Vec3 operator-(const Vec3& left, const Vec3& right)
{
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

Vec3 operator*(const Vec3& vector, const double scalar)
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

double Length(const Vec3& vector)
{
    return std::sqrt(Dot(vector, vector));
}

Vec3 Normalize(const Vec3& vector)
{
    constexpr double epsilon = 1e-9;
    const double length = Length(vector);
    if (length < epsilon)
    {
        throw std::invalid_argument("不能归一化零向量");
    }
    return vector * (1.0 / length);
}

struct GroundHit
{
    bool blockingHit{};
    Vec3 point{};
    Vec3 normal{0.0, 0.0, 1.0};
};

struct FootTarget
{
    Vec3 position{};
    Vec3 forward{};
    Vec3 right{};
    Vec3 up{};
};

struct TwoBoneIKResult
{
    Vec3 root{};
    Vec3 joint{};
    Vec3 end{};
    bool reachedTarget{};
};

FootTarget BuildFootTarget(
    const GroundHit& hit,
    const Vec3& characterForward,
    const double soleThickness)
{
    if (!hit.blockingHit)
    {
        throw std::invalid_argument("没有地面命中，不能构造脚部目标");
    }

    const Vec3 up = Normalize(hit.normal);

    // 把角色前方向投影到地面切平面，作为脚掌的新前方向。
    // projected = forward - up * dot(forward, up)
    const Vec3 projectedForward =
        characterForward - up * Dot(characterForward, up);
    const Vec3 forward = Normalize(projectedForward);

    // UE 常用 X 前、Y 右、Z 上。已知 X 和 Z 后，用叉积得到 Y。
    const Vec3 right = Normalize(Cross(up, forward));

    return {
        hit.point + up * soleThickness,
        forward,
        right,
        up,
    };
}

TwoBoneIKResult SolveTwoBoneIK(
    const Vec3& root,
    const Vec3& joint,
    const Vec3& end,
    const Vec3& target,
    const Vec3& poleDirection)
{
    constexpr double epsilon = 1e-6;
    const double upperLength = Length(joint - root);
    const double lowerLength = Length(end - joint);
    if (upperLength < epsilon || lowerLength < epsilon)
    {
        throw std::invalid_argument("两段骨骼长度必须大于零");
    }

    const Vec3 rootToTarget = target - root;
    const double requestedDistance = Length(rootToTarget);
    if (requestedDistance < epsilon)
    {
        throw std::invalid_argument("IK 目标不能与根关节重合");
    }

    const Vec3 targetDirection = Normalize(rootToTarget);
    const double minimumReach =
        std::abs(upperLength - lowerLength) + epsilon;
    const double maximumReach = upperLength + lowerLength - epsilon;
    const double solvedDistance = std::clamp(
        requestedDistance,
        minimumReach,
        maximumReach);

    // poleDirection 决定膝盖向哪边弯。先去掉它沿目标方向的分量，
    // 剩下的就是 IK 平面内、垂直于髋到脚方向的弯曲方向。
    const Vec3 projectedPole =
        poleDirection - targetDirection * Dot(poleDirection, targetDirection);
    const Vec3 bendDirection = Normalize(projectedPole);

    // 余弦定理求膝盖在“髋到目标”方向上的投影长度 x。
    const double x =
        (upperLength * upperLength
            - lowerLength * lowerLength
            + solvedDistance * solvedDistance)
        / (2.0 * solvedDistance);
    const double ySquared = std::max(
        0.0,
        upperLength * upperLength - x * x);
    const double y = std::sqrt(ySquared);

    return {
        root,
        root + targetDirection * x + bendDirection * y,
        root + targetDirection * solvedDistance,
        requestedDistance >= minimumReach
            && requestedDistance <= maximumReach,
    };
}

double SmoothValue(
    const double current,
    const double target,
    const double responseSpeed,
    const double deltaSeconds)
{
    if (responseSpeed < 0.0 || deltaSeconds < 0.0)
    {
        throw std::invalid_argument("平滑速度和帧间隔不能为负数");
    }

    // 这个 alpha 来自指数衰减。相同总时长下，30 FPS 和 60 FPS 的
    // 结果几乎一致，不会因为帧率不同而改变脚部响应速度。
    const double alpha = 1.0 - std::exp(-responseSpeed * deltaSeconds);
    return current + (target - current) * alpha;
}

bool NearlyEqual(
    const double left,
    const double right,
    const double epsilon = 1e-6)
{
    return std::abs(left - right) <= epsilon;
}

void Require(const bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

int main()
{
    // 假设右腿初始为：髋在 95 cm 高处，膝盖和脚踝逐级向下。
    const Vec3 hip{0.0, 0.0, 95.0};
    const Vec3 knee{0.0, 20.0, 55.0};
    const Vec3 ankle{0.0, 0.0, 10.0};

    // 射线命中一个略微倾斜的坡面。
    const GroundHit hit{
        true,
        {18.0, 4.0, 3.0},
        Normalize({-0.15, 0.0, 1.0}),
    };
    const FootTarget footTarget = BuildFootTarget(
        hit,
        {1.0, 0.0, 0.0},
        4.0);

    // 示例腿的膝盖初始朝 +Y 弯，因此 pole 也取 +Y，防止解算时翻面。
    const TwoBoneIKResult solved = SolveTwoBoneIK(
        hip,
        knee,
        ankle,
        footTarget.position,
        {0.0, 1.0, 0.0});

    Require(solved.reachedTarget, "可达目标不应被标记为超出腿长");
    Require(
        NearlyEqual(Length(solved.joint - solved.root), Length(knee - hip)),
        "解算后大腿长度发生变化");
    Require(
        NearlyEqual(Length(solved.end - solved.joint), Length(ankle - knee)),
        "解算后小腿长度发生变化");
    Require(
        NearlyEqual(Length(solved.end - footTarget.position), 0.0),
        "脚踝没有到达地面目标");

    Require(NearlyEqual(Dot(footTarget.forward, footTarget.up), 0.0),
        "脚掌前方向与地面法线不垂直");
    Require(NearlyEqual(Dot(footTarget.right, footTarget.up), 0.0),
        "脚掌右方向与地面法线不垂直");
    Require(NearlyEqual(Length(footTarget.forward), 1.0),
        "脚掌前方向不是单位向量");
    Require(NearlyEqual(Length(footTarget.right), 1.0),
        "脚掌右方向不是单位向量");
    Require(NearlyEqual(Length(footTarget.up), 1.0),
        "脚掌上方向不是单位向量");

    // 验证目标太远时会伸到最大腿长，而不是拉长骨骼。
    const TwoBoneIKResult unreachable = SolveTwoBoneIK(
        hip,
        knee,
        ankle,
        {500.0, 0.0, 100.0},
        {0.0, 1.0, 0.0});
    Require(!unreachable.reachedTarget, "超出腿长的目标应被标记为不可达");
    Require(
        Length(unreachable.end - unreachable.root)
            < Length(knee - hip) + Length(ankle - knee),
        "不可达目标没有被限制到最大腿长");

    // 当两段骨骼不等长时，目标也不能离根关节无限近。
    const TwoBoneIKResult tooClose = SolveTwoBoneIK(
        hip,
        knee,
        ankle,
        {0.0, 0.0, 94.9},
        {0.0, 1.0, 0.0});
    Require(!tooClose.reachedTarget, "过近目标也应被标记为不可达");

    // 比较 1 秒内 30 FPS 与 60 FPS 的指数平滑结果。
    double valueAt30Fps = 0.0;
    for (int frame = 0; frame < 30; ++frame)
    {
        valueAt30Fps = SmoothValue(valueAt30Fps, 20.0, 8.0, 1.0 / 30.0);
    }
    double valueAt60Fps = 0.0;
    for (int frame = 0; frame < 60; ++frame)
    {
        valueAt60Fps = SmoothValue(valueAt60Fps, 20.0, 8.0, 1.0 / 60.0);
    }
    Require(NearlyEqual(valueAt30Fps, valueAt60Fps),
        "平滑结果不应依赖 30 FPS 或 60 FPS");

    std::cout
        << "FOOT_IK_LAB=PASS\n"
        << "foot_target=("
        << footTarget.position.x << ", "
        << footTarget.position.y << ", "
        << footTarget.position.z << ")\n"
        << "solved_knee=("
        << solved.joint.x << ", "
        << solved.joint.y << ", "
        << solved.joint.z << ")\n"
        << "smoothed_offset=" << valueAt60Fps << '\n';
    return 0;
}
