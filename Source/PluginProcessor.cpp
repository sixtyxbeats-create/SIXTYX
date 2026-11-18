#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

class CircularAudioBuffer {
public:
    void prepare(double sampleRate, int maxSeconds, int numChannels) {
        bufferSize = static_cast<int>(sampleRate * maxSeconds);
        buffer.setSize(numChannels, bufferSize);
        buffer.clear();
        writePosition = 0;
    }
    void pushSamples(const juce::AudioBuffer<float>& input) {
        const int numSamples = input.getNumSamples();
        const int numChannels = juce::jmin(buffer.getNumChannels(), input.getNumChannels());
        for (int ch = 0; ch < numChannels; ++ch) {
            const float* src = input.getReadPointer(ch);
            float* dst = buffer.getWritePointer(ch);
            if (writePosition + numSamples <= bufferSize) {
                std::memcpy(dst + writePosition, src, sizeof(float) * numSamples);
            } else {
                const int firstPart = bufferSize - writePosition;
                const int secondPart = numSamples - firstPart;
                std::memcpy(dst + writePosition, src, sizeof(float) * firstPart);
                std::memcpy(dst, src + firstPart, sizeof(float) * secondPart);
            }
        }
        writePosition = (writePosition + numSamples) % bufferSize;
    }
    float readInterpolated(int channel, float samplesBehind) const {
        float readPos = static_cast<float>(writePosition) - samplesBehind;
        while (readPos < 0.0f) readPos += static_cast<float>(bufferSize);
        while (readPos >= static_cast<float>(bufferSize)) readPos -= static_cast<float>(bufferSize);
        const int idx0 = static_cast<int>(readPos);
        const int idx1 = (idx0 + 1) % bufferSize;
        const float frac = readPos - static_cast<float>(idx0);
        const float* data = buffer.getReadPointer(channel);
        return data[idx0] + frac * (data[idx1] - data[idx0]);
    }
private:
    juce::AudioBuffer<float> buffer;
    int bufferSize = 0;
    int writePosition = 0;
};

class TransitDSPChain {
public:
    void prepare(const juce::dsp::ProcessSpec& spec) {
        sampleRate = spec.sampleRate;
        historyBuffer.prepare(sampleRate, 8, spec.numChannels);
        svfFilter.prepare(spec);
        svfFilter.setType(juce::dsp::StateVariableTPTFilterType::highpass);
        reverb.prepare(spec);
        juce::Reverb::Parameters revParams;
        revParams.roomSize = 0.88f; revParams.damping = 0.35f; revParams.wetLevel = 1.0f; revParams.dryLevel = 0.0f; revParams.width = 1.0f;
        reverb.setParameters(revParams);
        wetBuffer.setSize(spec.numChannels, spec.maximumBlockSize);
    }
    void process(juce::AudioBuffer<float>& buffer, double bpm, double ppqPosition,
                 float transitionMacro, bool stutterActive, int stutterDiv,
                 float reverseMix, float gateDepth, float glitchAmount) {
        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        historyBuffer.pushSamples(buffer);
        const double samplesPerBeat = (60.0 / juce::jmax(20.0, bpm)) * sampleRate;

        if (stutterActive) {
            const int sliceLength = juce::jmax(64, static_cast<int>(samplesPerBeat / stutterDiv));
            for (int i = 0; i < numSamples; ++i) {
                const int posInSlice = stutterPhase % sliceLength;
                for (int ch = 0; ch < numChannels; ++ch) {
                    float s = historyBuffer.readInterpolated(ch, static_cast<float>(sliceLength - posInSlice));
                    buffer.setSample(ch, i, s);
                }
                stutterPhase++;
            }
        } else { stutterPhase = 0; }

        if (reverseMix > 0.001f) {
            wetBuffer.setSize(numChannels, numSamples, false, false, true);
            const int swellLength = static_cast<int>(samplesPerBeat * 2.0);
            for (int i = 0; i < numSamples; ++i) {
                float revOffset = static_cast<float>((reversePhase % swellLength) + 1);
                float swellEnv = (static_cast<float>(reversePhase % swellLength) / static_cast<float>(swellLength));
                for (int ch = 0; ch < numChannels; ++ch) {
                    wetBuffer.setSample(ch, i, historyBuffer.readInterpolated(ch, revOffset) * (swellEnv * swellEnv));
                }
                reversePhase++;
            }
            if (numChannels >= 2) reverb.processStereo(wetBuffer.getWritePointer(0), wetBuffer.getWritePointer(1), numSamples);
            for (int ch = 0; ch < numChannels; ++ch) buffer.addFrom(ch, 0, wetBuffer, ch, 0, numSamples, reverseMix);
        }

        if (gateDepth > 0.001f) {
            const double sixteenthPos = std::fmod(ppqPosition * 4.0, 1.0);
            buffer.applyGain((sixteenthPos < 0.5) ? 1.0f : (1.0f - gateDepth));
        }

        if (glitchAmount > 0.001f) {
            const int holdFactor = 1 + static_cast<int>(glitchAmount * 24.0f);
            const float bitSteps = std::pow(2.0f, 16.0f - (glitchAmount * 12.0f));
            for (int ch = 0; ch < numChannels; ++ch) {
                float* data = buffer.getWritePointer(ch);
                float held = 0.0f;
                for (int i = 0; i < numSamples; ++i) {
                    if (i % holdFactor == 0) held = std::round(data[i] * bitSteps) / bitSteps;
                    data[i] = held;
                }
            }
        }

        if (transitionMacro > 0.001f) {
            svfFilter.setCutoffFrequency(20.0f * std::pow(400.0f, transitionMacro));
            svfFilter.setResonance(1.0f + transitionMacro * 4.5f);
            juce::dsp::AudioBlock<float> block(buffer);
            svfFilter.process(juce::dsp::ProcessContextReplacing<float>(block));
        }
    }
private:
    double sampleRate = 44100.0;
    CircularAudioBuffer historyBuffer;
    juce::AudioBuffer<float> wetBuffer;
    juce::dsp::StateVariableTPTFilter<float> svfFilter;
    juce::Reverb reverb;
    int stutterPhase = 0, reversePhase = 0;
};

