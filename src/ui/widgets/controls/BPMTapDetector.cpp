#include "BPMTapDetector.h"
#include "../../styles/ColorPalette.h"
namespace BeatMate::UI {
BPMTapDetector::BPMTapDetector(){setupUI();}
void BPMTapDetector::setupUI(){
    m_tapBtn=std::make_unique<juce::TextButton>("TAP");m_tapBtn->setColour(juce::TextButton::buttonColourId,Colors::primary());m_tapBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primaryHover());m_tapBtn->setColour(juce::TextButton::textColourOffId,juce::Colours::white);m_tapBtn->setColour(juce::TextButton::textColourOnId,juce::Colours::white);m_tapBtn->onClick=[this]{onTap();};addAndMakeVisible(*m_tapBtn);
    m_bpmLabel=std::make_unique<juce::Label>("bpm","0.0 BPM");m_bpmLabel->setFont(juce::Font(16.0f,juce::Font::bold));m_bpmLabel->setColour(juce::Label::textColourId,Colors::success());addAndMakeVisible(*m_bpmLabel);
    m_tapCountLabel=std::make_unique<juce::Label>("tc","0 taps");m_tapCountLabel->setFont(juce::Font(10.0f));m_tapCountLabel->setColour(juce::Label::textColourId,Colors::textMuted());addAndMakeVisible(*m_tapCountLabel);
    m_resetBtn=std::make_unique<juce::TextButton>("Reset");m_resetBtn->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());m_resetBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());m_resetBtn->onClick=[this]{onReset();};addAndMakeVisible(*m_resetBtn);
}
void BPMTapDetector::onTap(){
    auto now=juce::Time::getMillisecondCounterHiRes();
    if(!m_tapTimes.empty()&&(now-m_tapTimes.back())>3000) m_tapTimes.clear();
    m_tapTimes.push_back((int64_t)now);
    if((int)m_tapTimes.size()>kMaxTaps) m_tapTimes.erase(m_tapTimes.begin());
    m_tapCountLabel->setText(juce::String(m_tapTimes.size())+" taps",juce::dontSendNotification);
    calculateBPM();
}
void BPMTapDetector::onReset(){m_tapTimes.clear();m_bpm=0.0;m_bpmLabel->setText("0.0 BPM",juce::dontSendNotification);m_tapCountLabel->setText("0 taps",juce::dontSendNotification);}
void BPMTapDetector::calculateBPM(){
    if(m_tapTimes.size()<2)return;
    double total=0;int count=0;
    for(size_t i=1;i<m_tapTimes.size();++i){total+=(double)(m_tapTimes[i]-m_tapTimes[i-1]);count++;}
    double avg=total/count;m_bpm=60000.0/avg;
    m_bpmLabel->setText(juce::String(m_bpm,1)+" BPM",juce::dontSendNotification);
    m_listeners.call([this](Listener&l){l.bpmDetected(m_bpm);});
}
void BPMTapDetector::resized(){m_tapBtn->setBounds(0,0,80,40);m_bpmLabel->setBounds(88,0,120,20);m_tapCountLabel->setBounds(88,22,80,16);m_resetBtn->setBounds(216,5,60,30);}
} // namespace BeatMate::UI
