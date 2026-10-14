// Offline check: sweeps a pointer across the word, strikes it, then writes what the editor
// drew (PNG) and what the engine played (WAV), and prints levels and frame cost.
//   TexturaSnapshot out.png out.wav [voice 1-3] [dark] [delay] [koi] [code] [ticks] [knob] [bench]
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <juce_audio_formats/juce_audio_formats.h>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File png (argc > 1 ? juce::String (argv[1]) : juce::String ("liberatus.png"));
    const juce::File wav (argc > 2 ? juce::String (argv[2]) : juce::String ("liberatus.wav"));
    const int voice = argc > 3 ? juce::String (argv[3]).getIntValue() : 1;

    LiberatusProcessor proc;
    for (int i = 4; i < argc; ++i)
    {
        if (juce::String (argv[i]) == "dark") proc.darkTheme = true;
        if (juce::String (argv[i]) == "delay") proc.apvts.getParameter ("fx")->setValueNotifyingHost (1.0f);
    }
    proc.apvts.getParameter ("voice")->setValueNotifyingHost ((float) (voice - 1) / 2.0f);

    const double sr = 48000.0;
    const int block = 800;                       // one 60 Hz frame
    proc.setPlayConfigDetails (0, 2, sr, block);
    proc.prepareToPlay (sr, block);
    juce::Thread::sleep (400);                   // let the room impulse load

    std::unique_ptr<LiberatusEditor> editor (dynamic_cast<LiberatusEditor*> (proc.createEditor()));
    editor->setSize (1280, 720);

    bool koi = false, code = false, ticks = false, knobs = false;
    for (int i = 4; i < argc; ++i)
    {
        koi = koi || juce::String (argv[i]) == "koi";
        code = code || juce::String (argv[i]) == "code";
        ticks = ticks || juce::String (argv[i]) == "ticks";
        knobs = knobs || juce::String (argv[i]) == "knob";
    }
    auto save = [&] (const char* suffix, float scale)
    {
        auto shot = png.getSiblingFile (png.getFileNameWithoutExtension() + suffix + ".png");
        shot.deleteFile();
        juce::FileOutputStream os (shot);
        juce::PNGImageFormat().writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds(), true, scale), os);
    };
    // the boot screen and the wipe run before anything is played
    {
        juce::AudioBuffer<float> io (2, 800);
        juce::MidiBuffer midi;
        for (int f = 0; f < 156; ++f)
        {
            editor->advance (1.0f / 60.0f);
            if (f == 30) save ("_boot", 1.0f);
            if (f == 100) save ("_wipe", 1.0f);
            io.clear();
            proc.processBlock (io, midi);
        }
    }
    // bench [seconds]: the cost of a frame, drawn at retina scale the way the window draws it
    for (int i = 4; i < argc; ++i)
        if (juce::String (argv[i]) == "bench")
        {
            const int n = 60 * (i + 1 < argc ? std::max (1, juce::String (argv[i + 1]).getIntValue()) : 10);
            juce::Image page (juce::Image::ARGB, 2560, 1440, true);
            const char* const names[3] = { "moving", "still", "away" };
            for (int pass = 0; pass < 3; ++pass)
            {
                double step = 0, draw = 0;
                int tiles[2] = { 0, 0 };
                editor->benchTiles = tiles;
                for (int f = 0; f < n; ++f)
                {
                    const float t = (float) f / 60.0f;
                    const double t0 = juce::Time::getMillisecondCounterHiRes();
                    if (pass == 0) editor->setPointer ({ 640.0f + 420.0f * std::sin (t * 1.7f), 300.0f + 60.0f * std::sin (t * 5.0f) }, true);
                    if (pass == 2) editor->setPointer ({ 640.0f, 300.0f }, false);
                    editor->advance (1.0f / 60.0f);
                    const double t1 = juce::Time::getMillisecondCounterHiRes();
                    {
                        juce::Graphics g (page);
                        g.addTransform (juce::AffineTransform::scale (2.0f));
                        editor->paintEntireComponent (g, true);
                    }
                    step += t1 - t0;
                    draw += juce::Time::getMillisecondCounterHiRes() - t1;
                }
                std::printf ("%-7s step %.2f ms  draw %.2f ms  per frame, %d%% of the word at rest\n", names[pass], step / n, draw / n, tiles[0] > 0 ? 100 * tiles[1] / tiles[0] : 0);
                editor->benchTiles = nullptr;
            }
            // how far the quick drawing sits from plain outlines: draw one frame both ways (edge pixels differ a little)
            juce::Image plain (juce::Image::ARGB, 2560, 1440, true);
            for (auto* img : { &page, &plain })
            {
                editor->plainDraw = img == &plain;
                juce::Graphics g (*img);
                g.addTransform (juce::AffineTransform::scale (2.0f));
                editor->paintEntireComponent (g, true);
            }
            editor->plainDraw = false;
            int worst = 0, differing = 0;
            {
                const juce::Image::BitmapData a (page, juce::Image::BitmapData::readOnly), b (plain, juce::Image::BitmapData::readOnly);
                for (int y = 0; y < a.height; ++y)
                    for (int x = 0; x < a.width; ++x)
                    {
                        const auto ca = a.getPixelColour (x, y), cb = b.getPixelColour (x, y);
                        const int d = std::max ({ std::abs (ca.getRed() - cb.getRed()), std::abs (ca.getGreen() - cb.getGreen()), std::abs (ca.getBlue() - cb.getBlue()) });
                        worst = std::max (worst, d);
                        differing += d > 0 ? 1 : 0;
                    }
            }
            std::printf ("against plain drawing: %d pixels differ, worst by %d of 255\n", differing, worst);
            return 0;
        }
    if (koi) editor->showPond (true);
    const int frames = 60 * 5;
    juce::AudioBuffer<float> out (2, frames * block), io (2, block);
    juce::MidiBuffer midi;
    const float y = 300.0f;
    double uiSeconds = 0;
    juce::Image image;
    for (int f = 0; f < frames; ++f)
    {
        const float t = (float) f / 60.0f;
        // sweep right across the word for two seconds, strike once, then rest at 70%
        const float x = t < 2.0f ? 120.0f + 520.0f * t : 900.0f;
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        editor->setPointer ({ x, y + 30.0f * std::sin (t * 5.0f) }, true);
        if (f == 150 && ! koi && ! code) editor->strike ({ 500.0f, 280.0f }, 1.0f);
        if (koi && f == 20) editor->click ({ 640.0f, 560.0f });
        // knob: turn each of the four in turn and save its page of the screen
        if (knobs && f >= 20 && f < 20 + 4 * 30)
        {
            const int which = (f - 20) / 30;
            static const float to[4] = { 0.64f, 0.73f, 0.77f, 0.56f };
            editor->turnKnob (which, to[which]);
            if ((f - 20) % 30 == 24)
            {
                const auto name = "_knob" + juce::String (which + 1);
                save (name.toRawUTF8(), 2.0f);
            }
        }
        if (code && f == 6) editor->startCode();
        if (code && f == 50) save ("_code1", 1.0f);
        if (code && f == 110) save ("_code2", 1.0f);
        if (code && f == 150) save ("_code3", 1.0f);
        if (code && f == 190) save ("_code4", 1.0f);
        if (ticks && f >= 180 && f < 180 + 7 * 14 && (f - 180) % 14 == 0) editor->uiSound ((f - 180) / 14, 0.5f);
        editor->advance (1.0f / 60.0f);
        if (f == 130 || f == 156 || f == 299)
        {
            image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
            juce::File shot = f == 130 ? png : f == 299 ? png.getSiblingFile (png.getFileNameWithoutExtension() + "_end.png") : png.getSiblingFile (png.getFileNameWithoutExtension() + "_strike.png");
            shot.deleteFile();
            juce::FileOutputStream os (shot);
            juce::PNGImageFormat().writeImageToStream (image, os);
        }
        uiSeconds += (juce::Time::getMillisecondCounterHiRes() - t0) * 0.001;
        io.clear();
        proc.processBlock (io, midi);
        out.copyFrom (0, f * block, io, 0, 0, block);
        out.copyFrom (1, f * block, io, 1, 0, block);
    }

    wav.deleteFile();
    juce::WavAudioFormat fmt;
    if (auto stream = std::unique_ptr<juce::OutputStream> (wav.createOutputStream()))
        if (auto writer = fmt.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (24)))
            writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());

    auto db = [] (float v) { return juce::Decibels::gainToDecibels (v, -120.0f); };
    std::printf ("peak %.1f dBFS  rms L %.1f R %.1f dBFS  ui+snapshots %.1f ms/frame avg\n",
                 db (out.getMagnitude (0, out.getNumSamples())), db (out.getRMSLevel (0, 0, out.getNumSamples())),
                 db (out.getRMSLevel (1, 0, out.getNumSamples())), 1000.0 * uiSeconds / frames);
    for (int s = 0; s < 5; ++s)
        std::printf ("  second %d: rms %.1f dBFS\n", s, db (out.getRMSLevel (0, s * 48000, 48000)));
    bool finite = true;
    for (int i = 0; i < out.getNumSamples(); ++i) finite = finite && std::isfinite (out.getSample (0, i));
    std::printf ("finite: %s\n", finite ? "yes" : "NO");
    return 0;
}
