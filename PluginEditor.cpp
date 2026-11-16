#include "PluginEditor.h"

HarmLookAndFeel::HarmLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Theme::bg);
    setColour (juce::Label::textColourId, Theme::text);
    setColour (juce::Slider::textBoxTextColourId, Theme::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId, Theme::panel);
    setColour (juce::ComboBox::outlineColourId, Theme::track);
    setColour (juce::ComboBox::textColourId, Theme::text);
    setColour (juce::ComboBox::arrowColourId, Theme::textDim);
    setColour (juce::PopupMenu::backgroundColourId, Theme::panel);
    setColour (juce::PopupMenu::textColourId, Theme::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, Theme::text);
}

void HarmLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                        float start, float end, juce::Slider&)
{
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float angle = start + pos * (end - start);

    juce::Path bgArc, valArc;
    bgArc.addCentredArc (c.x, c.y, r - 3.0f, r - 3.0f, 0.0f, start, end, true);
    valArc.addCentredArc (c.x, c.y, r - 3.0f, r - 3.0f, 0.0f, start, angle, true);
    g.setColour (Theme::track);
    g.strokePath (bgArc, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (Theme::accent);
    g.strokePath (valArc, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (Theme::panel.brighter (0.15f));
    g.fillEllipse (c.x - r * 0.62f, c.y - r * 0.62f, r * 1.24f, r * 1.24f);
    juce::Path tick;
    tick.addRoundedRectangle (-1.5f, -r * 0.6f, 3.0f, r * 0.32f, 1.5f);
    g.setColour (Theme::text);
    g.fillPath (tick, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& name)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    addAndMakeVisible (slider);
    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::Font (juce::FontOptions (12.5f)));
    label.setColour (juce::Label::textColourId, Theme::textDim);
    addAndMakeVisible (label);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id, slider);
}

void Knob::resized()
{
    auto b = getLocalBounds();
    label.setBounds (b.removeFromTop (16));
    slider.setBounds (b);
}

HarmonizerEditor::HarmonizerEditor (HarmonizerProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      input (p.apvts, "input", "Input"), dry (p.apvts, "dry", "Voice"), harmony (p.apvts, "harmony", "Harmonies"),
      formant (p.apvts, "formant", "Formant"), consonants (p.apvts, "consonants", "Consonants"),
      velocity (p.apvts, "velocity", "Velocity"),
      attack (p.apvts, "attack", "Attack"), release (p.apvts, "release", "Release"), spread (p.apvts, "spread", "Spread"),
      chorus (p.apvts, "chorus", "Chorus"), delay (p.apvts, "delay", "Delay"), delayTime (p.apvts, "delaytime", "Time"),
      feedback (p.apvts, "feedback", "Feedback"), reverb (p.apvts, "reverb", "Reverb"), size (p.apvts, "size", "Size"),
      output (p.apvts, "output", "Output")
{
    setLookAndFeel (&lnf);
    for (auto* k : { &input, &dry, &harmony, &formant, &consonants, &velocity, &attack, &release, &spread,
                     &chorus, &delay, &delayTime, &feedback, &reverb, &size, &output })
        addAndMakeVisible (k);

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("lowest")))
        lowest.addItemList (choice->choices, 1);
    addAndMakeVisible (lowest);
    lowestAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "lowest", lowest);
    lowestLabel.setText ("Lowest note", juce::dontSendNotification);
    lowestLabel.setJustificationType (juce::Justification::centred);
    lowestLabel.setFont (juce::Font (juce::FontOptions (12.5f)));
    lowestLabel.setColour (juce::Label::textColourId, Theme::textDim);
    addAndMakeVisible (lowestLabel);

    setSize (760, 470);
    startTimerHz (20);
}

HarmonizerEditor::~HarmonizerEditor()
{
    setLookAndFeel (nullptr);
}

juce::String HarmonizerEditor::noteName (float hz)
{
    if (hz <= 0.0f) return "-";
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int note = (int) std::lround (69.0 + 12.0 * std::log2 (hz / 440.0));
    return juce::String (names[((note % 12) + 12) % 12]) + juce::String (note / 12 - 1);
}

void HarmonizerEditor::timerCallback()
{
    statusSinging = proc.singing.load();
    statusNote = statusSinging ? noteName (proc.detectedHz.load()) : "-";
    statusVoices = juce::String (proc.heldVoices.load());
    statusLevel = proc.inputLevel.load();
    repaint (secStatus);
}

void HarmonizerEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bg);

    g.setColour (Theme::text);
    g.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    g.drawText ("GLIZZARINE", 20, 12, 150, 26, juce::Justification::centredLeft);
    g.setColour (Theme::accent);
    g.drawText ("HARMONIZER", 152, 12, 200, 26, juce::Justification::centredLeft);

    auto section = [&g] (juce::Rectangle<int> r, const juce::String& title)
    {
        g.setColour (Theme::panel);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (Theme::edge);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
        g.setColour (Theme::accent);
        g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
        g.drawText (title, r.getX() + 12, r.getY() + 6, 200, 16, juce::Justification::centredLeft);
    };
    section (secVoice, "VOICE");
    section (secPlay, "PLAY");
    section (secSpace, "SPACE");
    section (secStatus, "INPUT");

    // status: input meter, detected note, held keys
    auto s = secStatus.reduced (12).withTrimmedTop (18);
    auto meter = s.removeFromLeft (14);
    g.setColour (Theme::track);
    g.fillRoundedRectangle (meter.toFloat(), 3.0f);
    const float lvl = juce::jlimit (0.0f, 1.0f, 1.0f + juce::Decibels::gainToDecibels (statusLevel + 1.0e-6f) / 60.0f);
    auto filled = meter.toFloat();
    filled = filled.removeFromBottom (filled.getHeight() * lvl);
    g.setColour (statusLevel > 0.9f ? juce::Colours::red : Theme::accent);
    g.fillRoundedRectangle (filled, 3.0f);

    s.removeFromLeft (14);
    g.setColour (statusSinging ? Theme::accent : Theme::track);
    g.fillEllipse ((float) s.getX(), (float) s.getY() + 4.0f, 10.0f, 10.0f);
    g.setColour (Theme::textDim);
    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    g.drawText (statusSinging ? "singing" : "silent", s.getX() + 16, s.getY(), 120, 18, juce::Justification::centredLeft);
    g.drawText ("detected note", s.getX(), s.getY() + 30, 140, 16, juce::Justification::centredLeft);
    g.setColour (Theme::text);
    g.setFont (juce::Font (juce::FontOptions (30.0f, juce::Font::bold)));
    g.drawText (statusNote, s.getX(), s.getY() + 46, 140, 36, juce::Justification::centredLeft);
    g.setColour (Theme::textDim);
    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    g.drawText ("keys held: " + statusVoices, s.getX(), s.getY() + 92, 140, 16, juce::Justification::centredLeft);
    const double ms = 1000.0 * proc.latencySamples.load() / juce::jmax (1.0, proc.getCurrentSampleRate());
    g.drawText ("latency: " + juce::String (ms, 1) + " ms", s.getX(), s.getY() + 110, 140, 16, juce::Justification::centredLeft);
}

void HarmonizerEditor::resized()
{
    auto area = getLocalBounds().reduced (14).withTrimmedTop (36);
    auto top = area.removeFromTop (205);
    secStatus = top.removeFromLeft (180);
    top.removeFromLeft (10);
    secVoice = top;
    area.removeFromTop (10);
    secPlay = area.removeFromLeft (310);
    area.removeFromLeft (10);
    secSpace = area;

    auto layoutRow = [] (juce::Rectangle<int> r, std::initializer_list<juce::Component*> items)
    {
        r = r.reduced (8).withTrimmedTop (18);
        const int w = r.getWidth() / (int) items.size();
        for (auto* c : items) c->setBounds (r.removeFromLeft (w).reduced (2));
    };

    auto v = secVoice;
    layoutRow (v, { &input, &dry, &harmony, &formant, &consonants, &velocity });

    auto pl = secPlay.reduced (8).withTrimmedTop (18);
    const int w = pl.getWidth() / 4;
    attack.setBounds (pl.removeFromLeft (w).reduced (2));
    release.setBounds (pl.removeFromLeft (w).reduced (2));
    spread.setBounds (pl.removeFromLeft (w).reduced (2));
    auto low = pl.reduced (2);
    lowestLabel.setBounds (low.removeFromTop (16));
    lowest.setBounds (low.removeFromTop (26).reduced (0, 2));

    layoutRow (secSpace, { &chorus, &delay, &delayTime, &feedback, &reverb, &size, &output });
}
