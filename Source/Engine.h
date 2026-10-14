#pragma once
// Liberatus sound engine: three one-shot voices (grain, keys, bell) into a small room.
// Plain C++ apart from the convolver. Everything here runs on the audio thread except
// makeImpulse(), which is called from the message thread.

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

#include "Ticks.h"

namespace lib
{
constexpr float kPi = 3.14159265358979f;
constexpr int kMaxVoices = 40;

inline float clamp01 (float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

// Small deterministic generator (the room impulse must be the same on every machine).
struct Mulberry
{
    uint32_t t;
    explicit Mulberry (uint32_t seed) : t (seed) {}
    float next()
    {
        t += 0x6d2b79f5u;
        uint32_t e = t;
        e = (e ^ (e >> 15)) * (1u | e);
        e ^= e + (e ^ (e >> 7)) * (61u | e);
        return (float) ((double) (e ^ (e >> 14)) / 4294967296.0);
    }
};

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void lowpass (float fc, float q, float sr)
    {
        fc = juce::jlimit (20.0f, 0.45f * sr, fc);
        const float w = 2.0f * kPi * fc / sr, cs = std::cos (w), al = std::sin (w) / (2.0f * q);
        const float a0 = 1.0f + al;
        b0 = (1.0f - cs) * 0.5f / a0;  b1 = (1.0f - cs) / a0;  b2 = b0;
        a1 = -2.0f * cs / a0;          a2 = (1.0f - al) / a0;
    }

    float tick (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

struct Knobs { float wave = 0.55f, prism = 0.5f, depth = 0.45f, drift = 0.3f; };

// One note request from the UI or from MIDI.
struct Event
{
    int mode = 1;        // 1 grain, 2 keys, 3 bell
    float delay = 0;     // seconds from now
    float semis = 0;     // semitones above the mode's root
    float vel = 0.5f;
};

inline float rootHz (int mode) { return mode == 2 ? 174.61f : (mode == 3 ? 220.0f : 146.83f); }

// Attack to `peak` over `ta`, then an exponential fall with time constant `tau`.
inline float env (float t, float ta, float peak, float tau)
{
    if (t <= 0.0f) return 0.0f;
    return t < ta ? peak * t / ta : peak * std::exp (-(t - ta) / tau);
}

struct GrainTable
{
    static constexpr int N = 2048;
    std::array<float, N + 1> v {};
    GrainTable()
    {
        const float amps[6] = { 1.0f, 0.42f, 0.18f, 0.09f, 0.04f, 0.02f };
        float peak = 0;
        for (int i = 0; i <= N; ++i)
        {
            float s = 0;
            for (int h = 0; h < 6; ++h) s += amps[h] * std::sin (2.0f * kPi * (float) ((h + 1) * i) / (float) N);
            v[(size_t) i] = s;
            peak = std::max (peak, std::abs (s));
        }
        for (auto& x : v) x /= peak;
    }
    float at (double phase) const
    {
        const double p = phase * N;
        const int i = (int) p;
        const float f = (float) (p - i);
        return v[(size_t) i] + (v[(size_t) i + 1] - v[(size_t) i]) * f;
    }
};

struct Voice
{
    bool on = false;
    int mode = 1, ctl = 0;
    float sr = 48000, t = 0, f = 0, dur = 1, tau = 0.2f, vel = 0.5f;
    float panL = 0.7f, panR = 0.7f;
    float c0 = 0, c1 = 0, tauF = 0.1f;          // grain filter sweep
    float modStart = 0, modEnd = 0;             // keys modulation depth, Hz
    double ph[3] = { 0, 0, 0 };
    float inc[3] = { 0, 0, 0 };
    Biquad lp;

    void setPan (float p)
    {
        const float a = (juce::jlimit (-1.0f, 1.0f, p) + 1.0f) * 0.25f * kPi;
        panL = std::cos (a);  panR = std::sin (a);
    }

    void start (const Event& e, const Knobs& k, float sampleRate, juce::Random& rnd)
    {
        on = true;  mode = e.mode;  sr = sampleRate;  t = -e.delay;  vel = e.vel;  ctl = 0;
        ph[0] = ph[1] = ph[2] = 0;  lp.z1 = lp.z2 = 0;
        const float r = rnd.nextFloat() * 2.0f - 1.0f;
        f = rootHz (mode) * std::pow (2.0f, e.semis / 12.0f);
        auto cents = [] (float c) { return std::pow (2.0f, c / 1200.0f); };

        if (mode == 2)
        {
            dur = 1.3f + 3.0f * k.wave;  tau = dur / 6.9f;
            inc[0] = f / sr;  inc[1] = f / sr;  inc[2] = std::min (7.0f * f, 9000.0f) / sr;
            modStart = f * (1.2f + 1.4f * vel) * (0.6f + 0.8f * k.prism);
            modEnd = 0.18f * f;
            lp.lowpass (std::min (12000.0f, f * (6.0f + 8.0f * k.prism)), 1.12f, sr);
            setPan (r * (0.2f + 0.5f * k.prism));
        }
        else if (mode == 3)
        {
            dur = 0.9f + 2.2f * k.wave;  tau = dur / 6.9f;
            const float ratio[3] = { 1.0f, 2.756f, 5.404f };
            for (int i = 0; i < 3; ++i)
                inc[i] = f * ratio[i] * cents ((rnd.nextFloat() - 0.5f) * 6.0f * k.drift) / sr;
            lp.lowpass (std::min (14000.0f, f * (4.0f + 8.0f * k.prism)), 1.12f, sr);
            setPan (r * (0.25f + 0.55f * k.prism));
        }
        else
        {
            dur = (0.7f + 3.1f * k.wave) * (f > 600.0f ? 0.75f : 1.0f);  tau = dur / 6.9f;
            inc[0] = f * cents ((rnd.nextFloat() - 0.5f) * 14.0f * k.drift) / sr;
            inc[1] = 2.0f * f * cents (4.0f + 6.0f * k.drift) / sr;
            c0 = std::min (14000.0f, f * (4.0f + 10.0f * k.prism * (0.4f + 0.6f * vel)));
            c1 = std::min (14000.0f, 1.6f * f);
            tauF = 0.09f + 0.1f * k.wave;
            lp.lowpass (c0, 1.06f, sr);
            setPan (r * (0.2f + 0.65f * k.prism));
        }
    }

    // Adds this voice to the room (stereo) and to the wet-only bus (mono).
    void render (float* L, float* R, float* wet, int n, const GrainTable& table)
    {
        const float dt = 1.0f / sr;
        for (int i = 0; i < n; ++i, t += dt)
        {
            if (t < 0.0f) continue;
            if (t > dur + 0.1f) { on = false; return; }
            float x = 0, w = 0;

            if (mode == 2)
            {
                const float depth = t < 0.002f ? modStart : modEnd + (modStart - modEnd) * std::exp (-(t - 0.002f) / 0.16f);
                const float m = std::sin (2.0f * kPi * (float) ph[1]);
                const float tine = std::sin (2.0f * kPi * (float) ph[2]) * env (t, 0.002f, 0.03f * vel, 0.025f);
                x = (std::sin (2.0f * kPi * (float) ph[0]) + tine) * env (t, 0.004f, 0.17f * vel, tau);
                ph[0] += inc[0] + depth * m * dt;  ph[1] += inc[1];  ph[2] += inc[2];
                x = lp.tick (x);
            }
            else if (mode == 3)
            {
                const float g[3] = { 1.0f, 0.32f, 0.12f }, y[3] = { 1.0f, 0.45f, 0.22f };
                for (int p = 0; p < 3; ++p)
                {
                    x += std::sin (2.0f * kPi * (float) ph[p]) * env (t, 0.002f, 0.16f * vel * g[p], tau * y[p]);
                    ph[p] += inc[p];
                }
                w = 0.25f * x;
                x = lp.tick (x);
            }
            else
            {
                if ((ctl++ & 31) == 0)
                    lp.lowpass (t < 0.004f ? c0 : c1 + (c0 - c1) * std::exp (-(t - 0.004f) / tauF), 1.06f, sr);
                const float c = 0.2f * vel;
                x = lp.tick (table.at (ph[0])) * env (t, 0.004f, c, tau);
                w = std::sin (2.0f * kPi * (float) ph[1]) * env (t, 0.03f, 0.35f * c, 1.2f * tau);
                ph[0] += inc[0];  ph[1] += inc[1];
            }

            for (auto& p : ph) if (p >= 1.0) p -= std::floor (p);
            if (ph[0] < 0.0) ph[0] -= std::floor (ph[0]);
            L[i] += x * panL;  R[i] += x * panR;  wet[i] += w;
        }
    }
};

// Reverb length in seconds for a WAVE setting.
inline float roomSeconds (float wave) { return 1.2f * std::pow (5.0f / 1.2f, clamp01 (wave)); }

// A decaying, darkening noise tail with six early taps, level-matched so every length
// sits at the same loudness.
inline juce::AudioBuffer<float> makeImpulse (double sr, float seconds, int seed)
{
    const int n = (int) sr, s = std::max (2, (int) std::lround (sr * seconds));
    const int pre = std::min (s - 1, (int) std::lround (0.011 * sr)), fade = std::max (1, (int) std::lround (0.006 * sr));
    const int len = std::max (1, s - pre);
    const float fall = std::exp (-6.9f / (float) len), hp = 1.0f - std::exp (-2.0f * kPi * 140.0f / (float) n);
    const float taps[6][2] = { { 0.0071f, 0.16f }, { 0.0113f, -0.12f }, { 0.0167f, 0.1f },
                               { 0.0241f, -0.08f }, { 0.0337f, 0.06f }, { 0.0459f, -0.045f } };
    juce::AudioBuffer<float> ir (2, s);
    ir.clear();
    double power = 0;
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = ir.getWritePointer (ch);
        Mulberry rng ((uint32_t) (7919 * seed + 104729 * ch + 1));
        float lpS = 0, hpS = 0, amp = 1;
        for (int i = pre; i < s; ++i)
        {
            const int k = i - pre;
            const float coef = 0.66f - (float) k / (float) len * 0.54f;
            lpS += coef * (2.0f * rng.next() - 1.0f - lpS);
            hpS += hp * (lpS - hpS);
            d[i] = (lpS - hpS) * amp * (k < fade ? (float) k / (float) fade : 1.0f);
            amp *= fall;
        }
        for (auto& tp : taps)
        {
            const int at = (int) std::lround (sr * (tp[0] + (ch ? 0.0013f : 0.0f)));
            if (at < s) d[at] += tp[1];
        }
        for (int i = 0; i < s; ++i) power += (double) d[i] * d[i];
    }
    const double rms = std::sqrt (power / (2.0 * s));
    ir.applyGain ((float) (0.00125 * (44100.0 / sr) / std::max (rms, 1e-9)));
    return ir;
}

class Engine
{
public:
    void prepare (double sampleRate, int blockSize)
    {
        sr = (float) sampleRate;
        for (auto& v : voices) v.on = false;
        const int dn = (int) (2.0 * sampleRate) + 8;
        dl.assign ((size_t) dn, 0.0f);  dr.assign ((size_t) dn, 0.0f);  dpos = 0;
        for (auto* b : { &preL, &preR, &wet, &sendL, &sendR }) b->assign ((size_t) blockSize, 0.0f);
        conv.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        conv.reset();
        convBuf.setSize (2, blockSize);
        dTime = 0.32f;  compGr = 0;  master = 0;
        ticks.prepare (sr);  sting.prepare (sr);
        dTone.lowpass (4200.0f, 1.12f, sr);
    }

    // Message thread.
    void loadRoom (float seconds, int seed)
    {
        conv.loadImpulseResponse (makeImpulse (sr, seconds, seed), sr, juce::dsp::Convolution::Stereo::yes,
                                  juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    }

    // Modes 10 and up are the interface's own sounds; 20 is the typed-code sting.
    void noteOn (const Event& e, const Knobs& k)
    {
        if (e.mode == 20) { if (! sting.running()) sting.start (rnd); return; }
        if (e.mode >= 10) { ticks.trigger (e.mode - 10, e.vel, rnd); return; }
        for (auto& v : voices)
            if (! v.on) { v.start (e, k, sr, rnd); return; }
    }

    int liveVoices() const { int c = 0; for (auto& v : voices) c += v.on; return c; }

    // delayFx false: reverb. Writes stereo into outL/outR (overwrites).
    void render (float* outL, float* outR, int n, const Knobs& k, bool delayFx, bool soundOn)
    {
        if ((int) preL.size() < n)
            for (auto* b : { &preL, &preR, &wet, &sendL, &sendR }) b->assign ((size_t) n, 0.0f);
        std::fill_n (preL.data(), n, 0.0f);  std::fill_n (preR.data(), n, 0.0f);  std::fill_n (wet.data(), n, 0.0f);
        std::fill_n (sendL.data(), n, 0.0f);
        ticks.render (sendL.data(), n, rnd);
        for (auto& v : voices)
            if (v.on) v.render (preL.data(), preR.data(), wet.data(), n, table);

        // room send
        if (convBuf.getNumSamples() < n) convBuf.setSize (2, n, false, false, true);
        for (int i = 0; i < n; ++i)
        {
            convBuf.setSample (0, i, preL[(size_t) i] + wet[(size_t) i]);
            convBuf.setSample (1, i, preR[(size_t) i] + wet[(size_t) i]);
        }
        juce::dsp::AudioBlock<float> block (convBuf.getArrayOfWritePointers(), 2, (size_t) n);
        conv.process (juce::dsp::ProcessContextReplacing<float> (block));

        const float wetT = delayFx ? 0.0f : 0.14f + 0.56f * k.depth;
        const float dOutT = delayFx ? 0.14f + 0.5f * k.depth : 0.0f;
        const float fb = 0.18f + 0.56f * k.depth;
        const float dryT = 1.0f - 0.35f * k.depth;
        const float timeT = delayFx ? 0.09f * std::pow (0.8f / 0.09f, k.wave) : dTime;
        if (delayFx) dTone.lowpass (1400.0f * std::pow (2.0f, 2.7f * k.prism), 1.12f, sr);
        const float sm = 1.0f - std::exp (-1.0f / (0.1f * sr)), smT = 1.0f - std::exp (-1.0f / (0.12f * sr));
        const float masterT = soundOn ? 0.16f : 0.0f, smM = 1.0f - std::exp (-1.0f / (0.1f * sr));
        const float att = 1.0f - std::exp (-1.0f / (0.004f * sr)), rel = 1.0f - std::exp (-1.0f / (0.22f * sr));
        const int dn = (int) dl.size();
        const auto* cl = convBuf.getReadPointer (0);
        const auto* cr = convBuf.getReadPointer (1);

        for (int i = 0; i < n; ++i)
        {
            wetG += (wetT - wetG) * sm;  dOutG += (dOutT - dOutG) * sm;  dryG += (dryT - dryG) * sm;
            fbG += (fb - fbG) * sm;      dTime += (timeT - dTime) * smT;  master += (masterT - master) * smM;

            // ping-pong: left feeds right, right feeds back into left
            const float pl = preL[(size_t) i], pr = preR[(size_t) i];
            const float dOutL = tap (dl, dTime, dn), dOutR = tap (dr, dTime, dn);
            dl[(size_t) dpos] = dTone.tick (0.5f * (pl + pr) + wet[(size_t) i]) + dOutR * fbG;
            dr[(size_t) dpos] = dOutL * fbG;
            if (++dpos >= dn) dpos = 0;

            const float tk = 0.55f * sendL[(size_t) i];   // interface sounds join dry, ahead of the glue
            float l = pl * dryG + cl[i] * wetG + dOutL * dOutG + tk;
            float r = pr * dryG + cr[i] * wetG + dOutR * dOutG + tk;

            // glue: soft-knee 6:1 above -16 dB, linked, with the make-up a browser compressor adds
            const float peak = std::max (std::abs (l), std::abs (r));
            const float db = peak > 1e-6f ? 20.0f * std::log10 (peak) : -120.0f;
            const float over = db + 16.0f;
            float gr = 0;
            if (over > 5.0f) gr = -(5.0f / 6.0f) * over;
            else if (over > -5.0f) gr = -(5.0f / 6.0f) * (over + 5.0f) * (over + 5.0f) / 20.0f;
            compGr += (gr - compGr) * (gr < compGr ? att : rel);
            const float g = std::pow (10.0f, (compGr + 8.0f) / 20.0f) * master;
            outL[i] = 2.0f * safety (l * g);
            outR[i] = 2.0f * safety (r * g);
        }
        sting.render (outL, outR, n, rnd);
    }

private:
    float tap (const std::vector<float>& buf, float seconds, int dn) const
    {
        const float d = juce::jlimit (1.0f, (float) dn - 2.0f, seconds * sr);
        float rp = (float) dpos - d;
        if (rp < 0) rp += (float) dn;
        const int i0 = (int) rp, i1 = i0 + 1 >= dn ? 0 : i0 + 1;
        const float fr = rp - (float) i0;
        return buf[(size_t) i0] + (buf[(size_t) i1] - buf[(size_t) i0]) * fr;
    }

    // Linear to 0.35, then a tanh shoulder that can never pass 0.5.
    static float safety (float x)
    {
        const float a = std::abs (x);
        const float y = a <= 0.35f ? a : 0.35f + 0.15f * std::tanh ((a - 0.35f) / 0.15f);
        return x < 0 ? -y : y;
    }

    float sr = 48000;
    std::array<Voice, kMaxVoices> voices;
    GrainTable table;
    Ticks ticks;
    Sting sting;
    juce::Random rnd { 24301 };
    std::vector<float> preL, preR, wet, sendL, sendR, dl, dr;
    int dpos = 0;
    Biquad dTone;
    juce::dsp::Convolution conv { juce::dsp::Convolution::NonUniform { 1024 } };
    juce::AudioBuffer<float> convBuf;
    float wetG = 0, dOutG = 0, dryG = 1, fbG = 0.4f, dTime = 0.32f, compGr = 0, master = 0;
};
} // namespace lib
