#pragma once
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class BrowserPanel : public juce::Component {
public:
    BrowserPanel();
    ~BrowserPanel() override {
        // m_tree declare avant m_rootItem : ordre de destruction impose
        if (m_tree) m_tree->setRootItem(nullptr);
        m_tree.reset();
        m_rootItem.reset();
    }
    void paint(juce::Graphics& g) override;void resized() override;
    class Listener{public:virtual ~Listener()=default;virtual void filterChanged(const juce::String&,const juce::String&){}};
    void addListener(Listener*l){m_listeners.add(l);}void removeListener(Listener*l){m_listeners.remove(l);}
private:
    void setupUI();void populateTree();
    std::unique_ptr<juce::TreeView> m_tree;std::unique_ptr<juce::Label> m_titleLabel;
    juce::ListenerList<Listener> m_listeners;
    class RootItem : public juce::TreeViewItem {public:
        juce::String text;juce::String category;BrowserPanel* owner=nullptr;
        bool isCategory=false;
        bool hovered=false;
        bool mightContainSubItems() override{return getNumSubItems()>0;}
        void paintItem(juce::Graphics& g,int w,int h) override;
        void itemClicked(const juce::MouseEvent&) override;
        int getItemHeight() const override { return isCategory ? 26 : 22; }
    };

    // File system explorer item (lazy-loading)
    class FileSystemItem : public juce::TreeViewItem {
    public:
        FileSystemItem(const juce::File& file, BrowserPanel* owner, const juce::String& displayName = {});
        bool mightContainSubItems() override;
        juce::String getUniqueName() const override;
        void paintItem(juce::Graphics& g, int w, int h) override;
        void itemOpennessChanged(bool isNowOpen) override;
        void itemClicked(const juce::MouseEvent&) override;
        int getItemHeight() const override { return 22; }
    private:
        juce::File m_file;
        BrowserPanel* m_owner = nullptr;
        juce::String m_displayName;
        bool m_hasScanned = false;
    };

    std::unique_ptr<RootItem> m_rootItem;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BrowserPanel)
};
} // namespace BeatMate::UI
