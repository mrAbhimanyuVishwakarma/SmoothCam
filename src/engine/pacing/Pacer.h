#pragma once

#include "Clock.h"

#include <cstdint>
#include <span>

namespace smoothcam
{
// Maps camera timestamps onto the system clock.
//
// The source reader gives timestamps relative to stream start, measured close
// to the sensor. Arrival time on our side includes decode and scheduling
// jitter. We track offset = arrival - deviceTime and keep (roughly) its
// minimum, which is the least-delayed frame we've seen: mapped times then keep
// the camera's own even spacing instead of our arrival jitter.
class TimestampMapper
{
public:
    Hns Map(Hns deviceTime, Hns arrivalTime);
    void Reset() { m_valid = false; }

    // How far behind its mapped time the last frame arrived (EMA, >= 0).
    Hns ArrivalJitter() const { return static_cast<Hns>(m_jitter); }

private:
    bool m_valid = false;
    Hns m_offset = 0;
    Hns m_lastDevice = 0;
    double m_jitter = 0;
};

// Smoothed estimate of the camera's real frame interval.
class IntervalEstimator
{
public:
    void Add(Hns frameTime);
    void Reset();

    Hns Interval() const { return static_cast<Hns>(m_interval); }
    double Fps() const { return m_interval > 0 ? kHnsPerSecond / m_interval : 0.0; }
    bool HasEstimate() const { return m_count >= 2; }

private:
    Hns m_last = 0;
    double m_interval = 0;
    int m_count = 0;
    int m_bigGaps = 0;
};

struct SourceFrame
{
    uint64_t id = 0;
    Hns time = 0;
};

struct PaceDecision
{
    enum class Kind
    {
        None,        // no frames yet
        Hold,        // show `next` as-is
        Interpolate, // between prev and next at alpha
        Extrapolate, // continue motion past `next` by alpha frame intervals
    };
    Kind kind = Kind::None;
    uint64_t prevId = 0;
    uint64_t nextId = 0;
    float alpha = 0;
};

struct PaceOptions
{
    bool allowExtrapolation = false;
    // Phases this close to a real frame just show the real frame: crisper and free.
    float snap = 0.02f;
    // Never extrapolate further than this fraction of a frame interval.
    float maxExtrapolation = 1.0f;
    // Don't interpolate across gaps longer than this (camera hiccup) - hold instead.
    Hns maxGap = 250 * kHnsPerMs;
};

// `frames` must be sorted by time, oldest first.
PaceDecision DecideFrame(std::span<const SourceFrame> frames, Hns virtualTime, const PaceOptions& options);
} // namespace smoothcam
