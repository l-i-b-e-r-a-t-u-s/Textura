#include "PluginEditor.h"

using juce::Colour;
using juce::Point;
using juce::Rectangle;
using lib::mixf;
using lib::sstep;

namespace
{
// Chords for the grain voice (semitones above D3), one per quarter of the word.
const int grainChords[4][8] = { { 0, 7, 11, 14, 16, 19, 23, 26 }, { -3, 4, 9, 11, 12, 16, 21, 23 },
                                { -7, 0, 4, 9, 11, 16, 19, 23 }, { -5, 2, 4, 9, 11, 14, 16, 21 } };
// Keys voice (above F3).
const int keyChords[4][8] = { { 0, 4, 7, 11, 14, 16, 19, 23 }, { -1, 2, 6, 9, 11, 14, 18, 21 },
                              { -3, 0, 4, 7, 11, 12, 16, 19 }, { -7, 0, 4, 9, 11, 12, 16, 21 } };
// Bell voice (above A3): one scale, no chords.
const int bellScale[15] = { 0, 3, 5, 7, 10, 12, 15, 17, 19, 22, 24, 27, 29, 31, 34 };

const char* const knobIds[4] = { "wave", "prism", "depth", "drift" };
const char* const knobNames[4] = { "WAVE", "PRISM", "DEPTH", "DRIFT" };
const float knobDefaults[4] = { 0.55f, 0.5f, 0.45f, 0.3f };

juce::Font mono (float h, float tracking = 0.12f)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain)).withExtraKerningFactor (tracking);
}

void text (juce::Graphics& g, const juce::String& s, float x, float y, float w, juce::Justification j, Colour c, float size = 10.0f)
{
    g.setColour (c);
    g.setFont (mono (size));
    g.drawText (s, Rectangle<float> (x, y - size, w, size * 2.0f), j, false);
}

float textWidth (const juce::String& s, float size = 10.0f)
{
    return juce::GlyphArrangement::getStringWidth (mono (size), s);
}

Point<float> softLimit (Point<float> p, float limit)
{
    const float l = p.getDistanceFromOrigin();
    return l > 1e-4f ? p * (limit * std::tanh (l / limit) / l) : p;
}

// Progress along a CSS-style timing curve whose handles sit at (x1, y1) and (x2, y2).
float curve (float n, float x1, float y1, float x2, float y2)
{
    if (n <= 0.0f) return 0.0f;
    if (n >= 1.0f) return 1.0f;
    float t = n;
    for (int i = 0; i < 8; ++i)
    {
        const float s = 1.0f - t;
        const float x = 3.0f * s * s * t * x1 + 3.0f * s * t * t * x2 + t * t * t;
        const float dx = 3.0f * s * s * x1 + 6.0f * s * t * (x2 - x1) + 3.0f * t * t * (1.0f - x2);
        if (std::abs (dx) < 1e-6f) break;
        t = juce::jlimit (0.0f, 1.0f, t - (x - n) / dx);
    }
    const float s = 1.0f - t;
    return 3.0f * s * s * t * y1 + 3.0f * s * t * t * y2 + t * t * t;
}
float easeOut (float n) { return curve (n, 0.16f, 1.0f, 0.3f, 1.0f); }
float easeInOut (float n) { return curve (n, 0.65f, 0.0f, 0.35f, 1.0f); }

// The look the typed code throws over the page: everything collapses to one amber phosphor
// tone. Each entry is the colour for one brightness; tear gives the inverted band's colours.
struct Tone { float r, g, b; };
Tone spinHue (Tone c, float degrees)
{
    const float a = juce::degreesToRadians (degrees), k = std::cos (a), n = std::sin (a);
    return { c.r * (0.213f + k * 0.787f - n * 0.213f) + c.g * (0.715f - k * 0.715f - n * 0.715f) + c.b * (0.072f - k * 0.072f + n * 0.928f),
             c.r * (0.213f - k * 0.213f + n * 0.143f) + c.g * (0.715f + k * 0.285f + n * 0.140f) + c.b * (0.072f - k * 0.072f - n * 0.283f),
             c.r * (0.213f - k * 0.213f - n * 0.787f) + c.g * (0.715f - k * 0.715f + n * 0.715f) + c.b * (0.072f + k * 0.928f + n * 0.072f) };
}
Tone soak (Tone c, float amount)
{
    const float l = 0.213f * c.r + 0.715f * c.g + 0.072f * c.b;
    return { l + (c.r - l) * amount, l + (c.g - l) * amount, l + (c.b - l) * amount };
}
Tone clip (Tone c) { return { juce::jlimit (0.0f, 1.0f, c.r), juce::jlimit (0.0f, 1.0f, c.g), juce::jlimit (0.0f, 1.0f, c.b) }; }

std::array<juce::PixelARGB, 256> codeTones (bool lightPage, bool tear)
{
    std::array<juce::PixelARGB, 256> out;
    for (int i = 0; i < 256; ++i)
    {
        float l = (float) i / 255.0f;
        if (lightPage) l = 1.0f - l;
        Tone c = clip ({ 1.351f * l, 1.203f * l, 0.937f * l });
        c = clip (soak (c, 5.0f));
        const float dim = lightPage ? 0.95f : 1.0f;
        c = clip ({ (c.r * dim - 0.5f) * 1.3f + 0.5f, (c.g * dim - 0.5f) * 1.3f + 0.5f, (c.b * dim - 0.5f) * 1.3f + 0.5f });
        if (tear) c = clip (soak (clip (spinHue ({ 1.0f - c.r, 1.0f - c.g, 1.0f - c.b }, 120.0f)), 3.0f));
        out[(size_t) i] = juce::PixelARGB (255, (juce::uint8) juce::roundToInt (c.r * 255.0f), (juce::uint8) juce::roundToInt (c.g * 255.0f), (juce::uint8) juce::roundToInt (c.b * 255.0f));
    }
    return out;
}

// The creature the typed code puts in the word's place, as ink: its outline with the face cut
// out of it, traced from the picture in assets. One character is one square.
constexpr int creatureCols = 46, creatureRows = 56;
juce::Path figure()
{
    static const char* const rows[creatureRows] = {
        "..............................................",
        "................###...........................",
        "...............#######........................",
        "...............########.......................",
        "................###..##.......................",
        "......................#.......................",
        "......................#.......................",
        "......................#.......................",
        "......................#.......................",
        "......................#.......................",
        "..................##########..................",
        "................###############...............",
        "..............##################..............",
        "............######################............",
        "...........########################...........",
        "..........##########################..........",
        ".........############################.........",
        "........##############################........",
        "........##############################........",
        ".......####.....#######################.......",
        ".......####....#.######################.......",
        "......####........######################......",
        "......####.........#####################......",
        ".....#####.##..#.#.###############..####......",
        ".....#####.##....#.#############....#####.....",
        ".....#####.##.......###########...#######.....",
        ".....########..........######....########.....",
        ".....#####.##.........######...##########.....",
        ".....#####.#........######....############....",
        ".....#####........#.#####..............###....",
        "....######........#.######################....",
        "....#####.........########################....",
        "....####..##......########################....",
        "....#########.....########################....",
        ".....####.####....#################.######....",
        "...#.####.##.###..##############.##.######....",
        "...#.####.......#.###########.......#####.#...",
        "..##.#####......#...##..#####.....#######.##..",
        "..#########..#.###...#..#..###.#..#######.##..",
        "..###.############........##################..",
        ".####.############....##.###############.####.",
        ".#######################.####################.",
        ".#####.################################.#####.",
        ".#####################################.######.",
        ".######.##############################.######.",
        ".######..###########################...######.",
        ".#####....#########################.....#####.",
        ".###......#..###################..#......####.",
        "..........####..#############...###...........",
        "..........#########........########...........",
        "...........########.......#########...........",
        "...........#######.........#######............",
        "...........#######.........#######............",
        "............#####...........#####.............",
        ".............###.............####.............",
        ".............................................." };
    juce::Path p;
    for (int y = 0; y < creatureRows; ++y)
        for (int x = 0; rows[y][x] != 0; ++x)
        {
            if (rows[y][x] != '#') continue;
            int end = x;
            while (rows[y][end] == '#') ++end;
            p.addRectangle ((float) x, (float) y, (float) (end - x), 1.0f);
            x = end - 1;
        }
    return p;
}

// The boot screen plays once per run of the plugin; later windows open straight into the wipe.
bool bootedOnce = false;

juce::String noteName (float hz)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int midi = juce::roundToInt (69.0f + 12.0f * std::log2 (hz / 440.0f));
    return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
}
}

//==============================================================================
LiberatusEditor::LiberatusEditor (LiberatusProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      vblank (this, [this]
      {
          // sixty frames a second however fast the display refreshes, thirty when nothing is going on
          const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
          if (lastFrame <= 0.0) lastFrame = now;
          const bool idle = ! ptrInside && ! koi && codeAt < 0.0f && strikeE < 0.01f && proc.level.load() < 0.02f && time > sweepStart + sweepDur + 1.0f;
          if (now - lastFrame < (idle ? 1.0 / 30.0 : 1.0 / 60.0) - 0.002) return;
          advance ((float) (now - lastFrame));
          lastFrame = now;
      })
{
    startedAt = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (bootedOnce) { bootHold = 0.0f; bootFade = 0.0f; sweepStart = 0.12f; sweepDur = 1.0f; uiIn = 0.22f; }
    else            { sweepStart = 1.0f; sweepDur = 1.35f; }
    bootedOnce = true;
    k = proc.knobs();
    setWantsKeyboardFocus (true);

    wordBox.setText (proc.getWord(), false);
    wordBox.setJustification (juce::Justification::centredLeft);
    wordBox.setInputRestrictions (16);
    wordBox.setFont (mono (15.0f, 0.06f));
    wordBox.setIndents (10, 0);
    auto apply = [this]
    {
        if (wordBox.getText().trim() != proc.getWord())
        {
            proc.setWord (wordBox.getText().trim());
            rebuildWord();
            uiSound (lib::tickSend);
        }
    };
    wordBox.onTextChange = [this]
    {
        const auto t = wordBox.getText();
        uiSound (lib::tickType, t.isEmpty() ? -1.0f : (float) ((int) t.getLastCharacter() % 24) / 23.0f);
    };
    wordBox.onReturnKey = [this, apply] { apply(); grabKeyboardFocus(); };
    wordBox.onFocusLost = apply;
    addAndMakeVisible (wordBox);

    mark.build (proc.getWord());
    setResizable (true, true);
    setResizeLimits (960, 560, 2600, 1600);
    setSize (proc.editorWidth, proc.editorHeight);
}

LiberatusEditor::~LiberatusEditor() = default;

LiberatusEditor::Palette LiberatusEditor::pal() const
{
    if (proc.darkTheme)
        return { Colour (0xff0c0c0d), Colour (0xffedede8), Colour (0xff8a8a85), Colour (0xff48484b), Colour (0xff2f5bff), Colour (0xffff5a1f),
                 Colour (0xff12c47a), Colour (0xff141416), Colour (0xff2a2a2b), Colour (0xff2a2a2e), Colour (0xff050506) };
    return { Colour (0xffebebe7), Colour (0xff0e0e0e), Colour (0xff66665f), Colour (0xffa3a39c), Colour (0xff2f5bff), Colour (0xffff5a1f),
             Colour (0xff12c47a), Colour (0xffe2e2dd), Colour (0xffcdcdc6), Colour (0xfff8f8f5), Colour (0xffc4c4be) };
}

//==============================================================================
void LiberatusEditor::resized()
{
    const float W = (float) getWidth(), H = (float) getHeight(), m = 40.0f;
    proc.editorWidth = getWidth();  proc.editorHeight = getHeight();

    deck = { m, H - 52.0f - 138.0f, 704.0f, 138.0f };
    closeBtn = Rectangle<float> (22.0f, 22.0f).withCentre ({ deck.getX() + 20.0f, deck.getY() + 18.0f });
    screen = { deck.getX() + 26.0f, deck.getY() + 34.0f, 206.0f, 90.0f };
    for (int i = 0; i < 4; ++i)
        keys[i] = { screen.getRight() + 12.0f + (float) (i % 2) * 46.0f, screen.getY() + 2.0f + (float) (i / 2) * 46.0f, 40.0f, 40.0f };
    for (int i = 0; i < 4; ++i)
        knobs[i] = Rectangle<float> (56.0f, 56.0f).withCentre ({ deck.getX() + 394.0f + (float) i * 82.0f, screen.getY() + 36.0f });

    const float sideW = std::min (430.0f, W - 2.0f * m - deck.getWidth() - 24.0f);
    sidePanel = sideW >= 280.0f ? Rectangle<float> (W - m - sideW, deck.getBottom() - 100.0f, sideW, 100.0f) : Rectangle<float>();
    strikeBtn = { sidePanel.getRight() - 22.0f - 150.0f, sidePanel.getY() + 24.0f, 150.0f, 52.0f };
    wordBox.setVisible (! sidePanel.isEmpty());
    wordBox.setBounds (Rectangle<float> (sidePanel.getX() + 22.0f, sidePanel.getY() + 36.0f, strikeBtn.getX() - sidePanel.getX() - 40.0f, 40.0f).toNearestInt());

    soundBtn = { W - m - 162.0f, 9.0f, 42.0f, 36.0f };
    badgeBtn = { m - 6.0f, 13.0f, 28.0f, 28.0f };
    nameBtn = { m + 24.0f, 15.0f, 94.0f, 24.0f };
    if (koi) pond.resize (W, H, rnd);
    themeBtn = { W - m - 62.0f, 19.0f, 36.0f, 16.0f };

    float lw = W - 88.0f, lh = lw / mark.aspect;
    const float top = 78.0f, bottom = deck.getY() - 56.0f;
    const float maxH = std::max (60.0f, bottom - top);
    if (lh > maxH) { lh = maxH; lw = lh * mark.aspect; }
    logo = Rectangle<float> (lw, lh).withCentre ({ W * 0.5f, (top + bottom) * 0.5f });

    membrane.resize (W, H);
    camD = H * 0.5f / std::tan (juce::degreesToRadians (16.0f));
    cam = { W * 0.5f, H * 0.5f };
    stepValid = false;
    buildColumns();
    layoutDots();
    buildFilm();

    const auto p = pal();
    wordBox.setColour (juce::TextEditor::backgroundColourId, p.cap);
    wordBox.setColour (juce::TextEditor::outlineColourId, p.panelEdge);
    wordBox.setColour (juce::TextEditor::focusedOutlineColourId, p.orange);
    wordBox.setColour (juce::TextEditor::textColourId, p.ink);
    wordBox.setColour (juce::TextEditor::highlightColourId, p.orange.withAlpha (0.35f));
    wordBox.setColour (juce::CaretComponent::caretColourId, p.ink);
    wordBox.applyColourToAllText (p.ink);
}

void LiberatusEditor::rebuildWord()
{
    mark.build (proc.getWord());
    resized();
}

// Swaps the word for the short stand-in mark and back. The box does not move.
void LiberatusEditor::swapMark (bool standIn)
{
    mark.build (proc.getWord(), standIn ? figure() : juce::Path());
    buildColumns();
    layoutDots();
}

// Cut the word into columns: for each column, the vertical runs of ink, with a vertex every
// cell so the membrane can bend them.
void LiberatusEditor::buildColumns()
{
    float s = 2.0f;
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) s = (float) d->scale;
    scaleSeen = s;
    const int pitchDev = std::max (2, juce::roundToInt (juce::jlimit (2.4f, 3.4f, logo.getWidth() / 455.0f) * s));
    const int cols = std::max (24, juce::roundToInt (logo.getWidth() * s / (float) pitchDev));
    const int rows = std::max (6, juce::roundToInt (logo.getHeight() * s / (float) pitchDev));
    pitch = logo.getWidth() / (float) cols;
    inkW = std::max (1.0f, std::round (0.47f * (float) pitchDev + 0.2f)) / s;

    restTiles.clear();
    const int n = rows * 8, rw = cols * 4;
    const auto cover = mark.raster (rw, n);
    columns.assign ((size_t) cols, {});
    std::vector<float> h ((size_t) n);
    for (int c = 0; c < cols; ++c)
    {
        for (int y = 0; y < n; ++y)
        {
            float a = 0;
            for (int x = 0; x < 4; ++x) a += cover[(size_t) (y * rw + c * 4 + x)];
            h[(size_t) y] = a * 0.25f;
        }
        int d = 0;
        while (d < n)
        {
            if (h[(size_t) d] <= 0.03f) { ++d; continue; }
            const int a = d;
            while (d < n && h[(size_t) d] > 0.03f) ++d;
            const int b = d;
            // sub-sample ends, so curves do not stair-step
            const float peakA = b - a > 1 ? std::max (h[(size_t) a], h[(size_t) a + 1]) : h[(size_t) a];
            const float peakB = b - a > 1 ? std::max (h[(size_t) b - 1], h[(size_t) b - 2]) : h[(size_t) b - 1];
            const float top = (float) a + 1.0f - std::min (1.0f, h[(size_t) a] / std::max (peakA, 1e-6f));
            const float bot = (float) b - 1.0f + std::min (1.0f, h[(size_t) b - 1] / std::max (peakB, 1e-6f));
            if (bot - top < 0.08f) continue;

            Run run;
            float at = top;
            run.y.push_back (top / (float) n);
            while (at < bot)
            {
                const float next = std::min (bot, (std::floor (at / 8.0f) + 1.0f) * 8.0f);
                float peak = 0;
                for (int y = (int) std::floor (at); y < (int) std::ceil (next) && y < n; ++y) peak = std::max (peak, h[(size_t) y]);
                run.cov.push_back (std::min (1.0f, peak));
                run.y.push_back (next / (float) n);
                at = next;
            }
            if (*std::max_element (run.cov.begin(), run.cov.end()) >= 0.12f)
                columns[(size_t) c].push_back (std::move (run));
        }
    }
}

