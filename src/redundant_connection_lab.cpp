#include <cassert>
#include <iostream>
#include <numeric>
#include <utility>
#include <vector>

class DisjointSet
{
public:
    explicit DisjointSet(int nodeCount)
        : parent(static_cast<std::size_t>(nodeCount + 1)),
          setSize(static_cast<std::size_t>(nodeCount + 1), 1)
    {
        // 节点编号从 1 开始，初始化时每个节点都是自己的根。
        std::iota(parent.begin(), parent.end(), 0);
    }

    int Find(int node)
    {
        // 递归寻找根节点，并在返回途中完成路径压缩。
        return parent[node] == node ? node : parent[node] = Find(parent[node]);
    }

    bool Union(int first, int second)
    {
        int firstRoot = Find(first);
        int secondRoot = Find(second);

        if (firstRoot == secondRoot)
        {
            // 两个端点原本已经连通，再加入这条边就会形成环。
            return false;
        }

        // 按集合大小合并：始终把较小集合挂到较大集合下面。
        if (setSize[firstRoot] < setSize[secondRoot])
        {
            std::swap(firstRoot, secondRoot);
        }

        parent[secondRoot] = firstRoot;
        setSize[firstRoot] += setSize[secondRoot];
        return true;
    }

private:
    std::vector<int> parent;
    std::vector<int> setSize;
};

std::pair<int, int> FindRedundantConnection(
    const std::vector<std::pair<int, int>>& edges)
{
    DisjointSet disjointSet(static_cast<int>(edges.size()));

    for (const auto& [first, second] : edges)
    {
        if (!disjointSet.Union(first, second))
        {
            return {first, second};
        }
    }

    return {-1, -1};
}

int main()
{
    assert((FindRedundantConnection({
               {1, 2},
               {1, 3},
               {2, 3},
           }) == std::pair{2, 3}));

    assert((FindRedundantConnection({
               {1, 2},
               {2, 3},
               {3, 4},
               {1, 4},
               {1, 5},
           }) == std::pair{1, 4}));

    assert((FindRedundantConnection({
               {1, 2},
               {2, 3},
               {3, 1},
           }) == std::pair{3, 1}));

    std::cout << "redundant connection checks passed: 3\n";
    return 0;
}
