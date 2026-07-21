#include "EnergyGraphDialog.h"
#include "../styles/ColorPalette.h"
#include "../../services/config/I18n.h"
namespace BeatMate::UI {
EnergyGraphDialog::EnergyGraphDialog(){
    setSize(600,350);
    m_titleLabel=std::make_unique<juce::Label>("t",BM_TJ("dialog.energy.title"));
    m_titleLabel->setFont(juce::Font(18.0f,juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId,Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    m_closeBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.energy.close"));
    m_closeBtn->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());
    m_closeBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primary());
    m_closeBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_closeBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_closeBtn->onClick=[this]{if(auto*dw=findParentComponentOfClass<juce::DialogWindow>())dw->exitModalState(1);};
    addAndMakeVisible(*m_closeBtn);
}
void EnergyGraphDialog::setTrackData(const juce::String& title,const std::vector<float>& energyData){
    m_titleLabel->setText(BM_TJ("dialog.energy.prefix")+title,juce::dontSendNotification);
    m_energyData=energyData;
    repaint();
}
void EnergyGraphDialog::paint(juce::Graphics& g){
    ProDraw::viewBackground(g, getWidth(), getHeight());
    if(m_energyData.empty()||m_graphArea.isEmpty())return;
    float w=(float)m_graphArea.getWidth();
    float h=(float)m_graphArea.getHeight();
    float x0=(float)m_graphArea.getX();
    float y0=(float)m_graphArea.getY();
    juce::Path path;
    for(size_t i=0;i<m_energyData.size();++i){
        float x=x0+w*(float)i/(float)(m_energyData.size()-1);
        float y=y0+h*(1.0f-juce::jlimit(0.0f,1.0f,m_energyData[i]));
        if(i==0)path.startNewSubPath(x,y);else path.lineTo(x,y);
    }
    g.setColour(Colors::success().withAlpha(0.3f));
    juce::Path filled(path);
    filled.lineTo(x0+w,y0+h);filled.lineTo(x0,y0+h);filled.closeSubPath();
    g.fillPath(filled);
    g.setColour(Colors::success());
    g.strokePath(path,juce::PathStrokeType(2.0f));
}
void EnergyGraphDialog::resized(){
    auto area=getLocalBounds().reduced(24);
    m_titleLabel->setBounds(area.removeFromTop(26));area.removeFromTop(12);
    auto btnRow=area.removeFromBottom(30);
    m_closeBtn->setBounds(btnRow.removeFromRight(100));
    area.removeFromBottom(8);
    m_graphArea=area;
}
} // namespace BeatMate::UI
