#include "Pacer.h"

#include <algorithm>

namespace smoothcam
{
Hns TimestampMapper::Map(Hns deviceTime, Hns arrivalTime)
{
    // A device clock that goes backwards means the stream restarted.
    if (m_valid && deviceTime < m_lastDevice)
        m_valid = false;
    m_lastDevice = deviceTime;

    const Hns offset = arrivalTime - deviceTime;
    if (!m_valid)
    {
        m_offset = offset;
        m_jitter = 0;
        m_valid = true;
    }
    else if (offset < m_offset)
    {
        m_offset = offset;
    }
    else
    {
        // Drift slowly upward so a single early outlier (or clock drift between
        // the camera and QPC) can't pin the offset forever.
        m_offset += (offset - m_offset) / 500;
    }

    const Hns mapped = deviceTime + m_offset;
    const double late = static_cast<double>(std::max<Hns>(0, arrivalTime - mapped));
    m_jitter += (late - m_jitter) * 0.05;
    return mapped;
}

void IntervalEstimator::Add(Hns frameTime)
{
    if (m_count > 0)
    {
        const Hns delta = frameTime - m_last;
        if (delta > 0)
        {
            if (m_count == 1)
            {
                m_interval = static_cast<double>(delta);
            }
            else if (delta < m_interval * 4)
            {
                // Faster reaction when the rate drops (low light) than when it
                // recovers, so we don't under-delay for long.
                const double k = delta > m_interval ? 0.2 : 0.05;
                m_interval += (delta - m_interval) * k;
                m_bigGaps = 0;
            }
            else if (++m_bigGaps >= 3)
            {
                // Several huge gaps in a row: the camera really did slow down.
                m_interval = static_cast<double>(delta);
                m_bigGaps = 0;
            }
            // else: a single hiccup (USB glitch, scheduler stall) - ignore it.
        }
    }
    m_last = frameTime;
    ++m_count;
}

void IntervalEstimator::Reset()
{
    m_last = 0;
    m_interval = 0;
    m_count = 0;
    m_bigGaps = 0;
}

PaceDecision DecideFrame(std::span<const SourceFrame> frames, Hns virtualTime, const PaceOptions& options)
{
    PaceDecision d;
    if (frames.empty())
        return d;

    auto hold = [&](const SourceFrame& f) {
        d.kind = PaceDecision::Kind::Hold;
        d.prevId = d.nextId = f.id;
        d.alpha = 0;
        return d;
    };

    if (virtualTime <= frames.front().time)
        return hold(frames.front());

    const auto& last = frames.back();
    if (virtualTime >= last.time)
    {
        if (!options.allowExtrapolation || frames.size() < 2)
            return hold(last);
        const auto& before = frames[frames.size() - 2];
        const Hns span = last.time - before.time;
        if (span <= 0 || span > options.maxGap)
            return hold(last);
        const float beta =
            std::min(static_cast<float>(virtualTime - last.time) / static_cast<float>(span), options.maxExtrapolation);
        if (beta < options.snap)
            return hold(last);
        d.kind = PaceDecision::Kind::Extrapolate;
        d.prevId = before.id;
        d.nextId = last.id;
        d.alpha = beta;
        return d;
    }

    // First frame strictly after virtualTime.
    auto it = std::upper_bound(frames.begin(), frames.end(), virtualTime,
                               [](Hns t, const SourceFrame& f) { return t < f.time; });
    const auto& next = *it;
    const auto& prev = *(it - 1);
    const Hns span = next.time - prev.time;
    if (span <= 0 || span > options.maxGap)
        return hold(prev);

    const float alpha = static_cast<float>(virtualTime - prev.time) / static_cast<float>(span);
    if (alpha < options.snap)
        return hold(prev);
    if (alpha > 1.0f - options.snap)
        return hold(next);

    d.kind = PaceDecision::Kind::Interpolate;
    d.prevId = prev.id;
    d.nextId = next.id;
    d.alpha = alpha;
    return d;
}
} // namespace smoothcam