void LiberatusEditor::layoutDots()
{
    const float W = (float) getWidth(), H = (float) getHeight(), step = 24.0f;
    const int cols = 2 * (int) std::ceil (W / step / 2.0f) + 3, rows = 2 * (int) std::ceil (H / step / 2.0f) + 3;
    const float x0 = W * 0.5f - (float) (cols - 1) * 0.5f * step, y0 = H * 0.5f - (float) (rows - 1) * 0.5f * step;
    dotSize = std::round (std::max (1.5f, 1.4f * scaleSeen)) / scaleSeen;

    const int mw = 384, mh = 72;
    const auto mask = mark.raster (mw, mh);
    auto cover = [&] (float x, float y)
    {
        const float fx = (x - logo.getX()) / logo.getWidth(), fy = (y - logo.getY()) / logo.getHeight();
        if (fx < 0.0f || fx >= 1.0f || fy < 0.0f || fy >= 1.0f) return 0.0f;
        return mask[(size_t) ((int) (fy * (float) mh) * mw + (int) (fx * (float) mw))];
    };
    dots.clear();
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
        {
            const float x = x0 + (float) c * step, y = y0 + (float) r * step;
            const float cv = std::max ({ cover (x, y), cover (x + 7, y), cover (x - 7, y), cover (x, y + 7), cover (x, y - 7) });
            dots.push_back ({ x, y, 1.0f - sstep (0.08f, 0.3f, cv) });
        }
}

// Vignette and film grain, baked into the page colour once so every frame starts from one
// plain copy instead of blending a layer over the top.
void LiberatusEditor::buildFilm()
{
    const float sc = juce::jlimit (1.0f, 2.0f, scaleSeen);
    const int W = juce::roundToInt ((float) getWidth() * sc), H = juce::roundToInt ((float) getHeight() * sc);
    if (W < 2 || H < 2) return;
    const bool dark = proc.darkTheme;
    const auto bg = pal().bg;
    juce::Image page (juce::Image::RGB, W, H, false, juce::SoftwareImageType());
    juce::Random r (7717);
    {
        const juce::Image::BitmapData bd (page, juce::Image::BitmapData::writeOnly);
        const float edge = dark ? 0.32f : 0.05f, from = dark ? 0.55f : 0.58f, grain = dark ? 0.06f : 0.04f;
        for (int y = 0; y < H; ++y)
        {
            const float dy = ((float) y / (float) H - 0.42f) / 1.1f;
            for (int x = 0; x < W; ++x)
            {
                const float dx = ((float) x / (float) W - 0.5f) / 1.3f;
                const float keep = 1.0f - edge * juce::jlimit (0.0f, 1.0f, (std::sqrt (dx * dx + dy * dy) - from) / (1.0f - from));
                const float n = 0.5f * (r.nextFloat() + r.nextFloat());
                // a light page takes the grain as shadow, a dark page as light
                auto tone = [&] (float c) { return (juce::uint8) juce::roundToInt (255.0f * keep * (dark ? c + (1.0f - c) * grain * n : c * (1.0f - grain * (1.0f - n)))); };
                bd.setPixelColour (x, y, Colour (tone (bg.getFloatRed()), tone (bg.getFloatGreen()), tone (bg.getFloatBlue())));
            }
        }
    }
    film = juce::NativeImageType().convert (page);
}

//==============================================================================
float LiberatusEditor::knobValue (int i) const { return proc.apvts.getRawParameterValue (knobIds[i])->load(); }

void LiberatusEditor::setKnob (int i, float v)
{
    screenKnob = i;  knobTouchedAt = time;
    v = juce::jlimit (0.0f, 1.0f, v);
    if (juce::roundToInt (32.0f * v) != juce::roundToInt (32.0f * knobValue (i))) uiSound (lib::tickKnob, v);
    if (auto* p = proc.apvts.getParameter (knobIds[i])) p->setValueNotifyingHost (v);
}

void LiberatusEditor::uiSound (int kind, float value)
{
    if (! proc.soundOn() || kind < 0 || kind >= lib::numTickKinds) return;
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (now - lastTick[(size_t) kind] < (double) lib::tickGap[kind]) return;
    lastTick[(size_t) kind] = now;
    proc.pushEvent ({ 10 + kind, 0.0f, 0.0f, value });
}

void LiberatusEditor::startCode()
{
    if (codeAt >= 0.0f) return;
    codeAt = time;  codeStage = 0;  tear = -1.0f;
    codeGlimpse = figure();
    proc.pushEvent ({ 20, 0.0f, 0.0f, 0.0f });
}

void LiberatusEditor::setChoice (const char* id, int index)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
        p->endChangeGesture();
    }
}

int LiberatusEditor::knobAt (Point<float> p) const
{
    if (deckHidden) return -1;
    for (int i = 0; i < 4; ++i)
        if (knobs[i].expanded (8.0f, 8.0f).withBottom (knobs[i].getBottom() + 26.0f).contains (p)) return i;
    return -1;
}

void LiberatusEditor::setPointer (Point<float> p, bool inside)
{
    ptr = p;
    if (inside && ! ptrInside) lastPtr = p;
    ptrInside = inside;
    ptrMoved = ptrMoved || inside;
}

void LiberatusEditor::mouseMove (const juce::MouseEvent& e)
{
    lastInputAt = time;  ghost = false;
    setPointer (e.position, true);
    hotKnob = knobAt (e.position);
}

void LiberatusEditor::mouseExit (const juce::MouseEvent&) { ptrInside = false; hotKnob = -1; }

void LiberatusEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    lastInputAt = time;  ghost = false;
    setPointer (e.position, true);
    const auto p = e.position;

    // the badge opens the pond; the name switches a click between striking and feeding
    if (badgeBtn.contains (p))
    {
        uiSound (lib::tickSwitch);
        koi = ! koi;
        if (koi) pond.resize ((float) getWidth(), (float) getHeight(), rnd); else { pond.clear(); feedMode = false; }
        return;
    }
    if (nameBtn.contains (p))
    {
        uiSound (lib::tickSwitch);
        feedMode = ! feedMode;
        if (feedMode && ! koi) { koi = true; pond.resize ((float) getWidth(), (float) getHeight(), rnd); }
        return;
    }

    if (closeBtn.expanded (4.0f).contains (p)) { deckHidden = ! deckHidden; uiSound (deckHidden ? lib::tickClose : lib::tickOpen); return; }
    if (soundBtn.contains (p)) { uiSound (lib::tickKey); setChoice ("sound", proc.soundOn() ? 0 : 1); return; }
    if (themeBtn.expanded (22.0f, 8.0f).contains (p)) { uiSound (lib::tickSwitch); proc.darkTheme = ! proc.darkTheme; resized(); return; }
    if (! sidePanel.isEmpty() && strikeBtn.contains (p)) { uiSound (lib::tickKey); downKey = 9; click (logo.getCentre()); return; }
    if (! deckHidden)
    {
        for (int i = 0; i < 4; ++i)
            if (keys[i].contains (p))
            {
                downKey = i;
                uiSound (lib::tickKey);
                if (i < 3) setChoice ("voice", i); else setChoice ("fx", proc.delayFx() ? 0 : 1);
                return;
            }
        dragKnob = knobAt (p);
        if (dragKnob >= 0)
        {
            dragLast = p;
            if (auto* prm = proc.apvts.getParameter (knobIds[dragKnob])) prm->beginChangeGesture();
            return;
        }
        if (deck.contains (p)) return;
    }
    if (sidePanel.contains (p)) return;
    click (p);
}

void LiberatusEditor::click (Point<float> p)
{
    if (! feedMode) { strike (p, 1.0f); return; }
    pond.feed (p.x, p.y, time, rnd);
    uiSound (lib::tickType);
    pending.push_back ({ p.x, p.y, 26.0f * 0.3f, std::max (20.0f, 1.6f * membrane.cell()) });
}

void LiberatusEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragKnob >= 0)
    {
        const auto d = e.position - dragLast;
        dragLast = e.position;
        setKnob (dragKnob, knobValue (dragKnob) + (d.x - d.y) / 200.0f * (e.mods.isShiftDown() ? 0.25f : 1.0f));
        return;
    }
    setPointer (e.position, true);
}

void LiberatusEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragKnob >= 0)
        if (auto* prm = proc.apvts.getParameter (knobIds[dragKnob])) prm->endChangeGesture();
    dragKnob = -1;  downKey = -1;
}

void LiberatusEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int i = knobAt (e.position);
    if (i < 0) return;
    setKnob (i, knobDefaults[i]);
    uiSound (lib::tickKnob, knobDefaults[i]);
}

void LiberatusEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int i = knobAt (e.position);
    if (i >= 0) setKnob (i, knobValue (i) + w.deltaY * 0.35f);
}

bool LiberatusEditor::keyPressed (const juce::KeyPress& key)
{
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    const auto mods = key.getModifiers();

    // "liber" typed in a row, each letter within 0.9 s of the last, sets off the hidden code
    if (! mods.isCommandDown() && ! mods.isCtrlDown() && ! mods.isAltDown())
    {
        const auto now = juce::Time::getMillisecondCounter();
        const bool letter = c >= 'a' && c <= 'z';
        typed = ((now - typedAt < 900 ? typed : juce::String()) + (letter ? juce::String::charToString (c) : juce::String())).getLastCharacters (5);
        typedAt = now;
        if (typed == "liber") { typed.clear(); startCode(); return true; }
    }

    if (c == '1' || c == '2' || c == '3') { uiSound (lib::tickKey); setChoice ("voice", c - '1'); return true; }
    if (c == 'f') { uiSound (lib::tickKey); setChoice ("fx", proc.delayFx() ? 0 : 1); return true; }
    if (c == 's') { uiSound (lib::tickSwitch); setChoice ("sound", proc.soundOn() ? 0 : 1); return true; }
    if (c == 'k')
    {
        uiSound (lib::tickSwitch);
        koi = ! koi;
        if (koi) pond.resize ((float) getWidth(), (float) getHeight(), rnd); else { pond.clear(); feedMode = false; }
        return true;
    }
    if (c == 't')
    {
        uiSound (lib::tickSwitch);
        proc.darkTheme = ! proc.darkTheme;
        resized();
        return true;
    }
    lastInputAt = time;  ghost = false;
    if (key == juce::KeyPress::spaceKey) { click (ptrInside ? ptr : logo.getCentre()); return true; }
    return false;
}

//==============================================================================
void LiberatusEditor::send (int mode, float delay, int semis, float vel)
{
    if (! proc.soundOn()) return;
    proc.pushEvent ({ mode, std::max (0.0f, delay), (float) semis, juce::jlimit (0.0f, 1.0f, vel) });
    noteHz = lib::rootHz (mode) * std::pow (2.0f, (float) semis / 12.0f);
    noteAt = time + delay;
    lastGrainAt = time + delay;
}

