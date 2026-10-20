#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

class AnimationClip
{
public:
    explicit AnimationClip(std::string name)
        : name_(std::move(name))
    {
        std::cout << "create clip: " << name_ << '\n';
    }

    ~AnimationClip()
    {
        std::cout << "destroy clip: " << name_ << '\n';
    }

    void Play() const
    {
        std::cout << "play clip: " << name_ << '\n';
    }

private:
    std::string name_;
};

class AnimationPlayer
{
public:
    explicit AnimationPlayer(std::unique_ptr<AnimationClip> clip)
        : clip_(std::move(clip))
    {
    }

    void Play() const
    {
        clip_->Play();
    }

private:
    // unique_ptr 自动处理析构和移动，因此本类不手写五个特殊成员函数。
    std::unique_ptr<AnimationClip> clip_;
};

class SkeletonAsset
{
public:
    explicit SkeletonAsset(std::string name)
        : name_(std::move(name))
    {
        std::cout << "create skeleton: " << name_ << '\n';
    }

    ~SkeletonAsset()
    {
        std::cout << "destroy skeleton: " << name_ << '\n';
    }

private:
    std::string name_;
};

class Character
{
public:
    Character(
        std::string name,
        std::shared_ptr<SkeletonAsset> skeleton)
        : name_(std::move(name)),
          skeleton_(std::move(skeleton))
    {
    }

private:
    std::string name_;
    std::shared_ptr<SkeletonAsset> skeleton_;
};

struct Node
{
    explicit Node(std::string inputName)
        : name(std::move(inputName))
    {
        std::cout << "create node: " << name << '\n';
    }

    ~Node()
    {
        std::cout << "destroy node: " << name << '\n';
    }

    std::string name;

    // 父节点拥有子节点；子节点只观察父节点，从而避免强引用环。
    std::weak_ptr<Node> parent;
    std::vector<std::shared_ptr<Node>> children;
};

bool Check(const char* name, bool condition)
{
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
    return condition;
}

int main()
{
    bool allPassed = true;

    // Rule of Zero：成员类型自然决定 AnimationPlayer 可移动但不可复制。
    static_assert(!std::is_copy_constructible_v<AnimationPlayer>);
    static_assert(std::is_move_constructible_v<AnimationPlayer>);

    {
        auto clip = std::make_unique<AnimationClip>("Walk");
        AnimationPlayer player{std::move(clip)};

        allPassed &= Check(
            "unique_ptr ownership moved from caller",
            clip == nullptr);
        player.Play();
    }

    std::weak_ptr<SkeletonAsset> skeletonObserver;
    {
        auto skeleton =
            std::make_shared<SkeletonAsset>("HumanSkeleton");
        skeletonObserver = skeleton;

        Character first{"Alice", skeleton};
        Character second{"Bob", skeleton};

        allPassed &= Check(
            "weak observer sees a live shared object",
            !skeletonObserver.expired());
    }
    allPassed &= Check(
        "shared object dies after its final owner",
        skeletonObserver.expired());

    std::weak_ptr<Node> parentObserver;
    std::weak_ptr<Node> childObserver;
    {
        auto parent = std::make_shared<Node>("parent");
        auto child = std::make_shared<Node>("child");

        parentObserver = parent;
        childObserver = child;
        parent->children.push_back(child);
        child->parent = parent;

        allPassed &= Check(
            "child can lock its living parent",
            child->parent.lock() == parent);
    }

    allPassed &= Check(
        "weak parent link does not create a reference cycle",
        parentObserver.expired() && childObserver.expired());

    return allPassed ? 0 : 1;
}
