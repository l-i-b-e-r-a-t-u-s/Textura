#pragma once
// The picture side of Liberatus: a wave-equation membrane the pointer plays, and the word
// cut into vertical columns that the membrane bends and splits into two colours.

#include <juce_gui_basics/juce_gui_basics.h>
#include <BinaryData.h>
#include <array>
#include <cmath>
#include <vector>

namespace lib
{
constexpr int kPartials = 24;

inline float mixf (float a, float b, float t) { return a + (b - a) * t; }
inline float sstep (float a, float b, float x)
{
    const float t = juce::jlimit (0.0f, 1.0f, (x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

// Leapfrog membrane on a coarse grid. Heights are in pixels of lift.
class Membrane
{
public:
    struct Impulse { float x, y, amp, sigma; };
    struct Sample { float h, gx, gy, e; };

    int nx = 8, ny = 8;
    float cx = 1, cy = 1;

    void resize (float W, float H)
    {
        const float n = std::max (3.5f, std::sqrt (W * H / 42000.0f));
        const int x = juce::jlimit (8, 320, juce::roundToInt (W / n)), y = juce::jlimit (8, 320, juce::roundToInt (H / n));
        cx = W / (float) x;  cy = H / (float) y;
        if (x == nx && y == ny && ! h.empty()) return;
        nx = x;  ny = y;
        const auto size = (size_t) (nx * ny);
        for (auto* b : { &h, &hp, &hn, &e, &en, &ex, &hs }) b->assign (size, 0.0f);
        // the border absorbs, so ripples leave the window instead of ringing back
        rim.resize (size);
        for (int yy = 0; yy < ny; ++yy)
            for (int xx = 0; xx < nx; ++xx)
            {
                const float edge = std::min (std::min ((float) xx + 0.5f, (float) (nx - xx) - 0.5f),
                                             std::min ((float) yy + 0.5f, (float) (ny - yy) - 0.5f));
                rim[(size_t) (yy * nx + xx)] = mixf (0.8f, 1.0f, sstep (0.0f, 10.0f, edge));
            }
    }

    float cell() const { return std::sqrt (cx * cy); }
    float stepSeconds (float speed) const { return 0.6f * cell() / speed; }

    // One step. The pointer is a moving line source; impulses are zero-volume wavelets, so
    // they ring out as alternating crests and leave the surface level behind them.
    struct LineSource { juce::Line<float> line; float amp = 0, sigma = 1; };

    void step (float dt, float speed, float ring, const LineSource* lines, int numLines,
               const Impulse* imps, int numImps)
    {
        std::fill (ex.begin(), ex.end(), 0.0f);
        for (int k = 0; k < numLines; ++k)
        {
            const auto& src = lines[k];
            if (src.amp == 0.0f) continue;
            const auto box = juce::Rectangle<float> (src.line.getStart(), src.line.getEnd()).expanded (3.0f * src.sigma);
            forBox (box, [&] (int i, float px, float py)
            {
                juce::Point<float> on;
                const float q = src.line.getDistanceFromPoint ({ px, py }, on) / src.sigma;
                ex[(size_t) i] += src.amp * std::exp (-0.5f * q * q);
            });
        }
        for (int k = 0; k < numImps; ++k)
        {
            const auto& im = imps[k];
            forBox (juce::Rectangle<float> (im.x, im.y, 0, 0).expanded (4.0f * im.sigma), [&] (int i, float px, float py)
            {
                const float qx = (px - im.x) / im.sigma, qy = (py - im.y) / im.sigma, q2 = qx * qx + qy * qy;
                ex[(size_t) i] += im.amp * (1.0f - 0.5f * q2) * std::exp (-0.5f * q2);
            });
        }

        const float c2x = std::min (0.48f, (speed * dt / cx) * (speed * dt / cx));
        const float c2y = std::min (0.48f, (speed * dt / cy) * (speed * dt / cy));
        const float damp0 = std::exp (-2.0f * dt / ring), eDecay = std::exp (-dt / 0.3f);
        const float* const hc = h.data();
        const float* const hq = hp.data();
        const float* const ec = e.data();
        const float* const xc = ex.data();
        const float* const rc = rim.data();
        float* const ho = hn.data();
        float* const eo = en.data();
        for (int y = 0; y < ny; ++y)
        {
            const int yu = std::max (0, y - 1) * nx, yd = std::min (ny - 1, y + 1) * nx, row = y * nx;
            auto cell = [&] (int x, int l, int r)
            {
                const int i = row + x;
                const float c = hc[i];
                const float lap = c2x * (hc[l] + hc[r] - 2.0f * c) + c2y * (hc[yu + x] + hc[yd + x] - 2.0f * c);
                const float nh = std::min (240.0f, std::max (-240.0f, (2.0f * c - hq[i] + lap) * (damp0 * rc[i]) + xc[i]));
                ho[i] = nh;
                const float eb = (4.0f * ec[i] + ec[l] + ec[r] + ec[yu + x] + ec[yd + x]) * 0.125f;
                eo[i] = std::min (16.0f, std::max (eb * eDecay, std::abs (nh) * 0.11f + std::abs (xc[i]) * 0.9f));
            };
            cell (0, row, row + std::min (nx - 1, 1));
            for (int x = 1; x < nx - 1; ++x) cell (x, row + x - 1, row + x + 1);
            if (nx > 1) cell (nx - 1, row + nx - 2, row + nx - 1);
        }
        hp.swap (h);  h.swap (hn);  e.swap (en);
    }

    // Blend the last two steps so the picture moves smoothly between simulation ticks.
    void settle (float alpha)
    {
        for (size_t i = 0; i < h.size(); ++i) hs[i] = mixf (hp[i], h[i], alpha);
    }

    float energyNear (float x, float y) const
    {
        const int ix = juce::jlimit (0, nx - 1, (int) (x / cx)), iy = juce::jlimit (0, ny - 1, (int) (y / cy));
        return e[(size_t) (iy * nx + ix)];
    }

    Sample sample (float x, float y) const
    {
        const float hl = height (x - cx, y), hr = height (x + cx, y), hu = height (x, y - cy), hd = height (x, y + cy);
        return { height (x, y), (hr - hl) / (2.0f * cx), (hd - hu) / (2.0f * cy), 1.0f - std::exp (-bilinear (e, x, y)) };
    }

private:
    float height (float x, float y) const { return bilinear (hs, x, y); }

    float bilinear (const std::vector<float>& b, float x, float y) const
    {
        const float gx = juce::jlimit (0.0f, (float) nx - 1.001f, x / cx - 0.5f), gy = juce::jlimit (0.0f, (float) ny - 1.001f, y / cy - 0.5f);
        const int ix = (int) gx, iy = (int) gy;
        const float fx = gx - (float) ix, fy = gy - (float) iy;
        const auto i = (size_t) (iy * nx + ix);
        const auto j = i + (size_t) nx;
        return mixf (mixf (b[i], b[i + 1], fx), mixf (b[j], b[j + 1], fx), fy);
    }

    template <typename Fn>
    void forBox (juce::Rectangle<float> box, Fn&& fn)
    {
        const int x0 = std::max (0, (int) (box.getX() / cx)), x1 = std::min (nx - 1, (int) (box.getRight() / cx));
        const int y0 = std::max (0, (int) (box.getY() / cy)), y1 = std::min (ny - 1, (int) (box.getBottom() / cy));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                fn (y * nx + x, ((float) x + 0.5f) * cx, ((float) y + 0.5f) * cy);
    }

    std::vector<float> h, hp, hn, e, en, ex, hs, rim;
};

// The wordmark's face. A Mac sets it in the system's bold grotesque; everywhere else gets the
// open face that ships inside the plugin, so the word looks the same with nothing installed.
inline juce::Font markFont()
{
   #if JUCE_MAC && ! LIBERATUS_OPEN_FACE
    return juce::Font (juce::FontOptions ("Helvetica Neue", 400.0f, juce::Font::bold)).withExtraKerningFactor (-0.035f);
   #else
    static const auto face = juce::Typeface::createSystemTypefaceFor (BinaryData::InterBold_ttf, (size_t) BinaryData::InterBold_ttfSize);
    return juce::Font (juce::FontOptions (face).withHeight (400.0f)).withExtraKerningFactor (-0.035f);
   #endif
}

// The word as an outline, plus what the sound reads from it: where each letter sits and how
// much ink each of 24 height bands holds at any position along it.
class Wordmark
{
public:
    juce::Path path;                 // x 0..aspect, y 0..1
    float aspect = 5.0f, baseline = 0.8f;
    std::vector<juce::Range<float>> letters;   // fractions of the width

    int unitsW = 0, unitsH = 0;      // the outline's size in font units, for the boot readout

    // With a stand-in shape, the box stays that of the word and the shape is set centred
    // inside it at full height, so swapping marks never moves the layout.
    void build (const juce::String& text, const juce::Path& standIn = {})
    {
        const auto word = text.trim().isEmpty() ? juce::String ("liberatus") : text.trim();
        const auto font = markFont();
        juce::GlyphArrangement ga;
        ga.addLineOfText (font, word, 0.0f, 0.0f);
        juce::Path raw;
        ga.createPath (raw);
        auto b = raw.getBounds();
        if (b.getWidth() < 1.0f || b.getHeight() < 1.0f) b = { 0, -300, 1200, 300 };
        juce::Rectangle<float> shape;
        if (! standIn.isEmpty())
        {
            const auto from = standIn.getBounds();
            const float scale = b.getHeight() / from.getHeight();
            raw = standIn;
            raw.applyTransform (juce::AffineTransform::translation (-from.getCentreX(), -from.getY()).scaled (scale).translated (b.getCentreX(), b.getY()));
            shape = raw.getBounds();
            b = b.getUnion (shape);
        }
        unitsW = juce::roundToInt (b.getWidth() * 2.5f);  unitsH = juce::roundToInt (b.getHeight() * 2.5f);
        aspect = b.getWidth() / b.getHeight();
        baseline = juce::jlimit (0.0f, 1.0f, -b.getY() / b.getHeight());
        path = raw;
        path.applyTransform (juce::AffineTransform::translation (-b.getX(), -b.getY()).scaled (1.0f / b.getHeight()));

        letters.clear();
        // a shape has no letters, so it is played as four equal parts, one per chord
        for (int i = 0; i < 4 && ! shape.isEmpty(); ++i)
            letters.push_back ({ (shape.getX() + shape.getWidth() * (float) i * 0.25f - b.getX()) / b.getWidth(),
                                 (shape.getX() + shape.getWidth() * (float) (i + 1) * 0.25f - b.getX()) / b.getWidth() });
        for (int i = 0; i < ga.getNumGlyphs() && shape.isEmpty(); ++i)
        {
            const auto gb = ga.getBoundingBox (i, 1, true);
            if (gb.getWidth() < 1.0f) continue;
            letters.push_back ({ juce::jlimit (0.0f, 1.0f, (gb.getX() - b.getX()) / b.getWidth()),
                                 juce::jlimit (0.0f, 1.0f, (gb.getRight() - b.getX()) / b.getWidth()) });
        }
        if (letters.empty()) letters.push_back ({ 0.0f, 1.0f });

        const auto cover = raster (512, 96);
        for (int x = 0; x < 512; ++x)
            for (int n = 0; n < kPartials; ++n)
            {
                const int top = 96 - (n + 1) * 4;
                float a = 0;
                for (int y = top; y < top + 4; ++y) a += cover[(size_t) (y * 512 + x)];
                profile[(size_t) (x * kPartials + n)] = a * 0.25f;
            }
    }

    // Ink coverage 0..1 on a w by h grid stretched over the word's box.
    std::vector<float> raster (int w, int h) const
    {
        juce::Image img (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
        {
            juce::Graphics g (img);
            g.setColour (juce::Colours::white);
            g.fillPath (path, juce::AffineTransform::scale ((float) w / aspect, (float) h));
        }
        std::vector<float> out ((size_t) (w * h));
        const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                out[(size_t) (y * w + x)] = (float) ((const juce::PixelARGB*) bd.getPixelPointer (x, y))->getAlpha() / 255.0f;
        return out;
    }

    int letterAt (float u) const
    {
        for (size_t i = 0; i < letters.size(); ++i)
            if (u >= letters[i].getStart() && u <= letters[i].getEnd()) return (int) i;
        return -1;
    }

    // Four chords spread across the word, left to right.
    int chordAt (float u) const
    {
        const int l = letterAt (u);
        return l < 0 ? -1 : std::min (3, l * 4 / (int) letters.size());
    }

    // Nearest position that lands well inside a letter.
    float snap (float u) const
    {
        float best = u, bestD = 1e9f;
        for (auto& l : letters)
        {
            const float pad = l.getLength() * 0.25f;
            const float c = juce::jlimit (l.getStart() + pad, l.getEnd() - pad, u);
            if (std::abs (c - u) < bestD) { bestD = std::abs (c - u); best = c; }
        }
        return best;
    }

    void profileAt (float u, float* out) const
    {
        if (u < 0.0f || u > 1.0f) { std::fill_n (out, kPartials, 0.0f); return; }
        const float r = 511.0f * u;
        const int a = (int) r, b = std::min (511, a + 1);
        const float f = r - (float) a;
        for (int i = 0; i < kPartials; ++i)
            out[i] = mixf (profile[(size_t) (a * kPartials + i)], profile[(size_t) (b * kPartials + i)], f);
    }

private:
    std::array<float, 512 * kPartials> profile {};
};
} // namespace lib
