#pragma once
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class UniversalImportPanel : public juce::Component, public juce::FileDragAndDropTarget {
public:
    UniversalImportPanel(); ~UniversalImportPanel() override=default;
    void paint(juce::Graphics& g) override;void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override{return true;}
    void filesDropped(const juce::StringArray& files,int,int) override;
    class Listener{public:virtual ~Listener()=default;virtual void filesDropped(const juce::StringArray&){}virtual void importRequested(const juce::StringArray&,bool){}};
    void addListener(Listener*l){m_listeners.add(l);}void removeListener(Listener*l){m_listeners.remove(l);}
private:
    void setupUI();
    std::unique_ptr<juce::Label> m_dropLabel;std::unique_ptr<juce::TextButton> m_browseBtn,m_importBtn;
    std::unique_ptr<juce::ToggleButton> m_autoAnalyze;double m_progress=0.0;
    class FileModel:public juce::ListBoxModel{public:juce::StringArray items;int getNumRows()override{return items.size();}void paintListBoxItem(int row,juce::Graphics& g,int w,int h,bool sel)override;};
    std::unique_ptr<FileModel> m_fileModel;std::unique_ptr<juce::ListBox> m_fileList;
    juce::ListenerList<Listener> m_listeners;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UniversalImportPanel)
};
}
