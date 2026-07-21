#include "MatchupDialog.h"
#include "../styles/ColorPalette.h"
#include "../../services/config/I18n.h"
namespace BeatMate::UI {
MatchupDialog::MatchupDialog(){
    setSize(500,400);
    m_titleLabel=std::make_unique<juce::Label>("t",BM_TJ("dialog.matchup.title"));
    m_titleLabel->setFont(juce::Font(18.0f,juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId,Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    m_track1Label=std::make_unique<juce::Label>("t1","-");
    m_track1Label->setFont(juce::Font(12.0f,juce::Font::bold));
    m_track1Label->setColour(juce::Label::textColourId,Colors::primary());
    m_track1Label->setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(*m_track1Label);

    m_track2Label=std::make_unique<juce::Label>("t2","-");
    m_track2Label->setFont(juce::Font(12.0f,juce::Font::bold));
    m_track2Label->setColour(juce::Label::textColourId,Colors::accent());
    m_track2Label->setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(*m_track2Label);

    m_scoreLabel=std::make_unique<juce::Label>("sc","0%");
    m_scoreLabel->setFont(juce::Font(48.0f,juce::Font::bold));
    m_scoreLabel->setColour(juce::Label::textColourId,Colors::success());
    m_scoreLabel->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*m_scoreLabel);

    auto makeDetail=[this](const juce::String& text){
        auto l=std::make_unique<juce::Label>("",text);
        l->setFont(juce::Font(12.0f));l->setColour(juce::Label::textColourId,Colors::textSecondary());
        addAndMakeVisible(*l);return l;
    };
    m_bpmDiffLabel=makeDetail(BM_TJ("dialog.matchup.bpmDiff"));
    m_keyCompatLabel=makeDetail(BM_TJ("dialog.matchup.keyCompat"));
    m_energyDiffLabel=makeDetail(BM_TJ("dialog.matchup.energyDiff"));

    m_closeBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.matchup.close"));
    m_closeBtn->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());
    m_closeBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primary());
    m_closeBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_closeBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_closeBtn->onClick=[this]{if(auto*dw=findParentComponentOfClass<juce::DialogWindow>())dw->exitModalState(1);};
    addAndMakeVisible(*m_closeBtn);
}
void MatchupDialog::setTracks(const juce::String& title1,const juce::String& artist1,double bpm1,const juce::String& key1,int energy1,
                               const juce::String& title2,const juce::String& artist2,double bpm2,const juce::String& key2,int energy2){
    m_track1Label->setText(title1+"\n"+artist1+"\nBPM: "+juce::String(bpm1,1)+" | Key: "+key1+" | Energy: "+juce::String(energy1),juce::dontSendNotification);
    m_track2Label->setText(title2+"\n"+artist2+"\nBPM: "+juce::String(bpm2,1)+" | Key: "+key2+" | Energy: "+juce::String(energy2),juce::dontSendNotification);
    m_bpm1=bpm1;m_bpm2=bpm2;m_key1=key1;m_key2=key2;m_energy1=energy1;m_energy2=energy2;
    calculateCompatibility();
}
void MatchupDialog::calculateCompatibility(){
    double bpmDiff=std::abs(m_bpm1-m_bpm2);
    double bpmScore=juce::jmax(0.0,100.0-bpmDiff*5.0);
    bool keyCompat=(m_key1==m_key2);
    double keyScore=keyCompat?100.0:50.0;
    double energyDiff=std::abs(m_energy1-m_energy2);
    double energyScore=juce::jmax(0.0,100.0-energyDiff*15.0);
    double total=(bpmScore*0.4+keyScore*0.4+energyScore*0.2);
    auto scoreColor=total>80?Colors::success():total>50?Colors::warning():Colors::error();
    m_scoreLabel->setText(juce::String((int)total)+"%",juce::dontSendNotification);
    m_scoreLabel->setColour(juce::Label::textColourId,scoreColor);
    auto stripDash = [](juce::String s){ if(s.endsWith("-")) s=s.dropLastCharacters(1).trimEnd(); return s; };
    m_bpmDiffLabel->setText(stripDash(BM_TJ("dialog.matchup.bpmDiff"))+" "+juce::String(bpmDiff,1)+" (score: "+juce::String((int)bpmScore)+"%)",juce::dontSendNotification);
    m_keyCompatLabel->setText(stripDash(BM_TJ("dialog.matchup.keyCompat"))+" "+(keyCompat?BM_TJ("dialog.matchup.compatible"):BM_TJ("dialog.matchup.incompatible"))+" (score: "+juce::String((int)keyScore)+"%)",juce::dontSendNotification);
    m_energyDiffLabel->setText(stripDash(BM_TJ("dialog.matchup.energyDiff"))+" "+juce::String((int)energyDiff)+" (score: "+juce::String((int)energyScore)+"%)",juce::dontSendNotification);
}
void MatchupDialog::paint(juce::Graphics& g){
    ProDraw::viewBackground(g, getWidth(), getHeight());
    auto area=getLocalBounds().reduced(24);
    float y1=(float)area.getY()+38.0f;
    float halfW=((float)getWidth()-48.0f)*0.5f-4.0f;
    g.setColour(Colors::bgMedium());g.fillRoundedRectangle(24.0f,y1,halfW,70.0f,8.0f);
    g.setColour(Colors::border());g.drawRoundedRectangle(24.0f,y1,halfW,70.0f,8.0f,1.0f);
    g.setColour(Colors::textPrimary());g.setFont(juce::Font(12.0f,juce::Font::bold));
    g.drawText(BM_TJ("dialog.matchup.trackA"),34,(int)y1-8,80,16,juce::Justification::centredLeft);
    float x2=24.0f+halfW+8.0f;
    g.setColour(Colors::bgMedium());g.fillRoundedRectangle(x2,y1,halfW,70.0f,8.0f);
    g.setColour(Colors::border());g.drawRoundedRectangle(x2,y1,halfW,70.0f,8.0f,1.0f);
    g.setColour(Colors::textPrimary());g.setFont(juce::Font(12.0f,juce::Font::bold));
    g.drawText(BM_TJ("dialog.matchup.trackB"),x2+10,(int)y1-8,80,16,juce::Justification::centredLeft);
    float y2=y1+70.0f+60.0f+8.0f;
    g.setColour(Colors::bgMedium());g.fillRoundedRectangle(24.0f,y2,(float)getWidth()-48.0f,80.0f,8.0f);
    g.setColour(Colors::border());g.drawRoundedRectangle(24.0f,y2,(float)getWidth()-48.0f,80.0f,8.0f,1.0f);
    g.setColour(Colors::textPrimary());g.setFont(juce::Font(12.0f,juce::Font::bold));
    g.drawText(BM_TJ("dialog.matchup.details"),34,(int)y2-8,80,16,juce::Justification::centredLeft);
}
void MatchupDialog::resized(){
    auto area=getLocalBounds().reduced(24);
    m_titleLabel->setBounds(area.removeFromTop(26));area.removeFromTop(12);
    auto tracksRow=area.removeFromTop(70);
    int halfW=tracksRow.getWidth()/2-4;
    m_track1Label->setBounds(tracksRow.removeFromLeft(halfW).reduced(8));
    tracksRow.removeFromLeft(8);
    m_track2Label->setBounds(tracksRow.reduced(8));
    area.removeFromTop(4);
    m_scoreLabel->setBounds(area.removeFromTop(60));
    area.removeFromTop(4);
    auto detArea=area.removeFromTop(80).reduced(12);
    m_bpmDiffLabel->setBounds(detArea.removeFromTop(22));
    m_keyCompatLabel->setBounds(detArea.removeFromTop(22));
    m_energyDiffLabel->setBounds(detArea.removeFromTop(22));
    auto btnRow=area.removeFromBottom(30);
    m_closeBtn->setBounds(btnRow.removeFromRight(100));
}
} // namespace BeatMate::UI
