#pragma once

#include <functional>
#include <memory>
#include <string>

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "../../core/midi/MIDIMapping.h"

namespace BeatMate::UI {

class MIDILearnDialog : public juce::Component
{
public:
    MIDILearnDialog(Core::MIDIMapping* mapping, juce::String actionId);
    ~MIDILearnDialog() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // called when a CC/Note arrives while the dialog is open
    void setLastReceived(int channel, int ccOrNote, bool isCC);

    // Callback fired after OK (with the final entry) or Cancel (empty).
    std::function<void(const Core::MIDIMappingEntry* /*nullptr if cancelled*/)> onCompleted;

    static void showAsync(Core::MIDIMapping* mapping,
                           const juce::String& actionId,
                           std::function<void(const Core::MIDIMappingEntry*)> done);

    static juce::File defaultMappingFile();

private:
    void commit();
    void cancel();

    Core::MIDIMapping* m_mapping = nullptr;
    juce::String m_actionId;

    std::unique_ptr<juce::Label>        m_title;
    std::unique_ptr<juce::Label>        m_actionLabel;
    std::unique_ptr<juce::Label>        m_channelLabel;
    std::unique_ptr<juce::TextEditor>   m_channelEdit;
    std::unique_ptr<juce::Label>        m_numberLabel;
    std::unique_ptr<juce::TextEditor>   m_numberEdit;
    std::unique_ptr<juce::ToggleButton> m_isCcToggle;
    std::unique_ptr<juce::Label>        m_statusLabel;
    std::unique_ptr<juce::TextButton>   m_okButton;
    std::unique_ptr<juce::TextButton>   m_cancelButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MIDILearnDialog)
};

}
