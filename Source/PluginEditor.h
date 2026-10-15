#pragma once
#include "PluginProcessor.h"
#include "Field.h"
#include "Koi.h"

class LiberatusEditor : public juce::AudioProcessorEditor
{
public:
    explicit LiberatusEditor (LiberatusProcessor&);
    ~LiberatusEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    // Driven by the vblank in the plugin and directly by the offline harness.
    void advance (float dt);
    void setPointer (juce::Point<float> p, bool inside);
    void strike (juce::Point<float> p, float strength);
    void click (juce::Point<float> p);
    void turnKnob (int i, float v) { setKnob (i, v); }   // harness: what dragging a knob does
    void startCode();                          // what typing the hidden code does
    void uiSound (int kind, float value = -1.0f);
    bool plainDraw = false;                    // harness only: draw with none of the shortcuts, to compare
    int* benchTiles = nullptr;                 // harness only: counts tiles seen and tiles drawn from rest
    void showPond (bool feed) { koi = true; feedMode = feed; pond.resize ((float) getWidth(), (float) getHeight(), rnd); }

private:
    struct Run { std::vector<float> y, cov; };          // y: fractions down the word's box
    struct Dot { float x, y, show; };
    struct Palette { juce::Colour bg, ink, ink2, ink3, blue, orange, green, panel, panelEdge, cap, capEdge; };

    Palette pal() const;
    void rebuildWord();
    void buildColumns();
    void layoutDots();

    void simulate (float dt);
    void playPointer (float dt);
    void grain (float delay, float vel, int index = -1);
    void keysNote (float delay, float vel, int index = -1, int direct = -1000);
    void bellNote (float delay, float vel, int index = -1);
    void strum (float u, float vel);
    void send (int mode, float delay, int semis, float vel);

    void drawDots (juce::Graphics&, const Palette&);
    void drawMarks (juce::Graphics&, const Palette&);
    void drawChrome (juce::Graphics&, const Palette&);
    void drawDeck (juce::Graphics&, const Palette&);
    void drawKnob (juce::Graphics&, const Palette&, int index);
    void drawKnobPage (juce::Graphics&, const Palette&);
    void drawKey (juce::Graphics&, const Palette&, juce::Rectangle<float>, const juce::String&, bool lit, bool down);
    void drawLed (juce::Graphics&, juce::Point<float> centre, juce::Colour lit, float size);   // transparent = off
    void drawPanel (juce::Graphics&, const Palette&, juce::Rectangle<float>);
    void drawBoot (juce::Graphics&, const Palette&);
    void drawCode (juce::Graphics&);
    void buildFilm();
    void swapMark (bool standIn);

    juce::Point<float> project (juce::Point<float> p, float z, float& scale) const;
    float lensAt (juce::Point<float> p) const;
    float knobValue (int i) const;
    void setKnob (int i, float v);
    void setChoice (const char* id, int index);
    int knobAt (juce::Point<float>) const;

    LiberatusProcessor& proc;
    lib::Membrane membrane;
    lib::Wordmark mark;
    lib::Pond pond;
    juce::TextEditor wordBox;
    juce::VBlankAttachment vblank;

    // layout
    juce::Rectangle<float> logo, deck, screen, sidePanel, strikeBtn, soundBtn, themeBtn, closeBtn;
    juce::Rectangle<float> keys[4], knobs[4];
    bool deckHidden = false;

    // columns
    std::vector<std::vector<Run>> columns;
    float pitch = 3, inkW = 1.5f, scaleSeen = 0;
    std::vector<Dot> dots;
    float dotSize = 1.5f;

    // pointer
    juce::Point<float> ptr, lastPtr, stepPtr;
    bool ptrInside = false, ptrMoved = false, stepValid = false, overLogo = false;
    float speed = 0, velX = 0, presence = 0, u = 0.5f, v = 0.5f;
    juce::Point<float> cam;
    float camD = 1000;

    // simulation
    lib::Knobs k;
    float acc = 0, phase = 0, time = 0, fieldE = 0, strikeE = 0;
    std::vector<lib::Membrane::Impulse> pending;
    juce::Random rnd;

    // sound
    float speedS = 0, travel = 0, nextRestAt = 0, lastGrainAt = -1;
    int chord = 0, arp = 0, arpDir = 1, bell = 7, midiSeen = 0;
    float noteHz = 0, noteAt = -10;
    std::array<float, lib::kPartials> profile {}, shown {};

    // knob drag
    int dragKnob = -1, hotKnob = -1, downKey = -1;
    int screenKnob = 0;                        // the knob whose page the screen shows while it is touched
    float knobTouchedAt = -10, knobMix = 0;
    juce::Point<float> dragLast;
    double startedAt = 0;
    double lastFrame = 0;

    // intro wipe, pond, and the hands-off wander
    float sweepStart = 0.35f, sweepDur = 1.35f, lastInputAt = 0;
    bool koi = false, feedMode = false, ghost = false, soundWas = false;
    juce::Rectangle<float> badgeBtn, nameBtn;
    float sweepAge (float x) const;
    float sweepProgress (float n) const;

    // boot screen and the fade-in of the controls behind it
    float bootHold = 0.9f, bootFade = 0.32f, uiIn = 1.3f, uiShown = -1;
    juce::Image film;                                   // the page colour with vignette and grain baked in

    // the word's ink at rest, painted once into tiles
    std::vector<juce::Image> restTiles;
    std::vector<int> restLeft;
    int restTop = 0;
    float restScale = 0;
    juce::Colour restInk;
    // the word painted by hand: three layers of coverage, put together into a picture
    struct Plate
    {
        int w = 0, h = 0;
        float scale = 0;
        juce::Point<int> origin;
        std::vector<juce::uint8> ink, blue, orange;
        std::vector<int> lo, hi, wasLo[2], wasHi[2];
        juce::Image page[2];
        int turn = 0;
    };
    Plate plate;
    struct PanelSkin { juce::Rectangle<float> at; float scale; bool dark; juce::Point<int> origin; juce::Image img; };
    std::vector<PanelSkin> panelSkins;
    bool inPanelSkin = false;

    // interface sounds, and the typed code
    std::array<double, lib::numTickKinds> lastTick {};
    juce::String typed;
    juce::uint32 typedAt = 0;
    float codeAt = -1, tear = -1, tearAt = 0;
    int codeStage = 0;
    bool inCodeShot = false;
    juce::Path codeGlimpse;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiberatusEditor)
};
