// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"
#include "PluginEditor.h"

UH20Processor::UH20Processor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "UH20", createLayout()) {}

juce::AudioProcessorValueTreeState::ParameterLayout UH20Processor::createLayout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto knob = [&](const char* id, const char* name, float def) {
        l.add(std::make_unique<AudioParameterFloat>(ParameterID{id, 1}, name, NormalisableRange<float>(0.f, 10.f, 0.1f), def));
    };
    knob("volume", "Volume", 5.f);
    knob("drivestage", "Drive", 0.f);   // new ID in v0.3: the v0.2 lead channel's "drive" must not switch the stage in on old sessions
    knob("treble", "Treble", 3.f);
    knob("middle", "Middle", 7.f);
    knob("bass", "Bass", 5.f);
    knob("presence", "Presence", 5.f);
    knob("jubilee", "Clip", 0.f);   // ID kept from v0.1 ("jubilee") so saved sessions load
    knob("sag", "Sag", 0.f);
    knob("master", "Master", 6.5f);
    l.add(std::make_unique<AudioParameterBool>(ParameterID{"bright", 1}, "Bright", false));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{"shift", 1}, "Bass shift", false));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"output", 1}, "Output", StringArray{"REC (analog cab)", "AMP (no cab)"}, 0));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"involts", 1}, "Input: volts at 0 dBFS", NormalisableRange<float>(0.1f, 4.f, 0.01f), 1.6f));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"trim", 1}, "Output trim (dB)", NormalisableRange<float>(-24.f, 24.f, 0.1f), 0.f));
    return l;
}

uh20::Params UH20Processor::readParams() const {
    auto v = [&](const char* id) { return (double)apvts.getRawParameterValue(id)->load(); };
    uh20::Params p;
    p.volume = v("volume"); p.drive = v("drivestage"); p.treble = v("treble"); p.middle = v("middle"); p.bass = v("bass");
    p.presence = v("presence"); p.clip = v("jubilee"); p.sag = v("sag"); p.master = v("master");
    p.bright = v("bright") > 0.5; p.shift = v("shift") > 0.5;
    p.output = (int)v("output"); p.inputVolts = v("involts"); p.trimDb = v("trim");
    return p;
}

void UH20Processor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    engine.setParams(readParams());
    engine.prepare(sampleRate);
    mono.assign((size_t)juce::jmax(1, samplesPerBlock), 0.f);
}

bool UH20Processor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void UH20Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nin = getTotalNumInputChannels(), nout = getTotalNumOutputChannels();
    if ((int)mono.size() < n) mono.resize((size_t)n);
    for (int i = 0; i < n; ++i) {
        float s = 0;
        for (int c = 0; c < nin; ++c) s += buffer.getReadPointer(c)[i];
        mono[(size_t)i] = nin > 0 ? s / (float)nin : 0.f;
    }
    engine.setParams(readParams());
    engine.process(mono.data(), n);
    for (int c = 0; c < nout; ++c) buffer.copyFrom(c, 0, mono.data(), n);
}

juce::AudioProcessorEditor* UH20Processor::createEditor() { return new UH20Editor(*this); }

void UH20Processor::getStateInformation(juce::MemoryBlock& destData) {
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary(*xml, destData);
}
void UH20Processor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType())) apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

#ifndef UH20_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new UH20Processor(); }
#endif
