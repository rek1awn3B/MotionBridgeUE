#include <cassert>
#include <cstddef>
#include <deque>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

struct MotionFrame
{
    std::size_t frameIndex{};
    double timestampSeconds{};
    std::vector<double> channels;
};

class IMotionSource
{
public:
    // 通过基类指针销毁派生类对象时，基类析构函数必须是 virtual。
    virtual ~IMotionSource() = default;

    // 查询操作不应修改数据源，因此声明为 const 成员函数。
    virtual std::string_view GetName() const = 0;
    virtual bool HasNextFrame() const = 0;

    // 读取会推进内部游标或弹出队列，因此不能声明为 const。
    virtual MotionFrame ReadNextFrame() = 0;
};

class BvhMotionSource final : public IMotionSource
{
public:
    BvhMotionSource(
        std::vector<std::vector<double>> inputFrames,
        double inputFrameTimeSeconds)
        : frames(std::move(inputFrames)),
          frameTimeSeconds(inputFrameTimeSeconds)
    {
        if (frameTimeSeconds <= 0.0)
        {
            throw std::invalid_argument("frame time must be positive");
        }
    }

    std::string_view GetName() const override
    {
        return "offline BVH";
    }

    bool HasNextFrame() const override
    {
        return nextFrameIndex < frames.size();
    }

    MotionFrame ReadNextFrame() override
    {
        if (!HasNextFrame())
        {
            throw std::out_of_range("BVH source has no frame left");
        }

        const std::size_t currentIndex = nextFrameIndex;
        ++nextFrameIndex;

        return MotionFrame{
            currentIndex,
            static_cast<double>(currentIndex) * frameTimeSeconds,
            frames[currentIndex],
        };
    }

private:
    std::vector<std::vector<double>> frames;
    double frameTimeSeconds{};
    std::size_t nextFrameIndex{};
};

class RealtimeMotionSource final : public IMotionSource
{
public:
    std::string_view GetName() const override
    {
        return "realtime stream";
    }

    bool HasNextFrame() const override
    {
        return !pendingFrames.empty();
    }

    MotionFrame ReadNextFrame() override
    {
        if (!HasNextFrame())
        {
            throw std::out_of_range("realtime source has no frame ready");
        }

        MotionFrame frame = std::move(pendingFrames.front());
        pendingFrames.pop_front();
        return frame;
    }

    void PushFrame(MotionFrame frame)
    {
        pendingFrames.push_back(std::move(frame));
    }

private:
    std::deque<MotionFrame> pendingFrames;
};

std::vector<MotionFrame> ConsumeAllFrames(IMotionSource& source)
{
    std::cout << "Consuming " << source.GetName() << '\n';

    std::vector<MotionFrame> consumedFrames;
    while (source.HasNextFrame())
    {
        consumedFrames.push_back(source.ReadNextFrame());
    }
    return consumedFrames;
}

int main()
{
    std::vector<std::unique_ptr<IMotionSource>> sources;

    sources.push_back(std::make_unique<BvhMotionSource>(
        std::vector<std::vector<double>>{
            {0.0, 0.0, 0.0},
            {1.0, 2.0, 3.0},
        },
        1.0 / 30.0));

    auto realtimeSource = std::make_unique<RealtimeMotionSource>();
    realtimeSource->PushFrame(MotionFrame{100, 5.0, {10.0, 20.0}});
    realtimeSource->PushFrame(MotionFrame{101, 5.02, {11.0, 21.0}});
    sources.push_back(std::move(realtimeSource));

    std::size_t checkedFrameCount = 0;
    for (const std::unique_ptr<IMotionSource>& source : sources)
    {
        std::vector<MotionFrame> frames = ConsumeAllFrames(*source);
        assert(frames.size() == 2);
        checkedFrameCount += frames.size();
    }

    assert(checkedFrameCount == 4);
    std::cout << "motion source checks passed: " << checkedFrameCount << '\n';
    return 0;
}