void LiberatusEditor::grain (float delay, float vel, int index)
{
    const int mode = proc.voiceMode();
    if (mode == 2) { keysNote (delay, vel, index); return; }
    if (mode == 3) { bellNote (delay, vel, index); return; }
    const auto& s = grainChords[chord];
    int a = index >= 0 ? index : juce::roundToInt ((1.0f - v) * 7.0f + (rnd.nextFloat() - 0.5f) * (1.0f + 3.0f * k.drift));
    a = juce::jlimit (0, 7, a);
    const int octave = (index < 0 && rnd.nextFloat() < 0.08f + 0.12f * k.drift) ? 12 : 0;
    send (1, delay, s[a] + octave, vel);
}

void LiberatusEditor::keysNote (float delay, float vel, int index, int direct)
{
    const auto& s = keyChords[chord];
    int semis = direct;
    if (direct == -1000)
    {
        int e = index;
        if (e < 0)
        {
            arp += arpDir;
            if (arp >= 4 || arp <= 0) arpDir = -arpDir;
            e = juce::roundToInt ((1.0f - v) * 3.0f) + arp;
        }
        semis = s[juce::jlimit (0, 7, e)];
    }
    send (2, delay, semis, vel);
}

void LiberatusEditor::bellNote (float delay, float vel, int index)
{
    int s = index;
    if (s < 0)
    {
        const int e = juce::roundToInt ((1.0f - v) * 14.0f);
        bell += velX < 0.0f ? -1 : 1;
        if (std::abs (bell - e) > 3) bell = e + 3 * (bell > e ? 1 : -1);
        s = bell;
    }
    s = juce::jlimit (0, 14, s);
    send (3, delay, bellScale[s], vel);
    if (index < 0 && s + 2 < 15 && rnd.nextFloat() < 0.18f + 0.2f * k.drift)
        send (3, delay + 0.02f, bellScale[s + 2], 0.5f * vel);
}

void LiberatusEditor::strum (float at, float vel)
{
    const int mode = proc.voiceMode();
    const int c = mark.chordAt (at);
    if (mode != 3 && c >= 0) chord = c;
    if (mode == 2)
    {
        keysNote (0.005f, 0.5f * vel, -1, keyChords[chord][0] - 12);
        for (int t = 1; t < 7; ++t) keysNote (0.02f + 0.042f * (float) t, vel * (0.7f - 0.04f * (float) t), t);
    }
    else if (mode == 3)
    {
        const int start = std::max (0, juce::roundToInt ((1.0f - v) * 14.0f) - 3);
        for (int n = 0; n < 7; ++n) bellNote (0.005f + 0.036f * (float) n, vel * (0.75f - 0.04f * (float) n), start + n);
    }
    else
    {
        const int order[6] = { 0, 2, 3, 4, 5, 6 };
        for (int t = 0; t < 6; ++t)
            grain (0.005f + 0.032f * (float) t + std::max (0.0f, (rnd.nextFloat() - 0.5f) * 0.012f * k.drift),
                   vel * (t == 0 ? 0.55f : 0.75f - 0.06f * (float) t), order[t]);
    }
}

void LiberatusEditor::strike (Point<float> p, float strength)
{
    pending.push_back ({ p.x, p.y, 26.0f * strength, std::max (20.0f, 1.6f * membrane.cell()) });
    if (koi) pond.strike (p.x, p.y, rnd);
    const float d = p.getDistanceFrom (ptr);
    strikeE = std::min (1.5f, strikeE + strength * std::exp (-(d * d) / 135200.0f));

    const float tx = (p.x - logo.getX()) / logo.getWidth(), ty = (p.y - logo.getY()) / logo.getHeight();
    const bool inside = tx >= 0.0f && tx <= 1.0f && ty >= 0.0f && ty <= 1.0f;
    float at = lib::clamp01 (tx), reach = 1.0f;
    std::array<float, lib::kPartials> ink {};
    mark.profileAt (at, ink.data());
    float inked = 0;
    for (auto x : ink) inked += x;
    if (! inside || inked < 0.5f) at = mark.snap (at);
    if (! inside)
    {
        const float dx = tx < 0.0f ? -tx * logo.getWidth() : (tx > 1.0f ? (tx - 1.0f) * logo.getWidth() : 0.0f);
        const float dy = ty < 0.0f ? -ty * logo.getHeight() : (ty > 1.0f ? (ty - 1.0f) * logo.getHeight() : 0.0f);
        reach = 0.5f + 0.5f * std::exp (-std::hypot (dx, dy) / std::max (1.0f, logo.getHeight()));
    }
    else v = lib::clamp01 (ty);
    strum (at, strength * (0.55f + 0.45f * reach));
}

// Moving across the word sprays notes: distance travelled sets how often, speed sets how hard.
void LiberatusEditor::playPointer (float dt)
{
    const float f = std::min (1.0f, speed / 1400.0f);
    speedS += (f - speedS) * (1.0f - std::exp (-dt / (f > speedS ? 0.06f : 0.45f)));
    overLogo = ptrInside && dragKnob < 0 && logo.contains (ptr);
    if (overLogo)
    {
        u = lib::clamp01 ((ptr.x - logo.getX()) / logo.getWidth());
        v = lib::clamp01 ((ptr.y - logo.getY()) / logo.getHeight());
    }
    mark.profileAt (u, profile.data());

    if (! overLogo) { travel = 0; nextRestAt = time + 0.8f; return; }
    const int c = mark.chordAt (u);
    if (c >= 0) chord = c;
    const float every = 64.0f - 40.0f * k.drift;
    travel += speed * dt;
    for (int t = 0; travel >= every && t < 2; ++t)
    {
        travel -= every;
        if (time - lastGrainAt < 0.045f) { travel = 0; break; }
        grain (0.012f * (float) t, 0.3f + 0.7f * speedS);
    }
    if (speed < 40.0f)
    {
        if (time >= nextRestAt)
        {
            grain (0.0f, 0.16f + 0.1f * rnd.nextFloat());
            nextRestAt = time + 1.4f + 2.2f * rnd.nextFloat();
        }
    }
    else nextRestAt = std::max (nextRestAt, time + 0.9f);
}

void LiberatusEditor::simulate (float dt)
{
    const float W = (float) getWidth(), H = (float) getHeight();
    const float waveSpeed = mixf (300.0f, 640.0f, k.wave), ring = 0.2f * std::pow (16.0f, k.wave);
    const float o = membrane.stepSeconds (waveSpeed), cell = membrane.cell();
    acc += dt;
    int steps = (int) std::floor (acc / o);
    if (steps > 8) { steps = 8; acc = 0; } else acc -= (float) steps * o;

    // DRIFT: ambient rain
    for (float t = 7.0f * std::pow (k.drift, 1.5f) * dt; t > 0.0f; t -= 1.0f)
        if (rnd.nextFloat() < std::min (1.0f, t) && pending.size() < 24)
            pending.push_back ({ rnd.nextFloat() * W, rnd.nextFloat() * H, 2.2f * (0.45f + 0.55f * rnd.nextFloat()), std::max (8.0f, 1.6f * cell) });

    const bool active = ptrMoved && ptrInside;
    if (active) { if (! stepValid) { stepPtr = ptr; stepValid = true; } } else stepValid = false;
    const float push = active ? 8.0f : 0.0f, wake = active ? 0.55f * std::min (speed, 3600.0f) : 0.0f;
    const float sigma = std::max (16.0f, 1.6f * cell);

    // the intro wipe drags a wake across the word as it reveals it
    const float sn = (time - sweepStart) / sweepDur;
    const bool sweeping = sn > 0.0f && sn < 1.0f;
    const float frontX = logo.getX() + sweepProgress (sn) * logo.getWidth();
    const float frontAmp = sweeping ? 0.014f * (sweepProgress (sn + 0.004f) - sweepProgress (sn - 0.004f)) / 0.008f * logo.getWidth() / sweepDur : 0.0f;

    for (int e = 0; e < steps; ++e)
    {
        const float t0 = (float) e / (float) steps, t1 = (float) (e + 1) / (float) steps;
        phase = std::fmod (phase + 2.0f * lib::kPi * 7.0f * o, 2.0f * lib::kPi);
        const lib::Membrane::LineSource lines[2] = {
            { { stepPtr + (ptr - stepPtr) * t0, stepPtr + (ptr - stepPtr) * t1 }, (push + wake * std::sin (phase)) * o, sigma },
            { { frontX, logo.getY() + 0.1f * logo.getHeight(), frontX, logo.getY() + 0.95f * logo.getHeight() },
              frontAmp * std::sin (phase) * o, std::max (10.0f, 1.6f * cell) } };
        const int n = e == 0 ? std::min (8, (int) pending.size()) : 0;
        membrane.step (o, waveSpeed, ring, lines, 2, pending.data(), n);
        if (n > 0) pending.erase (pending.begin(), pending.begin() + n);
    }
    if (steps > 0 && active) stepPtr = ptr;
    membrane.settle (juce::jlimit (0.0f, 1.0f, acc / o));
}

void LiberatusEditor::advance (float dt)
{
    dt = juce::jlimit (0.001f, 0.1f, dt);
    time += dt;

    const auto target = proc.knobs();
    const float a = 1.0f - std::exp (-10.0f * dt);
    k.wave += (target.wave - k.wave) * a;    k.prism += (target.prism - k.prism) * a;
    k.depth += (target.depth - k.depth) * a; k.drift += (target.drift - k.drift) * a;

    // left alone for twenty seconds, the standalone plays itself (never inside a host)
    if (juce::JUCEApplicationBase::isStandaloneApp() && time - lastInputAt > 20.0f && time > sweepStart + sweepDur)
    {
        const float f = time - lastInputAt - 20.0f;
        ghost = true;
        setPointer ({ logo.getX() + logo.getWidth() * (0.5f + 0.47f * std::sin (0.21f * f + 0.6f * std::sin (0.05f * f))),
                      logo.getY() + logo.getHeight() * (0.5f + 0.42f * std::sin (0.53f * f + 1.1f)) }, true);
    }

    // the typed code: the mark swaps to its stand-in with a full strike, then back with a softer one
    if (codeAt >= 0.0f)
    {
        const float h = time - codeAt;
        if (h >= 1.15f && codeStage == 0) { codeStage = 1; swapMark (true); strike (logo.getCentre(), 1.0f); }
        if (h >= 3.75f && codeStage == 1) { codeStage = 2; swapMark (false); strike (logo.getCentre(), 0.6f); }
        static const float bursts[6][2] = { { 0.0f, 0.26f }, { 0.62f, 0.16f }, { 1.15f, 0.34f }, { 2.38f, 0.18f }, { 3.28f, 0.22f }, { 3.85f, 0.3f } };
        bool bursting = false;
        for (auto& b : bursts) bursting = bursting || (h >= b[0] && h < b[0] + b[1]);
        if (! bursting) tear = -1.0f;
        else if (tear < 0.0f || time - tearAt >= 0.06f) { tear = rnd.nextFloat(); tearAt = time; }
        if (h >= 4.6f) { codeAt = -1.0f; tear = -1.0f; }
    }

    const float risen = easeOut ((time - uiIn) / 1.0f);
    if (risen != uiShown) { uiShown = risen; wordBox.setAlpha (risen); }

    if (dragKnob >= 0) { screenKnob = dragKnob;  knobTouchedAt = time; }
    const float knobWant = time - knobTouchedAt < 0.9f ? 1.0f : 0.0f;
    knobMix = knobWant > knobMix ? std::min (knobWant, knobMix + dt / 0.15f) : std::max (knobWant, knobMix - dt / 0.15f);

    const bool soundNow = proc.soundOn();
    if (soundNow && ! soundWas && time > 0.2f) strum (mark.snap (0.5f), 0.3f);
    soundWas = soundNow;

    const auto moved = ptr - lastPtr;
    lastPtr = ptr;
    const float inst = ptrInside ? moved.getDistanceFromOrigin() / dt : 0.0f;
    speed += (inst - speed) * (1.0f - std::exp (-dt / 0.024f));
    if (std::abs (moved.x) > 0.01f) velX = moved.x;

    const bool active = ptrMoved && ptrInside;
    presence += ((active ? 1.0f : 0.0f) - presence) * (1.0f - std::exp (-dt * (active ? 9.0f : 3.0f)));

    // DEPTH: the camera leans a little toward the pointer
    const float W = (float) getWidth(), H = (float) getHeight();
    const float lean = mixf (0.015f, 0.07f, k.depth) * presence;
    const Point<float> aim (W * 0.5f + juce::jlimit (-1.0f, 1.0f, ptr.x / W * 2.0f - 1.0f) * lean * camD,
                            H * 0.5f + juce::jlimit (-1.0f, 1.0f, ptr.y / H * 2.0f - 1.0f) * lean * camD);
    cam += (aim - cam) * (1.0f - std::exp (-dt * 2.4f));

    if (koi)
        for (auto at : pond.update (dt, time, { ptrMoved && ptrInside, ptr.x, ptr.y, speed }, rnd))
            if (pending.size() < 24) pending.push_back ({ at.x, at.y, 26.0f * 0.1f, std::max (20.0f, 1.6f * membrane.cell()) });

    simulate (dt);
    playPointer (dt);

    const int serial = proc.noteSerial.load();
    if (serial != midiSeen) { midiSeen = serial; noteHz = proc.lastNoteHz.load(); noteAt = time; }

    // the screen: the word's ink at the pointer, or a slow idle scan when nobody is playing
    std::array<float, lib::kPartials> want {};
    const float lvl = proc.level.load();
    if (overLogo) { for (int i = 0; i < lib::kPartials; ++i) want[(size_t) i] = profile[(size_t) i] * std::min (1.0f, 0.12f + speedS + 0.6f * lvl); }
    else
    {
        mark.profileAt (0.5f + 0.48f * std::sin (0.11f * time), want.data());
        for (auto& x : want) x *= 0.06f + 0.8f * lvl;
    }
    const float rate = 1.0f - std::exp (-dt / (overLogo ? 0.035f : 0.28f));
    for (int i = 0; i < lib::kPartials; ++i) shown[(size_t) i] += (want[(size_t) i] - shown[(size_t) i]) * rate;

    strikeE *= std::exp (-dt / std::max (0.3f, 0.2f * std::pow (16.0f, k.wave)));
    fieldE = juce::jlimit (0.0f, 1.0f, std::max (presence * (1.0f - std::exp (-speed / 1400.0f)), strikeE) + (overLogo ? 0.08f : 0.0f));
    repaint();
}

