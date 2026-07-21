#include "FAQDialog.h"
#include "../styles/ColorPalette.h"
#include "../../services/config/I18n.h"
namespace BeatMate::UI {
FAQDialog::FAQDialog(){
    setSize(600,450);
    m_titleLabel=std::make_unique<juce::Label>("t",BM_TJ("dialog.faq.title"));
    m_titleLabel->setFont(juce::Font(18.0f,juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId,Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    m_faqDisplay=std::make_unique<juce::TextEditor>();
    m_faqDisplay->setMultiLine(true,true);
    m_faqDisplay->setReadOnly(true);
    m_faqDisplay->setColour(juce::TextEditor::backgroundColourId,Colors::bgDark());
    m_faqDisplay->setColour(juce::TextEditor::textColourId,Colors::textSecondary());
    m_faqDisplay->setColour(juce::TextEditor::outlineColourId,Colors::border());
    m_faqDisplay->setFont(juce::Font(13.0f));
    addAndMakeVisible(*m_faqDisplay);

    m_closeBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.faq.close"));
    m_closeBtn->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());
    m_closeBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primary());
    m_closeBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_closeBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_closeBtn->onClick=[this]{if(auto*dw=findParentComponentOfClass<juce::DialogWindow>())dw->exitModalState(1);};
    addAndMakeVisible(*m_closeBtn);

    addFAQ(BM_TJ("dialog.faq.q1"),BM_TJ("dialog.faq.a1"));
    addFAQ(BM_TJ("dialog.faq.q2"),BM_TJ("dialog.faq.a2"));
    addFAQ(BM_TJ("dialog.faq.q3"),BM_TJ("dialog.faq.a3"));
    addFAQ(BM_TJ("dialog.faq.q4"),BM_TJ("dialog.faq.a4"));
    addFAQ(BM_TJ("dialog.faq.q5"),BM_TJ("dialog.faq.a5"));
    addFAQ(BM_TJ("dialog.faq.q6"),BM_TJ("dialog.faq.a6"));
}
void FAQDialog::addFAQ(const juce::String& question,const juce::String& answer){
    m_faqs.push_back({question,answer,false});
    refreshDisplay();
}
void FAQDialog::refreshDisplay(){
    juce::String text;
    for(size_t i=0;i<m_faqs.size();++i){
        text+="Q: "+m_faqs[i].question+"\n";
        text+="   "+m_faqs[i].answer+"\n\n";
    }
    m_faqDisplay->setText(text);
}
void FAQDialog::paint(juce::Graphics& g){ProDraw::viewBackground(g, getWidth(), getHeight());}
void FAQDialog::resized(){
    auto area=getLocalBounds().reduced(24);
    m_titleLabel->setBounds(area.removeFromTop(26));area.removeFromTop(12);
    auto btnRow=area.removeFromBottom(30);
    m_closeBtn->setBounds(btnRow.removeFromRight(100));
    area.removeFromBottom(8);
    m_faqDisplay->setBounds(area);
}
} // namespace BeatMate::UI
