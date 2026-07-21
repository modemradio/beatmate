#include "CamelotWheelDialog.h"
#include "../styles/ColorPalette.h"
#include "../../services/config/I18n.h"
namespace BeatMate::UI {
CamelotWheelDialog::CamelotWheelDialog(){
    setSize(500,550);
    m_titleLabel=std::make_unique<juce::Label>("t",BM_TJ("dialog.camelot.title"));
    m_titleLabel->setFont(juce::Font(18.0f,juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId,Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    m_selectedKeyLabel=std::make_unique<juce::Label>("sk",BM_TJ("dialog.camelot.clickKey"));
    m_selectedKeyLabel->setFont(juce::Font(14.0f,juce::Font::bold));
    m_selectedKeyLabel->setColour(juce::Label::textColourId,Colors::primary());
    m_selectedKeyLabel->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*m_selectedKeyLabel);

    m_compatibleLabel=std::make_unique<juce::Label>("ck",BM_TJ("dialog.camelot.compatible"));
    m_compatibleLabel->setFont(juce::Font(12.0f));
    m_compatibleLabel->setColour(juce::Label::textColourId,Colors::success());
    m_compatibleLabel->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*m_compatibleLabel);

    m_closeBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.camelot.close"));
    m_closeBtn->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());
    m_closeBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primary());
    m_closeBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_closeBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_closeBtn->onClick=[this]{if(auto*dw=findParentComponentOfClass<juce::DialogWindow>())dw->exitModalState(1);};
    addAndMakeVisible(*m_closeBtn);
}
void CamelotWheelDialog::setCurrentKey(const juce::String& key){
    m_highlightedKey=key;
    m_selectedKeyLabel->setText(BM_TJ("dialog.camelot.keyPrefix")+key,juce::dontSendNotification);
    repaint();
}
void CamelotWheelDialog::drawCamelotWheel(juce::Graphics& g){
    float cx=m_wheelArea.getCentreX(),cy=m_wheelArea.getCentreY();
    float radius=juce::jmin(m_wheelArea.getWidth(),m_wheelArea.getHeight())*0.45f;
    juce::StringArray keys={"1A","2A","3A","4A","5A","6A","7A","8A","9A","10A","11A","12A",
                            "1B","2B","3B","4B","5B","6B","7B","8B","9B","10B","11B","12B"};
    for(int i=0;i<12;++i){
        float angle=(float)i*juce::MathConstants<float>::twoPi/12.0f-juce::MathConstants<float>::halfPi;
        float nextAngle=(float)(i+1)*juce::MathConstants<float>::twoPi/12.0f-juce::MathConstants<float>::halfPi;
        // Outer ring (A keys)
        juce::Path outerArc;
        outerArc.addCentredArc(cx,cy,radius,radius,0,angle,nextAngle,true);
        outerArc.addCentredArc(cx,cy,radius*0.7f,radius*0.7f,0,nextAngle,angle,true);
        outerArc.closeSubPath();
        g.setColour(juce::Colour::fromHSV((float)i/12.0f,0.6f,0.7f,1.0f));
        g.fillPath(outerArc);
        g.setColour(Colors::bgDarker());g.strokePath(outerArc,juce::PathStrokeType(1.0f));
        // Inner ring (B keys)
        juce::Path innerArc;
        innerArc.addCentredArc(cx,cy,radius*0.7f,radius*0.7f,0,angle,nextAngle,true);
        innerArc.addCentredArc(cx,cy,radius*0.35f,radius*0.35f,0,nextAngle,angle,true);
        innerArc.closeSubPath();
        g.setColour(juce::Colour::fromHSV((float)i/12.0f,0.4f,0.9f,1.0f));
        g.fillPath(innerArc);
        g.setColour(Colors::bgDarker());g.strokePath(innerArc,juce::PathStrokeType(1.0f));
        float midAngle=(angle+nextAngle)*0.5f;
        float outerR=radius*0.85f,innerR=radius*0.52f;
        g.setColour(Colors::textPrimary());g.setFont(juce::Font(11.0f,juce::Font::bold));
        g.drawText(keys[i],juce::Rectangle<float>(cx+outerR*std::cos(midAngle)-16,cy+outerR*std::sin(midAngle)-8,32,16),juce::Justification::centred);
        g.drawText(keys[i+12],juce::Rectangle<float>(cx+innerR*std::cos(midAngle)-16,cy+innerR*std::sin(midAngle)-8,32,16),juce::Justification::centred);
    }
}
void CamelotWheelDialog::mouseDown(const juce::MouseEvent& e){
    if(m_wheelArea.contains(e.position)){
        float cx=m_wheelArea.getCentreX(),cy=m_wheelArea.getCentreY();
        float dx=e.position.x-cx,dy=e.position.y-cy;
        float angle=std::atan2(dy,dx)+juce::MathConstants<float>::halfPi;
        if(angle<0)angle+=juce::MathConstants<float>::twoPi;
        int segment=(int)(angle/(juce::MathConstants<float>::twoPi/12.0f))%12;
        float dist=std::sqrt(dx*dx+dy*dy);
        float radius=juce::jmin(m_wheelArea.getWidth(),m_wheelArea.getHeight())*0.45f;
        juce::String key=juce::String(segment+1)+(dist>radius*0.7f?"A":"B");
        m_highlightedKey=key;
        m_selectedKeyLabel->setText(BM_TJ("dialog.camelot.keyPrefix")+key,juce::dontSendNotification);
        if(onKeySelected)onKeySelected(key);
        repaint();
    }
}
void CamelotWheelDialog::paint(juce::Graphics& g){
    ProDraw::viewBackground(g, getWidth(), getHeight());
    drawCamelotWheel(g);
}
void CamelotWheelDialog::resized(){
    auto area=getLocalBounds().reduced(24);
    m_titleLabel->setBounds(area.removeFromTop(26));area.removeFromTop(8);
    auto btnArea=area.removeFromBottom(30);
    m_closeBtn->setBounds(btnArea.removeFromRight(100));
    area.removeFromBottom(8);
    m_compatibleLabel->setBounds(area.removeFromBottom(20));area.removeFromBottom(4);
    m_selectedKeyLabel->setBounds(area.removeFromBottom(24));area.removeFromBottom(8);
    int wheelSize=juce::jmin(area.getWidth(),area.getHeight());
    m_wheelArea=juce::Rectangle<float>((float)(area.getCentreX()-wheelSize/2),(float)area.getY(),(float)wheelSize,(float)wheelSize);
}
} // namespace BeatMate::UI