//==============================================================================
// The wipe eases in and out (a cubic bezier with handles at 0.65 and 0.35).
float LiberatusEditor::sweepProgress (float n) const
{
    if (n <= 0.0f) return 0.0f;
    if (n >= 1.0f) return 1.0f;
    float t = n;
    for (int i = 0; i < 6; ++i)
    {
        const float s = 1.0f - t;
        const float x = 3.0f * s * s * t * 0.65f + 3.0f * s * t * t * 0.35f + t * t * t;
        const float dx = 3.0f * s * s * 0.65f + 6.0f * s * t * (0.35f - 0.65f) + 3.0f * t * t * 0.65f;
        if (std::abs (dx) < 1e-6f) break;
        t = juce::jlimit (0.0f, 1.0f, t - (x - n) / dx);
    }
    return t * t * (3.0f - 2.0f * t);
}

// Seconds since the wipe passed x; negative before it arrives.
float LiberatusEditor::sweepAge (float x) const
{
    const float u = juce::jlimit (0.0f, 1.0f, (x - logo.getX()) / logo.getWidth());
    const float t = 0.5f - std::sin (std::asin (juce::jlimit (-1.0f, 1.0f, 1.0f - 2.0f * u)) / 3.0f), s = 1.0f - t;
    return time - (sweepStart + sweepDur * (3.0f * s * s * t * 0.65f + 3.0f * s * t * t * 0.35f + t * t * t));
}

Point<float> LiberatusEditor::project (Point<float> p, float z, float& scale) const
{
    scale = camD / (camD - z);
    return cam + (p - cam) * scale;
}

float LiberatusEditor::lensAt (Point<float> p) const
{
    const float r = juce::jlimit (110.0f, 170.0f, 0.11f * (float) getWidth());
    const auto d = (p - ptr) / r;
    const float f = std::max (0.0f, 1.0f - (d.x * d.x + d.y * d.y));
    return f * f * presence;
}

void LiberatusEditor::drawDots (juce::Graphics& g, const Palette& p)
{
    const float relief = mixf (0.35f, 2.4f, k.depth), lift = 18.0f * (0.4f + k.depth);
    const Point<float> centre ((float) getWidth() * 0.5f, (float) getHeight() * 0.5f);
    const float back = 160.0f, spread = (camD + back) / camD;
    juce::RectangleList<float> rest;
    const bool revealing = time < sweepStart + sweepDur + 0.5f;
    rest.ensureStorageAllocated ((int) dots.size());

    for (auto& d : dots)
    {
        const float reveal = revealing ? sstep (0.0f, 0.35f, sweepAge (d.x)) : 1.0f;
        if (d.show * reveal < 0.01f) continue;
        const Point<float> home (d.x, d.y);
        Point<float> q = home;
        float z = 0, e = 0, tint = 0;
        const float lens = lensAt (home);
        if (membrane.energyNear (d.x, d.y) > 0.02f || lens > 0.001f)
        {
            const auto m = membrane.sample (d.x, d.y);
            e = m.e;
            q -= softLimit (Point<float> (m.gx, m.gy) * 14.0f, 7.0f);
            q += (home - ptr) * (lens * 0.06f);
            z = 30.0f * std::tanh (m.h * relief / 30.0f) + lens * lift;
            tint = juce::jlimit (-1.0f, 1.0f, m.h * 0.12f) * sstep (0.1f, 0.7f, e);
        }
        float sc;
        const auto at = project (centre + (q - centre) * spread, z - back, sc);
        const float size = dotSize * (1.0f + 0.45f * e) * sc * spread;
        const auto r = Rectangle<float> (size, size).withCentre (at);
        if (e < 0.02f && d.show * reveal > 0.95f) { rest.addWithoutMerging (r); continue; }
        auto c = p.ink3.interpolatedWith (p.ink, 0.4f * e);
        c = c.interpolatedWith (tint > 0.0f ? p.orange : p.blue, std::abs (tint) * 0.75f);
        g.setColour (c.withAlpha (mixf (0.62f, 1.0f, 0.7f * e) * d.show * reveal));
        g.fillRect (r);
    }
    g.setColour (p.ink3.withAlpha (0.62f));
    g.fillRectList (rest);
}

void LiberatusEditor::drawMarks (juce::Graphics& g, const Palette& p)
{
    struct Vert { Point<float> c, off; float hw, hair, blue, orange; };
    static thread_local std::vector<Vert> vs;

    const float relief = mixf (0.35f, 2.4f, k.depth), prismGain = mixf (0.0f, 10.0f, k.prism), gapShare = mixf (0.1f, 1.0f, k.prism);
    const float lensR = juce::jlimit (110.0f, 170.0f, 0.11f * (float) getWidth());
    const float lift = 18.0f * (0.4f + k.depth), lensSplit = 4.8f * k.prism, shimmer = 1.6f * k.drift;
    const float yBase = logo.getY() + mark.baseline * logo.getHeight();
    const float hair = 0.5f * std::max (1.0f, std::floor (0.67f * inkW * scaleSeen + 0.5f)) / scaleSeen;
    const float thin = proc.darkTheme ? 0.45f : 0.3f;
    // One outline holding every column costs far more to fill than the same ink in small
    // pieces, so the ink is kept as two columns per piece.
    static thread_local std::vector<juce::Path> cores, blues, oranges;
    const size_t perPiece = 2, perTile = 8;
    for (auto* set : { &cores, &blues, &oranges })
    {
        set->resize ((columns.size() + perPiece - 1) / perPiece);
        for (auto& c : *set) c.clear();
    }
    const bool revealing = time < sweepStart + sweepDur + 1.0f;

    // Ink that is sitting still is drawn from tiles painted once, eight columns to a tile.
    // Only whole-pixel scales qualify, so a tile lands on exactly the pixels a fill would.
    const float px = (float) g.getInternalContext().getPhysicalPixelScaleFactor();
    // At whole-pixel scales the word is not handed over as outlines at all. Every piece of it
    // is a near-upright strip, so the strips are painted straight into a picture here and the
    // picture is drawn once. Other scales fall back to outlines.
    const bool painting = ! plainDraw && std::abs (px - std::round (px)) < 1e-3f && px >= 1.0f && px <= 3.0f;
    if (painting)
    {
        const auto box = logo.expanded (96.0f).getSmallestIntegerContainer().getIntersection (getLocalBounds());
        const int w = juce::roundToInt ((float) box.getWidth() * px), h = juce::roundToInt ((float) box.getHeight() * px);
        if (w != plate.w || h != plate.h || ! juce::approximatelyEqual (plate.scale, px) || plate.origin != box.getPosition())
        {
            plate.w = w;  plate.h = h;  plate.scale = px;  plate.origin = box.getPosition();
            for (auto* layer : { &plate.ink, &plate.blue, &plate.orange }) layer->assign ((size_t) (w * h), 0);
            plate.lo.assign ((size_t) h, w);  plate.hi.assign ((size_t) h, -1);
            for (int i = 0; i < 2; ++i)
            {
                plate.wasLo[i].assign ((size_t) h, w);  plate.wasHi[i].assign ((size_t) h, -1);
                plate.page[i] = juce::Image (juce::Image::ARGB, std::max (1, w), std::max (1, h), true, juce::SoftwareImageType());
            }
        }
    }
    const float plateX = (float) plate.origin.x, plateY = (float) plate.origin.y;
    auto strip = [&] (std::vector<juce::uint8>& layer, float xa, float wa, float ya, float xb, float wb, float yb)
    {
        xa = (xa - plateX) * px;  xb = (xb - plateX) * px;  ya = (ya - plateY) * px;  yb = (yb - plateY) * px;
        wa *= px;  wb *= px;
        if (yb < ya) { std::swap (xa, xb);  std::swap (ya, yb);  std::swap (wa, wb); }
        const float tall = yb - ya;
        if (tall < 1e-4f) return;
        const int r0 = std::max (0, (int) std::floor (ya)), r1 = std::min (plate.h - 1, (int) std::ceil (yb) - 1);
        for (int r = r0; r <= r1; ++r)
        {
            const float top = std::max (ya, (float) r), bottom = std::min (yb, (float) r + 1.0f), share = bottom - top;
            if (share <= 0.0f) continue;
            const float t = (0.5f * (top + bottom) - ya) / tall;
            const float mid = xa + (xb - xa) * t, half = wa + (wb - wa) * t, xl = mid - half, xr = mid + half;
            const int p0 = std::max (0, (int) std::floor (xl)), p1 = std::min (plate.w - 1, (int) std::ceil (xr) - 1);
            if (p1 < p0) continue;
            auto* row = layer.data() + (size_t) r * (size_t) plate.w;
            for (int x = p0; x <= p1; ++x)
            {
                const float across = std::min (xr, (float) x + 1.0f) - std::max (xl, (float) x);
                if (across > 0.0f) row[x] = (juce::uint8) std::min (255, (int) row[x] + (int) (255.0f * share * across + 0.5f));
            }
            plate.lo[(size_t) r] = std::min (plate.lo[(size_t) r], p0);
            plate.hi[(size_t) r] = std::max (plate.hi[(size_t) r], p1);
        }
    };

    const bool tiling = ! painting && ! revealing && ! inCodeShot && ! plainDraw && std::abs (px - std::round (px)) < 1e-3f && px >= 1.0f && px <= 3.0f;
    static thread_local std::vector<char> busy;
    busy.assign ((columns.size() + perTile - 1) / perTile, 0);
    if (tiling && (restTiles.size() != busy.size() || restScale != px || restInk != p.ink))
    {
        restScale = px;  restInk = p.ink;
        restTiles.clear();
        restTop = (int) std::floor (logo.getY()) - 1;
        const int high = (int) std::ceil (logo.getBottom()) + 1 - restTop;
        for (size_t t = 0; t < busy.size(); ++t)
        {
            const size_t end = std::min (columns.size(), (t + 1) * perTile);
            const int left = (int) std::floor (logo.getX() + (float) (t * perTile) * pitch) - 1;
            const int wide = (int) std::ceil (logo.getX() + (float) end * pitch) + 1 - left;
            juce::Image tile (juce::Image::ARGB, juce::roundToInt ((float) wide * px), juce::roundToInt ((float) high * px), true);
            juce::Graphics tg (tile);
            tg.addTransform (juce::AffineTransform::translation ((float) -left, (float) -restTop).scaled (px));
            tg.setColour (p.ink);
            for (size_t col = t * perTile; col < end; col += perPiece)
            {
                juce::Path piece;
                for (size_t c = col; c < std::min (end, col + perPiece); ++c)
                {
                    const float cx = logo.getX() + ((float) c + 0.5f) * pitch;
                    for (auto& run : columns[c])
                    {
                        const size_t n = run.y.size();
                        auto half = [&] (size_t i)
                        {
                            const float cov = i == 0 ? run.cov[0] : (i == n - 1 ? run.cov[n - 2] : std::max (run.cov[i - 1], run.cov[i]));
                            return 0.5f * inkW * mixf (0.4f, 1.0f, cov);
                        };
                        auto down = [&] (size_t i) { return logo.getY() + run.y[i] * logo.getHeight(); };
                        piece.startNewSubPath (cx - half (0), down (0));
                        for (size_t i = 1; i < n; ++i) piece.lineTo (cx - half (i), down (i));
                        for (size_t i = n; i-- > 0;) piece.lineTo (cx + half (i), down (i));
                        piece.closeSubPath();
                    }
                }
                if (! piece.isEmpty()) tg.fillPath (piece);
            }
            restTiles.push_back (tile);
            restLeft.resize (busy.size());
            restLeft[t] = left;
        }
    }

    for (size_t col = 0; col < columns.size(); ++col)
    {
        auto& core = cores[col / perPiece];
        auto& blue = blues[col / perPiece];
        auto& orange = oranges[col / perPiece];
        const float cx = logo.getX() + ((float) col + 0.5f) * pitch;
        // each column springs up from the baseline as the wipe passes, with a flash of colour
        float grow = 1.0f, flash = 0.0f;
        if (revealing)
        {
            const float age = sweepAge (cx);
            if (age <= 0.0f) continue;
            const float w = 46.0f, zeta = 0.4f, wd = w * std::sqrt (1.0f - zeta * zeta);
            grow = 1.0f - std::exp (-zeta * w * age) * (std::cos (wd * age) + (zeta * w / wd) * std::sin (wd * age));
            flash = std::exp (-age * 24.0f);
        }
        for (auto& run : columns[col])
        {
            const size_t n = run.y.size();
            vs.clear();
            bool coloured = false, still = true;
            for (size_t i = 0; i < n; ++i)
            {
                const float cov = i == 0 ? run.cov[0] : (i == n - 1 ? run.cov[n - 2] : std::max (run.cov[i - 1], run.cov[i]));
                const float halfW = 0.5f * inkW * mixf (0.4f, 1.0f, cov);
                const float y = yBase + (logo.getY() + run.y[i] * logo.getHeight() - yBase) * grow;
                const Point<float> home (cx, y);
                const auto m = membrane.sample (cx, y);
                const Point<float> slope (m.gx, m.gy);
                const float lens = lensAt (home);
                const auto d = home - ptr;

                Point<float> q (cx, yBase + (y - yBase) * (1.0f + m.h * 0.0045f));
                q -= softLimit (slope * 14.0f, 7.0f);
                q += d * (lens * 0.06f);
                q.x += std::sin (y * 0.09f + cx * 0.021f + time * 7.0f) * shimmer * sstep (0.15f, 0.95f, m.e);
                const float z = 30.0f * std::tanh (m.h * relief / 30.0f) + lens * lift;

                // PRISM: the ink splits down the slope into one hairline of colour per gap,
                // warm on crests and cool in troughs
                const auto split = slope * -prismGain + d * (lens * lensSplit / lensR) + Point<float> (3.0f * flash, 0.0f);
                const float sl = split.getDistanceFromOrigin();
                const float limit = halfW + hair + std::max (0.0f, pitch - 2.0f * (halfW + hair)) * gapShare;
                const float warm = sstep (-0.7f, 0.7f, m.h + lens * 4.0f);
                const float level = sstep (0.6f * halfW, halfW + 2.0f * hair, sl);
                float sc;
                const auto c = project (q, z, sc);
                vs.push_back ({ c, sl > 1e-4f ? split * (limit * std::tanh (sl / limit) / sl * sc) : Point<float>(),
                                halfW * sc * (1.0f - thin * level), hair * sc, 0.95f * std::max (1.0f - warm, flash) * level, 0.95f * std::max (warm, flash) * level });
                coloured = coloured || level > 0.02f;
                still = still && std::abs (c.x - cx) + std::abs (c.y - y) < 0.01f && std::abs (sc - 1.0f) < 2e-4f;
            }
            if (coloured || ! still) busy[col / perTile] = 1;

            if (painting)
            {
                for (size_t i = 0; i + 1 < n; ++i)
                {
                    const auto& a = vs[i];
                    const auto& b = vs[i + 1];
                    strip (plate.ink, a.c.x, a.hw, a.c.y, b.c.x, b.hw, b.c.y);
                    if (! coloured) continue;
                    if (a.blue + b.blue >= 0.04f)
                        strip (plate.blue, a.c.x - a.off.x, a.hair * a.blue, a.c.y - a.off.y, b.c.x - b.off.x, b.hair * b.blue, b.c.y - b.off.y);
                    if (a.orange + b.orange >= 0.04f)
                        strip (plate.orange, a.c.x + a.off.x, a.hair * a.orange, a.c.y + a.off.y, b.c.x + b.off.x, b.hair * b.orange, b.c.y + b.off.y);
                }
                continue;
            }
            core.startNewSubPath (vs[0].c.x - vs[0].hw, vs[0].c.y);
            for (size_t i = 1; i < n; ++i) core.lineTo (vs[i].c.x - vs[i].hw, vs[i].c.y);
            for (size_t i = n; i-- > 0;) core.lineTo (vs[i].c.x + vs[i].hw, vs[i].c.y);
            core.closeSubPath();

            if (! coloured) continue;
            for (size_t i = 0; i + 1 < n; ++i)
            {
                const auto& a = vs[i];
                const auto& b = vs[i + 1];
                auto quad = [&] (juce::Path& path, float sign, float wa, float wb)
                {
                    if (wa + wb < 0.04f) return;
                    const auto ca = a.c + a.off * sign, cb = b.c + b.off * sign;
                    path.startNewSubPath (ca.x - a.hair * wa, ca.y);
                    path.lineTo (cb.x - b.hair * wb, cb.y);
                    path.lineTo (cb.x + b.hair * wb, cb.y);
                    path.lineTo (ca.x + a.hair * wa, ca.y);
                    path.closeSubPath();
                };
                quad (blue, -1.0f, a.blue, b.blue);
                quad (orange, 1.0f, a.orange, b.orange);
            }
        }
    }
    if (painting && plate.w > 0 && plate.h > 0)
    {
        // blue, then orange over it, then ink over both, written into whichever of the two
        // pages was not the last one drawn
        auto& page = plate.page[plate.turn];
        auto& wasLo = plate.wasLo[plate.turn];
        auto& wasHi = plate.wasHi[plate.turn];
        plate.turn ^= 1;
        const juce::Colour tones[3] = { p.blue, p.orange, p.ink };
        // nearly every inked pixel holds ink alone, so that case is a table lookup
        juce::PixelARGB inkOnly[256];
        for (int c = 0; c < 256; ++c)
            inkOnly[c].setARGB ((juce::uint8) c, (juce::uint8) ((p.ink.getRed() * c + 127) / 255), (juce::uint8) ((p.ink.getGreen() * c + 127) / 255),
                                (juce::uint8) ((p.ink.getBlue() * c + 127) / 255));
        int left = plate.w, right = -1, top = plate.h, bottom = -1;
        {
            const juce::Image::BitmapData bd (page, juce::Image::BitmapData::readWrite);
            for (int r = 0; r < plate.h; ++r)
            {
                const int lo = plate.lo[(size_t) r], hi = plate.hi[(size_t) r];
                const int from = std::min (lo, wasLo[(size_t) r]), to = std::max (hi, wasHi[(size_t) r]);
                wasLo[(size_t) r] = lo;  wasHi[(size_t) r] = hi;
                plate.lo[(size_t) r] = plate.w;  plate.hi[(size_t) r] = -1;
                if (to < from) continue;
                left = std::min (left, from);  right = std::max (right, to);  top = std::min (top, r);  bottom = r;
                auto* out = (juce::PixelARGB*) bd.getLinePointer (r);
                const auto at = (size_t) r * (size_t) plate.w;
                juce::uint8* const layers[3] = { plate.blue.data() + at, plate.orange.data() + at, plate.ink.data() + at };
                for (int x = from; x <= to; ++x)
                {
                    // most of a row is bare page: step over it eight pixels at a time
                    if (x + 8 <= to + 1)
                    {
                        juce::uint64 any[3];
                        for (int l = 0; l < 3; ++l) std::memcpy (&any[l], layers[l] + x, 8);
                        if ((any[0] | any[1] | any[2]) == 0)
                        {
                            std::memset ((void*) (out + x), 0, 8 * sizeof (juce::PixelARGB));
                            x += 7;
                            continue;
                        }
                    }
                    if ((layers[0][x] | layers[1][x]) == 0)
                    {
                        out[x] = inkOnly[layers[2][x]];
                        layers[2][x] = 0;
                        continue;
                    }
                    int red = 0, green = 0, blu = 0, alpha = 0;
                    for (int l = 0; l < 3; ++l)
                    {
                        const int c = layers[l][x];
                        if (c == 0) continue;
                        layers[l][x] = 0;
                        const int keep = 255 - c;
                        red   = (tones[l].getRed() * c + red * keep + 127) / 255;
                        green = (tones[l].getGreen() * c + green * keep + 127) / 255;
                        blu   = (tones[l].getBlue() * c + blu * keep + 127) / 255;
                        alpha = (255 * c + alpha * keep + 127) / 255;
                    }
                    out[x].setARGB ((juce::uint8) alpha, (juce::uint8) red, (juce::uint8) green, (juce::uint8) blu);
                }
            }
        }
        if (right >= left)
        {
            const Rectangle<int> part (left, top, right - left + 1, bottom - top + 1);
            g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
            g.setOpacity (1.0f);
            g.drawImageTransformed (page.getClippedImage (part), juce::AffineTransform::translation ((float) part.getX(), (float) part.getY()).scaled (1.0f / px)
                                                                     .translated (plateX, plateY));
        }
    }
    g.setColour (p.blue);    for (auto& c : blues)   if (! c.isEmpty()) g.fillPath (c);
    g.setColour (p.orange);  for (auto& c : oranges) if (! c.isEmpty()) g.fillPath (c);
    g.setColour (p.ink);
    for (size_t i = 0; i < cores.size(); ++i)
        if (! cores[i].isEmpty() && ! (tiling && ! busy[i * perPiece / perTile])) g.fillPath (cores[i]);
    if (tiling)
    {
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.setOpacity (1.0f);
        for (size_t t = 0; t < busy.size(); ++t)
            if (! busy[t])
                g.drawImageTransformed (restTiles[t], juce::AffineTransform::scale (1.0f / px).translated ((float) restLeft[t], (float) restTop));
        if (benchTiles != nullptr) { benchTiles[0] += (int) busy.size(); for (auto b : busy) benchTiles[1] += b ? 0 : 1; }
    }

    // the wipe's playhead: one pixel of ink with a faint wake behind it
    const float sn = (time - sweepStart) / sweepDur;
    if (sn > 0.0f && sn < 1.0f)
    {
        const float a = std::min (1.0f, sn / 0.04f) * (1.0f - juce::jlimit (0.0f, 1.0f, (sn - 0.9f) / 0.1f));
        const float x = std::round (logo.getX() + sweepProgress (sn) * logo.getWidth());
        const auto line = Rectangle<float> (x - 64.0f, logo.getY() - 14.0f, 64.0f, logo.getHeight() + 28.0f);
        g.setGradientFill (juce::ColourGradient (p.ink.withAlpha (0.0f), line.getX(), 0, p.ink.withAlpha (0.045f * a), line.getRight(), 0, false));
        g.fillRect (line);
        g.setColour (p.ink.withAlpha (a));
        g.fillRect (x - 1.0f, line.getY(), 1.0f, line.getHeight());
    }
}

