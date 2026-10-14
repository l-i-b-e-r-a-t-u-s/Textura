#pragma once
// The interface's own sounds: a click for each key, knob step and switch, and the sting that
// plays when the hidden code is typed. Short filtered-noise and sine one-shots, mono.

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>

namespace lib
{
enum TickKind { tickKey = 0, tickKnob, tickSwitch, tickType, tickOpen, tickClose, tickSend, numTickKinds };

// Shortest gap allowed between two ticks of the same kind, in seconds.
constexpr float tickGap[numTickKinds] = { 0.02f, 0.022f, 0.05f, 0.025f, 0.08f, 0.08f, 0.3f };

// Second-order filter with the three shapes the ticks need. Low and high pass take their
// resonance in decibels, band pass takes a plain Q.
struct TickFilter
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void band (float fc, float q, float sr)
    {
        const float w = angle (fc, sr), cs = std::cos (w), al = std::sin (w) / (2.0f * q), a0 = 1.0f + al;
        b0 = al / a0;  b1 = 0;  b2 = -al / a0;  a1 = -2.0f * cs / a0;  a2 = (1.0f - al) / a0;
    }
    void low (float fc, float db, float sr)
    {
        const float w = angle (fc, sr), cs = std::cos (w), al = std::sin (w) / (2.0f * std::pow (10.0f, db / 20.0f)), a0 = 1.0f + al;
        b0 = (1.0f - cs) * 0.5f / a0;  b1 = (1.0f - cs) / a0;  b2 = b0;  a1 = -2.0f * cs / a0;  a2 = (1.0f - al) / a0;
    }
    void high (float fc, float db, float sr)
    {
        const float w = angle (fc, sr), cs = std::cos (w), al = std::sin (w) / (2.0f * std::pow (10.0f, db / 20.0f)), a0 = 1.0f + al;
        b0 = (1.0f + cs) * 0.5f / a0;  b1 = -(1.0f + cs) / a0;  b2 = b0;  a1 = -2.0f * cs / a0;  a2 = (1.0f - al) / a0;
    }
    float tick (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void clear() { z1 = z2 = 0; }

private:
    static float angle (float fc, float sr) { return 2.0f * 3.14159265f * juce::jlimit (20.0f, 0.45f * sr, fc) / sr; }
};

class Ticks
{
public:
    void prepare (float sampleRate) { sr = sampleRate; for (auto& s : shots) s.shape = Shot::off; }

    // value is 0..1 where the kind uses one (knob position, typed character), below 0 for none.
    void trigger (int kind, float value, juce::Random& rnd)
    {
        const bool has = value >= 0.0f;
        const float v = juce::jlimit (0.0f, 1.0f, value);
        switch (kind)
        {
            case tickKey:
                noise (0, 2900.0f + 500.0f * rnd.nextFloat(), 0.9f, 0.5f, 0.0024f);
                sine (0, 210.0f, 120.0f, 0.02f, 0.16f, 0.001f, 0.01f);
                break;
            case tickKnob:
                noise (0, 3600.0f * std::pow (2.0f, ((has ? v : 0.5f) - 0.5f) * 0.7f), 3.0f, 0.32f, 0.0013f);
                break;
            case tickSwitch:
                noise (0, 2100.0f, 1.1f, 0.45f, 0.0026f);
                sine (0, 160.0f, 95.0f, 0.025f, 0.14f, 0.001f, 0.012f);
                noise (0.03f, 3300.0f, 1.4f, 0.3f, 0.0018f);
                break;
            case tickType:
                noise (0, 2200.0f + 1000.0f * (has ? v : rnd.nextFloat()) + 120.0f * rnd.nextFloat(), 1.0f, 0.14f, 0.0018f);
                break;
            case tickOpen:
                sine (0, 784.872f, 1046.496f, 0.09f, 0.05f, 0.008f, 0.06f);
                sweep (0, 500.0f, 3200.0f, 0.2f, 0.05f);
                break;
            case tickClose:
                sine (0, 1046.496f, 784.872f, 0.09f, 0.045f, 0.006f, 0.05f);
                sweep (0, 3200.0f, 450.0f, 0.18f, 0.045f);
                break;
            case tickSend:
                chime (0, 523.248f, 0.07f);
                chime (0.085f, 654.06f, 0.065f);
                chime (0.17f, 784.872f, 0.06f);
                break;
            default: break;
        }
    }

