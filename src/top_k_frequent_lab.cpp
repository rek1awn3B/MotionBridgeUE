#include <algorithm>
#include <functional>
#include <iostream>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

std::vector<int> TopKFrequent(
    const std::vector<int>& numbers,
    const int k)
{
    if (k <= 0 || numbers.empty())
    {
        return {};
    }

    std::unordered_map<int, int> frequencyByNumber;
    for (const int number : numbers)
    {
        ++frequencyByNumber[number];
    }

    using FrequencyAndNumber = std::pair<int, int>;
    std::priority_queue<
        FrequencyAndNumber,
        std::vector<FrequencyAndNumber>,
        std::greater<FrequencyAndNumber>> minHeap;

    for (const auto& [number, frequency] : frequencyByNumber)
    {
        minHeap.emplace(frequency, number);

        if (minHeap.size() > static_cast<std::size_t>(k))
        {
            // 堆顶是当前候选中频率最低的元素，超出容量时将它淘汰。
            minHeap.pop();
        }
    }

    std::vector<int> result;
    result.reserve(minHeap.size());

    while (!minHeap.empty())
    {
        result.push_back(minHeap.top().second);
        minHeap.pop();
    }

    return result;
}

bool CheckUnorderedResult(
    const char* testName,
    std::vector<int> actual,
    std::vector<int> expected)
{
    std::sort(actual.begin(), actual.end());
    std::sort(expected.begin(), expected.end());

    const bool passed = actual == expected;
    std::cout << (passed ? "[PASS] " : "[FAIL] ")
              << testName << '\n';
    return passed;
}

int main()
{
    bool allPassed = true;

    allPassed &= CheckUnorderedResult(
        "two most frequent values",
        TopKFrequent({1, 1, 1, 2, 2, 3}, 2),
        {1, 2}
    );

    allPassed &= CheckUnorderedResult(
        "negative and positive values",
        TopKFrequent({-4, -4, -4, 2, 2, 7}, 1),
        {-4}
    );

    allPassed &= CheckUnorderedResult(
        "k includes every distinct value",
        TopKFrequent({4, 4, 5, 6}, 3),
        {4, 5, 6}
    );

    allPassed &= CheckUnorderedResult(
        "zero k returns empty result",
        TopKFrequent({1, 1, 2}, 0),
        {}
    );

    allPassed &= CheckUnorderedResult(
        "empty input",
        TopKFrequent({}, 3),
        {}
    );

    return allPassed ? 0 : 1;
}