//==============================================================================
void LiberatusEditor::drawPanel (juce::Graphics& g, const Palette& p, Rectangle<float> r)
{
    // A panel never changes between resizes, so at whole-pixel scales it is painted once
    // and kept.
    const float px = (float) g.getInternalContext().getPhysicalPixelScaleFactor();
    if (! inPanelSkin && ! inCodeShot && ! plainDraw && std::abs (px - std::round (px)) < 1e-3f && px >= 1.0f && px <= 3.0f)
    {
        PanelSkin* skin = nullptr;
        for (auto& have : panelSkins) if (have.at == r && juce::approximatelyEqual (have.scale, px) && have.dark == proc.darkTheme) skin = &have;
        if (skin == nullptr)
        {
            if (panelSkins.size() >= 4) panelSkins.clear();
            const auto box = r.withBottom (r.getBottom() + 3.0f).getSmallestIntegerContainer().expanded (1);
            juce::Image img (juce::Image::ARGB, juce::roundToInt ((float) box.getWidth() * px), juce::roundToInt ((float) box.getHeight() * px), true);
            {
                juce::Graphics ig (img);
                ig.addTransform (juce::AffineTransform::translation ((float) -box.getX(), (float) -box.getY()).scaled (px));
                inPanelSkin = true;
                drawPanel (ig, p, r);
                inPanelSkin = false;
            }
            panelSkins.push_back ({ r, px, proc.darkTheme, box.getPosition(), img });
            skin = &panelSkins.back();
        }
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.setOpacity (1.0f);
        g.drawImageTransformed (skin->img, juce::AffineTransform::scale (1.0f / px).translated ((float) skin->origin.x, (float) skin->origin.y));
        return;
    }
    g.setColour (juce::Colours::black.withAlpha (proc.darkTheme ? 0.35f : 0.06f));
    g.fillRoundedRectangle (r.translated (0.0f, 3.0f), 14.0f);
    g.setColour (p.panel);
    g.fillRoundedRectangle (r, 14.0f);
    g.setColour (p.panelEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.0f);
    // a slotted screw in each corner, set into the panel, each turned its own way
    const bool dark = proc.darkTheme;
    const Point<float> at[4] = { r.getTopLeft() + Point<float> (10, 10), r.getTopRight() + Point<float> (-10, 10),
                                 r.getBottomLeft() + Point<float> (10, -10), r.getBottomRight() + Point<float> (-10, -10) };
    const float turn[4] = { 0.70f, -0.35f, 1.25f, 0.15f };
    for (int i = 0; i < 4; ++i)
    {
        const auto c = at[i];
        const auto head = Rectangle<float> (7.0f, 7.0f).withCentre (c);
        g.setColour (dark ? juce::Colours::black.withAlpha (0.55f) : juce::Colours::black.withAlpha (0.10f));
        g.fillEllipse (head.expanded (1.0f).translated (0.0f, -0.5f));        // the hole it sits in
        g.setColour (dark ? Colour (0xff3a3a3d).withAlpha (0.6f) : juce::Colours::white.withAlpha (0.5f));
        g.fillEllipse (head.expanded (1.0f).translated (0.0f, 0.6f));         // its lit lower lip
        g.setGradientFill (juce::ColourGradient (dark ? Colour (0xff5a5a5e) : Colour (0xfffbfbf9), c.x - 2.0f, c.y - 3.0f,
                                                 dark ? Colour (0xff2c2c2f) : Colour (0xffd6d6d0), c.x + 2.0f, c.y + 3.5f, false));
        g.fillEllipse (head);
        g.setColour (dark ? juce::Colours::black.withAlpha (0.5f) : Colour (0xffb4b4ad));
        g.drawEllipse (head.reduced (0.25f), 0.6f);
        const Point<float> d (std::cos (turn[i]) * 2.4f, std::sin (turn[i]) * 2.4f);
        g.setColour (dark ? juce::Colours::black.withAlpha (0.55f) : Colour (0xff6e6e68));
        g.drawLine ({ c - d, c + d }, 1.1f);
        g.setColour (dark ? juce::Colours::white.withAlpha (0.08f) : juce::Colours::white.withAlpha (0.7f));
        g.drawLine ({ c - d + Point<float> (0.0f, 0.7f), c + d + Point<float> (0.0f, 0.7f) }, 0.5f);
    }
}

void LiberatusEditor::drawLed (juce::Graphics& g, Point<float> c, Colour lit, float size)
{
    const auto dot = Rectangle<float> (size, size).withCentre (c);
    if (lit.isTransparent())
    {
        // off: a flat, quiet lamp, not a hole
        g.setColour (proc.darkTheme ? Colour (0xff3b413e) : Colour (0xffa9b3ad));
        g.fillEllipse (dot);
        return;
    }
    g.setColour (lit.withAlpha (0.18f));
    g.fillEllipse (dot.expanded (size * 0.45f));
    g.setColour (lit);
    g.fillEllipse (dot);
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.fillEllipse (Rectangle<float> (size * 0.35f, size * 0.35f).withCentre (c + Point<float> (-size * 0.15f, -size * 0.15f)));
}

void LiberatusEditor::drawKey (juce::Graphics& g, const Palette& p, Rectangle<float> r, const juce::String& label, bool lit, bool down)
{
    const float sink = down ? 2.0f : 0.0f;
    g.setColour (juce::Colours::black.withAlpha (proc.darkTheme ? 0.45f : 0.07f));
    g.fillRoundedRectangle (r.translated (0.0f, 5.0f).expanded (1.0f, 0.0f), 8.0f);   // soft shadow under the key
    g.setColour (p.capEdge);
    g.fillRoundedRectangle (r.translated (0.0f, 3.0f), 7.0f);
    const auto top = r.translated (0.0f, sink);
    g.setColour (p.cap);
    g.fillRoundedRectangle (top, 7.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (proc.darkTheme ? 0.04f : 0.5f), top.getX(), top.getY(),
                                             juce::Colours::white.withAlpha (0.0f), top.getX(), top.getY() + top.getHeight() * 0.6f, false));
    g.fillRoundedRectangle (top.reduced (1.0f), 6.0f);
    g.setColour (p.panelEdge);
    g.drawRoundedRectangle (top.reduced (0.5f), 7.0f, 1.0f);
    drawLed (g, top.getTopLeft() + Point<float> (8.5f, 8.5f), lit ? p.orange : Colour(), 4.5f);
    if (label.isNotEmpty())
        text (g, label, top.getX(), top.getCentreY() + 1.0f, top.getWidth(), juce::Justification::centred, p.ink, 12.0f);
}

