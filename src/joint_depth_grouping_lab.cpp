#include <cstddef>
#include <iostream>
#include <vector>

int GetJointDepth(
    const std::size_t jointIndex,
    const std::vector<int>& parent,
    std::vector<int>& depth)
{
    // 已经计算过的深度直接复用，避免重复沿父节点向上搜索。
    if (depth[jointIndex] != -1)
    {
        return depth[jointIndex];
    }

    // 父节点为 -1 的关节是根节点，其深度为 0。
    if (parent[jointIndex] == -1)
    {
        depth[jointIndex] = 0;
        return 0;
    }

    const auto parentIndex =
        static_cast<std::size_t>(parent[jointIndex]);

    depth[jointIndex] =
        GetJointDepth(parentIndex, parent, depth) + 1;

    return depth[jointIndex];
}

std::vector<std::vector<int>> GroupJointsByDepth(
    const std::vector<int>& parent)
{
    std::vector<int> depth(parent.size(), -1);
    std::vector<std::vector<int>> result;

    for (std::size_t jointIndex = 0;
         jointIndex < parent.size();
         ++jointIndex)
    {
        const int currentDepth =
            GetJointDepth(jointIndex, parent, depth);

        const auto requiredLayerCount =
            static_cast<std::size_t>(currentDepth + 1);

        if (result.size() < requiredLayerCount)
        {
            result.resize(requiredLayerCount);
        }

        // jointIndex 从小到大扫描，因此同层关节自然保持编号递增。
        result[static_cast<std::size_t>(currentDepth)].push_back(
            static_cast<int>(jointIndex));
    }

    return result;
}

bool CheckLayers(
    const char* testName,
    const std::vector<std::vector<int>>& actual,
    const std::vector<std::vector<int>>& expected)
{
    const bool passed = actual == expected;
    std::cout << (passed ? "[PASS] " : "[FAIL] ") << testName << '\n';
    return passed;
}

int main()
{
    bool allPassed = true;

    allPassed &= CheckLayers(
        "sample hierarchy",
        GroupJointsByDepth({-1, 0, 0, 1, 1, 2}),
        {{0}, {1, 2}, {3, 4, 5}});

    allPassed &= CheckLayers(
        "root and parents may appear after children",
        GroupJointsByDepth({2, 2, -1, 0}),
        {{2}, {0, 1}, {3}});

    allPassed &= CheckLayers(
        "same layer stays sorted by joint index",
        GroupJointsByDepth({-1, 0, 0, 2, 1}),
        {{0}, {1, 2}, {3, 4}});

    allPassed &= CheckLayers(
        "single joint",
        GroupJointsByDepth({-1}),
        {{0}});

    allPassed &= CheckLayers(
        "empty hierarchy",
        GroupJointsByDepth({}),
        {});

    return allPassed ? 0 : 1;
}
