#include "Standalone.h"

#include <juce_audio_utils/juce_audio_utils.h>

#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace standalone
{
#if JucePlugin_Build_Standalone
namespace
{
juce::StandalonePluginHolder* holder()
{
    return juce::StandalonePluginHolder::getInstance();
}
} // namespace

void unmuteInput()
{
    if (auto* h = holder())
        h->getMuteInputValue().setValue (false);
}

int roundTripLatency()
{
    if (auto* h = holder())
        if (auto* device = h->deviceManager.getCurrentAudioDevice())
            return device->getInputLatencyInSamples() + device->getOutputLatencyInSamples();
    return 0;
}

juce::String inputDescription()
{
    if (auto* h = holder())
        if (auto* device = h->deviceManager.getCurrentAudioDevice())
        {
            const int inputs = device->getActiveInputChannels().countNumberOfSetBits();
            if (inputs == 0)
                return "NO INPUT ON " + device->getName().toUpperCase();
            return device->getName().toUpperCase() + " // " + juce::String (inputs) + " IN";
        }
    return "NO AUDIO DEVICE";
}

void showAudioSettings()
{
    if (auto* h = holder())
        h->showAudioSettingsDialog();
}
#else
void unmuteInput() {}
int roundTripLatency() { return 0; }
juce::String inputDescription() { return "THE HOST'S INPUT"; }
void showAudioSettings() {}
#endif
} // namespace standalone