    // Adds into out.
    void render (float* out, int n, juce::Random& rnd)
    {
        for (auto& s : shots)
        {
            if (s.shape == Shot::off) continue;
            for (int i = 0; i < n; ++i)
            {
                if (s.wait > 0) { --s.wait; continue; }
                const float t = (float) s.at / sr;
                if (t >= s.length) { s.shape = Shot::off; break; }
                float x = 0, g = 0;
                if (s.shape == Shot::noise)
                {
                    x = s.filter.tick (rnd.nextFloat() * 2.0f - 1.0f);
                    g = t < 0.0007f ? s.peak * t / 0.0007f : s.peak * std::exp (-(t - 0.0007f) / s.fall);
                }
                else if (s.shape == Shot::sine)
                {
                    const float f = s.f0 == s.f1 ? s.f0 : s.f0 * std::pow (s.f1 / s.f0, std::min (1.0f, t / s.glide));
                    s.phase += f / sr;
                    s.phase -= std::floor (s.phase);
                    x = std::sin (2.0f * 3.14159265f * s.phase);
                    g = t < s.rise ? s.peak * t / s.rise : s.peak * std::exp (-(t - s.rise) / s.fall);
                }
                else
                {
                    if ((s.at & 15) == 0) s.filter.low (s.f0 * std::pow (s.f1 / s.f0, std::min (1.0f, t / s.glide)), 0.8f, sr);
                    x = s.filter.tick (rnd.nextFloat() * 2.0f - 1.0f);
                    const float q = t / s.glide;
                    g = q < 0.4f ? s.peak * q / 0.4f : s.peak * std::max (0.0f, 1.0f - (q - 0.4f) / 0.6f);
                }
                out[i] += x * g;
                ++s.at;
            }
        }
    }

private:
    struct Shot
    {
        enum Shape { off, noise, sine, sweep } shape = off;
        int wait = 0, at = 0;
        float f0 = 0, f1 = 0, glide = 1, peak = 0, rise = 0, fall = 0.01f, length = 0, phase = 0;
        TickFilter filter;
    };

    Shot* take (float delay)
    {
        for (auto& s : shots)
            if (s.shape == Shot::off)
            {
                s = Shot();
                s.wait = (int) ((delay + 0.002f) * sr);
                return &s;
            }
        return nullptr;
    }

    void noise (float delay, float fc, float q, float peak, float fall)
    {
        if (auto* s = take (delay)) { s->shape = Shot::noise; s->filter.band (fc, q, sr); s->peak = peak; s->fall = fall; s->length = 0.0007f + 9.0f * fall; }
    }
    void sine (float delay, float from, float to, float glide, float peak, float rise, float fall)
    {
        if (auto* s = take (delay)) { s->shape = Shot::sine; s->f0 = from; s->f1 = to; s->glide = std::max (1e-4f, glide); s->peak = peak; s->rise = rise; s->fall = fall; s->length = rise + 8.0f * fall; }
    }
    void sweep (float delay, float from, float to, float seconds, float peak)
    {
        if (auto* s = take (delay)) { s->shape = Shot::sweep; s->f0 = from; s->f1 = to; s->glide = seconds; s->peak = peak; s->length = seconds; }
    }
    void chime (float delay, float f, float peak)
    {
        sine (delay, f, f, 0, peak, 0.004f, 0.32f);
        sine (delay, 2.0f * f, 2.0f * f, 0, 0.22f * peak, 0.003f, 0.12f);
    }

    float sr = 48000;
    std::array<Shot, 24> shots;
};

// The sting for the typed code: a burst of square blips, a crushed noise riser, a sub drop,
// a detuned saw chord opening and closing through a resonant filter, and scattered static.
// It runs on its own bus with its own squeeze, so it sounds even when the instrument is off.
class Sting
{
public:
    void prepare (float sampleRate) { sr = sampleRate; at = -1; }
    bool running() const { return at >= 0; }

    void start (juce::Random& rnd)
    {
        at = 0;  gr = 0;
        for (int i = 0; i < numBlips; ++i)
        {
            blipAt[(size_t) i] = 0.032f * (float) i + 0.006f * rnd.nextFloat();
            blipHz[(size_t) i] = 700.0f + 220.0f * (float) juce::roundToInt (12.0f * rnd.nextFloat());
            blipPhase[(size_t) i] = 0;
        }
        for (int i = 0; i < numBursts; ++i)
        {
            burstAt[(size_t) i] = 1.3f + 2.3f * rnd.nextFloat();
            burstFilter[(size_t) i].clear();
            burstFilter[(size_t) i].high (1500.0f + 4500.0f * rnd.nextFloat(), 1.0f, sr);
        }
        riser.clear();  pad.clear();
        subPhase = 0;
        for (auto& p : sawPhase) p = rnd.nextFloat();
    }

