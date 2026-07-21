#include "EulaDialog.h"
#include "../styles/ColorPalette.h"
#include "../../services/config/I18n.h"
namespace BeatMate::UI {
EulaDialog::EulaDialog(){
    setSize(600,500);
    m_titleLabel=std::make_unique<juce::Label>("t",BM_TJ("dialog.eula.title"));
    m_titleLabel->setFont(juce::Font(18.0f,juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId,Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    m_textBrowser=std::make_unique<juce::TextEditor>();
    m_textBrowser->setMultiLine(true,true);
    m_textBrowser->setReadOnly(true);
    m_textBrowser->setColour(juce::TextEditor::backgroundColourId,Colors::bgDark());
    m_textBrowser->setColour(juce::TextEditor::textColourId,Colors::textSecondary());
    m_textBrowser->setColour(juce::TextEditor::outlineColourId,Colors::border());
    m_textBrowser->setFont(juce::Font(12.0f));
    m_textBrowser->setText(BM_TJ("dialog.eula.body"));
    addAndMakeVisible(*m_textBrowser);

    m_acceptCheck=std::make_unique<juce::ToggleButton>(BM_TJ("dialog.eula.accept"));
    m_acceptCheck->setColour(juce::ToggleButton::textColourId,Colors::textSecondary());
    m_acceptCheck->setColour(juce::ToggleButton::tickColourId,Colors::primary());
    m_acceptCheck->onClick=[this]{m_acceptBtn->setEnabled(m_acceptCheck->getToggleState());};
    addAndMakeVisible(*m_acceptCheck);

    m_refuseBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.eula.refuse"));
    m_refuseBtn->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());
    m_refuseBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primary());
    m_refuseBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_refuseBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_refuseBtn->onClick=[this]{if(auto*dw=findParentComponentOfClass<juce::DialogWindow>())dw->exitModalState(0);};
    addAndMakeVisible(*m_refuseBtn);

    m_acceptBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.eula.acceptBtn"));
    m_acceptBtn->setColour(juce::TextButton::buttonColourId,Colors::primary());
    m_acceptBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primaryHover());
    m_acceptBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_acceptBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_acceptBtn->setEnabled(false);
    m_acceptBtn->onClick=[this]{if(auto*dw=findParentComponentOfClass<juce::DialogWindow>())dw->exitModalState(1);};
    addAndMakeVisible(*m_acceptBtn);
}
bool EulaDialog::isAccepted() const{return m_acceptCheck&&m_acceptCheck->getToggleState();}
bool EulaDialog::showDialog(juce::Component*){
    auto*content=new EulaDialog();
    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(content);opts.dialogTitle=BM_TJ("dialog.eula.windowTitle");
    opts.dialogBackgroundColour=Colors::bgDarker();opts.resizable=false;opts.useNativeTitleBar=true;
    return opts.runModal()==1;
}
void EulaDialog::paint(juce::Graphics& g){ProDraw::viewBackground(g, getWidth(), getHeight());}
void EulaDialog::resized(){
    auto area=getLocalBounds().reduced(24);
    m_titleLabel->setBounds(area.removeFromTop(26));area.removeFromTop(12);
    auto btnRow=area.removeFromBottom(30);
    m_acceptBtn->setBounds(btnRow.removeFromRight(100));btnRow.removeFromRight(8);
    m_refuseBtn->setBounds(btnRow.removeFromRight(100));
    area.removeFromBottom(12);
    m_acceptCheck->setBounds(area.removeFromBottom(24));
    area.removeFromBottom(12);
    m_textBrowser->setBounds(area);
}
}
