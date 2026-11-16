#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include "HarmonizerEngine.h"

// Clean stereo ping-pong delay
class PingPongDelay
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        size = (int) (sr * 2.1) + 4;
        bufL.assign ((size_t) size, 0.0f);
        bufR.assign ((size_t) size, 0.0f);
        w = 0;
        lpL = lpR = 0.0f;
    }

    void process (float* L, float* R, int n, float mix, float timeMs, float feedback)
    {
        if (mix <= 0.0001f) return;
        const int d = juce::jlimit (1, size - 2, (int) (timeMs * 0.001 * sr));
        const float c = 0.35f; // gentle darkening of the repeats
        for (int i = 0; i < n; ++i)
        {
            int r = w - d; if (r < 0) r += size;
            const float yl = bufL[(size_t) r], yr = bufR[(size_t) r];
            lpL += c * (yl - lpL); lpR += c * (yr - lpR);
            const float inMono = 0.5f * (L[i] + R[i]);
            bufL[(size_t) w] = inMono + lpR * feedback;   // cross-feed = ping-pong
            bufR[(size_t) w] = lpL * feedback;
            L[i] += yl * mix;
            R[i] += yr * mix;
            if (++w >= size) w = 0;
        }
    }

private:
    double sr = 44100.0;
    std::vector<float> bufL, bufR;
    int size = 0, w = 0;
    float lpL = 0.0f, lpR = 0.0f;
};

class HarmonizerProcessor : public juce::AudioProcessor
{
public:
    HarmonizerProcessor();
    ~HarmonizerProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Glizzarine Harmonizer"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    // for the display
    std::atomic<float> detectedHz { 0.0f };
    std::atomic<bool> singing { false };
    std::atomic<int> heldVoices { 0 };
    std::atomic<float> inputLevel { 0.0f };
    std::atomic<int> latencySamples { 0 };
    double getCurrentSampleRate() const { return currentSampleRate; }

private:
    float val (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    HarmonizerEngine engine;
    juce::dsp::Chorus<float> chorus;
    juce::dsp::Reverb reverb;
    PingPongDelay delay;
    std::vector<float> mono;
    juce::LinearSmoothedValue<float> outGain, inGain;
    double currentSampleRate = 44100.0;
    float lastLowest = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonizerProcessor)
};
