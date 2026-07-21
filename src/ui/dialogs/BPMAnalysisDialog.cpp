#include "BPMAnalysisDialog.h"
#include "../styles/ColorPalette.h"
#include "../../services/config/I18n.h"
#include <spdlog/spdlog.h>
namespace BeatMate::UI {
BPMAnalysisDialog::BPMAnalysisDialog(){
    setSize(400,350);
    m_titleLabel=std::make_unique<juce::Label>("t",BM_TJ("dialog.bpm.title"));
    m_titleLabel->setFont(juce::Font(18.0f,juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId,Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    m_detectedBPMLabel=std::make_unique<juce::Label>("dbpm","---");
    m_detectedBPMLabel->setFont(juce::Font(48.0f,juce::Font::bold));
    m_detectedBPMLabel->setColour(juce::Label::textColourId,Colors::success());
    m_detectedBPMLabel->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*m_detectedBPMLabel);

    m_confidenceLabel=std::make_unique<juce::Label>("conf",BM_TJ("dialog.bpm.confidence")+juce::String(" -"));
    m_confidenceLabel->setFont(juce::Font(12.0f));
    m_confidenceLabel->setColour(juce::Label::textColourId,Colors::textMuted());
    m_confidenceLabel->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*m_confidenceLabel);

    m_overrideLabel=std::make_unique<juce::Label>("ol",BM_TJ("dialog.bpm.manual"));
    m_overrideLabel->setFont(juce::Font(12.0f));
    m_overrideLabel->setColour(juce::Label::textColourId,Colors::textSecondary());
    addAndMakeVisible(*m_overrideLabel);

    m_overrideSlider=std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal,juce::Slider::TextBoxRight);
    m_overrideSlider->setRange(40.0,300.0,0.1);
    m_overrideSlider->setValue(120.0);
    m_overrideSlider->setColour(juce::Slider::backgroundColourId,Colors::bgLight());
    m_overrideSlider->setColour(juce::Slider::trackColourId,Colors::primary());
    m_overrideSlider->setColour(juce::Slider::thumbColourId,Colors::primary());
    m_overrideSlider->setColour(juce::Slider::textBoxTextColourId,Colors::textPrimary());
    m_overrideSlider->setColour(juce::Slider::textBoxBackgroundColourId,Colors::bgLight());
    m_overrideSlider->setColour(juce::Slider::textBoxOutlineColourId,Colors::border());
    addAndMakeVisible(*m_overrideSlider);

    m_applyBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.bpm.apply"));
    m_applyBtn->setColour(juce::TextButton::buttonColourId,Colors::primary());
    m_applyBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primaryHover());
    m_applyBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_applyBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_applyBtn->onClick=[this]{
        if(onBpmOverridden)onBpmOverridden(m_overrideSlider->getValue());
        if(auto*dw=findParentComponentOfClass<juce::DialogWindow>())dw->exitModalState(1);
    };
    addAndMakeVisible(*m_applyBtn);

    m_cancelBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.bpm.close"));
    m_cancelBtn->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());
    m_cancelBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primary());
    m_cancelBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_cancelBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_cancelBtn->onClick=[this]{if(auto*dw=findParentComponentOfClass<juce::DialogWindow>())dw->exitModalState(0);};
    addAndMakeVisible(*m_cancelBtn);
}
void BPMAnalysisDialog::setDetectedBPM(double bpmVal,double confidence){
    m_detectedBPMLabel->setText(juce::String(bpmVal,1),juce::dontSendNotification);
    m_confidenceLabel->setText(BM_TJ("dialog.bpm.confidence")+juce::String(" ")+juce::String(confidence*100.0,1)+"%",juce::dontSendNotification);
    m_overrideSlider->setValue(bpmVal,juce::dontSendNotification);
    auto color=confidence>0.9?Colors::success():confidence>0.7?Colors::warning():Colors::error();
    m_confidenceLabel->setColour(juce::Label::textColourId,color);
}
double BPMAnalysisDialog::bpm() const{return m_overrideSlider->getValue();}
void BPMAnalysisDialog::paint(juce::Graphics& g){
    ProDraw::viewBackground(g, getWidth(), getHeight());
    g.setColour(Colors::bgMedium());
    g.fillRoundedRectangle(24.0f,54.0f,(float)getWidth()-48.0f,110.0f,8.0f);
    g.setColour(Colors::border());
    g.drawRoundedRectangle(24.0f,54.0f,(float)getWidth()-48.0f,110.0f,8.0f,1.0f);
    g.setColour(Colors::textPrimary());g.setFont(juce::Font(12.0f,juce::Font::bold));
    g.drawText(BM_TJ("dialog.bpm.detected"),34,46,150,16,juce::Justification::centredLeft);
}
void BPMAnalysisDialog::resized(){
    auto area=getLocalBounds().reduced(24);
    m_titleLabel->setBounds(area.removeFromTop(26));area.removeFromTop(8);
    auto detectArea=area.removeFromTop(110);
    m_detectedBPMLabel->setBounds(detectArea.removeFromTop(70));
    m_confidenceLabel->setBounds(detectArea);
    area.removeFromTop(16);
    auto overrideRow=area.removeFromTop(30);
    m_overrideLabel->setBounds(overrideRow.removeFromLeft(90));
    m_applyBtn->setBounds(overrideRow.removeFromRight(90));
    overrideRow.removeFromRight(8);
    m_overrideSlider->setBounds(overrideRow);
    area.removeFromTop(16);
    auto btnRow=area.removeFromBottom(30);
    m_cancelBtn->setBounds(btnRow.removeFromRight(100));
}
} // namespace BeatMate::UI