class SteinexTransitFXProcessor : public juce::AudioProcessor {
public:
    SteinexTransitFXProcessor()
        : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          apvts(*this, nullptr, "PARAMS", createParameterLayout()) {}
    ~SteinexTransitFXProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        dspChain.prepare({ sampleRate, static_cast<juce::uint32>(samplesPerBlock), static_cast<juce::uint32>(getTotalNumOutputChannels()) });
    }
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override {
        juce::ScopedNoDenormals noDenormals;
        double bpm = 128.0, ppq = 0.0;
        if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) {
            if (pos->getBpm().hasValue()) bpm = *pos->getBpm();
            if (pos->getPpqPosition().hasValue()) ppq = *pos->getPpqPosition();
        }
        bool midiStutter = false;
        for (const auto m : midi) if (m.getMessage().isNoteOn()) midiStutter = true;
        
        dspChain.process(buffer, bpm, ppq,
            apvts.getRawParameterValue("TRANSITION_MACRO")->load(),
            (apvts.getRawParameterValue("STUTTER_ON")->load() > 0.5f) || midiStutter,
            (int)apvts.getRawParameterValue("STUTTER_DIV")->load() == 0 ? 1 : 4,
            apvts.getRawParameterValue("BACKVERB_MIX")->load(),
            apvts.getRawParameterValue("CHOP_DEPTH")->load(),
            apvts.getRawParameterValue("GLITCH_AMT")->load());
    }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "TRANSIT FX"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override {
        std::unique_ptr<juce::XmlElement> xml(apvts.copyState().createXml());
        copyXmlToBinary(*xml, destData);
    }
    void setStateInformation(const void* data, int sizeInBytes) override {
        std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
        if (xmlState && xmlState->hasTagName(apvts.state.getType())) apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
    }

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() {
        return {
            std::make_unique<juce::AudioParameterFloat>("TRANSITION_MACRO", "Transition", 0.0f, 1.0f, 0.0f),
            std::make_unique<juce::AudioParameterBool>("STUTTER_ON", "Stutter", false),
            std::make_unique<juce::AudioParameterChoice>("STUTTER_DIV", "Division", juce::StringArray{"1/8", "1/16"}, 1),
            std::make_unique<juce::AudioParameterFloat>("BACKVERB_MIX", "Backverb", 0.0f, 1.0f, 0.0f),
            std::make_unique<juce::AudioParameterFloat>("CHOP_DEPTH", "Chop", 0.0f, 1.0f, 0.0f),
            std::make_unique<juce::AudioParameterFloat>("GLITCH_AMT", "Glitch", 0.0f, 1.0f, 0.0f)
        };
    }
private:
    TransitDSPChain dspChain;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SteinexTransitFXProcessor)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new SteinexTransitFXProcessor();
}
