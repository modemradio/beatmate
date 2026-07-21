#include "AudioAnalysisDialog.h"
#include "../styles/ColorPalette.h"
#include "../../services/config/I18n.h"
namespace BeatMate::UI {
AudioAnalysisDialog::AudioAnalysisDialog(){
    setSize(700,550);
    m_titleLabel=std::make_unique<juce::Label>("t",BM_TJ("dialog.audioAnalysis.title"));
    m_titleLabel->setFont(juce::Font(18.0f,juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId,Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    m_bpmLabel=std::make_unique<juce::Label>("bpm",BM_TJ("dialog.audioAnalysis.bpmEmpty"));
    m_bpmLabel->setFont(juce::Font(14.0f,juce::Font::bold));
    m_bpmLabel->setColour(juce::Label::textColourId,Colors::success());
    addAndMakeVisible(*m_bpmLabel);

    m_keyLabel=std::make_unique<juce::Label>("key",BM_TJ("dialog.audioAnalysis.keyEmpty"));
    m_keyLabel->setFont(juce::Font(14.0f,juce::Font::bold));
    m_keyLabel->setColour(juce::Label::textColourId,Colors::primary());
    addAndMakeVisible(*m_keyLabel);

    m_energyLabel=std::make_unique<juce::Label>("energy",BM_TJ("dialog.audioAnalysis.energyEmpty"));
    m_energyLabel->setFont(juce::Font(14.0f,juce::Font::bold));
    m_energyLabel->setColour(juce::Label::textColourId,Colors::warning());
    addAndMakeVisible(*m_energyLabel);

    m_lufsLabel=std::make_unique<juce::Label>("lufs",BM_TJ("dialog.audioAnalysis.lufsEmpty"));
    m_lufsLabel->setFont(juce::Font(14.0f,juce::Font::bold));
    m_lufsLabel->setColour(juce::Label::textColourId,Colors::stemVocals());
    addAndMakeVisible(*m_lufsLabel);

    m_tabComponent=std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
    m_tabComponent->setColour(juce::TabbedComponent::backgroundColourId,Colors::bgDark());
    auto* waveformTab=new juce::Component();
    waveformTab->setSize(650,200);
    m_tabComponent->addTab(BM_TJ("dialog.audioAnalysis.waveform"),Colors::bgDark(),waveformTab,true);
    auto* spectrumTab=new juce::Component();
    spectrumTab->setSize(650,200);
    m_tabComponent->addTab(BM_TJ("dialog.audioAnalysis.spectrum"),Colors::bgDark(),spectrumTab,true);
    auto* structureTab=new juce::Component();
    structureTab->setSize(650,100);
    m_tabComponent->addTab(BM_TJ("dialog.audioAnalysis.structure"),Colors::bgDark(),structureTab,true);
    addAndMakeVisible(*m_tabComponent);

    m_closeBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.close"));
    m_closeBtn->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());
    m_closeBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primary());
    m_closeBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_closeBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_closeBtn->onClick=[this]{if(auto*dw=findParentComponentOfClass<juce::DialogWindow>())dw->exitModalState(1);};
    addAndMakeVisible(*m_closeBtn);
}
void AudioAnalysisDialog::setTrackInfo(const juce::String& title,const juce::String& artist,
                                        double bpm,const juce::String& key,int energy,double lufs){
    if(auto*dw=findParentComponentOfClass<juce::DialogWindow>())
        dw->setName(BM_TJ("dialog.audioAnalysis.trackPrefix")+artist+" - "+title);
    m_bpmLabel->setText(BM_TJ("dialog.audioAnalysis.bpmPrefix")+juce::String(bpm,1),juce::dontSendNotification);
    m_keyLabel->setText(BM_TJ("dialog.audioAnalysis.keyPrefix")+key,juce::dontSendNotification);
    m_energyLabel->setText(BM_TJ("dialog.audioAnalysis.energyPrefix")+juce::String(energy)+"/10",juce::dontSendNotification);
    m_lufsLabel->setText(BM_TJ("dialog.audioAnalysis.lufsPrefix")+juce::String(lufs,1),juce::dontSendNotification);
}
void AudioAnalysisDialog::showDialog(juce::Component*){
    auto*content=new AudioAnalysisDialog();
    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(content);
    opts.dialogTitle=BM_TJ("dialog.audioAnalysis.windowTitle");
    opts.dialogBackgroundColour=Colors::bgDarker();
    opts.resizable=false;opts.useNativeTitleBar=true;
    opts.runModal();
}
void AudioAnalysisDialog::paint(juce::Graphics& g){
    juce::ColourGradient bgGrad(juce::Colour(0xFF0D0D14),0.0f,0.0f,
        juce::Colour(0xFF08080E),0.0f,static_cast<float>(getHeight()),false);
    g.setGradientFill(bgGrad);
    g.fillRect(getLocalBounds());

    g.setColour(Colors::primary().withAlpha(0.03f));
    g.fillEllipse(-50.0f,-50.0f,200.0f,200.0f);

    g.setColour(Colors::accent().withAlpha(0.02f));
    g.fillEllipse(static_cast<float>(getWidth())-80.0f,
                  static_cast<float>(getHeight())-80.0f,160.0f,160.0f);

    g.setColour(Colors::primary().withAlpha(0.6f));
    g.fillRect(24.0f,28.0f,3.0f,18.0f);

    auto area=getLocalBounds().reduced(24);
    float statsY=static_cast<float>(area.getY()+38);
    float statsW=static_cast<float>(area.getWidth());
    auto statsPanel=juce::Rectangle<float>(20.0f,statsY-4.0f,statsW+8.0f,32.0f);
    g.setColour(Colors::glassWhite());
    g.fillRoundedRectangle(statsPanel,6.0f);
    g.setColour(Colors::glassBorder());
    g.drawRoundedRectangle(statsPanel,6.0f,0.5f);

    float statW=statsW/4.0f;
    for(int i=1;i<4;++i){
        float sepX=24.0f+(float)i*statW;
        g.setColour(Colors::glassBorder());
        g.drawLine(sepX,statsY+2.0f,sepX,statsY+24.0f,0.5f);
    }

    float tabY=statsY+40.0f;
    float tabH=static_cast<float>(getHeight())-tabY-70.0f;
    auto tabPanel=juce::Rectangle<float>(16.0f,tabY,static_cast<float>(getWidth())-32.0f,tabH);
    g.setColour(Colors::glassWhite());
    g.fillRoundedRectangle(tabPanel,8.0f);
    g.setColour(Colors::glassBorder());
    g.drawRoundedRectangle(tabPanel,8.0f,0.5f);

    float footerY=static_cast<float>(getHeight())-54.0f;
    g.setColour(Colors::primary().withAlpha(0.08f));
    g.fillRect(0.0f,footerY,static_cast<float>(getWidth()),1.0f);

    g.setColour(Colors::glassBorder());
    g.drawRect(getLocalBounds().toFloat(),0.5f);
}
void AudioAnalysisDialog::resized(){
    auto area=getLocalBounds().reduced(24);
    m_titleLabel->setBounds(area.removeFromTop(30));
    area.removeFromTop(8);
    auto statsRow=area.removeFromTop(24);
    int statW=statsRow.getWidth()/4;
    m_bpmLabel->setBounds(statsRow.removeFromLeft(statW));
    m_keyLabel->setBounds(statsRow.removeFromLeft(statW));
    m_energyLabel->setBounds(statsRow.removeFromLeft(statW));
    m_lufsLabel->setBounds(statsRow);
    area.removeFromTop(12);
    auto btnArea=area.removeFromBottom(30);
    m_closeBtn->setBounds(btnArea.removeFromRight(100));
    area.removeFromBottom(8);
    m_tabComponent->setBounds(area);
}
}
