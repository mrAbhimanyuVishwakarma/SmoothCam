#include "pacing/Pacer.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace smoothcam;

namespace
{
std::vector<SourceFrame> MakeFrames(int count, Hns start, Hns interval)
{
    std::vector<SourceFrame> f;
    for (int i = 0; i < count; ++i)
        f.push_back({static_cast<uint64_t>(i + 1), start + i * interval});
    return f;
}

constexpr Hns k30 = kHnsPerSecond / 30;
constexpr Hns k24 = kHnsPerSecond / 24;
constexpr Hns k60 = kHnsPerSecond / 60;
} // namespace

TEST(Pacer, EmptyGivesNone)
{
    EXPECT_EQ(DecideFrame({}, 1000, {}).kind, PaceDecision::Kind::None);
}

TEST(Pacer, ThirtyToSixtyAlternatesRealAndHalfway)
{
    auto frames = MakeFrames(4, 0, k30);
    PaceOptions opt;
    // Output ticks at 60 Hz starting on a real frame.
    auto d0 = DecideFrame(frames, k30, opt);
    EXPECT_EQ(d0.kind, PaceDecision::Kind::Hold);
    EXPECT_EQ(d0.nextId, 2u);

    auto d1 = DecideFrame(frames, k30 + k60, opt);
    EXPECT_EQ(d1.kind, PaceDecision::Kind::Interpolate);
    EXPECT_EQ(d1.prevId, 2u);
    EXPECT_EQ(d1.nextId, 3u);
    EXPECT_NEAR(d1.alpha, 0.5f, 1e-3);
}

TEST(Pacer, TwentyFourToSixtyCyclesPhases)
{
    auto frames = MakeFrames(10, 0, k24);
    PaceOptions opt;
    // 24 -> 60 is a 2.5x ratio: phases repeat 0, .4, .8, .2, .6
    const float expected[] = {0.0f, 0.4f, 0.8f, 0.2f, 0.6f};
    for (int i = 0; i < 5; ++i)
    {
        const Hns t = 2 * k24 + i * k60;
        auto d = DecideFrame(frames, t, opt);
        if (expected[i] == 0.0f)
        {
            EXPECT_EQ(d.kind, PaceDecision::Kind::Hold) << i;
        }
        else
        {
            EXPECT_EQ(d.kind, PaceDecision::Kind::Interpolate) << i;
            EXPECT_NEAR(d.alpha, expected[i], 2e-3) << i;
        }
    }
}

TEST(Pacer, HoldsLatestWhenAhead)
{
    auto frames = MakeFrames(3, 0, k30);
    auto d = DecideFrame(frames, 10 * k30, {});
    EXPECT_EQ(d.kind, PaceDecision::Kind::Hold);
    EXPECT_EQ(d.nextId, 3u);
}

TEST(Pacer, HoldsOldestWhenBehind)
{
    auto frames = MakeFrames(3, 5 * k30, k30);
    auto d = DecideFrame(frames, 0, {});
    EXPECT_EQ(d.kind, PaceDecision::Kind::Hold);
    EXPECT_EQ(d.nextId, 1u);
}

TEST(Pacer, ExtrapolatesOnlyWhenAllowed)
{
    auto frames = MakeFrames(3, 0, k30);
    PaceOptions opt;
    opt.allowExtrapolation = true;
    auto d = DecideFrame(frames, 2 * k30 + k30 / 2, opt);
    EXPECT_EQ(d.kind, PaceDecision::Kind::Extrapolate);
    EXPECT_EQ(d.prevId, 2u);
    EXPECT_EQ(d.nextId, 3u);
    EXPECT_NEAR(d.alpha, 0.5f, 1e-3);

    // Never further than one interval.
    auto beyond = DecideFrame(frames, 2 * k30 + 5 * k30, opt);
    EXPECT_FLOAT_EQ(beyond.alpha, 1.0f);
}

TEST(Pacer, DoesNotInterpolateAcrossHiccups)
{
    std::vector<SourceFrame> frames = {{1, 0}, {2, 500 * kHnsPerMs}};
    auto d = DecideFrame(frames, 250 * kHnsPerMs, {});
    EXPECT_EQ(d.kind, PaceDecision::Kind::Hold);
    EXPECT_EQ(d.nextId, 1u);
}

TEST(IntervalEstimator, ConvergesAndTracksLowLight)
{
    IntervalEstimator est;
    Hns t = 0;
    for (int i = 0; i < 60; ++i)
        est.Add(t += k30);
    EXPECT_NEAR(est.Fps(), 30.0, 0.1);

    // Camera drops to 15 fps in the dark: estimate follows within ~a second.
    for (int i = 0; i < 15; ++i)
        est.Add(t += 2 * k30);
    EXPECT_NEAR(est.Fps(), 15.0, 1.0);
}

TEST(IntervalEstimator, IgnoresSingleHiccup)
{
    IntervalEstimator est;
    Hns t = 0;
    for (int i = 0; i < 60; ++i)
        est.Add(t += k30);
    est.Add(t += 10 * k30);
    EXPECT_NEAR(est.Fps(), 30.0, 2.0);
}

TEST(TimestampMapper, RemovesArrivalJitter)
{
    TimestampMapper m;
    // Device clock is perfectly regular; arrival has 0..8 ms of jitter.
    const Hns base = 123'456'789;
    const int jitterMs[] = {3, 0, 8, 2, 5, 1, 7, 0, 4, 6};
    Hns prevMapped = 0;
    for (int i = 0; i < 200; ++i)
    {
        const Hns device = i * k30;
        const Hns arrival = base + device + jitterMs[i % 10] * kHnsPerMs;
        const Hns mapped = m.Map(device, arrival);
        if (i > 20)
        {
            // Mapped spacing stays within 0.5 ms of the true interval.
            EXPECT_NEAR(static_cast<double>(mapped - prevMapped), static_cast<double>(k30), 5000.0) << i;
            EXPECT_LE(mapped, arrival);
        }
        prevMapped = mapped;
    }
    EXPECT_GT(m.ArrivalJitter(), 0);
}

TEST(TimestampMapper, ResetsWhenDeviceClockRestarts)
{
    TimestampMapper m;
    m.Map(10 * k30, 1'000'000'000);
    const Hns mapped = m.Map(0, 2'000'000'000);
    EXPECT_EQ(mapped, 2'000'000'000);
}
