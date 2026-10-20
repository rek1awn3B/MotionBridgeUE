#include <cstddef>
#include <iostream>
#include <stack>
#include <vector>

std::vector<int> DailyTemperatures(const std::vector<int>& temperatures)
{
    std::vector<int> answer(temperatures.size(), 0);
    std::stack<std::size_t> unresolvedIndices;

    for (std::size_t currentIndex = 0;
         currentIndex < temperatures.size();
         ++currentIndex)
    {
        // 当前温度更高时，它解决了栈顶旧日期的问题。
        // 相等温度不能弹出，因为题目要求严格更高。
        while (!unresolvedIndices.empty()
               && temperatures[currentIndex]
                   > temperatures[unresolvedIndices.top()])
        {
            const std::size_t previousIndex = unresolvedIndices.top();
            unresolvedIndices.pop();

            answer[previousIndex] =
                static_cast<int>(currentIndex - previousIndex);
        }

        // 尚未找到更高温度的日期按温度单调不升地保存在栈中。
        unresolvedIndices.push(currentIndex);
    }

    // 扫描结束后仍留在栈中的日期没有更高温度，答案保持初始值 0。
    return answer;
}

bool CheckVector(
    const char* testName,
    const std::vector<int>& actual,
    const std::vector<int>& expected)
{
    const bool passed = actual == expected;
    std::cout << (passed ? "[PASS] " : "[FAIL] ") << testName << '\n';

    if (!passed)
    {
        std::cout << "  actual: ";
        for (const int value : actual)
        {
            std::cout << value << ' ';
        }

        std::cout << "\n  expected: ";
        for (const int value : expected)
        {
            std::cout << value << ' ';
        }
        std::cout << '\n';
    }

    return passed;
}

int main()
{
    bool allPassed = true;

    allPassed &= CheckVector(
        "sample",
        DailyTemperatures({73, 74, 75, 71, 69, 72, 76, 73}),
        {1, 1, 4, 2, 1, 1, 0, 0});

    allPassed &= CheckVector(
        "strictly decreasing",
        DailyTemperatures({80, 70, 60, 50}),
        {0, 0, 0, 0});

    allPassed &= CheckVector(
        "equal temperatures stay unresolved",
        DailyTemperatures({70, 70, 71}),
        {2, 1, 0});

    allPassed &= CheckVector(
        "one day resolves several earlier days",
        DailyTemperatures({75, 70, 71, 72, 76}),
        {4, 1, 1, 1, 0});

    allPassed &= CheckVector(
        "empty input",
        DailyTemperatures({}),
        {});

    return allPassed ? 0 : 1;
}