void LiberatusEditor::drawKnob (juce::Graphics& g, const Palette& p, int i)
{
    const Colour tones[4] = { p.blue, p.green, proc.darkTheme ? Colour (0xffd9d9d4) : Colour (0xfff3f3ef), p.orange };
    const auto r = knobs[i];
    const auto c = r.getCentre();
    const float rad = r.getWidth() * 0.5f, val = knobValue (i);
    const bool hot = i == hotKnob || i == dragKnob;
    const Colour tick = i == 2 ? p.ink : tones[i];

    for (int t = 0; t <= 10; ++t)
    {
        const float a = juce::degreesToRadians (-135.0f + 27.0f * (float) t);
        const bool on = (float) t / 10.0f <= val + 0.001f;
        const Point<float> dir (std::sin (a), -std::cos (a));
        g.setColour (tick.withAlpha (on ? (hot ? 1.0f : 0.85f) : 0.25f));
        g.drawLine ({ c + dir * (rad + 5.0f), c + dir * (rad + (t % 5 == 0 ? 10.0f : 8.0f)) }, 1.2f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.22f));
    g.fillEllipse (r.translated (0.0f, 3.0f));
    g.setGradientFill (juce::ColourGradient (tones[i].brighter (0.35f), c.x - rad * 0.4f, c.y - rad * 0.6f,
                                             tones[i].darker (0.25f), c.x + rad * 0.5f, c.y + rad, true));
    g.fillEllipse (r);
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.drawEllipse (r.reduced (0.5f), 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.28f));
    g.drawEllipse (r.reduced (2.5f), 1.0f);

    const float a = juce::degreesToRadians (-135.0f + 270.0f * val);
    const Point<float> dir (std::sin (a), -std::cos (a));
    g.setColour (i == 2 ? Colour (0xff1a1a1a) : Colour (0xff101010).withAlpha (0.85f));
    g.drawLine ({ c + dir * (rad * 0.12f), c + dir * (rad * 0.86f) }, 1.8f);
    text (g, knobNames[i], c.x - 40.0f, r.getBottom() + 22.0f, 80.0f, juce::Justification::centred, hot ? p.ink : p.ink2, 9.5f);
}

// What the screen shows while a knob is being turned: its name and number, a small moving
// picture of what it does, a row of ticks, and a line saying what it drives.
void LiberatusEditor::drawKnobPage (juce::Graphics& g, const Palette& p)
{
    static const char* const drives[4] = { "RIPPLE SPEED + RING TIME", "SPECTRAL SPLIT", "Z RELIEF + PARALLAX TILT", "AMBIENT RAIN + JITTER" };
    const int i = juce::jlimit (0, 3, screenKnob);
    const float value = juce::jlimit (0.0f, 1.0f, knobValue (i)), W = screen.getWidth(), H = screen.getHeight();
    const float x0 = screen.getX(), y0 = screen.getY();
    const Colour ink (0xffe9e9e4), dim (0xff4a4a47), ghost = juce::Colours::white.withAlpha (0.07f), grid = juce::Colours::white.withAlpha (0.16f);
    const Colour blue = p.blue.interpolatedWith (juce::Colours::white, 0.22f);
    const Colour tone = i == 0 ? blue : (i == 1 ? p.green : (i == 3 ? p.orange : ink));
    using J = juce::Justification;

    text (g, knobNames[i], x0 + 10.0f, y0 + 11.0f, 80.0f, J::centredLeft, ink, 8.0f);
    const auto tag = "K0" + juce::String (i + 1);
    const float tagW = textWidth (tag, 8.0f);
    text (g, tag, x0 + W - 10.0f - tagW - 2.0f, y0 + 11.0f, tagW + 2.0f, J::centredRight, dim, 8.0f);
    g.setColour (tone);
    g.fillRect (std::round (x0 + W - 10.0f - tagW) - 8.0f, y0 + 9.0f, 4.0f, 4.0f);
    g.setColour (ink);
    g.setFont (mono (32.0f, -0.02f));
    g.drawText (juce::String (juce::roundToInt (99.0f * value)).paddedLeft ('0', 2), Rectangle<float> (x0 + 9.0f, y0 + 21.0f, 0.46f * W, 36.0f), J::centredLeft, false);

    const float bx = x0 + std::round (0.48f * W), by = y0 + 17.0f, bw = x0 + W - 10.0f - bx, bh = 37.0f;
    const juce::PathStrokeType thin (1.0f);
    g.saveState();
    g.reduceClipRegion (Rectangle<float> (bx, by - 2.0f, bw, bh + 4.0f).toNearestInt());
    if (i == 0)
    {
        // WAVE: a ringing ripple inside its dotted envelope; more wave rings longer
        const float mid = by + bh * 0.5f, amp = bh * 0.5f - 1.0f, fall = 7.0f - 6.2f * value;
        g.setColour (grid);
        g.fillRect (bx, std::round (mid), bw, 1.0f);
        g.setColour (dim);
        for (float side = 1.0f; side >= -1.0f; side -= 2.0f)
            for (float n = 0.0f; n <= bw; n += 3.0f)
                g.fillRect (bx + n, mid - side * std::exp (-fall * n / bw) * amp - 0.5f, 1.0f, 1.0f);
        juce::Path wave;
        for (float n = 0.0f; n <= bw; n += 1.0f)
        {
            const float u = n / bw, y = mid - std::exp (-fall * u) * amp * std::sin (u * lib::kPi * 10.0f - 5.0f * time);
            if (n == 0.0f) wave.startNewSubPath (bx, y); else wave.lineTo (bx + n, y);
        }
        g.setColour (tone);
        g.strokePath (wave, thin);
    }
    else if (i == 1)
    {
        // PRISM: one line into a triangle, three out; more prism fans them wider
        const float mid = by + bh * 0.5f, apex = bx + 0.22f * bw, start = bx + 0.5f * bw, fan = 1.5f + value * 0.4f * bh;
        juce::Path glass;
        glass.addTriangle (apex, mid - 7.0f, apex + 6.0f, mid + 5.0f, apex - 6.0f, mid + 5.0f);
        g.setColour (dim);
        g.strokePath (glass, thin);
        g.setColour (ink);
        g.fillRect (bx, mid - 0.5f, apex - 3.0f - bx, 1.0f);
        for (int ray = -1; ray <= 1; ++ray)
        {
            juce::Path line;
            const float off = (float) ray * fan;
            line.startNewSubPath (apex + 3.0f, mid);
            line.lineTo (start, mid + off);
            for (float x = start; x <= bx + bw; x += 2.0f)
                line.lineTo (x, mid + off + std::sin ((x - start) * 0.12f - 3.0f * time + 1.7f * (float) ray) * (0.4f + 1.4f * value) * std::min (1.0f, (x - start) / 12.0f));
            g.setColour (ray < 0 ? blue : (ray > 0 ? p.orange : ink));
            g.strokePath (line, thin);
        }
    }
    else if (i == 2)
    {
        // DEPTH: a gridded sheet that tips away as depth rises, its rungs rolling toward you
        const float tip = 0.12f + 1.18f * value, cx = bx + bw * 0.5f, cy = by + bh * 0.5f, size = 0.62f * std::min (bw, 1.6f * bh);
        const float up = std::cos (tip), back = std::sin (tip);
        auto at = [&] (float tx, float ty)
        {
            const float sc = 3.2f / (3.2f - ty * back) * size * 0.5f;
            return Point<float> (cx + tx * sc, cy - ty * up * sc * 0.62f);
        };
        juce::Path mesh, rim;
        for (int n = 0; n <= 6; ++n)
        {
            const float tx = (float) n / 6.0f * 2.0f - 1.0f;
            mesh.startNewSubPath (at (tx, -1.0f));  mesh.lineTo (at (tx, 1.0f));
        }
        const float roll = std::fmod (0.18f * time, 1.0f / 3.0f);
        for (int n = 0; n <= 6; ++n)
        {
            const float ty = -1.0f + 2.0f * (float) n / 6.0f + roll;
            if (ty > 1.0f) continue;
            mesh.startNewSubPath (at (-1.0f, ty));  mesh.lineTo (at (1.0f, ty));
        }
        g.setColour (dim);
        g.strokePath (mesh, thin);
        rim.startNewSubPath (at (-1.0f, -1.0f));  rim.lineTo (at (1.0f, -1.0f));  rim.lineTo (at (1.0f, 1.0f));  rim.lineTo (at (-1.0f, 1.0f));
        rim.closeSubPath();
        g.setColour (tone);
        g.strokePath (rim, thin);
    }
    else
    {
        // DRIFT: forty-two specks falling; more drift makes them fall faster, shake and trail
        const float fallRate = 0.04f + 0.42f * value, shake = 3.2f * value;
        for (int n = 0; n < 42; ++n)
        {
            auto hash = [n] (float e) { const float v = 43758.5453f * std::sin (127.1f * (float) n + 311.7f * e); return v - std::floor (v); };
            const float sx = hash (1.0f), sy = hash (2.0f), ss = hash (3.0f);
            const float fy = by + std::fmod (sy + time * fallRate * (0.6f + 0.8f * ss), 1.0f) * bh;
            const float x = bx + sx * bw + std::sin (time * (6.0f + 9.0f * ss) + 40.0f * ss) * shake;
            const float y = fy + std::cos (time * (7.0f + 5.0f * ss) + 30.0f * sx) * shake * 0.5f;
            const float dot = ss > 0.7f ? 2.0f : 1.0f, tail = 10.0f * fallRate * ss;
            g.setColour (tone.withAlpha (0.45f + 0.55f * ss));
            g.fillRect (std::round (x), std::round (y), dot, dot);
            if (tail > 1.5f) g.fillRect (std::round (x), std::round (y - tail), 1.0f, std::round (tail) - 1.0f);
        }
    }
    g.restoreState();

    const float rowY = y0 + H - 26.0f, rowW = W - 20.0f;
    const int lit = juce::roundToInt (32.0f * value);
    for (int n = 0; n <= 32; ++n)
    {
        const bool major = n % 8 == 0;
        g.setColour (n <= lit && lit > 0 ? tone : (major ? dim : ghost));
        g.fillRect (std::round (x0 + 10.0f + (float) n / 32.0f * (rowW - 1.0f)), major ? rowY - 1.0f : rowY, 1.0f, major ? 6.0f : 4.0f);
    }
    text (g, drives[i], x0 + 10.0f, y0 + H - 9.0f, W - 20.0f, J::centredLeft, dim, 8.0f);
}

