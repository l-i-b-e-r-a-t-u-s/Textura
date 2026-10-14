#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Engine.h"

class LiberatusProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    LiberatusProcessor();
    ~LiberatusProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Textura"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // UI thread: queue a note. Dropped silently if the queue is full.
    void pushEvent (const lib::Event&);

    lib::Knobs knobs() const;
    int voiceMode() const { return 1 + (int) voice->load(); }
    bool delayFx() const { return fx->load() > 0.5f; }
    bool soundOn() const { return sound->load() > 0.5f; }

    juce::String getWord() const { return word; }
    void setWord (const juce::String& w) { word = w; }

    // Read by the editor.
    std::atomic<float> level { 0 };          // 0..1, -60..-20 dB RMS before the master
    std::atomic<float> lastNoteHz { 0 };     // most recent MIDI note, 0 if none
    std::atomic<int> noteSerial { 0 };
    static constexpr int scopeSize = 1024;
    std::array<std::atomic<float>, scopeSize> scope {};
    std::atomic<int> scopePos { 0 };

    juce::AudioProcessorValueTreeState apvts;
    int editorWidth = 1280, editorHeight = 720;
    bool darkTheme = false;

private:
    void timerCallback() override;

    lib::Engine engine;
    std::atomic<float>* wave = nullptr; std::atomic<float>* prism = nullptr;
    std::atomic<float>* depth = nullptr; std::atomic<float>* drift = nullptr;
    std::atomic<float>* voice = nullptr; std::atomic<float>* fx = nullptr; std::atomic<float>* sound = nullptr;

    juce::AbstractFifo fifo { 256 };
    std::array<lib::Event, 256> queue;
    juce::SpinLock pushLock;

    juce::String word { "liberatus" };
    double sr = 48000;
    bool prepared = false;
    float roomLoaded = 0, waveSeen = -1;
    int waveStill = 0, roomSeed = 1;
    float levelSm = 0;
};
