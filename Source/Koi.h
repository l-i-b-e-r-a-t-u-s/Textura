#pragma once
// The pond under the page: koi that wander, gather round a resting pointer and peck at it,
// scatter from strikes and fast sweeps, and eat whatever is dropped in feed mode.

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <limits>
#include <vector>

namespace lib
{
class Pond
{
public:
    struct Pointer { bool active; float x, y, speed; };

    void resize (float width, float height, juce::Random& rnd)
    {
        W = width;  H = height;
        const float scale = juce::jlimit (0.6f, 1.25f, std::min (W, H) / 820.0f);
        const int want = W < 640.0f ? 4 : 7;
        if ((int) fish.size() == want) return;
        fish.clear();
        const int order[7] = { 0, 1, 2, 3, 0, 4, 5 };
        for (int i = 0; i < want; ++i) fish.push_back (make (scale, order[i % 7], rnd));
    }

    void clear() { fish.clear(); food.clear(); fade = 0; }
    bool empty() const { return fish.empty(); }

    void strike (float x, float y, juce::Random& rnd)
    {
        for (auto& f : fish)
        {
            const float dx = f.x - x, dy = f.y - y, r = std::hypot (dx, dy);
            if (r > 560.0f) continue;
            const float a = std::min (1.0f, 2.0f * (1.0f - r / 560.0f));
            if (a <= f.scare) continue;
            f.scare = a;  f.fleeA = std::atan2 (dy, dx) + range (rnd, -0.3f, 0.3f);  f.slot = none();
            f.av += 4.0f * wrap (f.fleeA - f.a) * a;
            f.speed = std::max (f.speed, f.base * (2.0f + 4.0f * a));
        }
    }

    void feed (float x, float y, float now, juce::Random& rnd)
    {
        const int n = 6 + rnd.nextInt (4);
        for (int i = 0; i < n && food.size() < 60; ++i)
        {
            const float a = rnd.nextFloat() * juce::MathConstants<float>::twoPi, v = range (rnd, 12.0f, 60.0f);
            food.push_back ({ x + range (rnd, -5, 5), y + range (rnd, -5, 5), std::cos (a) * v, std::sin (a) * v, now,
                              range (rnd, 2.3f, 3.5f), range (rnd, 0.0f, 6.28f) });
        }
    }

