#include "PluginProcessor.h"
#include "PluginEditor.h"

CymbalisProcessor::CymbalisProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void CymbalisProcessor::prepareToPlay (double, int)
{
    setLatencySamples (0);
}

void CymbalisProcessor::releaseResources()
{
}

bool CymbalisProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void CymbalisProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midi);
    buffer.clear();
}

void CymbalisProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ignoreUnused (destData);
}

void CymbalisProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::ignoreUnused (data, sizeInBytes);
}

juce::AudioProcessorEditor* CymbalisProcessor::createEditor()
{
    return new CymbalisEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CymbalisProcessor();
}
