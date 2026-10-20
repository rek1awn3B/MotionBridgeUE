#include <algorithm>
#include <iostream>
#include <unordered_map>
#include <vector>

int LongestConsecutive(const std::vector<int>& numbers)
{
    // 键是出现过的整数；新区间的左右边界保存整个区间长度。
    // 内部元素的长度允许过期，因为后续合并只会读取相邻区间的边界。
    std::unordered_map<long long, int> intervalLength;
    intervalLength.reserve(numbers.size() * 2 + 1);

    int maximumLength = 0;
    for (const int value : numbers)
    {
        const long long number = value;
        if (intervalLength.contains(number))
        {
            continue;
        }

        const auto leftIterator = intervalLength.find(number - 1);
        const auto rightIterator = intervalLength.find(number + 1);
        const int leftLength = leftIterator == intervalLength.end()
            ? 0
            : leftIterator->second;
        const int rightLength = rightIterator == intervalLength.end()
            ? 0
            : rightIterator->second;

        const int mergedLength = leftLength + 1 + rightLength;
        const long long leftBoundary = number - leftLength;
        const long long rightBoundary = number + rightLength;

        // number 负责标记“已经出现”；两端负责为后续合并提供正确长度。
        intervalLength[number] = mergedLength;
        intervalLength[leftBoundary] = mergedLength;
        intervalLength[rightBoundary] = mergedLength;
        maximumLength = std::max(maximumLength, mergedLength);
    }

    return maximumLength;
}

bool Check(
    const char* name,
    const std::vector<int>& numbers,
    int expected)
{
    const int actual = LongestConsecutive(numbers);
    const bool passed = actual == expected;
    std::cout << (passed ? "[PASS] " : "[FAIL] ")
              << name << ": " << actual << '\n';
    return passed;
}

int main()
{
    bool allPassed = true;
    allPassed &= Check("普通合并", {100, 4, 200, 1, 3, 2}, 4);
    allPassed &= Check("重复元素", {0, 3, 7, 2, 5, 8, 4, 6, 0, 1}, 9);
    allPassed &= Check("空数组", {}, 0);
    allPassed &= Check("左右区间桥接", {1, 2, 4, 5, 3}, 5);
    return allPassed ? 0 : 1;
}