    // Returns where pellets were eaten this frame (each one makes a small ripple).
    std::vector<juce::Point<float>> update (float dt, float now, Pointer m, juce::Random& rnd)
    {
        std::vector<juce::Point<float>> eaten;
        dt = std::min (0.05f, dt);
        fade = std::min (1.0f, fade + 1.2f * dt);
        int pecking = 0;
        for (auto& f : fish) pecking += hasSlot (f);

        for (auto& e : fish)
        {
            float aim = e.a + (0.5f * std::sin (0.23f * now + e.seed) + 0.3f * std::sin (0.41f * now + 2.0f * e.seed)) * 0.8f;
            float want = e.base * (0.85f + 0.3f * std::sin (0.5f * now + e.seed));
            const float wall = std::max (std::max (110.0f - e.x, e.x - (W - 110.0f)), std::max (110.0f - e.y, e.y - (H - 110.0f)));
            if (wall > 0.0f) aim = turn (aim, std::atan2 (H * 0.5f - e.y, W * 0.5f - e.x), juce::jlimit (0.0f, 1.0f, wall / 110.0f));
            for (auto& o : fish)
            {
                if (&o == &e) continue;
                const float dx = o.x - e.x, dy = o.y - e.y, r = std::hypot (dx, dy);
                if (r > e.len || r < 0.01f) continue;
                if ((dx * std::cos (e.a) + dy * std::sin (e.a)) / r > -0.2f) aim = turn (aim, std::atan2 (-dy, -dx), 0.45f * (1.0f - r / e.len));
            }

            float lure = 0;
            bool atPointer = false;
            if (m.active)
            {
                const float dx = m.x - e.x, dy = m.y - e.y, dist = std::hypot (dx, dy);
                if (m.speed > 900.0f)
                {
                    const float reach = 180.0f + std::min (320.0f, 0.15f * m.speed);
                    if (dist < reach)
                    {
                        const float s = (1.0f - dist / reach) * std::min (1.0f, m.speed / 1400.0f);
                        if (s > e.scare) { e.scare = s;  e.fleeA = std::atan2 (-dy, -dx);  e.slot = none(); }
                    }
                }
                else if (e.scare < 0.2f && m.speed < 350.0f && dist < 480.0f)
                {
                    lure = 1.0f - dist / 480.0f;
                    const bool still = m.speed < 60.0f;
                    if (! still && hasSlot (e)) e.slot = none();
                    if (still && ! hasSlot (e) && dist < 0.9f * e.len && pecking < 3 && now > e.coolUntil)
                    {
                        e.slot = std::atan2 (e.y - m.y, e.x - m.x);
                        e.nextPeck = now + range (rnd, 0.8f, 1.8f);  e.peckAt = -1;
                        e.interestUntil = now + range (rnd, 6.0f, 14.0f);
                        ++pecking;
                    }
                    if (hasSlot (e) && now > e.interestUntil)
                    {
                        e.slot = none();  e.coolUntil = now + range (rnd, 3.0f, 7.0f);
                        e.av += (rnd.nextBool() ? -1.0f : 1.0f) * 1.2f;
                    }
                    if (! hasSlot (e))
                    {
                        if (still)
                        {
                            const float ang = e.seed + 0.25f * now, rad = 1.3f * e.len;
                            aim = turn (aim, std::atan2 (m.y + std::sin (ang) * rad - e.y, m.x + std::cos (ang) * rad - e.x), 0.8f);
                            want *= 0.8f;
                        }
                        else
                        {
                            if (dist > 0.6f * e.len) aim = turn (aim, std::atan2 (dy, dx), 0.6f);
                            want = std::min (1.3f * want, 1.5f * e.base);
                        }
                    }
                    else
                    {
                        atPointer = true;
                        for (auto& o : fish)
                        {
                            if (&o == &e || ! hasSlot (o)) continue;
                            const float gap = wrap (e.slot - o.slot);
                            if (std::abs (gap) < 2.0f) e.slot += (gap >= 0.0f ? 1.0f : -1.0f) * dt * 2.0f * (1.0f - std::abs (gap) / 2.0f);
                        }
                        if (e.peckAt < 0.0f && now >= e.nextPeck)
                        {
                            e.peckAt = now;
                            if (e.pecks <= 0) e.pecks = 1 + rnd.nextInt (3);
                        }
                        const float far = 1.5f * e.w + 12.0f, near = 0.25f * e.w;
                        float hold = far;
                        bool lunge = false;
                        if (e.peckAt >= 0.0f)
                        {
                            const float t = now - e.peckAt;
                            if (t < 0.14f) { hold = near + (far - near) * (1.0f - std::sin (t / 0.14f * juce::MathConstants<float>::halfPi)); lunge = true; }
                            else if (t < 0.22f) hold = near;
                            else if (t < 0.95f) hold = near + (far - near) * (0.5f - 0.5f * std::cos ((t - 0.22f) / 0.73f * juce::MathConstants<float>::pi));
                            else { e.peckAt = -1;  --e.pecks;  e.nextPeck = now + (e.pecks > 0 ? range (rnd, 0.12f, 0.3f) : range (rnd, 1.1f, 3.4f)); }
                        }
                        aim = std::atan2 (dy, dx) + 0.12f * std::sin (0.9f * now + e.seed);
                        want = juce::jlimit (-16.0f, lunge ? 140.0f : 55.0f, (dist - hold) * (lunge ? 7.0f : 1.8f));
                        const float tx = m.x + std::cos (e.slot) * hold, ty = m.y + std::sin (e.slot) * hold;
                        const float off = std::hypot (tx - e.x, ty - e.y);
                        if (off > 6.0f) aim = turn (aim, std::atan2 (ty - e.y, tx - e.x), juce::jlimit (0.0f, 0.7f, off / 50.0f));
                    }
                }
            }
            if (! atPointer) e.slot = none();
            e.lure += (lure - e.lure) * std::min (1.0f, 3.0f * dt);

            if (! food.empty() && e.scare < 0.3f)
            {
                int best = -1;
                float bestD = 600.0f;
                for (int i = 0; i < (int) food.size(); ++i)
                {
                    const float d = std::hypot (food[(size_t) i].x - e.x, food[(size_t) i].y - e.y);
                    if (d < bestD) { bestD = d; best = i; }
                }
                if (best >= 0)
                {
                    const auto p = food[(size_t) best];
                    atPointer = false;  e.slot = none();
                    aim = std::atan2 (p.y - e.y, p.x - e.x);
                    want = bestD < 70.0f ? e.base * (0.7f + bestD / 70.0f) : e.base * (1.4f + std::min (1.0f, bestD / 250.0f));
                    if (bestD < 0.9f * e.w)
                    {
                        food.erase (food.begin() + best);
                        eaten.push_back ({ p.x, p.y });
                        e.speed *= 0.55f;  e.phase += 1.4f;
                    }
                }
            }

            if (e.scare > 0.02f)
            {
                aim = turn (aim, e.fleeA, std::min (1.0f, 1.6f * e.scare));
                want = std::max (want, e.base * (1.0f + 8.0f * e.scare));
            }
            e.scare *= std::exp (-dt * 0.9f);

            const float err = wrap (aim - e.a), maxTurn = 1.4f + 3.2f * e.scare + (atPointer ? 2.2f : 0.0f);
            e.av += (juce::jlimit (-maxTurn, maxTurn, 3.2f * err) - e.av) * std::min (1.0f, 5.0f * dt);
            e.a = wrap (e.a + e.av * dt);
            if (! atPointer) want *= 1.0f - std::min (0.45f, 0.25f * std::abs (err));
            e.speed += (want - e.speed) * std::min (1.0f, dt * (e.scare > 0.3f ? 6.0f : (atPointer ? 5.0f : 2.2f)));
            const float v = std::abs (e.speed);
            e.phase += dt * (2.2f + 0.05f * v);
            const float head = e.a + std::sin (e.phase) * (0.05f + 0.1f * std::min (1.0f, v / 70.0f));
            e.x += std::cos (head) * e.speed * dt;
            e.y += std::sin (head) * e.speed * dt;

            // the body trails the head, each joint bending only a little
            const float link = e.len / 13.0f;
            float px = e.x, py = e.y, back = head + juce::MathConstants<float>::pi;
            e.spine[0] = px;  e.spine[1] = py;
            for (int i = 1; i < 14; ++i)
            {
                float ang = std::atan2 (e.spine[2 * i + 1] - py, e.spine[2 * i] - px);
                const float bend = wrap (ang - back);
                if (bend > 0.16f) ang = back + 0.16f; else if (bend < -0.16f) ang = back - 0.16f;
                px += std::cos (ang) * link;  py += std::sin (ang) * link;
                e.spine[2 * i] = px;  e.spine[2 * i + 1] = py;
                back = ang;
            }
            for (int i = 0; i < 14; ++i)
            {
                const int a = std::max (0, i - 1), b = std::min (13, i + 1);
                float nx = -(e.spine[2 * b + 1] - e.spine[2 * a + 1]), ny = e.spine[2 * b] - e.spine[2 * a];
                const float l = std::max (1e-6f, std::hypot (nx, ny));
                nx /= l;  ny /= l;
                const float half = girth ((float) i / 13.0f) * e.w;
                e.edge[4 * i] = e.spine[2 * i] + nx * half;      e.edge[4 * i + 1] = e.spine[2 * i + 1] + ny * half;
                e.edge[4 * i + 2] = e.spine[2 * i] - nx * half;  e.edge[4 * i + 3] = e.spine[2 * i + 1] - ny * half;
            }
        }

        const float drag = std::exp (-2.4f * dt);
        for (size_t i = food.size(); i-- > 0;)
        {
            auto& p = food[i];
            if (now - p.born > 30.0f) { food.erase (food.begin() + (long) i); continue; }
            p.vx *= drag;  p.vy *= drag;
            p.x += (p.vx + 3.0f * std::sin (0.6f * now + p.seed)) * dt;
            p.y += (p.vy + 3.0f * std::cos (0.5f * now + p.seed)) * dt;
        }
        return eaten;
    }

