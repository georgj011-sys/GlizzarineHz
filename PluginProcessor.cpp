#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const float lowestNoteHz[] = { 78.0f, 104.0f, 140.0f, 185.0f };   // E2(-), A2(-), D3(-), G3(-), a little below

    juce::NormalisableRange<float> msRange (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout HarmonizerProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto add = [&p] (const char* id, const char* name, NormalisableRange<float> r, float def, const char* unit = "")
    {
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
                                                            AudioParameterFloatAttributes().withLabel (unit)));
    };

    add ("input",      "Input Gain",   NormalisableRange<float> (-12.0f, 24.0f), 0.0f, "dB");
    add ("dry",        "Voice",        NormalisableRange<float> (0.0f, 1.0f), 0.8f);
    add ("harmony",    "Harmonies",    NormalisableRange<float> (0.0f, 1.0f), 0.7f);
    add ("formant",    "Formant",      NormalisableRange<float> (-1.0f, 1.0f), 0.0f);
    add ("consonants", "Consonants",   NormalisableRange<float> (0.0f, 1.0f), 0.6f);
    add ("velocity",   "Velocity",     NormalisableRange<float> (0.0f, 1.0f), 0.6f);
    add ("attack",     "Attack",       msRange (1.0f, 500.0f, 30.0f), 15.0f, "ms");
    add ("release",    "Release",      msRange (0.05f, 6.0f, 0.8f), 0.6f, "s");
    add ("spread",     "Stereo Spread", NormalisableRange<float> (0.0f, 1.0f), 0.7f);
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "lowest", 1 }, "Lowest Sung Note",
                                                         StringArray { "E2 (bass)", "A2", "D3", "G3 (high voice)" }, 0));
    add ("chorus",     "Chorus",       NormalisableRange<float> (0.0f, 1.0f), 0.15f);
    add ("delay",      "Delay",        NormalisableRange<float> (0.0f, 1.0f), 0.0f);
    add ("delaytime",  "Delay Time",   msRange (40.0f, 1500.0f, 375.0f), 375.0f, "ms");
    add ("feedback",   "Feedback",     NormalisableRange<float> (0.0f, 0.9f), 0.35f);
    add ("reverb",     "Reverb",       NormalisableRange<float> (0.0f, 1.0f), 0.25f);
    add ("size",       "Room Size",    NormalisableRange<float> (0.0f, 1.0f), 0.7f);
    add ("output",     "Output",       NormalisableRange<float> (-24.0f, 12.0f), 0.0f, "dB");
    return { p.begin(), p.end() };
}

HarmonizerProcessor::HarmonizerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Voice", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

bool HarmonizerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void HarmonizerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate);
    lastLowest = -1.0f;

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (1, samplesPerBlock), 2 };
    chorus.prepare (spec);
    chorus.setRate (0.6f);
    chorus.setDepth (0.3f);
    chorus.setCentreDelay (9.0f);
    chorus.setFeedback (0.0f);
    reverb.prepare (spec);
    delay.prepare (sampleRate);

    mono.assign ((size_t) juce::jmax (1, samplesPerBlock), 0.0f);
    outGain.reset (sampleRate, 0.05);
    inGain.reset (sampleRate, 0.05);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (val ("output")));
    inGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (val ("input")));
}

void HarmonizerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int inCh = getTotalNumInputChannels();
    if (n <= 0) return;
    if ((int) mono.size() < n) mono.resize ((size_t) n);

    // parameters
    HarmonizerEngine::Params hp;
    hp.dry = val ("dry");
    hp.harmony = val ("harmony");
    hp.formant = val ("formant");
    hp.consonants = val ("consonants");
    hp.attack = val ("attack") * 0.001f;
    hp.release = val ("release");
    hp.spread = val ("spread");
    const int lowIdx = juce::jlimit (0, 3, (int) val ("lowest"));
    hp.lowestHz = lowestNoteHz[lowIdx];
    engine.setParams (hp);
    engine.setVelocitySensitivity (val ("velocity"));
    if (hp.lowestHz != lastLowest)
    {
        lastLowest = hp.lowestHz;
        latencySamples = engine.getLatencySamples();
        setLatencySamples (engine.getLatencySamples());
    }

    inGain.setTargetValue (juce::Decibels::decibelsToGain (val ("input")));
    outGain.setTargetValue (juce::Decibels::decibelsToGain (val ("output")));

    // mono voice input
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float s = 0.0f;
        for (int c = 0; c < juce::jmin (2, inCh); ++c) s += buffer.getReadPointer (c)[i];
        if (inCh > 1) s *= 0.5f;
        s *= inGain.getNextValue();
        mono[(size_t) i] = s;
        peak = juce::jmax (peak, std::abs (s));
    }
    inputLevel = juce::jmax (peak, inputLevel.load() * 0.9f);

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    // MIDI and audio, split at each MIDI event so notes start on time
    int pos = 0;
    auto renderTo = [&] (int end)
    {
        if (end > pos) engine.process (mono.data() + pos, L + pos, R + pos, end - pos);
        pos = end;
    };

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        renderTo (juce::jlimit (0, n, meta.samplePosition));
        if (m.isNoteOn())                         engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff())                   engine.noteOff (m.getNoteNumber());
        else if (m.isSustainPedalOn())            engine.setSustain (true);
        else if (m.isSustainPedalOff())           engine.setSustain (false);
        else if (m.isAllNotesOff() || m.isAllSoundOff()) { engine.setSustain (false); engine.allNotesOff(); }
    }
    renderTo (n);
    midi.clear();

    // effects
    juce::dsp::AudioBlock<float> block (buffer);
    auto stereoBlock = block.getSubsetChannelBlock (0, 2);
    const float chorusMix = val ("chorus");
    if (chorusMix > 0.001f)
    {
        chorus.setMix (chorusMix * 0.5f);
        chorus.process (juce::dsp::ProcessContextReplacing<float> (stereoBlock));
    }
    delay.process (L, R, n, val ("delay") * 0.6f, val ("delaytime"), val ("feedback"));

    const float rv = val ("reverb");
    juce::dsp::Reverb::Parameters rp;
    rp.roomSize = 0.3f + 0.69f * val ("size");
    rp.damping = 0.4f;
    rp.wetLevel = rv * 0.45f;
    rp.dryLevel = 1.0f - rv * 0.35f;
    rp.width = 1.0f;
    rp.freezeMode = 0.0f;
    reverb.setParameters (rp);
    if (rv > 0.001f)
        reverb.process (juce::dsp::ProcessContextReplacing<float> (stereoBlock));

    // output gain + gentle safety (only bends near full scale)
    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.getNextValue();
        auto safe = [] (float x)
        {
            const float a = std::abs (x);
            return a < 0.9f ? x : std::copysign (0.9f + 0.1f * std::tanh ((a - 0.9f) * 10.0f), x);
        };
        L[i] = safe (L[i] * g);
        R[i] = safe (R[i] * g);
    }
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);

    detectedHz = engine.isVoiced() ? engine.currentPitchHz() : 0.0f;
    singing = engine.isVoiced();
    heldVoices = engine.activeVoices();
}

void HarmonizerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void HarmonizerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* HarmonizerProcessor::createEditor()
{
    return new HarmonizerEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HarmonizerProcessor();
}