    // Adds into both channels.
    void render (float* outL, float* outR, int n, juce::Random& rnd)
    {
        if (at < 0) return;
        static constexpr float chord[5] = { 146.83f, 174.61f, 220.0f, 261.63f, 329.63f };
        const float twoPi = 2.0f * 3.14159265f, drop = 1.15f, close = 3.85f;
        const float att = 1.0f - std::exp (-1.0f / (0.003f * sr)), rel = 1.0f - std::exp (-1.0f / (0.25f * sr));
        const float up = std::pow (2.0f, 7.0f / 1200.0f), down = 1.0f / up;

        for (int i = 0; i < n; ++i, ++at)
        {
            const float t = (float) at / sr;
            if (t > 5.0f) { at = -1; return; }
            float x = 0;

            for (int b = 0; b < numBlips; ++b)
            {
                const float q = t - blipAt[(size_t) b];
                if (q < 0.0f || q >= 0.04f) continue;
                auto& ph = blipPhase[(size_t) b];
                ph += blipHz[(size_t) b] / sr;
                ph -= std::floor (ph);
                x += (ph < 0.5f ? 1.0f : -1.0f) * shape (q, 0.05f, 0.002f, 0.014f, 0.012f);
            }

            if (t < 1.1f)
            {
                if ((at & 15) == 0) riser.band (300.0f * std::pow (30.0f, std::min (1.0f, t / 0.95f)), 5.0f, sr);
                x += crush (riser.tick (rnd.nextFloat() * 2.0f - 1.0f)) * shape (t, 0.22f, 0.05f, 0.6f, 0.35f);
            }

            const float q = t - drop;
            if (q >= 0.0f && q < 1.5f)
            {
                subPhase += 120.0f * std::pow (0.3f, std::min (1.0f, q / 0.9f)) / sr;
                subPhase -= std::floor (subPhase);
                x += std::tanh (3.0f * std::sin (twoPi * subPhase)) * shape (q, 0.55f, 0.01f, 0.25f, 1.1f);
            }

            if (q >= 0.0f && t < close + 0.6f)
            {
                const float c = t - close;
                if ((at & 15) == 0)
                    pad.low (c < 0.0f ? 240.0f * std::pow (3800.0f / 240.0f, std::min (1.0f, q / 1.2f))
                                      : 3800.0f * std::pow (160.0f / 3800.0f, std::min (1.0f, c / 0.5f)), 7.0f, sr);
                const float fall = c < 0.0f ? 1.0f : std::pow (0.2f, std::min (1.0f, c / 0.55f));
                float saws = 0;
                for (int s = 0; s < 10; ++s)
                {
                    auto& ph = sawPhase[(size_t) s];
                    ph += chord[s / 2] * (s & 1 ? up : down) * fall / sr;
                    ph -= std::floor (ph);
                    saws += 2.0f * ph - 1.0f;
                }
                const float g = c < 0.0f ? std::min (1.0f, q / 0.06f) : std::pow (1e-4f, std::min (1.0f, c / 0.55f));
                x += pad.tick (saws * 0.045f) * g;
            }

            float bursts = 0;
            for (int b = 0; b < numBursts; ++b)
            {
                const float bq = t - burstAt[(size_t) b];
                if (bq < 0.0f || bq >= 0.07f) continue;
                bursts += burstFilter[(size_t) b].tick (rnd.nextFloat() * 2.0f - 1.0f) * shape (bq, 0.16f, 0.002f, 0.03f, 0.02f);
            }
            x += crush (bursts);

            // 10:1 above -14 dB with a narrow knee
            x *= 0.3f;
            const float db = std::abs (x) > 1e-6f ? 20.0f * std::log10 (std::abs (x)) : -120.0f, over = db + 14.0f;
            float want = 0;
            if (over > 2.0f) want = -0.9f * over;
            else if (over > -2.0f) want = -0.9f * (over + 2.0f) * (over + 2.0f) / 8.0f;
            gr += (want - gr) * (want < gr ? att : rel);
            const float y = juce::jlimit (-0.98f, 0.98f, x * std::pow (10.0f, (gr + 6.5f) / 20.0f));
            outL[i] += y;
            outR[i] += y;
        }
    }

private:
    // Rise in a straight line, hold, then fall away exponentially to nothing.
    static float shape (float t, float peak, float rise, float hold, float fall)
    {
        if (t < rise) return peak * t / rise;
        if (t < rise + hold) return peak;
        const float q = (t - rise - hold) / fall;
        return q >= 1.0f ? 0.0f : peak * std::pow (1e-4f / peak, q);
    }
    static float crush (float x) { return std::round (juce::jlimit (-1.0f, 1.0f, x) * 7.0f) / 7.0f; }

    static constexpr int numBlips = 34, numBursts = 12;
    float sr = 48000, gr = 0, subPhase = 0;
    int at = -1;
    std::array<float, numBlips> blipAt {}, blipHz {}, blipPhase {};
    std::array<float, numBursts> burstAt {};
    std::array<TickFilter, numBursts> burstFilter;
    std::array<float, 10> sawPhase {};
    TickFilter riser, pad;
};
} // namespace lib