    void draw (juce::Graphics& g, bool light, float now) const
    {
        using juce::Colour;
        for (auto& f : fish)
        {
            g.setColour ((light ? Colour (40, 32, 24) : juce::Colours::black).withAlpha (0.16f * fade));
            g.fillPath (body (f), juce::AffineTransform::translation (0.9f * f.w, 1.6f * f.w));
        }
        for (auto& f : fish)
        {
            const auto shape = body (f);
            const Colour fin = finColour (f.variety), rib = (light ? Colour (60, 50, 40) : juce::Colours::white).withAlpha (0.12f * fade);
            const float still = 1.0f - std::min (1.0f, std::abs (f.speed) / 90.0f);
            auto fill = [&] (const juce::Path& p, juce::Point<float> from, juce::Point<float> to, float alpha)
            {
                g.setGradientFill (juce::ColourGradient (fin.withAlpha (alpha * fade), from, fin.withAlpha (0.06f * fade), to, false));
                g.fillPath (p);
            };

            // tail
            const juce::Point<float> root (f.spine[26], f.spine[27]);
            const float ta = std::atan2 (root.y - f.spine[25], root.x - f.spine[24]) + 0.32f * std::sin (f.phase - 2.4f), tl = 0.36f * f.len;
            auto polar = [&] (float ang, float r) { return root + juce::Point<float> (std::cos (ang), std::sin (ang)) * r; };
            juce::Path tail;
            tail.startNewSubPath (f.edge[52], f.edge[53]);
            tail.quadraticTo (polar (ta - 0.32f, 0.6f * tl), polar (ta - 0.5f, tl));
            tail.quadraticTo (polar (ta - 0.18f, 0.75f * tl), polar (ta, 0.58f * tl));
            tail.quadraticTo (polar (ta + 0.18f, 0.75f * tl), polar (ta + 0.5f, tl));
            tail.quadraticTo (polar (ta + 0.32f, 0.6f * tl), { f.edge[54], f.edge[55] });
            tail.closeSubPath();
            fill (tail, root, polar (ta, 0.58f * tl), 0.62f);
            g.setColour (rib);
            for (float o : { -0.42f, -0.21f, 0.21f, 0.42f }) g.drawLine ({ root, polar (ta + o, tl * (0.82f - 0.2f * std::abs (o))) }, 0.7f);

            // side fins
            const float sa = std::atan2 (f.spine[7] - f.spine[9], f.spine[6] - f.spine[8]), sl = 0.25f * f.len;
            for (float side : { 1.0f, -1.0f })
            {
                const juce::Point<float> at (side > 0 ? f.edge[12] : f.edge[14], side > 0 ? f.edge[13] : f.edge[15]);
                const float ang = sa + side * (2.0f + 0.45f * still + 0.18f * std::sin (0.7f * f.phase + side));
                const juce::Point<float> dir (std::cos (ang), std::sin (ang)), tip = at + dir * sl;
                const juce::Point<float> out (std::cos (ang + side * juce::MathConstants<float>::halfPi), std::sin (ang + side * juce::MathConstants<float>::halfPi));
                const float wide = 0.45f * sl;
                juce::Path p;
                p.startNewSubPath (at);
                p.cubicTo (at + out * (wide * 0.2f), tip + out * wide, tip);
                p.cubicTo (tip - out * (wide * 0.3f), at - out * (wide * 0.1f) + dir * (sl * 0.3f), at);
                fill (p, at, tip, 0.58f);
                g.setColour (rib);
                for (float t : { 0.25f, 0.55f, 0.85f }) g.drawLine ({ at, tip + out * (wide * t * 0.9f) }, 0.7f);
            }

            g.setColour (bodyColour (f.variety).withMultipliedAlpha (fade));
            g.fillPath (shape);
            {
                juce::Graphics::ScopedSaveState clip (g);
                g.reduceClipRegion (shape);
                for (auto& pt : f.patches)
                {
                    const int s = juce::jlimit (0, 12, juce::roundToInt (13.0f * pt.t));
                    const float ang = std::atan2 (f.spine[2 * s + 1] - f.spine[2 * s + 3], f.spine[2 * s] - f.spine[2 * s + 2]);
                    const float half = girth ((float) s / 13.0f) * f.w, r = pt.r * f.len;
                    const float cx = f.spine[2 * s] + std::cos (ang + juce::MathConstants<float>::halfPi) * pt.side * half;
                    const float cy = f.spine[2 * s + 1] + std::sin (ang + juce::MathConstants<float>::halfPi) * pt.side * half;
                    juce::Graphics::ScopedSaveState one (g);
                    g.addTransform (juce::AffineTransform::scale (pt.stretch, 1.0f).rotated (ang).translated (cx, cy));
                    juce::ColourGradient grad (pt.colour.withMultipliedAlpha (fade), 0, 0, pt.colour.withAlpha (0.0f), r, 0, true);
                    grad.addColour (0.55, pt.colour.withMultipliedAlpha (fade));
                    g.setGradientFill (grad);
                    g.fillEllipse (-r, -r, 2.0f * r, 2.0f * r);
                }
                juce::Path ridge;
                ridge.startNewSubPath (f.spine[2], f.spine[3]);
                for (int i = 2; i < 11; ++i) ridge.lineTo (f.spine[2 * i], f.spine[2 * i + 1]);
                g.setColour ((f.variety == 3 ? Colour (255, 248, 220).withAlpha (0.35f) : juce::Colours::white.withAlpha (0.16f)).withMultipliedAlpha (fade));
                g.strokePath (ridge, juce::PathStrokeType (0.55f * f.w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (juce::Colours::black.withAlpha (0.1f * fade));
                g.strokePath (shape, juce::PathStrokeType (0.5f * f.w));
            }
            const juce::Point<float> brow ((f.spine[0] + f.spine[2]) * 0.5f, (f.spine[1] + f.spine[3]) * 0.5f);
            const float across = f.a + juce::MathConstants<float>::halfPi, eye = std::max (1.1f, 0.09f * f.w);
            g.setColour (Colour (20, 18, 16).withAlpha (0.85f * fade));
            for (float side : { 1.0f, -1.0f })
                g.fillEllipse (juce::Rectangle<float> (2.0f * eye, 2.0f * eye).withCentre (brow + juce::Point<float> (std::cos (across), std::sin (across)) * (0.62f * f.w * side)));
            if (light)
            {
                g.setColour (Colour (20, 20, 20).withAlpha (0.14f * fade));
                g.strokePath (shape, juce::PathStrokeType (0.8f));
            }
        }
        for (auto& p : food)
        {
            const float age = now - p.born, a = fade * (age > 27.0f ? (30.0f - age) / 3.0f : 1.0f);
            g.setColour (juce::Colours::black.withAlpha (0.18f * a));
            g.fillEllipse (juce::Rectangle<float> (2 * p.r, 2 * p.r).withCentre ({ p.x + 1.6f, p.y + 2.4f }));
            g.setColour (Colour (0xffb8742c).withAlpha (a));
            g.fillEllipse (juce::Rectangle<float> (2 * p.r, 2 * p.r).withCentre ({ p.x, p.y }));
            g.setColour (Colour (255, 236, 200).withAlpha (0.6f * a));
            g.fillEllipse (juce::Rectangle<float> (0.76f * p.r, 0.76f * p.r).withCentre ({ p.x - 0.3f * p.r, p.y - 0.35f * p.r }));
        }
    }

private:
    struct Patch { float t, side, r, stretch; juce::Colour colour; };
    struct Fish
    {
        float x, y, a, av = 0, speed = 20, base, len, w, seed, phase, scare = 0, fleeA = 0, lure = 0;
        float slot = std::numeric_limits<float>::quiet_NaN(), nextPeck = 0, peckAt = -1, interestUntil = 0, coolUntil = 0;
        int pecks = 0, variety = 0;
        std::vector<Patch> patches;
        float spine[28] {}, edge[56] {};
    };
    struct Pellet { float x, y, vx, vy, born, r, seed; };

    static float none() { return std::numeric_limits<float>::quiet_NaN(); }
    static bool hasSlot (const Fish& f) { return ! std::isnan (f.slot); }
    static float range (juce::Random& r, float a, float b) { return a + r.nextFloat() * (b - a); }
    static float wrap (float a) { return std::atan2 (std::sin (a), std::cos (a)); }
    static float turn (float from, float to, float amount) { return from + wrap (to - from) * amount; }
    static float girth (float t)
    {
        return t < 0.26f ? 0.58f + 0.42f * std::sin (t / 0.26f * juce::MathConstants<float>::halfPi)
                         : 1.0f - 0.86f * std::pow ((t - 0.26f) / 0.74f, 1.35f);
    }

    static juce::Colour bodyColour (int v)
    {
        const juce::uint32 c[6] = { 0xfff4f0e6, 0xfff4f0e6, 0xfff4f0e6, 0xffe9b24e, 0xff1c1b1a, 0xff8095a9 };
        return juce::Colour (c[v]);
    }
    static juce::Colour finColour (int v)
    {
        return v == 3 ? juce::Colour (255, 236, 190) : (v == 4 ? juce::Colour (235, 230, 222) : (v == 5 ? juce::Colour (240, 222, 205) : juce::Colour (255, 250, 240)));
    }

    Fish make (float scale, int variety, juce::Random& rnd) const
    {
        const juce::Colour white (0xfff4f0e6), red (0xffec5326), black (0xff1c1b1a), amber (0xffe9793a);
        Fish f;
        f.len = range (rnd, 105, 160) * scale;  f.w = 0.17f * f.len;
        f.x = range (rnd, 0.12f, 0.88f) * W;    f.y = range (rnd, 0.12f, 0.88f) * H;
        f.a = range (rnd, -juce::MathConstants<float>::pi, juce::MathConstants<float>::pi);
        f.base = range (rnd, 24, 36) * scale;   f.seed = range (rnd, 0, 6.28f);  f.phase = range (rnd, 0, 6.28f);
        f.variety = variety;
        for (int i = 0; i < 14; ++i)
        {
            f.spine[2 * i] = f.x - std::cos (f.a) * f.len / 13.0f * (float) i;
            f.spine[2 * i + 1] = f.y - std::sin (f.a) * f.len / 13.0f * (float) i;
        }
        auto blotches = [&] (int n)
        {
            for (int i = 0; i < n; ++i)
                f.patches.push_back ({ 0.08f + (float) i / (float) std::max (1, n - 1) * range (rnd, 0.5f, 0.62f), range (rnd, -0.35f, 0.35f),
                                       range (rnd, 0.15f, 0.23f), range (rnd, 1.3f, 2.0f), red });
        };
        switch (variety)
        {
            case 0: blotches (3 + rnd.nextInt (2)); break;
            case 1: f.patches.push_back ({ 0.07f, 0, 0.1f, 1, red }); break;
            case 2: blotches (3);
                    for (int i = 0; i < 4; ++i) f.patches.push_back ({ range (rnd, 0.2f, 0.75f), range (rnd, -0.8f, 0.8f), range (rnd, 0.04f, 0.08f), range (rnd, 1.0f, 1.6f), black });
                    break;
            case 4: f.patches = { { 0.1f, 0.1f, 0.17f, 1.4f, white }, { 0.42f, -0.3f, 0.2f, 1.8f, red }, { 0.62f, 0.35f, 0.15f, 1.6f, white } }; break;
            case 5: for (float t : { 0.2f, 0.42f, 0.64f }) { f.patches.push_back ({ t, 0.95f, 0.12f, 1.6f, amber }); f.patches.push_back ({ t, -0.95f, 0.12f, 1.6f, amber }); }
                    f.patches.push_back ({ 0.05f, 0, 0.08f, 1, juce::Colour (0xffc9d3dc) });
                    break;
            default: break;
        }
        return f;
    }

    static juce::Path body (const Fish& f)
    {
        const auto* e = f.edge;
        const juce::Point<float> nose (f.spine[0] + std::cos (f.a) * f.w * 0.55f, f.spine[1] + std::sin (f.a) * f.w * 0.55f);
        juce::Path p;
        p.startNewSubPath (nose);
        p.quadraticTo (e[0], e[1], (e[0] + e[4]) * 0.5f, (e[1] + e[5]) * 0.5f);
        for (int i = 1; i < 13; ++i) p.quadraticTo (e[4 * i], e[4 * i + 1], (e[4 * i] + e[4 * i + 4]) * 0.5f, (e[4 * i + 1] + e[4 * i + 5]) * 0.5f);
        p.lineTo (e[52], e[53]);
        p.lineTo (e[54], e[55]);
        for (int i = 12; i > 0; --i) p.quadraticTo (e[4 * i + 2], e[4 * i + 3], (e[4 * i + 2] + e[4 * i - 2]) * 0.5f, (e[4 * i + 3] + e[4 * i - 1]) * 0.5f);
        p.quadraticTo (e[2], e[3], nose.x, nose.y);
        p.closeSubPath();
        return p;
    }

    float W = 1280, H = 720, fade = 0;
    std::vector<Fish> fish;
    std::vector<Pellet> food;
};
} // namespace lib
