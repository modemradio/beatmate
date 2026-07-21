#include "ProgressDialog.h"
#include "../styles/ColorPalette.h"
#include "../../services/config/I18n.h"
namespace BeatMate::UI {
ProgressDialog::ProgressDialog(const juce::String& title){
    setSize(450,180);
    m_startTime=juce::Time::getMillisecondCounter();

    m_titleLabel=std::make_unique<juce::Label>("t",title);
    m_titleLabel->setFont(juce::Font(16.0f,juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId,Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    m_statusLabel=std::make_unique<juce::Label>("st",BM_TJ("dialog.progress.running"));
    m_statusLabel->setFont(juce::Font(12.0f));
    m_statusLabel->setColour(juce::Label::textColourId,Colors::textSecondary());
    addAndMakeVisible(*m_statusLabel);

    m_etaLabel=std::make_unique<juce::Label>("eta",BM_TJ("dialog.progress.etaCalc"));
    m_etaLabel->setFont(juce::Font(11.0f));
    m_etaLabel->setColour(juce::Label::textColourId,Colors::textMuted());
    addAndMakeVisible(*m_etaLabel);

    m_cancelBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.progress.cancel"));
    m_cancelBtn->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());
    m_cancelBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primary());
    m_cancelBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_cancelBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_cancelBtn->onClick=[this]{
        if(onCancelled)onCancelled();
        if(auto*dw=findParentComponentOfClass<juce::DialogWindow>())dw->exitModalState(0);
    };
    addAndMakeVisible(*m_cancelBtn);

    startTimer(500);
}
void ProgressDialog::setProgress(int value){m_value=value;updateETA();repaint();}
void ProgressDialog::setStatus(const juce::String& text){m_statusLabel->setText(text,juce::dontSendNotification);}
void ProgressDialog::setRange(int min,int max){m_min=min;m_max=max;}
void ProgressDialog::timerCallback(){repaint();}
void ProgressDialog::updateETA(){
    if(m_value>m_min&&m_max>m_min){
        juce::int64 elapsed=juce::Time::getMillisecondCounter()-m_startTime;
        juce::int64 remaining=(elapsed*(m_max-m_value))/(m_value-m_min);
        int secs=(int)(remaining/1000);
        m_etaLabel->setText(BM_TJ("dialog.progress.etaPrefix")+juce::String(secs/60).paddedLeft('0',2)+":"+juce::String(secs%60).paddedLeft('0',2),juce::dontSendNotification);
    }
}
void ProgressDialog::paint(juce::Graphics& g){
    juce::ColourGradient bgGrad(juce::Colour(0xFF0D0D14),0.0f,0.0f,
        juce::Colour(0xFF08080E),0.0f,static_cast<float>(getHeight()),false);
    g.setGradientFill(bgGrad);
    g.fillRect(getLocalBounds());

    g.setColour(Colors::primary().withAlpha(0.03f));
    g.fillEllipse(-50.0f,-50.0f,200.0f,200.0f);

    g.setColour(Colors::primary().withAlpha(0.6f));
    g.fillRect(24.0f,26.0f,3.0f,16.0f);

    auto panelArea=getLocalBounds().reduced(12).toFloat();
    panelArea.removeFromTop(8.0f);
    panelArea.removeFromBottom(4.0f);
    g.setColour(Colors::glassWhite());
    g.fillRoundedRectangle(panelArea,8.0f);
    g.setColour(Colors::glassBorder());
    g.drawRoundedRectangle(panelArea,8.0f,0.5f);

    auto area=getLocalBounds().reduced(24);
    int barY=area.getY()+70;
    int barH=22;
    float barW=(float)(getWidth()-48);

    g.setColour(juce::Colour(0xFF0A0A12));
    g.fillRoundedRectangle(24.0f,(float)barY,barW,(float)barH,6.0f);
    g.setColour(Colors::bgLight().withAlpha(0.3f));
    g.drawRoundedRectangle(24.0f,(float)barY,barW,(float)barH,6.0f,0.5f);

    float progress=0.0f;
    if(m_max>m_min)progress=(float)(m_value-m_min)/(float)(m_max-m_min);
    if(progress>0.0f){
        float fillW=barW*progress;

        g.setColour(Colors::primary().withAlpha(0.1f));
        g.fillRoundedRectangle(22.0f,(float)barY-2.0f,fillW+4.0f,(float)barH+4.0f,8.0f);

        juce::ColourGradient grad(Colors::primary(),24.0f,(float)barY,
            Colors::accent(),24.0f+barW,(float)barY,false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(24.0f,(float)barY,fillW,(float)barH,6.0f);

        juce::ColourGradient glossGrad(juce::Colours::white.withAlpha(0.12f),
            0.0f,(float)barY,juce::Colours::transparentWhite,0.0f,(float)barY+(float)barH*0.5f,false);
        g.setGradientFill(glossGrad);
        g.fillRoundedRectangle(24.0f,(float)barY,fillW,(float)barH*0.5f,6.0f);

        g.setColour(Colors::primary().withAlpha(0.5f));
        g.fillEllipse(24.0f+fillW-8.0f,(float)barY+2.0f,12.0f,(float)barH-4.0f);
    }

    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.setFont(juce::Font(11.0f,juce::Font::bold));
    g.drawText(juce::String((int)(progress*100))+"%",25,barY+1,(int)barW,barH,juce::Justification::centred);
    g.setColour(Colors::textPrimary());
    g.drawText(juce::String((int)(progress*100))+"%",24,barY,(int)barW,barH,juce::Justification::centred);

    g.setColour(Colors::glassBorder());
    g.drawRect(getLocalBounds().toFloat(),0.5f);
}
void ProgressDialog::resized(){
    auto area=getLocalBounds().reduced(24);
    m_titleLabel->setBounds(area.removeFromTop(24));area.removeFromTop(4);
    m_statusLabel->setBounds(area.removeFromTop(20));area.removeFromTop(4);
    area.removeFromTop(26);
    auto bottomRow=area.removeFromTop(20);
    m_etaLabel->setBounds(bottomRow.removeFromLeft(bottomRow.getWidth()-100));
    m_cancelBtn->setBounds(bottomRow);
}
} // namespace BeatMate::UI