void LiberatusEditor::drawDeck (juce::Graphics& g, const Palette& p)
{
    g.setColour (p.cap);
    g.fillEllipse (closeBtn);
    g.setColour (p.panelEdge);
    if (deckHidden)
    {
        g.drawEllipse (closeBtn, 1.0f);
        text (g, "+", closeBtn.getX(), closeBtn.getCentreY(), closeBtn.getWidth(), juce::Justification::centred, p.ink, 13.0f);
        return;
    }
    drawPanel (g, p, deck);
    g.setColour (p.cap);
    g.fillEllipse (closeBtn);
    g.setColour (p.capEdge);
    g.drawEllipse (closeBtn, 1.0f);
    text (g, juce::String::fromUTF8 ("\xc3\x97"), closeBtn.getX(), closeBtn.getCentreY(), closeBtn.getWidth(), juce::Justification::centred, p.ink, 13.0f);

    const auto hint = juce::String::fromUTF8 ("DRAG \xc2\xb7 SCROLL \xc2\xb7 \xc3\x97" "2 RESET");
    const float hw = textWidth (hint, 9.0f);
    text (g, hint, deck.getRight() - 26.0f - hw, deck.getY() + 16.0f, hw + 4.0f, juce::Justification::centredRight, p.ink3, 9.0f);
    g.setColour (p.panelEdge);
    g.drawLine (closeBtn.getRight() + 10.0f, deck.getY() + 16.0f, deck.getRight() - 40.0f - hw, deck.getY() + 16.0f, 1.0f);

    // screen
    const Colour glass (0xff0b0b0c), dim (0xff2a2a2c), lit (0xffe9e9e4), green (0xff37d67a);
    g.setColour (glass);
    g.fillRoundedRectangle (screen, 8.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (screen.reduced (0.5f), 8.0f, 1.0f);
    const int mode = proc.voiceMode();
    const float sx = screen.getX() + 10.0f, sr = screen.getRight() - 10.0f;
    // the two pages of the screen trade places with a short slide
    const float turned = knobMix * knobMix * (3.0f - 2.0f * knobMix);
    auto page = [&] (float alpha, float slide, auto&& draw)
    {
        if (alpha <= 0.0f) return;
        g.saveState();
        g.reduceClipRegion (screen.reduced (1.0f).toNearestInt());
        if (alpha < 1.0f) g.beginTransparencyLayer (alpha);
        g.addTransform (juce::AffineTransform::translation (slide, 0.0f));
        draw();
        if (alpha < 1.0f) g.endTransparencyLayer();
        g.restoreState();
    };
    page (1.0f - turned, -10.0f * turned, [&]
    {
    text (g, "LIVE", sx, screen.getY() + 11.0f, 40.0f, juce::Justification::centredLeft, lit, 8.0f);
    text (g, mode == 2 ? "KEYS" : (mode == 3 ? "BELLS" : "GRAINS"), sx + 34.0f, screen.getY() + 11.0f, 60.0f, juce::Justification::centredLeft, Colour (0xff6d6d6a), 8.0f);
    const float lvl = proc.soundOn() ? proc.level.load() : 0.0f;
    for (int i = 0; i < 8; ++i)
    {
        const bool on = lvl * 8.0f > (float) i + 0.2f;
        g.setColour (on ? (i >= 6 ? p.orange : green) : dim);
        g.fillRect (sr - 34.0f + (float) i * 4.2f, screen.getY() + 8.0f, 2.6f, 6.0f);
    }
    text (g, "SND", sr - 62.0f, screen.getY() + 11.0f, 26.0f, juce::Justification::centredLeft, Colour (0xff6d6d6a), 8.0f);
    text (g, proc.delayFx() ? "DELAY" : "REVERB", sr - 60.0f, screen.getY() + 23.0f, 60.0f, juce::Justification::centredRight, lit, 8.0f);

    const auto bars = Rectangle<float> (sx + 6.0f, screen.getY() + 30.0f, screen.getWidth() - 32.0f, 42.0f);
    const float bw = bars.getWidth() / (float) lib::kPartials, bh = bars.getHeight() / 10.0f;
    int hottest = 0;
    for (int i = 1; i < lib::kPartials; ++i) if (shown[(size_t) i] > shown[(size_t) hottest]) hottest = i;
    for (int i = 0; i < lib::kPartials; ++i)
    {
        const int on = (int) (std::min (1.0f, shown[(size_t) i]) * 10.0f + 0.2f);
        for (int r = 0; r < 10; ++r)
        {
            const bool l = r < on;
            g.setColour (l ? (i == hottest && on > 3 ? p.orange : lit) : dim.withAlpha (r == 0 ? 1.0f : 0.55f));
            g.fillRect (bars.getX() + (float) i * bw, bars.getBottom() - (float) (r + 1) * bh, bw - 1.6f, bh - 1.4f);
        }
    }
    const bool sounding = time - noteAt < 0.8f && time >= noteAt;
    text (g, sounding ? juce::String (noteHz, 1) : juce::String ("---.-"), sx, screen.getBottom() - 9.0f, 50.0f, juce::Justification::centredLeft, lit, 8.0f);
    text (g, "HZ", sx + 38.0f, screen.getBottom() - 9.0f, 20.0f, juce::Justification::centredLeft, Colour (0xff6d6d6a), 8.0f);

    juce::Path wavePath;
    const auto scopeBox = Rectangle<float> (sr - 46.0f, screen.getBottom() - 15.0f, 46.0f, 12.0f);
    const int head = proc.scopePos.load();
    for (int i = 0; i < 46; ++i)
    {
        const float s = proc.scope[(size_t) ((head - 46 * 6 + i * 6 + LiberatusProcessor::scopeSize * 4) % LiberatusProcessor::scopeSize)].load();
        const float y = scopeBox.getCentreY() - juce::jlimit (-1.0f, 1.0f, s * 9.0f) * scopeBox.getHeight() * 0.5f;
        if (i == 0) wavePath.startNewSubPath (scopeBox.getX(), y); else wavePath.lineTo (scopeBox.getX() + (float) i, y);
    }
    g.setColour (green);
    g.strokePath (wavePath, juce::PathStrokeType (1.0f));
    g.fillRect (scopeBox.getX() - 4.0f, scopeBox.getY(), 1.0f, scopeBox.getHeight());
    });
    page (turned, 10.0f * (1.0f - turned), [&] { drawKnobPage (g, p); });

    const char* const labels[4] = { "1", "2", "3", "FX" };
    for (int i = 0; i < 4; ++i)
        drawKey (g, p, keys[i], labels[i], i < 3 ? mode == i + 1 : proc.delayFx(), downKey == i);
    for (int i = 0; i < 4; ++i) drawKnob (g, p, i);
}

void LiberatusEditor::drawChrome (juce::Graphics& g, const Palette& p)
{
    const float W = (float) getWidth(), H = (float) getHeight(), m = 40.0f;
    const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
    using J = juce::Justification;

    // top bar
    const Rectangle<float> badge (m, 19.0f, 16.0f, 16.0f);
    g.setColour (p.ink);
    g.fillRoundedRectangle (badge, 4.0f);
    g.setColour (p.bg);
    const float bars[4] = { 4.0f, 8.0f, 6.0f, 9.0f };
    for (int i = 0; i < 4; ++i) g.fillRect (badge.getX() + 3.5f + (float) i * 2.6f, badge.getBottom() - 3.5f - bars[i], 1.3f, bars[i]);
    text (g, "TEXTURA", m + 26.0f, 27.0f, 110.0f, J::centredLeft, p.ink, 10.5f);
    g.setColour (p.panelEdge);
    g.fillRect (m + 122.0f, 21.0f, 1.0f, 12.0f);
    text (g, "LB-OS 0.1", m + 134.0f, 27.0f, 100.0f, J::centredLeft, p.ink2, 10.0f);
    text (g, "FREE", m + 222.0f, 27.0f, 60.0f, J::centredLeft, p.ink3, 10.0f);

    g.setColour (p.green);
    g.fillEllipse (Rectangle<float> (5.0f, 5.0f).withCentre ({ W * 0.5f - 62.0f, 27.0f }));
    text (g, juce::Time::getCurrentTime().formatted ("%H:%M:%S"), W * 0.5f - 52.0f, 27.0f, 76.0f, J::centredLeft, p.ink, 10.5f);
    text (g, "LOCAL", W * 0.5f + 26.0f, 27.0f, 60.0f, J::centredLeft, p.ink2, 10.0f);

    text (g, "SOUND", soundBtn.getX() - 62.0f, 27.0f, 54.0f, J::centredRight, p.ink2, 9.5f);
    drawKey (g, p, soundBtn, {}, false, false);
    drawLed (g, soundBtn.getTopLeft() + Point<float> (9.0f, 9.0f), proc.soundOn() ? p.green : Colour(), 5.5f);
    {
        // a small solid speaker; a bold cross beside it when muted, two waves when on
        const auto c = soundBtn.getCentre() + Point<float> (-1.5f, 0.5f);
        juce::Path sp;
        sp.startNewSubPath (c.x - 4.0f, c.y - 1.6f);
        sp.lineTo (c.x - 2.0f, c.y - 1.6f);
        sp.lineTo (c.x + 1.0f, c.y - 4.2f);
        sp.lineTo (c.x + 1.0f, c.y + 4.2f);
        sp.lineTo (c.x - 2.0f, c.y + 1.6f);
        sp.lineTo (c.x - 4.0f, c.y + 1.6f);
        sp.closeSubPath();
        g.setColour (p.ink);
        g.fillPath (sp);
        if (proc.soundOn())
        {
            juce::Path arcs;
            arcs.addCentredArc (c.x + 1.5f, c.y, 2.6f, 2.6f, 0.0f, 0.6f, 2.54f, true);
            arcs.addCentredArc (c.x + 1.5f, c.y, 4.8f, 4.8f, 0.0f, 0.6f, 2.54f, true);
            g.strokePath (arcs, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            const auto x = c + Point<float> (5.0f, 0.0f);
            juce::Path cross;
            cross.startNewSubPath (x.x - 1.8f, x.y - 1.8f); cross.lineTo (x.x + 1.8f, x.y + 1.8f);
            cross.startNewSubPath (x.x + 1.8f, x.y - 1.8f); cross.lineTo (x.x - 1.8f, x.y + 1.8f);
            g.strokePath (cross, juce::PathStrokeType (1.6f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
        }
    }
    {
        const auto sun = Point<float> (themeBtn.getX() - 16.0f, 27.0f), moon = Point<float> (themeBtn.getRight() + 16.0f, 27.0f);
        // outline sun and moon, as line icons; the one in use is inked
        g.setColour (proc.darkTheme ? p.ink3 : p.ink);
        g.drawEllipse (Rectangle<float> (5.5f, 5.5f).withCentre (sun), 1.1f);
        for (int i = 0; i < 8; ++i)
        {
            const float a = (float) i * lib::kPi / 4.0f;
            const Point<float> d (std::cos (a), std::sin (a));
            g.drawLine ({ sun + d * 4.6f, sun + d * 6.4f }, 1.1f);
        }
        // the track sits into the bar, the thumb stands up off it
        g.setColour (proc.darkTheme ? Colour (0xff1c1c1f) : Colour (0xffdcdcd6));
        g.fillRoundedRectangle (themeBtn, 5.0f);
        g.setColour (p.capEdge);
        g.drawRoundedRectangle (themeBtn.reduced (0.5f), 5.0f, 1.0f);
        const auto thumb = Rectangle<float> (17.0f, 12.0f).withCentre ({ proc.darkTheme ? themeBtn.getRight() - 10.5f : themeBtn.getX() + 10.5f, themeBtn.getCentreY() });
        g.setColour (juce::Colours::black.withAlpha (proc.darkTheme ? 0.4f : 0.12f));
        g.fillRoundedRectangle (thumb.translated (0.0f, 1.0f), 3.5f);
        g.setColour (proc.darkTheme ? Colour (0xff3a3a3e) : p.cap);
        g.fillRoundedRectangle (thumb, 3.5f);
        g.setColour (proc.darkTheme ? Colour (0xff4a4a4e) : p.capEdge);
        g.drawRoundedRectangle (thumb.reduced (0.5f), 3.5f, 0.8f);
        g.setColour (p.ink3);
        for (int i = -1; i <= 1; ++i) g.fillRect (thumb.getCentreX() + (float) i * 2.6f - 0.4f, thumb.getCentreY() - 2.5f, 0.8f, 5.0f);
        // the crescent's outline: the moon's rim outside the bite, then the bite's rim inside the moon
        juce::Path crescent;
        {
            const float R = 5.0f, r = 4.2f;
            const Point<float> b (2.8f, -2.2f);
            const float bite0 = std::atan2 (b.y, b.x);
            bool started = false;
            for (int k = 0; k <= 96; ++k)
            {
                const float a = bite0 + lib::kPi * 2.0f * (float) k / 96.0f;
                const Point<float> q (std::cos (a) * R, std::sin (a) * R);
                if (q.getDistanceFrom (b) < r) continue;
                if (! started) { crescent.startNewSubPath (moon + q); started = true; } else crescent.lineTo (moon + q);
            }
            for (int k = 96; k >= 0; --k)
            {
                const float a = bite0 + lib::kPi + lib::kPi * 2.0f * ((float) k / 96.0f - 0.5f);
                const Point<float> q (b.x + std::cos (a) * r, b.y + std::sin (a) * r);
                if (q.getDistanceFromOrigin() > R) continue;
                crescent.lineTo (moon + q);
            }
            crescent.closeSubPath();
        }
        g.setColour (proc.darkTheme ? p.ink : p.ink3);
        g.strokePath (crescent, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // around the word
    const auto box = logo.expanded (10.0f, 14.0f);
    g.setColour (p.ink3);
    const float arm = 8.0f;
    auto corner = [&] (Point<float> c, float dx, float dy)
    {
        g.drawLine (c.x, c.y, c.x + dx * arm, c.y, 1.0f);
        g.drawLine (c.x, c.y, c.x, c.y + dy * arm, 1.0f);
    };
    corner (box.getTopLeft(), 1, 1);     corner (box.getTopRight(), -1, 1);
    corner (box.getBottomLeft(), 1, -1); corner (box.getBottomRight(), -1, -1);
    for (int i = 0; i <= 4; ++i)
        g.fillRect (logo.getX() + logo.getWidth() * (float) i / 4.0f, box.getY() - 2.0f, 1.0f, 5.0f);
    text (g, "0", box.getX() - 22.0f, box.getY(), 16.0f, J::centredRight, p.ink3, 8.5f);
    text (g, juce::String (lib::kPartials), box.getRight() + 6.0f, box.getY(), 20.0f, J::centredLeft, p.ink3, 8.5f);
    text (g, "MOVE" + dot + (feedMode ? "FEED" : "STRIKE") + dot + "LISTEN", logo.getRight() - 300.0f, box.getY() - 26.0f, 300.0f, J::centredRight, p.ink2, 10.0f);

    const bool sounding = time - noteAt < 0.8f && time >= noteAt;
    const float ry = box.getBottom() + 24.0f;
    float x = logo.getRight();
    auto right = [&] (const juce::String& s, Colour c)
    {
        const float w = textWidth (s) + 2.0f;
        x -= w;
        text (g, s, x, ry, w, J::centredLeft, c);
        x -= 7.0f;
    };
    right ("HZ", p.ink2);
    right (sounding ? juce::String (noteHz, 1) : juce::String ("---.-"), sounding ? p.ink : p.ink3);
    right (sounding ? noteName (noteHz) : juce::String ("--"), sounding ? p.ink : p.ink3);
    right (juce::String::fromUTF8 ("\xc2\xb7"), p.ink3);
    right (juce::String (v, 3), overLogo ? p.ink : p.ink3);
    right ("V", p.ink2);
    right (juce::String (u, 3), overLogo ? p.ink : p.ink3);
    right ("U", p.ink2);

    // pointer crosshair, as on a plotting table
    if (presence > 0.02f && ! deck.contains (ptr) && ! sidePanel.contains (ptr))
    {
        g.setColour (p.ink3.withAlpha (0.35f * presence));
        g.fillRect (ptr.x, 56.0f, 1.0f, H - 112.0f);
        g.fillRect (0.0f, ptr.y, W, 1.0f);
    }

    // bottom bar
    const float by = H - 22.0f;
    text (g, juce::String::fromUTF8 ("\xc2\xa9 2026 LIBERATUS"), m, by, 150.0f, J::centredLeft, p.ink2, 9.5f);
    text (g, "FREE FOREVER", m + 132.0f, by, 120.0f, J::centredLeft, p.ink3, 9.5f);
    struct Hint { const char* key; const char* what; };
    const Hint hints[] = { { "1-3", "VOICE" }, { "F", "FX" }, { "S", "SOUND" }, { "T", "FINISH" }, { "SPACE", feedMode ? "FEED" : "STRIKE" } };
    float total = 0;
    for (auto& h : hints) total += textWidth (h.key, 9.0f) + 12.0f + 8.0f + textWidth (h.what, 9.5f) + 26.0f;
    float hx = W * 0.5f - total * 0.5f + 60.0f;
    for (auto& h : hints)
    {
        const float kw = textWidth (h.key, 9.0f) + 12.0f;
        const Rectangle<float> cap (hx, by - 8.0f, kw, 16.0f);
        g.setColour (p.cap);
        g.fillRoundedRectangle (cap, 3.5f);
        g.setColour (p.panelEdge);
        g.drawRoundedRectangle (cap.reduced (0.5f), 3.5f, 1.0f);
        text (g, h.key, hx, by, kw, J::centred, p.ink, 9.0f);
        hx += kw + 8.0f;
        const float ww = textWidth (h.what, 9.5f);
        text (g, h.what, hx, by, ww + 4.0f, J::centredLeft, p.ink2, 9.5f);
        hx += ww + 26.0f;
    }
    const int secs = (int) (juce::Time::getMillisecondCounterHiRes() * 0.001 - startedAt);
    text (g, juce::String::formatted ("%02d:%02d:%02d:%02d", secs / 86400, (secs / 3600) % 24, (secs / 60) % 60, secs % 60),
          W - m - 160.0f, by, 100.0f, J::centredRight, p.ink, 10.0f);
    for (int i = 0; i < 12; ++i)
    {
        g.setColour (fieldE * 12.0f > (float) (11 - i) ? p.ink : p.ink3.withAlpha (0.5f));
        g.fillRect (W - m - 50.0f + (float) i * 4.2f, by - 4.0f, 2.0f, 8.0f);
    }

    // side panel: the word, and a strike button
    if (! sidePanel.isEmpty())
    {
        drawPanel (g, p, sidePanel);
        text (g, "WORD", sidePanel.getX() + 24.0f, sidePanel.getY() + 22.0f, 60.0f, J::centredLeft, p.ink2, 9.0f);
        text (g, "RETURN TO SET", (float) wordBox.getRight() - 120.0f, sidePanel.getY() + 22.0f, 120.0f, J::centredRight, p.ink3, 8.5f);
        const bool down = downKey == 9;
        g.setColour (p.orange.darker (0.45f));
        g.fillRoundedRectangle (strikeBtn.translated (0.0f, 3.0f), 9.0f);
        const auto top = strikeBtn.translated (0.0f, down ? 2.0f : 0.0f);
        g.setColour (p.orange);
        g.fillRoundedRectangle (top, 9.0f);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.fillEllipse (Rectangle<float> (5.0f, 5.0f).withCentre (top.getTopLeft() + Point<float> (11.0f, 11.0f)));
        text (g, feedMode ? "FEED" : "STRIKE", top.getX(), top.getCentreY(), top.getWidth(), J::centred, juce::Colours::white, 10.5f);
    }
}

void LiberatusEditor::paint (juce::Graphics& g)
{
    const auto p = pal();
    if (film.isValid())
    {
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImage (film, getLocalBounds().toFloat());
    }
    else g.fillAll (p.bg);
    if (koi) pond.draw (g, ! proc.darkTheme, time);
    drawDots (g, p);
    drawMarks (g, p);

    // the controls rise into place once the boot screen has cleared
    if (uiShown >= 1.0f) { drawChrome (g, p); drawDeck (g, p); }
    else if (uiShown > 0.0f)
    {
        g.saveState();
        g.addTransform (juce::AffineTransform::translation (0.0f, 10.0f * (1.0f - uiShown)));
        g.beginTransparencyLayer (uiShown);
        drawChrome (g, p);
        drawDeck (g, p);
        g.endTransparencyLayer();
        g.restoreState();
    }
}

void LiberatusEditor::paintOverChildren (juce::Graphics& g)
{
    if (inCodeShot) return;
    const auto p = pal();
    drawBoot (g, p);
    if (codeAt >= 0.0f) drawCode (g);
}

// A short list that types itself in while a bar fills, then clears to show the page.
void LiberatusEditor::drawBoot (juce::Graphics& g, const Palette& p)
{
    if (bootHold <= 0.0f || time >= bootHold + bootFade) return;
    const float W = (float) getWidth(), H = (float) getHeight(), m = 40.0f;
    const float gone = easeOut ((time - bootHold) / bootFade);
    using J = juce::Justification;
    g.saveState();
    if (gone > 0.0f) g.beginTransparencyLayer (1.0f - gone);
    g.fillAll (p.bg);

    const float fill = easeInOut (time / bootHold), barY = H - m - 1.0f;
    g.setColour (p.ink.withAlpha (0.12f));
    g.fillRect (m, barY, W - 2.0f * m, 1.0f);
    g.setColour (p.ink);
    g.fillRect (m, barY, (W - 2.0f * m) * fill, 1.0f);

    float scale = 2.0f;
    if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds())) scale = (float) d->scale;
    const auto times = juce::String::fromUTF8 (" \xc3\x97 ");
    const juce::String rows[5][2] = { { "FIELD", "CALIBRATING" },
                                      { "WORDMARK", juce::String (mark.unitsW) + times + juce::String (mark.unitsH) },
                                      { "PARTIALS", juce::String (lib::kPartials) },
                                      { "DISPLAY", juce::String (getWidth()) + times + juce::String (getHeight()) + " @" + juce::String (scale, scale == std::floor (scale) ? 0 : 1) + "X" },
                                      { "OUTPUT", "READY " } };
    const float pitch = 19.0f, lastY = barY - 20.0f - 7.0f;
    for (int i = 0; i < 7; ++i)
    {
        const float y = lastY - (float) (6 - i) * pitch;
        const float typedIn = std::floor (14.0f * juce::jlimit (0.0f, 1.0f, (time - 0.04f - 0.11f * (float) i) / 0.12f)) / 14.0f;
        if (typedIn <= 0.0f) continue;
        juce::String key, value;
        if (i == 0) value = "LIBERATUS SYSTEM";
        else if (i == 1) key = "LB-OS 0.1";
        else
        {
            const auto& r = rows[i - 2];
            key = juce::String::fromUTF8 ("\xe2\x80\xba ") + r[0] + " " + juce::String::repeatedString (".", std::max (2, 17 - r[0].length())) + " ";
            value = r[1];
        }
        const float kw = key.isEmpty() ? 0.0f : textWidth (key), vw = textWidth (value);
        const float cursor = i == 6 ? 7.0f : 0.0f;
        g.saveState();
        g.reduceClipRegion (Rectangle<float> (m, y - 10.0f, (kw + vw + cursor + 2.0f) * typedIn, 20.0f).toNearestInt());
        if (key.isNotEmpty()) text (g, key, m, y, kw + 4.0f, J::centredLeft, i == 1 ? p.ink2 : p.ink3);
        text (g, value, m + kw, y, vw + 4.0f, J::centredLeft, p.ink);
        if (i == 6 && std::fmod (time, 0.5f) < 0.25f)
        {
            g.setColour (p.ink);
            g.fillRect (m + kw + vw, y - 5.0f, 6.0f, 10.0f);
        }
        g.restoreState();
    }
    const auto pct = juce::String (juce::roundToInt (100.0f * fill)).paddedLeft ('0', 2);
    text (g, "%", W - m - 10.0f, lastY, 10.0f, J::centredRight, p.ink2);
    text (g, pct, W - m - 51.0f, lastY, 40.0f, J::centredRight, p.ink);

    if (gone > 0.0f) g.endTransparencyLayer();
    g.restoreState();
}

// The typed code's four and a half seconds: the page turns to amber phosphor and shakes in
// bursts, a short list types out, and the creature shows itself over the word's place.
void LiberatusEditor::drawCode (juce::Graphics& g)
{
    const float h = time - codeAt, W = (float) getWidth(), H = (float) getHeight();
    const float tint = std::min (juce::jlimit (0.0f, 1.0f, h / 0.22f), juce::jlimit (0.0f, 1.0f, (4.37f - h) / 0.22f));

    if (tint > 0.0f)
    {
        const float scale = juce::jlimit (1.0f, 2.0f, (float) g.getInternalContext().getPhysicalPixelScaleFactor());
        inCodeShot = true;
        auto shot = createComponentSnapshot (getLocalBounds(), false, scale).convertedToFormat (juce::Image::ARGB);
        inCodeShot = false;
        const auto tones = codeTones (! proc.darkTheme, false), torn = codeTones (! proc.darkTheme, true);
        const int mix = juce::roundToInt (256.0f * tint);
        const int tearTop = tear >= 0.0f ? juce::roundToInt (0.92f * tear * H * scale) : -1;
        const int tearEnd = tearTop + juce::roundToInt ((6.0f + 40.0f * tear) * scale), tearLeft = juce::roundToInt (18.0f * scale);
        {
            const juce::Image::BitmapData bd (shot, juce::Image::BitmapData::readWrite);
            for (int y = 0; y < bd.height; ++y)
            {
                auto* px = (juce::PixelARGB*) bd.getLinePointer (y);
                const bool band = tearTop >= 0 && y >= tearTop && y < tearEnd;
                for (int x = 0; x < bd.width; ++x)
                {
                    const int r = px[x].getRed(), gr = px[x].getGreen(), b = px[x].getBlue();
                    const auto to = (band && x >= tearLeft ? torn : tones)[(size_t) ((54 * r + 183 * gr + 19 * b) >> 8)];
                    px[x].setARGB (255, (juce::uint8) (r + (((int) to.getRed() - r) * mix >> 8)),
                                        (juce::uint8) (gr + (((int) to.getGreen() - gr) * mix >> 8)),
                                        (juce::uint8) (b + (((int) to.getBlue() - b) * mix >> 8)));
                }
            }
        }
        // bursts shake the page in hard steps
        Point<float> shake;
        if (tear >= 0.0f)
        {
            static const Point<float> at[5] = { { -6.0f, 1.0f }, { 5.0f, -2.0f }, { -2.0f, 3.0f }, { 7.0f, 0.0f }, { 0.0f, 0.0f } };
            const float f = std::fmod (h, 0.15f) / 0.15f * 4.0f;
            const int seg = juce::jlimit (0, 3, (int) f);
            shake = f - (float) seg < 0.5f ? at[seg] : (at[seg] + at[seg + 1]) * 0.5f;
        }
        const auto base = tones[proc.darkTheme ? 12 : 232];
        g.setColour (Colour (base.getRed(), base.getGreen(), base.getBlue()));
        g.fillAll();
        g.drawImage (shot, Rectangle<float> (shake.x, shake.y, W, H));
    }

    // white flash, then scan lines that flicker for as long as the overlay is up
    if (h < 0.28f)
    {
        const float q = h / 0.28f;
        g.setColour (juce::Colours::white.withAlpha (0.95f * (1.0f - q) * (1.0f - q)));
        g.fillAll();
    }
    g.setColour (juce::Colours::black.withAlpha (0.28f * (std::fmod (h, 0.11f) < 0.055f ? 1.0f : 0.7f)));
    for (float y = 0.0f; y < H; y += 3.0f) g.fillRect (0.0f, y, W, 1.0f);

    static const char* const lines[8] = { "> liberatus ......................... (lat.) set free",
                                          "> licence key ....................... none needed",
                                          "> account ........................... none needed",
                                          "> mailing list ...................... none",
                                          "> price ............................. 0.00",
                                          "> source ............................ open",
                                          "> the word is the instrument ........ play",
                                          "FREE AS IN FREE" };
    const float size = juce::jlimit (9.0f, 12.0f, 0.0105f * W), left = juce::jlimit (16.0f, 40.0f, 0.03f * W), pitch = 1.75f * size;
    const Colour phosphor (0xffffd08a), red (0xffff5a1f), cyan (0xff2f5bff);
    const int count = juce::jlimit (0, 8, (int) std::floor ((h - 0.09f) / 0.12f) + 1);
    float y = 76.0f + 0.5f * pitch;
    auto ghosted = [&] (const juce::String& s, float x, float ty, float sz, float off, Colour main)
    {
        const float w = W - x - 16.0f;
        using J = juce::Justification;
        g.setFont (mono (sz, sz > size ? 0.12f : 0.06f));
        g.setColour (cyan.withAlpha (0.5f));  g.drawText (s, Rectangle<float> (x - off, ty - sz, w, 2.0f * sz), J::centredLeft, false);
        g.setColour (red.withAlpha (0.5f));   g.drawText (s, Rectangle<float> (x + off, ty - sz, w, 2.0f * sz), J::centredLeft, false);
        g.setColour (main);                   g.drawText (s, Rectangle<float> (x, ty - sz, w, 2.0f * sz), J::centredLeft, false);
    };
    for (int i = 0; i < count; ++i)
    {
        const bool hot = i == 7;
        if (hot) y += 6.0f + 0.3f * pitch;
        ghosted (juce::String::fromUTF8 (lines[i]), left, y, hot ? 1.6f * size : size, hot ? 2.0f : 1.0f, hot ? Colour (0xfffff6ea) : phosphor);
        y += hot ? 1.6f * pitch : pitch;
    }
    if (std::fmod (h, 0.5f) < 0.25f)
    {
        g.setColour (phosphor);
        g.fillRect (left, y - 0.55f * size, 0.6f * size, 1.1f * size);
    }

    // the glimpse: held states, cut between with no easing
    const float q = (h - 1.15f) / 1.5f;
    if (q >= 0.06f && q < 0.92f)
    {
        float op = 1.0f, top = 0.0f, bottom = 0.0f, tx = 0.0f, skew = 0.0f, zoom = 1.0f;
        if (q < 0.12f)      { bottom = 0.55f; tx = -14.0f; skew = -8.0f; }
        else if (q < 0.18f) { top = 0.4f; tx = 10.0f; }
        else if (q < 0.22f) { op = 0.2f; top = 0.4f; tx = 10.0f; }
        else if (q < 0.48f) {}
        else if (q < 0.52f) { top = 0.22f; bottom = 0.48f; tx = 22.0f; skew = 6.0f; }
        else if (q < 0.70f) {}
        else if (q < 0.74f) { zoom = 1.03f; }
        else if (q < 0.78f) { op = 0.15f; zoom = 1.03f; }
        else if (q < 0.86f) { op = 0.9f; bottom = 0.3f; tx = -8.0f; }
        else                { op = 0.4f; top = 0.6f; tx = -8.0f; }

        // the creature itself, in its own colours, over the one standing in the columns
        static const auto picture = juce::ImageCache::getFromMemory (BinaryData::creature_png, BinaryData::creature_pngSize);
        const float gh = std::min (0.58f * H, 440.0f), gw = gh * (float) picture.getWidth() / (float) picture.getHeight();
        const auto box = Rectangle<float> (gw, gh).withCentre ({ 0.5f * W, 0.5f * H - 0.05f * gh });
        auto warp = [&] (float w, float hgt)
        {
            return juce::AffineTransform::scale (gw / w, gh / hgt).translated (box.getX(), box.getY())
                       .followedBy (juce::AffineTransform::shear (std::tan (juce::degreesToRadians (skew)), 0.0f).translated (-std::tan (juce::degreesToRadians (skew)) * box.getCentreY(), 0.0f))
                       .scaled (zoom, zoom, box.getCentreX(), box.getCentreY()).translated (tx, 0.0f);
        };
        const auto place = warp ((float) creatureCols, (float) creatureRows);
        g.saveState();
        g.reduceClipRegion (box.expanded (60.0f, 0.0f).withTrimmedTop (top * gh).withTrimmedBottom (bottom * gh).toNearestInt());
        g.setColour (red.withAlpha (0.85f * op));      g.fillPath (codeGlimpse, place.translated (5.0f, 0.0f));
        g.setColour (cyan.withAlpha (0.85f * op));     g.fillPath (codeGlimpse, place.translated (-5.0f, 0.0f));
        g.setOpacity (op);
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        g.drawImageTransformed (picture, warp ((float) picture.getWidth(), (float) picture.getHeight()));
        g.restoreState();
    }
}
