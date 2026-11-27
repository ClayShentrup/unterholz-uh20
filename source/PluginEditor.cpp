// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginEditor.h"

using namespace juce;
namespace uh20ui {

static String u8s(const char* s) { return String(CharPointer_UTF8(s)); }
static Font uiFont(float h, bool bold) {
    return Font(FontOptions("Helvetica Neue", h, bold ? Font::bold : Font::plain));
}

void KnobLook::drawRotarySlider(Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s) {
    const float cx = x + w * 0.5f, cy = y + h * 0.5f, r = 27.f;
    g.setColour(Colour(0xff595955));
    for (int t = 0; t <= 10; ++t) {
        const float a = start + (end - start) * t / 10.f;
        g.drawLine(cx + std::sin(a) * (r + 5), cy - std::cos(a) * (r + 5), cx + std::sin(a) * (r + 9), cy - std::cos(a) * (r + 9), 1.f);
    }
    g.setColour(kInk); g.fillEllipse(cx - r, cy - r, 2 * r, 2 * r);
    g.setColour(Colour(0xff383836)); g.drawEllipse(cx - r + 6, cy - r + 6, 2 * r - 12, 2 * r - 12, 1.f);
    const float a = start + (end - start) * pos;
    const bool accent = (bool)s.getProperties()["accent"], dim = (bool)s.getProperties()["dim"];
    g.setColour(accent ? kOrange : (dim ? Colour(0xff737370) : Colour(0xfff5f2eb)));
    g.drawLine(cx + std::sin(a) * 6, cy - std::cos(a) * 6, cx + std::sin(a) * (r - 3), cy - std::cos(a) * (r - 3), 3.f);
}

void Switch::paint(Graphics& g) {
    const bool on = isOn && isOn();
    auto box = Rectangle<float>(0, 0, (float)getWidth(), 22.f);
    g.setColour(kInk); g.drawRect(box, 1.f);
    if (on) { g.setColour(kOrange); g.fillRect(box.reduced(2.f)); }
    g.setColour(on ? Colour(0xfff7f5ee) : kInk); g.setFont(uiFont(12.f, true));
    g.drawText(label, box, Justification::centred);
    g.setColour(kMuted); g.setFont(uiFont(11.f, false));
    g.drawText(sub, Rectangle<float>(0, 25.f, (float)getWidth(), 14.f), Justification::centred);
}

Panel::Panel(AudioProcessorValueTreeState& state) : apvts(state) {
    struct K { const char *id, *de, *en, *help; };
    const K ks[] = {
        {"volume", "LAUT", "rhythm volume", "LAUT (rhythm volume): gain of the clean channel. Clean up to about 5, edge of breakup at 6-7, crunch above."},
        {"drive", "ANTRIEB", "lead drive", "ANTRIEB (lead drive): gain of the LEAD channel only. Low = edge-of-breakup crunch, about 5 = creamy crunch, 8-10 = saturated."},
        {"treble", "HÖHEN", "treble", "HOEHEN (treble): top end. In RHYTHMUS the tone controls come before the gain stages, so more treble also means more gain."},
        {"middle", "MITTEN", "middle", "MITTEN (middle): midrange body. In LEAD it works after the distortion, so it shapes the crunch without changing the gain."},
        {"bass", "TIEFEN", "bass", "TIEFEN (bass): low end. In RHYTHMUS keep it at 5 or below; above that the low end gets loose."},
        {"presence", "PRÄSENZ", "presence", "PRAESENZ (presence): upper-mid bite from the power stage's feedback loop. 0 = smooth and dark, 10 = cutting."},
        {"jubilee", "CLIP", "LED clip", "CLIP: LEDs clip the peaks: flatter tops, more squash and sustain, and thicker because the clipping is asymmetric. 0 = off."},
        {"sag", "DURCHHANG", "sag", "DURCHHANG (sag): resistance in the power supply. Higher = the rails droop under hard chords, so loud notes squash and then bloom back as the capacitors recharge."},
        {"master", "SUMME", "master", "SUMME (master): drive into the power stage. RHYTHMUS: 6.5 = the studio clean tone. LEAD: higher = more power-amp crunch."},
    };
    int i = 0;
    for (auto& k : ks) {
        Knob kn{k.id, u8s(k.de), k.en, k.help, std::make_unique<Slider>(), nullptr};
        auto& s = *kn.s;
        s.setSliderStyle(Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
        s.setRotaryParameters(MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
        s.setMouseDragSensitivity(220);
        s.setLookAndFeel(&look);
        s.setTooltip(kn.help);
        s.getProperties().set("help", kn.help);
        if (i == 6 || i == 7) s.getProperties().set("accent", true);
        addAndMakeVisible(s);
        s.addMouseListener(this, false);
        kn.a = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(apvts, kn.id, s);
        apvts.addParameterListener(kn.id, this);
        knobs.push_back(std::move(kn));
        ++i;
    }
    struct S { const char *label, *sub, *help; std::function<bool()> on; std::function<void()> click; };
    const S ss[] = {
        {"RHYTHMUS", "clean", "RHYTHMUS: the clean channel (studio-preamp style). Use LAUT for its gain.",
         [this] { return value("channel") < 0.5f; }, [this] { setChoice("channel", 0); }},
        {"LEAD", "drive", "LEAD: the drive channel (Marshall-style). Use ANTRIEB for its gain; SUMME adds power-amp crunch.",
         [this] { return value("channel") > 0.5f; }, [this] { setChoice("channel", 1); }},
        {"HELL", "bright", "HELL (bright): treble bleed across LAUT. Adds sparkle at low LAUT settings and fades out as LAUT goes up.",
         [this] { return value("bright") > 0.5f; }, [this] { toggle("bright"); }},
        {"TIEF", "shift", "TIEF (bass shift): lets more low end into the first stage. Fatter, warmer; a little looser.",
         [this] { return value("shift") > 0.5f; }, [this] { toggle("shift"); }},
        {"REC", "analog cab", "REC: the analog cab-sim output (the hardware's REC jack). For recording straight into a DAW.",
         [this] { return value("output") < 0.5f; }, [this] { setChoice("output", 0); }},
        {"AMP", "no cab", "AMP: no cabinet. For a real amp and speaker, or your own cabinet impulse response after the plugin.",
         [this] { return value("output") > 0.5f; }, [this] { setChoice("output", 1); }},
    };
    for (auto& s : ss) {
        auto sw = std::make_unique<Switch>(s.label, s.sub);
        sw->isOn = s.on; sw->onClick = s.click;
        sw->getProperties().set("help", s.help);
        sw->addMouseListener(this, false);
        addAndMakeVisible(*sw);
        switches.push_back(std::move(sw));
    }
    for (auto id : {"channel", "bright", "shift", "output", "involts", "trim"}) apvts.addParameterListener(id, this);
    setSize(W, H);
}

Panel::~Panel() {
    for (auto& k : knobs) { apvts.removeParameterListener(k.id, this); k.s->setLookAndFeel(nullptr); }
    for (auto id : {"channel", "bright", "shift", "output", "involts", "trim"}) apvts.removeParameterListener(id, this);
}

void Panel::setChoice(const char* id, int index) {
    if (auto* p = apvts.getParameter(id)) {
        p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1((float)index)); p->endChangeGesture();
    }
    repaint();
}
void Panel::toggle(const char* id) {
    if (auto* p = apvts.getParameter(id)) {
        p->beginChangeGesture(); p->setValueNotifyingHost(p->getValue() > 0.5f ? 0.f : 1.f); p->endChangeGesture();
    }
    repaint();
}

void Panel::resized() {
    const int KY = 150;
    for (size_t i = 0; i < knobs.size(); ++i) knobs[i].s->setBounds(knobX((int)i) - 38, KY - 38, 76, 76);
    const int SY = 248;
    const int xs[] = {26, 112, 204, 268, 360, 424}, ws[] = {84, 62, 62, 62, 62, 62};
    for (size_t i = 0; i < switches.size(); ++i) switches[i]->setBounds(xs[i], SY, ws[i], 40);
}

void Panel::mouseEnter(const MouseEvent& e) {
    help = e.eventComponent->getProperties()["help"].toString();
    repaint(0, H - 34, W, 34);
}
void Panel::mouseExit(const MouseEvent&) { help = {}; repaint(0, H - 34, W, 34); }

void Panel::paint(Graphics& g) {
    g.fillAll(kPanel);
    g.setColour(kBand); g.fillRect(0, 0, W, 56);
    // title, letter-spaced
    g.setColour(Colour(0xfff2efe6)); g.setFont(uiFont(30.f, true));
    float x = 26;
    for (auto c : String("UNTERHOLZ")) {
        const String ch = String::charToString(c);
        g.drawSingleLineText(ch, (int)x, 42);
        x += GlyphArrangement::getStringWidth(g.getCurrentFont(), ch) + 7.f;
    }
    g.setColour(kOrange); g.fillRect(W - 30, 14, 10, 28);
    g.setColour(Colour(0xffcccac0)); g.setFont(uiFont(12.f, true));
    g.drawSingleLineText(u8s("UH-20   JFET-VORVERSTÄRKER"), W - 300, 26);
    g.setColour(Colour(0xff8c8c85)); g.setFont(uiFont(11.f, false));
    g.drawSingleLineText("OPEN HARDWARE  /  ENTWURF 11.2026", W - 300, 44);
    // rules and section labels
    g.setColour(kRule);
    g.drawLine(20, 74, (float)W - 20, 74); g.drawLine(20, 232, (float)W - 20, 232);
    g.drawLine((float)knobX(5) + 52, 84, (float)knobX(5) + 52, 222); g.drawLine((float)knobX(7) + 52, 84, (float)knobX(7) + 52, 222);
    g.setColour(Colour(0xff666661)); g.setFont(uiFont(11.f, false));
    g.drawSingleLineText("VORSTUFE / preamp", 24, 92);
    g.drawSingleLineText("CHARAKTER", knobX(6) - 30, 92);
    g.drawSingleLineText("AUSGANG", knobX(8) - 30, 92);
    // knob captions and values
    const int KY = 150;
    const bool rhythm = value("channel") < 0.5f;
    for (size_t i = 0; i < knobs.size(); ++i) {
        auto& k = knobs[i];
        const bool dim = (i == 1 && rhythm);
        if ((bool)k.s->getProperties()["dim"] != dim) { k.s->getProperties().set("dim", dim); k.s->repaint(); }
        const int cx = knobX((int)i);
        g.setColour(kInk); g.setFont(uiFont(12.f, true));
        g.drawText(k.de, cx - 50, KY + 38, 100, 16, Justification::centred);
        g.setColour(kMuted); g.setFont(uiFont(11.f, false));
        g.drawText(k.en, cx - 50, KY + 53, 100, 14, Justification::centred);
        g.setColour(kInk); g.setFont(uiFont(12.f, true));
        g.drawText(String(value(k.id.toRawUTF8()), 1), cx - 30, KY - 27 - 26, 60, 16, Justification::centred);
    }
    g.setColour(Colour(0xff666661)); g.setFont(uiFont(11.f, false));
    g.drawSingleLineText("EINGANG " + String(value("involts"), 2) + " V = 0 dBFS     TRIM " + (value("trim") >= 0 ? "+" : "") + String(value("trim"), 1) + " dB",
                         W - 310, 248 + 16);
    // hover help strip
    g.setColour(kBand); g.fillRect(0, H - 34, W, 34);
    if (help.isNotEmpty()) { g.setColour(kOrange); g.fillRect(12, H - 24, 6, 14); }
    g.setColour(help.isNotEmpty() ? Colour(0xfff2efe6) : Colour(0xff8c8c85)); g.setFont(uiFont(11.f, false));
    g.drawText(help.isNotEmpty() ? help : String("Point at any knob or switch to see what it does."), 26, H - 34, W - 40, 34, Justification::centredLeft);
}

} // namespace uh20ui

UH20Editor::UH20Editor(UH20Processor& p) : AudioProcessorEditor(p), panel(p.apvts) {
    addAndMakeVisible(panel);
    setResizable(true, true);
    getConstrainer()->setFixedAspectRatio((double)uh20ui::Panel::W / uh20ui::Panel::H);
    setResizeLimits(uh20ui::Panel::W / 2, uh20ui::Panel::H / 2, uh20ui::Panel::W * 2, uh20ui::Panel::H * 2);
    setSize(uh20ui::Panel::W, uh20ui::Panel::H);
}

void UH20Editor::resized() {
    const float s = (float)getWidth() / uh20ui::Panel::W;
    panel.setBounds(0, 0, uh20ui::Panel::W, uh20ui::Panel::H);
    panel.setTransform(AffineTransform::scale(s));
}
