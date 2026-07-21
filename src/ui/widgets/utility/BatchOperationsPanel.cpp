#include "BatchOperationsPanel.h"
#include "../../styles/ColorPalette.h"
#include "../../../services/config/I18n.h"
namespace BeatMate::UI {
BatchOperationsPanel::BatchOperationsPanel(){setupUI();}
void BatchOperationsPanel::setupUI(){
    m_titleLabel=std::make_unique<juce::Label>("t",BM_TJ("widget.BatchOperationsPanel.title"));m_titleLabel->setFont(juce::Font(16.0f,juce::Font::bold));m_titleLabel->setColour(juce::Label::textColourId,Colors::textPrimary());addAndMakeVisible(*m_titleLabel);
    m_operationCombo=std::make_unique<juce::ComboBox>();m_operationCombo->addItem(BM_TJ("widget.BatchOperationsPanel.op.analyze"),1);m_operationCombo->addItem(BM_TJ("widget.BatchOperationsPanel.op.editTags"),2);m_operationCombo->addItem(BM_TJ("widget.BatchOperationsPanel.op.normalize"),3);m_operationCombo->addItem(BM_TJ("widget.BatchOperationsPanel.op.export"),4);m_operationCombo->setSelectedId(1);addAndMakeVisible(*m_operationCombo);
    m_startBtn=std::make_unique<juce::TextButton>(BM_TJ("widget.BatchOperationsPanel.start"));m_startBtn->setColour(juce::TextButton::buttonColourId,Colors::primary());m_startBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_startBtn->onClick=[this]{m_listeners.call([this](Listener&l){l.startRequested(m_operationCombo->getText(),{});});};addAndMakeVisible(*m_startBtn);
    m_cancelBtn=std::make_unique<juce::TextButton>(BM_TJ("widget.BatchOperationsPanel.cancel"));m_cancelBtn->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());m_cancelBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());m_cancelBtn->setEnabled(false);
    m_cancelBtn->onClick=[this]{m_listeners.call(&Listener::cancelled);};addAndMakeVisible(*m_cancelBtn);
    m_statusLabel=std::make_unique<juce::Label>("st",BM_TJ("widget.BatchOperationsPanel.ready"));m_statusLabel->setFont(juce::Font(11.0f));m_statusLabel->setColour(juce::Label::textColourId,Colors::textMuted());addAndMakeVisible(*m_statusLabel);
}
void BatchOperationsPanel::updateProgress(int current,int total){m_progress=total>0?(double)current/total:0.0;m_statusLabel->setText(juce::String(current)+" / "+juce::String(total),juce::dontSendNotification);repaint();}
void BatchOperationsPanel::paint(juce::Graphics& g){
    ProDraw::viewBackground(g, getWidth(), getHeight());
    int pbY=getHeight()-24;g.setColour(Colors::bgLight());g.fillRoundedRectangle(16.0f,(float)pbY,getWidth()-32.0f,16.0f,4.0f);
    if(m_progress>0){g.setColour(Colors::primary());g.fillRoundedRectangle(16.0f,(float)pbY,(getWidth()-32.0f)*(float)m_progress,16.0f,4.0f);}
}
void BatchOperationsPanel::resized(){
    m_titleLabel->setBounds(16,16,300,24);m_operationCombo->setBounds(16,48,getWidth()-32,26);
    m_startBtn->setBounds(getWidth()-200,getHeight()-52,90,28);m_cancelBtn->setBounds(getWidth()-100,getHeight()-52,80,28);
    m_statusLabel->setBounds(16,getHeight()-52,200,20);
}
} // namespace BeatMate::UI
