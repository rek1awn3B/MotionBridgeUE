#include <cstddef>
#include <iostream>
#include <list>
#include <string_view>
#include <unordered_map>
#include <utility>

class LRUCache
{
public:
    explicit LRUCache(const int capacity)
        : capacity_(capacity > 0
              ? static_cast<std::size_t>(capacity)
              : 0U)
    {
    }

    int Get(const int key)
    {
        const auto found = positions_.find(key);
        if (found == positions_.end())
        {
            return -1;
        }

        // splice 只重新连接链表节点，不复制元素，复杂度为 O(1)。
        entries_.splice(entries_.begin(), entries_, found->second);
        return found->second->second;
    }

    void Put(const int key, const int value)
    {
        const auto found = positions_.find(key);
        if (found != positions_.end())
        {
            found->second->second = value;
            entries_.splice(entries_.begin(), entries_, found->second);
            return;
        }

        // 新节点放在表头，表头始终代表最近使用。
        entries_.emplace_front(key, value);
        positions_[key] = entries_.begin();

        if (entries_.size() > capacity_)
        {
            // 尾节点最久未使用；先用它的 key 清理哈希表，再删除节点。
            const int leastRecentlyUsedKey = entries_.back().first;
            positions_.erase(leastRecentlyUsedKey);
            entries_.pop_back();
        }
    }

private:
    using Entry = std::pair<int, int>;
    using EntryList = std::list<Entry>;

    std::size_t capacity_{};
    EntryList entries_;
    std::unordered_map<int, EntryList::iterator> positions_;
};

bool CheckEqual(
    const std::string_view testName,
    const int actual,
    const int expected)
{
    const bool passed = actual == expected;
    std::cout << (passed ? "[PASS] " : "[FAIL] ")
              << testName
              << ": expected=" << expected
              << ", actual=" << actual << '\n';
    return passed;
}

int main()
{
    bool allPassed = true;

    LRUCache cache(2);
    cache.Put(1, 10);
    cache.Put(2, 20);
    allPassed &= CheckEqual("read existing key", cache.Get(1), 10);
    cache.Put(3, 30);
    allPassed &= CheckEqual("evict least recent key", cache.Get(2), -1);
    cache.Put(4, 40);
    allPassed &= CheckEqual("second eviction", cache.Get(1), -1);
    allPassed &= CheckEqual("keep key 3", cache.Get(3), 30);
    allPassed &= CheckEqual("keep key 4", cache.Get(4), 40);

    LRUCache updateCache(2);
    updateCache.Put(1, 10);
    updateCache.Put(2, 20);
    updateCache.Put(1, 11);
    updateCache.Put(3, 30);
    allPassed &= CheckEqual(
        "update refreshes recency",
        updateCache.Get(1),
        11);
    allPassed &= CheckEqual(
        "update does not grow cache",
        updateCache.Get(2),
        -1);

    LRUCache oneSlotCache(1);
    oneSlotCache.Put(7, 70);
    oneSlotCache.Put(8, 80);
    allPassed &= CheckEqual(
        "capacity one evicts old key",
        oneSlotCache.Get(7),
        -1);
    allPassed &= CheckEqual(
        "capacity one keeps new key",
        oneSlotCache.Get(8),
        80);

    LRUCache zeroCapacityCache(0);
    zeroCapacityCache.Put(9, 90);
    allPassed &= CheckEqual(
        "zero capacity stores nothing",
        zeroCapacityCache.Get(9),
        -1);

    return allPassed ? 0 : 1;
}
