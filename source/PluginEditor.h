// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

namespace uh20ui {

const juce::Colour kPanel{0xffdbd9cf}, kInk{0xff1a1a1a}, kBand{0xff141414}, kOrange{0xffe05c1f}, kRule{0xffb3b0a6}, kMuted{0xff6b6b66};

class KnobLook : public juce::LookAndFeel_V4 {
public:
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
};

// a flat switch button: black outline, filled orange when on
class Switch : public juce::Component {
public:
    Switch(juce::String label, juce::String sub) : label(std::move(label)), sub(std::move(sub)) {}
    std::function<bool()> isOn;
    std::function<void()> onClick;
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }
private:
    juce::String label, sub;
};

// the 900 x 338 front panel
class Panel : public juce::Component, private juce::AudioProcessorValueTreeState::Listener, private juce::AsyncUpdater {
public:
    explicit Panel(juce::AudioProcessorValueTreeState& state);
    ~Panel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    static constexpr int W = 900, H = 338;
private:
    juce::AudioProcessorValueTreeState& apvts;
    KnobLook look;
    struct Knob { juce::String id, de, en, help; std::unique_ptr<juce::Slider> s; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> a; };
    std::vector<Knob> knobs;
    std::vector<std::unique_ptr<Switch>> switches;
    juce::String help;
    static int knobX(int i) { return 56 + i * 90 + (i >= 6 ? 14 : 0); }
    void parameterChanged(const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { repaint(); }
    float value(const char* id) const { return apvts.getRawParameterValue(id)->load(); }
    void setChoice(const char* id, int index);
    void toggle(const char* id);
};

} // namespace uh20ui

class UH20Editor : public juce::AudioProcessorEditor {
public:
    explicit UH20Editor(UH20Processor&);
    void resized() override;
    void paint(juce::Graphics& g) override { g.fillAll(uh20ui::kPanel); }
private:
    uh20ui::Panel panel;
    juce::TooltipWindow tooltips{this, 600};
};
