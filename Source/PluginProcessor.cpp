#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout LiberatusProcessor::createLayout()
{
    using namespace juce;
    APVTS::ParameterLayout layout;
    auto knob = [&] (const char* id, const char* name, float def)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 1.0f), def));
    };
    knob ("wave", "Wave", 0.55f);
    knob ("prism", "Prism", 0.5f);
    knob ("depth", "Depth", 0.45f);
    knob ("drift", "Drift", 0.3f);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "voice", 1 }, "Voice", StringArray { "Grains", "Keys", "Bells" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "fx", 1 }, "FX", StringArray { "Reverb", "Delay" }, 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "sound", 1 }, "Sound", true));
    return layout;
}

LiberatusProcessor::LiberatusProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "state", createLayout())
{
    wave = apvts.getRawParameterValue ("wave");    prism = apvts.getRawParameterValue ("prism");
    depth = apvts.getRawParameterValue ("depth");  drift = apvts.getRawParameterValue ("drift");
    voice = apvts.getRawParameterValue ("voice");  fx = apvts.getRawParameterValue ("fx");
    sound = apvts.getRawParameterValue ("sound");
   #if ! LIBERATUS_HEADLESS
    startTimerHz (20);
   #endif
}

LiberatusProcessor::~LiberatusProcessor() { stopTimer(); }

lib::Knobs LiberatusProcessor::knobs() const
{
    return { wave->load(), prism->load(), depth->load(), drift->load() };
}

bool LiberatusProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void LiberatusProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    engine.prepare (sampleRate, samplesPerBlock);
    roomLoaded = lib::roomSeconds (wave->load());
    engine.loadRoom (roomLoaded, roomSeed++);
    prepared = true;
}

// The room is rebuilt when WAVE has settled on a new length, never while it is moving.
void LiberatusProcessor::timerCallback()
{
    if (! prepared) return;
    const float w = wave->load();
    if (std::abs (w - waveSeen) > 0.001f) { waveSeen = w; waveStill = 0; return; }
    if (++waveStill < 7) return;
    const float want = lib::roomSeconds (w);
    if (std::abs (want - roomLoaded) > 0.12f)
    {
        roomLoaded = want;
        engine.loadRoom (want, roomSeed++);
    }
}

void LiberatusProcessor::pushEvent (const lib::Event& e)
{
    const juce::SpinLock::ScopedLockType lock (pushLock);
    const auto scope1 = fifo.write (1);
    if (scope1.blockSize1 > 0) queue[(size_t) scope1.startIndex1] = e;
    else if (scope1.blockSize2 > 0) queue[(size_t) scope1.startIndex2] = e;
}

void LiberatusProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const auto k = knobs();
    const int mode = voiceMode();

    {
        const auto r = fifo.read (fifo.getNumReady());
        for (int i = 0; i < r.blockSize1; ++i) engine.noteOn (queue[(size_t) (r.startIndex1 + i)], k);
        for (int i = 0; i < r.blockSize2; ++i) engine.noteOn (queue[(size_t) (r.startIndex2 + i)], k);
    }

    // MIDI plays the current voice chromatically; each mode's root sits on its own key.
    const int rootKey = mode == 2 ? 53 : (mode == 3 ? 57 : 50);
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (! m.isNoteOn()) continue;
        lib::Event e { mode, (float) meta.samplePosition / (float) sr, (float) (m.getNoteNumber() - rootKey), 0.25f + 0.75f * m.getFloatVelocity() };
        engine.noteOn (e, k);
        lastNoteHz.store ((float) juce::MidiMessage::getMidiNoteInHertz (m.getNoteNumber()));
        noteSerial.fetch_add (1);
    }
    midi.clear();

    if (buffer.getNumChannels() < 2) { buffer.clear(); return; }
    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);
    engine.render (L, R, n, k, delayFx(), soundOn());
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);

    double sum = 0;
    int sp = scopePos.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (L[i] + R[i]);
        sum += (double) m * m;
        if ((i & 3) == 0) { scope[(size_t) sp].store (m, std::memory_order_relaxed); sp = (sp + 1) % scopeSize; }
    }
    scopePos.store (sp, std::memory_order_relaxed);
    const float rms = (float) std::sqrt (sum / std::max (1, n));
    const float target = lib::clamp01 ((juce::Decibels::gainToDecibels (rms, -120.0f) + 60.0f) / 40.0f);
    const float blockT = (float) n / (float) sr;
    levelSm += (target - levelSm) * (1.0f - std::exp (-blockT / (target > levelSm ? 0.03f : 0.25f)));
    level.store (levelSm);
}

void LiberatusProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("word", word, nullptr);
    state.setProperty ("dark", darkTheme, nullptr);
    state.setProperty ("w", editorWidth, nullptr);
    state.setProperty ("h", editorHeight, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void LiberatusProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.isValid()) return;
        word = state.getProperty ("word", word).toString();
        darkTheme = (bool) state.getProperty ("dark", darkTheme);
        editorWidth = (int) state.getProperty ("w", editorWidth);
        editorHeight = (int) state.getProperty ("h", editorHeight);
        apvts.replaceState (state);
    }
}

juce::AudioProcessorEditor* LiberatusProcessor::createEditor() { return new LiberatusEditor (*this); }

#if ! LIBERATUS_HEADLESS
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LiberatusProcessor(); }
#endif
