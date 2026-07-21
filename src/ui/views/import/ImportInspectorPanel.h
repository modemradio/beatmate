#pragma once
#include <functional>
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include "ImportStagingStore.h"
#include "../../IRetranslatable.h"

namespace BeatMate::UI {

class ImportInspectorPanel : public juce::Component, public IRetranslatable {
public:
    ImportInspectorPanel();

    void setEntry(const StagedFile* entry, int index);
    void clearEntry();

    std::function<void(int index, int field, const juce::String& value)> onFieldEdited;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void retranslateUi() override;

private:
    void commitField(int field, juce::TextEditor& editor);
    juce::String infoLine() const;

    int m_index = -1;
    bool m_hasEntry = false;
    StagedFile m_snapshot;

    std::unique_ptr<juce::Label> m_titleLabel, m_artistLabel, m_albumLabel, m_genreLabel;
    std::unique_ptr<juce::TextEditor> m_titleEd, m_artistEd, m_albumEd, m_genreEd;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ImportInspectorPanel)
};

} // namespace BeatMate::UI
