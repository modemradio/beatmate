#include "DocumentationDialog.h"
#include "../styles/ColorPalette.h"
#include "../../services/config/I18n.h"
namespace BeatMate::UI {
DocumentationDialog::DocumentationDialog(){
    setSize(700,500);
    m_browser=std::make_unique<juce::TextEditor>();
    m_browser->setMultiLine(true,true);
    m_browser->setReadOnly(true);
    m_browser->setColour(juce::TextEditor::backgroundColourId,Colors::bgDark());
    m_browser->setColour(juce::TextEditor::textColourId,Colors::textSecondary());
    m_browser->setColour(juce::TextEditor::outlineColourId,juce::Colours::transparentBlack);
    m_browser->setFont(juce::Font(13.0f));
    m_browser->setText(BM_TJ("dialog.doc.content"));
    addAndMakeVisible(*m_browser);

    m_closeBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.doc.close"));
    m_closeBtn->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());
    m_closeBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primary());
    m_closeBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_closeBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_closeBtn->onClick=[this]{if(auto*dw=findParentComponentOfClass<juce::DialogWindow>())dw->exitModalState(1);};
    addAndMakeVisible(*m_closeBtn);
}
void DocumentationDialog::setContent(const juce::String& text){m_browser->setText(text);}
void DocumentationDialog::paint(juce::Graphics& g){ProDraw::viewBackground(g, getWidth(), getHeight());}
void DocumentationDialog::resized(){
    auto area=getLocalBounds().reduced(16);
    auto btnRow=area.removeFromBottom(30);
    m_closeBtn->setBounds(btnRow.removeFromRight(100));
    area.removeFromBottom(8);
    m_browser->setBounds(area);
}
} // namespace BeatMate::UI
