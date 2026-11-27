// Renders the plugin panel to a PNG (for the README): uh20_screenshot out.png [scale]
#include <juce_gui_basics/juce_gui_basics.h>
#include "../source/PluginProcessor.h"
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out(juce::File::getCurrentWorkingDirectory().getChildFile(argc > 1 ? argv[1] : "screenshot.png"));
    const float scale = argc > 2 ? (float)std::atof(argv[2]) : 2.0f;
    UH20Processor proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, scale);
    out.deleteFile();
    juce::FileOutputStream os(out);
    juce::PNGImageFormat().writeImageToStream(img, os);
    std::printf("wrote %s (%dx%d)\n", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    return 0;
}
