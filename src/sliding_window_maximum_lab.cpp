#include <cstddef>
#include <deque>
#include <iostream>
#include <string_view>
#include <vector>

std::vector<int> SlidingWindowMaximum(
    const std::vector<int>& numbers,
    const int windowSizeInput)
{
    if (windowSizeInput <= 0 ||
        static_cast<std::size_t>(windowSizeInput) > numbers.size())
    {
        return {};
    }

    const std::size_t windowSize =
        static_cast<std::size_t>(windowSizeInput);
    std::deque<std::size_t> candidates;
    std::vector<int> result;
    result.reserve(numbers.size() - windowSize + 1U);

    for (std::size_t index = 0; index < numbers.size(); ++index)
    {
        // front() 读取队头，pop_front() 删除队头。
        // 队头下标已经离开当前窗口时，它不再有资格成为答案。
        while (!candidates.empty() &&
               candidates.front() + windowSize <= index)
        {
            candidates.pop_front();
        }

        // back() 读取队尾，pop_back() 删除队尾。
        // 队尾元素不大于新元素时，它更小（或相等）且更早过期，永远不会
        // 再成为后续窗口的最大值，所以可以直接淘汰。
        while (!candidates.empty() &&
               numbers[candidates.back()] <= numbers[index])
        {
            candidates.pop_back();
        }

        // push_back() 把当前下标加入队尾。队列中始终保存候选下标，
        // 对应数值从队头到队尾单调递减。
        candidates.push_back(index);

        if (index + 1U >= windowSize)
        {
            result.push_back(numbers[candidates.front()]);
        }
    }

    return result;
}

bool CheckEqual(
    const std::string_view testName,
    const std::vector<int>& actual,
    const std::vector<int>& expected)
{
    const bool passed = actual == expected;
    std::cout << (passed ? "[PASS] " : "[FAIL] ") << testName;
    if (!passed)
    {
        std::cout << ": expected=";
        for (const int value : expected)
        {
            std::cout << value << ' ';
        }
        std::cout << "actual=";
        for (const int value : actual)
        {
            std::cout << value << ' ';
        }
    }
    std::cout << '\n';
    return passed;
}

int main()
{
    bool allPassed = true;

    allPassed &= CheckEqual(
        "standard example",
        SlidingWindowMaximum({1, 3, -1, -3, 5, 3, 6, 7}, 3),
        {3, 3, 5, 5, 6, 7});
    allPassed &= CheckEqual(
        "window size one",
        SlidingWindowMaximum({4, 2, 8}, 1),
        {4, 2, 8});
    allPassed &= CheckEqual(
        "whole array window",
        SlidingWindowMaximum({4, 2, 8}, 3),
        {8});
    allPassed &= CheckEqual(
        "duplicate values",
        SlidingWindowMaximum({5, 5, 5, 4, 5}, 2),
        {5, 5, 5, 5});
    allPassed &= CheckEqual(
        "strictly decreasing",
        SlidingWindowMaximum({9, 7, 5, 3, 1}, 3),
        {9, 7, 5});
    allPassed &= CheckEqual(
        "invalid zero window",
        SlidingWindowMaximum({1, 2, 3}, 0),
        {});
    allPassed &= CheckEqual(
        "window larger than array",
        SlidingWindowMaximum({1, 2, 3}, 4),
        {});

    return allPassed ? 0 : 1;
}
