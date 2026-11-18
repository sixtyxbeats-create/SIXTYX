#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

class SteinexTransitFXProcessor;

class SteinexTransitFXEditor : public juce::AudioProcessorEditor {
public:
    explicit SteinexTransitFXEditor(juce::AudioProcessor& p) : AudioProcessorEditor(&p), processorRef(p) {
        setSize(920, 540);
        setupKnob(macroKnob, "TRANSITION");
        setupKnob(backverbKnob, "BACKVERB");
        setupKnob(chopKnob, "CHOPPER");
        setupKnob(glitchKnob, "GLITCH");

        stutterButton.setButtonText("STUTTER TRIGGER");
        stutterButton.setClickingTogglesState(true);
        addAndMakeVisible(stutterButton);

        auto& apvts = static_cast<SteinexTransitFXProcessor&>(processorRef).apvts;
        macroAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "TRANSITION_MACRO", macroKnob);
        backverbAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "BACKVERB_MIX", backverbKnob);
        chopAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "CHOP_DEPTH", chopKnob);
        glitchAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "GLITCH_AMT", glitchKnob);
        stutterAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, "STUTTER_ON", stutterButton);
    }

    ~SteinexTransitFXEditor() override = default;

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xFF0D0F12));
        g.setColour(juce::Colour(0xFF14181F));
        g.fillRect(0, 0, getWidth(), 56);
        g.setColour(juce::Colour(0xFF00F0FF));
        g.setFont(juce::FontOptions("Segoe UI", 22.0f, juce::Font::bold));
        g.drawText("STEINEX", 24, 12, 140, 32, juce::Justification::left);
        g.setColour(juce::Colour(0xFFE6EAF0));
        g.setFont(juce::FontOptions("Segoe UI", 18.0f, juce::Font::plain));
        g.drawText("|  TRANSIT FX", 135, 13, 200, 32, juce::Justification::left);
    }

    void resized() override {
        macroKnob.setBounds(370, 80, 180, 180);
        stutterButton.setBounds(55, 360, 170, 90);
        backverbKnob.setBounds(275, 340, 130, 130);
        chopKnob.setBounds(485, 340, 130, 130);
        glitchKnob.setBounds(695, 340, 130, 130);
    }

private:
    void setupKnob(juce::Slider& slider, const juce::String& name) {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
        slider.setName(name);
        addAndMakeVisible(slider);
    }

    juce::AudioProcessor& processorRef;
    juce::Slider macroKnob, backverbKnob, chopKnob, glitchKnob;
    juce::TextButton stutterButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> macroAttach, backverbAttach, chopAttach, glitchAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> stutterAttach;
};

juce::AudioProcessorEditor* SteinexTransitFXProcessor::createEditor() {
    return new SteinexTransitFXEditor(*this);
}
