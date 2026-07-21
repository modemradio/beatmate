#include "QuickActionBar.h"
#include "../../styles/ColorPalette.h"
#include "../../../services/config/I18n.h"
namespace BeatMate::UI {
QuickActionBar::QuickActionBar(){
    auto make=[this](const juce::String&t){auto b=std::make_unique<juce::TextButton>(t);b->setColour(juce::TextButton::buttonColourId,juce::Colours::transparentBlack);b->setColour(juce::TextButton::buttonOnColourId,Colors::primary().withAlpha(0.25f));b->setColour(juce::TextButton::textColourOffId,Colors::textSecondary());b->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());addAndMakeVisible(*b);return b;};
    m_importBtn=make(BM_TJ("widget.QuickActionBar.import"));m_importBtn->onClick=[this]{m_listeners.call(&Listener::importClicked);};
    m_analyzeBtn=make(BM_TJ("widget.QuickActionBar.analyze"));m_analyzeBtn->onClick=[this]{m_listeners.call(&Listener::analyzeClicked);};
    m_normalizeBtn=make(BM_TJ("widget.QuickActionBar.normalize"));m_normalizeBtn->onClick=[this]{m_listeners.call(&Listener::normalizeClicked);};
    m_exportBtn=make(BM_TJ("widget.QuickActionBar.export"));m_exportBtn->onClick=[this]{m_listeners.call(&Listener::exportClicked);};
}
void QuickActionBar::paint(juce::Graphics& g){g.fillAll(Colors::bgDark());g.setColour(Colors::border());g.drawRect(getLocalBounds(),1);}
void QuickActionBar::resized(){int x=4,w=80;m_importBtn->setBounds(x,2,w,getHeight()-4);x+=w+4;m_analyzeBtn->setBounds(x,2,w,getHeight()-4);x+=w+4;m_normalizeBtn->setBounds(x,2,w+10,getHeight()-4);x+=w+14;m_exportBtn->setBounds(x,2,w,getHeight()-4);}
} // namespace BeatMate::UI
