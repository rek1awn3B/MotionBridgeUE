#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace
{
constexpr double Pi = 3.14159265358979323846;

struct Vec3
{
    double x{};
    double y{};
    double z{};
};

Vec3 Lerp(const Vec3& start, const Vec3& end, const double alpha)
{
    return {
        start.x + (end.x - start.x) * alpha,
        start.y + (end.y - start.y) * alpha,
        start.z + (end.z - start.z) * alpha,
    };
}

struct Quaternion
{
    double w{1.0};
    double x{};
    double y{};
    double z{};

    [[nodiscard]] Quaternion Normalized() const
    {
        const double length = std::sqrt(w * w + x * x + y * y + z * z);
        if (length <= 1e-12)
        {
            throw std::runtime_error("cannot normalize a zero quaternion");
        }
        return {w / length, x / length, y / length, z / length};
    }
};

double Dot(const Quaternion& left, const Quaternion& right)
{
    return left.w * right.w + left.x * right.x +
        left.y * right.y + left.z * right.z;
}

Quaternion operator-(const Quaternion& value)
{
    return {-value.w, -value.x, -value.y, -value.z};
}

Quaternion operator*(const Quaternion& value, const double scalar)
{
    return {
        value.w * scalar,
        value.x * scalar,
        value.y * scalar,
        value.z * scalar,
    };
}

Quaternion operator+(const Quaternion& left, const Quaternion& right)
{
    return {
        left.w + right.w,
        left.x + right.x,
        left.y + right.y,
        left.z + right.z,
    };
}

Quaternion FromAxisAngleZ(const double angleRadians)
{
    const double halfAngle = angleRadians * 0.5;
    return {std::cos(halfAngle), 0.0, 0.0, std::sin(halfAngle)};
}

Quaternion Slerp(Quaternion start, Quaternion end, const double alpha)
{
    start = start.Normalized();
    end = end.Normalized();

    double cosine = Dot(start, end);
    // q 与 -q 表示相同旋转。点积为负时翻转终点，保证走最短圆弧。
    if (cosine < 0.0)
    {
        end = -end;
        cosine = -cosine;
    }

    cosine = std::clamp(cosine, -1.0, 1.0);
    if (cosine > 0.9995)
    {
        return (start * (1.0 - alpha) + end * alpha).Normalized();
    }

    const double angle = std::acos(cosine);
    const double sine = std::sin(angle);
    const double startWeight = std::sin((1.0 - alpha) * angle) / sine;
    const double endWeight = std::sin(alpha * angle) / sine;
    return (start * startWeight + end * endWeight).Normalized();
}

double RotationAngleRadians(const Quaternion& value)
{
    const Quaternion normalized = value.Normalized();
    return 2.0 * std::acos(std::clamp(std::abs(normalized.w), 0.0, 1.0));
}

struct MotionFrame
{
    std::uint64_t sequenceNumber{};
    double timestampSeconds{};
    Vec3 rootPosition;
    Quaternion rootRotation;
    std::vector<Quaternion> localBoneRotations;
};

enum class PushResult
{
    Inserted,
    InsertedOutOfOrder,
    DuplicateSequence,
    InvalidTimestamp,
    BoneCountMismatch,
};

struct SampleResult
{
    MotionFrame frame;
    std::uint64_t lowerSequence{};
    std::uint64_t upperSequence{};
    double alpha{};
    bool clampedToBoundary{};
};

class MotionFrameBuffer
{
public:
    explicit MotionFrameBuffer(const std::size_t capacity)
        : capacity_(std::max<std::size_t>(capacity, 1))
    {
    }

    PushResult Push(MotionFrame frame)
    {
        if (!std::isfinite(frame.timestampSeconds))
        {
            return PushResult::InvalidTimestamp;
        }

        std::scoped_lock lock(mutex_);
        if (expectedBoneCount_.has_value() &&
            frame.localBoneRotations.size() != *expectedBoneCount_)
        {
            return PushResult::BoneCountMismatch;
        }
        if (std::ranges::any_of(
                frames_,
                [&frame](const MotionFrame& existing)
                {
                    return existing.sequenceNumber == frame.sequenceNumber;
                }))
        {
            return PushResult::DuplicateSequence;
        }

        if (!expectedBoneCount_.has_value())
        {
            expectedBoneCount_ = frame.localBoneRotations.size();
        }

        // 按“采样时间戳、帧序号”排序插入。网络晚到的旧帧不能直接追加到末尾。
        const auto insertionPoint = std::ranges::lower_bound(
            frames_,
            std::pair{frame.timestampSeconds, frame.sequenceNumber},
            {},
            [](const MotionFrame& value)
            {
                return std::pair{value.timestampSeconds, value.sequenceNumber};
            });
        const bool outOfOrder = insertionPoint != frames_.end();
        frames_.insert(insertionPoint, std::move(frame));

        if (frames_.size() > capacity_)
        {
            const std::size_t overflow = frames_.size() - capacity_;
            frames_.erase(
                frames_.begin(),
                frames_.begin() + static_cast<std::ptrdiff_t>(overflow));
        }
        return outOfOrder
            ? PushResult::InsertedOutOfOrder
            : PushResult::Inserted;
    }

    [[nodiscard]] std::optional<SampleResult> Sample(
        const double playbackTimestampSeconds) const
    {
        std::scoped_lock lock(mutex_);
        if (frames_.empty() || !std::isfinite(playbackTimestampSeconds))
        {
            return std::nullopt;
        }

        if (playbackTimestampSeconds <= frames_.front().timestampSeconds)
        {
            return SampleResult{
                frames_.front(),
                frames_.front().sequenceNumber,
                frames_.front().sequenceNumber,
                0.0,
                true,
            };
        }
        if (playbackTimestampSeconds >= frames_.back().timestampSeconds)
        {
            return SampleResult{
                frames_.back(),
                frames_.back().sequenceNumber,
                frames_.back().sequenceNumber,
                0.0,
                true,
            };
        }

        const auto upper = std::ranges::upper_bound(
            frames_,
            playbackTimestampSeconds,
            {},
            &MotionFrame::timestampSeconds);
        const auto lower = std::prev(upper);
        const double duration =
            upper->timestampSeconds - lower->timestampSeconds;
        if (duration <= 1e-12)
        {
            return std::nullopt;
        }

        const double alpha = std::clamp(
            (playbackTimestampSeconds - lower->timestampSeconds) / duration,
            0.0,
            1.0);
        MotionFrame sampled;
        sampled.sequenceNumber = lower->sequenceNumber;
        sampled.timestampSeconds = playbackTimestampSeconds;
        sampled.rootPosition = Lerp(
            lower->rootPosition,
            upper->rootPosition,
            alpha);
        sampled.rootRotation = Slerp(
            lower->rootRotation,
            upper->rootRotation,
            alpha);
        sampled.localBoneRotations.reserve(lower->localBoneRotations.size());
        for (std::size_t index = 0;
             index < lower->localBoneRotations.size();
             ++index)
        {
            sampled.localBoneRotations.push_back(Slerp(
                lower->localBoneRotations[index],
                upper->localBoneRotations[index],
                alpha));
        }

        return SampleResult{
            std::move(sampled),
            lower->sequenceNumber,
            upper->sequenceNumber,
            alpha,
            false,
        };
    }

    [[nodiscard]] std::size_t Size() const
    {
        std::scoped_lock lock(mutex_);
        return frames_.size();
    }

    [[nodiscard]] std::optional<std::uint64_t> OldestSequence() const
    {
        std::scoped_lock lock(mutex_);
        return frames_.empty()
            ? std::nullopt
            : std::optional<std::uint64_t>(frames_.front().sequenceNumber);
    }

    void Reset()
    {
        std::scoped_lock lock(mutex_);
        frames_.clear();
        expectedBoneCount_.reset();
    }

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::vector<MotionFrame> frames_;
    std::optional<std::size_t> expectedBoneCount_;
};


class PlaybackClock
{
public:
    double Advance(
        const double oldestTimestamp,
        const double newestTimestamp,
        const double deltaSeconds,
        const double playbackDelaySeconds,
        const double maxSpeedAdjustment,
        const double hardResyncThresholdSeconds)
    {
        const double targetTimestamp = std::max(
            newestTimestamp - std::max(playbackDelaySeconds, 0.0),
            oldestTimestamp);
        if (!initialized_)
        {
            timestampSeconds_ = targetTimestamp;
            initialized_ = true;
            return timestampSeconds_;
        }

        const double clockError = targetTimestamp - timestampSeconds_;
        if (std::abs(clockError) >
            std::max(hardResyncThresholdSeconds, 0.0))
        {
            timestampSeconds_ = targetTimestamp;
            return timestampSeconds_;
        }

        const double maxCorrection = deltaSeconds * std::clamp(
            maxSpeedAdjustment,
            0.0,
            1.0);
        timestampSeconds_ += deltaSeconds + std::clamp(
            clockError,
            -maxCorrection,
            maxCorrection);
        timestampSeconds_ = std::min(
            timestampSeconds_,
            newestTimestamp);
        return timestampSeconds_;
    }

private:
    double timestampSeconds_{};
    bool initialized_{};
};

MotionFrame MakeFrame(
    const std::uint64_t sequence,
    const double timestamp,
    const double rootX,
    const double angleDegrees)
{
    const Quaternion rotation =
        FromAxisAngleZ(angleDegrees * Pi / 180.0);
    return {
        sequence,
        timestamp,
        {rootX, 0.0, 0.0},
        rotation,
        {rotation, Quaternion{}},
    };
}

void Expect(const bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void ExpectNear(
    const double actual,
    const double expected,
    const double tolerance,
    const char* message)
{
    if (std::abs(actual - expected) > tolerance)
    {
        throw std::runtime_error(message);
    }
}
} // namespace

int main()
{
    MotionFrameBuffer buffer(3);
    Expect(
        buffer.Push(MakeFrame(100, 1.000, 0.0, 0.0)) ==
            PushResult::Inserted,
        "first frame should be appended");
    Expect(
        buffer.Push(MakeFrame(102, 1.066, 6.6, 90.0)) ==
            PushResult::Inserted,
        "newest frame should be appended");
    Expect(
        buffer.Push(MakeFrame(101, 1.033, 3.3, 45.0)) ==
            PushResult::InsertedOutOfOrder,
        "late frame should be inserted in timestamp order");
    Expect(
        buffer.Push(MakeFrame(101, 1.033, 3.3, 45.0)) ==
            PushResult::DuplicateSequence,
        "duplicate sequence should be rejected");

    const std::optional<SampleResult> middle = buffer.Sample(1.0495);
    Expect(middle.has_value(), "middle sample should exist");
    Expect(
        middle->lowerSequence == 101 && middle->upperSequence == 102,
        "sample should use the surrounding frames");
    ExpectNear(middle->alpha, 0.5, 1e-9, "sample alpha should be one half");
    ExpectNear(
        middle->frame.rootPosition.x,
        4.95,
        1e-9,
        "root position should use linear interpolation");
    ExpectNear(
        RotationAngleRadians(middle->frame.rootRotation) * 180.0 / Pi,
        67.5,
        1e-6,
        "root rotation should use spherical interpolation");
    ExpectNear(
        RotationAngleRadians(middle->frame.localBoneRotations.front()) *
            180.0 / Pi,
        67.5,
        1e-6,
        "bone rotation should use spherical interpolation");

    Expect(
        buffer.Push(MakeFrame(103, 1.099, 9.9, 135.0)) ==
            PushResult::Inserted,
        "fourth frame should be accepted");
    Expect(buffer.Size() == 3, "capacity should stay bounded");
    Expect(
        buffer.OldestSequence() == std::optional<std::uint64_t>(101),
        "capacity overflow should discard the oldest timestamp");

    const std::optional<SampleResult> early = buffer.Sample(0.0);
    Expect(
        early.has_value() && early->clampedToBoundary &&
            early->frame.sequenceNumber == 101,
        "playback before the buffer should hold the oldest frame");

    MotionFrame invalidBoneCount = MakeFrame(104, 1.132, 13.2, 180.0);
    invalidBoneCount.localBoneRotations.pop_back();
    Expect(
        buffer.Push(std::move(invalidBoneCount)) ==
            PushResult::BoneCountMismatch,
        "a stream cannot silently change its bone count");

    // 两个生产线程同时写入同一个缓冲区，验证锁保护下容量和数据结构仍然有效。
    MotionFrameBuffer concurrentBuffer(128);
    std::jthread evenProducer(
        [&concurrentBuffer]
        {
            for (std::uint64_t sequence = 0; sequence < 1000; sequence += 2)
            {
                concurrentBuffer.Push(MakeFrame(
                    sequence,
                    static_cast<double>(sequence) / 60.0,
                    static_cast<double>(sequence),
                    0.0));
            }
        });
    std::jthread oddProducer(
        [&concurrentBuffer]
        {
            for (std::uint64_t sequence = 1; sequence < 1000; sequence += 2)
            {
                concurrentBuffer.Push(MakeFrame(
                    sequence,
                    static_cast<double>(sequence) / 60.0,
                    static_cast<double>(sequence),
                    0.0));
            }
        });
    evenProducer.join();
    oddProducer.join();
    Expect(
        concurrentBuffer.Size() == 128,
        "concurrent producers should preserve bounded capacity");

    PlaybackClock playbackClock;
    ExpectNear(
        playbackClock.Advance(1.000, 1.132, 0.016, 0.100, 0.1, 0.25),
        1.032,
        1e-9,
        "playback clock should start behind the newest frame");
    ExpectNear(
        playbackClock.Advance(1.000, 1.132, 0.016, 0.100, 0.1, 0.25),
        1.048,
        1e-9,
        "playback clock should advance while no new packet arrives");
    ExpectNear(
        playbackClock.Advance(1.000, 1.165, 0.016, 0.100, 0.1, 0.25),
        1.0656,
        1e-9,
        "playback clock should correct small drift gradually");
    ExpectNear(
        playbackClock.Advance(1.000, 2.000, 0.016, 0.100, 0.1, 0.25),
        1.900,
        1e-9,
        "playback clock should hard-resync after a large gap");
    ExpectNear(
        playbackClock.Advance(1.000, 2.000, 1.000, 0.100, 0.1, 0.25),
        2.000,
        1e-9,
        "playback clock must not run beyond received data");

    buffer.Reset();
    Expect(buffer.Size() == 0, "reset should clear all frames");

    std::cout << "REALTIME_FRAME_BUFFER_LAB=PASS\n";
    std::cout << "sample_sequences=" << middle->lowerSequence << ','
              << middle->upperSequence << '\n';
    std::cout << "sample_alpha=" << middle->alpha << '\n';
    std::cout << "sample_root_x=" << middle->frame.rootPosition.x << '\n';
    std::cout << "sample_rotation_degrees="
              << RotationAngleRadians(middle->frame.rootRotation) * 180.0 / Pi
              << '\n';
}
