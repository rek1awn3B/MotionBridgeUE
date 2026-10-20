#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

enum class BvhChannel
{
    XPosition,
    YPosition,
    ZPosition,
    XRotation,
    YRotation,
    ZRotation
};

struct BvhJoint
{
    std::string name;
    int parentIndex{-1};
    std::array<double, 3> offset{};
    std::vector<BvhChannel> channels;
    bool isEndSite{false};
};

struct BvhMotion
{
    double frameTime{};
    std::vector<std::vector<double>> frames;

    [[nodiscard]] std::size_t FrameCount() const
    {
        return frames.size();
    }

    [[nodiscard]] double FramesPerSecond() const
    {
        if (frameTime <= 0.0)
        {
            throw std::logic_error("Frame time must be positive.");
        }

        return 1.0 / frameTime;
    }
};

struct BvhData
{
    std::vector<BvhJoint> joints;
    std::unordered_map<std::string, std::size_t> jointIndexByName;
    BvhMotion motion;

    void ValidateAndBuildJointIndex()
    {
        jointIndexByName.clear();
        jointIndexByName.reserve(joints.size());

        for (std::size_t index = 0; index < joints.size(); ++index)
        {
            const BvhJoint& joint = joints[index];

            // 根节点没有父节点；其他节点的父节点必须已经出现在数组前面。
            if (index == 0)
            {
                if (joint.parentIndex != -1)
                {
                    throw std::invalid_argument(
                        "The first joint must be the root."
                    );
                }
            }
            else if (joint.parentIndex < 0
                     || static_cast<std::size_t>(joint.parentIndex) >= index)
            {
                throw std::invalid_argument(
                    joint.name + ": invalid parent index."
                );
            }

            const auto [iterator, inserted] =
                jointIndexByName.emplace(joint.name, index);

            // 同名关节会让按名称映射动作产生歧义，因此立即拒绝。
            if (!inserted)
            {
                throw std::invalid_argument(
                    joint.name + ": duplicate joint name."
                );
            }
        }
    }

    [[nodiscard]] std::size_t FindJointIndex(
        const std::string& name
    ) const
    {
        const auto iterator = jointIndexByName.find(name);
        if (iterator == jointIndexByName.end())
        {
            throw std::out_of_range(name + ": joint not found.");
        }

        return iterator->second;
    }

    [[nodiscard]] std::size_t ChannelCount() const
    {
        std::size_t count = 0;
        for (const BvhJoint& joint : joints)
        {
            count += joint.channels.size();
        }
        return count;
    }
};

BvhData CreateExampleData()
{
    BvhData data{
        .joints = {
            {
                "Hips",
                -1,
                {0.0, 0.0, 0.0},
                {
                    BvhChannel::XPosition,
                    BvhChannel::YPosition,
                    BvhChannel::ZPosition,
                    BvhChannel::ZRotation,
                    BvhChannel::YRotation,
                    BvhChannel::XRotation,
                },
                false,
            },
            {
                "Spine",
                0,
                {0.0, 9.9, -1.2},
                {
                    BvhChannel::ZRotation,
                    BvhChannel::YRotation,
                    BvhChannel::XRotation,
                },
                false,
            },
            {
                "Spine_EndSite",
                1,
                {0.0, 8.0, 0.0},
                {},
                true,
            },
        },
        .jointIndexByName = {},
        .motion = {
            .frameTime = 1.0 / 30.0,
            .frames = {
                {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
            },
        },
    };

    data.ValidateAndBuildJointIndex();
    return data;
}

bool Check(const char* testName, const bool condition)
{
    std::cout << (condition ? "[PASS] " : "[FAIL] ")
              << testName << '\n';
    return condition;
}

template <typename ExceptionType, typename FunctionType>
bool CheckThrows(const char* testName, FunctionType function)
{
    try
    {
        function();
    }
    catch (const ExceptionType&)
    {
        std::cout << "[PASS] " << testName << '\n';
        return true;
    }
    catch (...)
    {
    }

    std::cout << "[FAIL] " << testName << '\n';
    return false;
}

int main()
{
    bool allPassed = true;
    BvhData data = CreateExampleData();

    allPassed &= Check(
        "parent-index hierarchy",
        data.joints[2].parentIndex == 1
    );
    allPassed &= Check(
        "name lookup uses unordered map",
        data.FindJointIndex("Spine") == 1
    );
    allPassed &= Check(
        "channel count includes no End Site channels",
        data.ChannelCount() == 9
    );
    allPassed &= Check(
        "frame metadata",
        data.motion.FrameCount() == 1
            && std::abs(data.motion.FramesPerSecond() - 30.0) < 1e-9
    );

    allPassed &= CheckThrows<std::invalid_argument>(
        "duplicate names are rejected",
        []
        {
            BvhData duplicateData = CreateExampleData();
            duplicateData.joints.push_back(duplicateData.joints.back());
            duplicateData.joints.back().parentIndex = 1;
            duplicateData.ValidateAndBuildJointIndex();
        }
    );

    allPassed &= CheckThrows<std::invalid_argument>(
        "invalid parent index is rejected",
        []
        {
            BvhData invalidData = CreateExampleData();
            invalidData.joints[1].parentIndex = 2;
            invalidData.ValidateAndBuildJointIndex();
        }
    );

    return allPassed ? 0 : 1;
}
