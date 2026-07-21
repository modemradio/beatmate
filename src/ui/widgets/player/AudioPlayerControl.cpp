#include "AudioPlayerControl.h"
#include "../controls/RotaryKnobWidget.h"
#include "../../styles/ColorPalette.h"
#include "../../../services/config/I18n.h"
#include <spdlog/spdlog.h>
namespace BeatMate::UI {
AudioPlayerControl::AudioPlayerControl(){setupUI();}
void AudioPlayerControl::setupUI(){
    m_playPauseBtn=std::make_unique<juce::TextButton>(">");m_playPauseBtn->setColour(juce::TextButton::buttonColourId,Colors::primary());m_playPauseBtn->setColour(juce::TextButton::textColourOffId,juce::Colours::white);
    m_playPauseBtn->onClick=[this]{spdlog::info("[AudioPlayerControl] playPause clicked (now {})", m_playing?"paused":"playing");m_playing=!m_playing;m_playPauseBtn->setButtonText(m_playing?"||":">");m_listeners.call(&Listener::playPauseClicked);};addAndMakeVisible(*m_playPauseBtn);
    m_stopBtn=std::make_unique<juce::TextButton>("[]");m_stopBtn->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());m_stopBtn->setColour(juce::TextButton::textColourOffId,Colors::textSecondary());
    m_stopBtn->onClick=[this]{spdlog::info("[AudioPlayerControl] stop clicked");m_listeners.call(&Listener::stopClicked);};addAndMakeVisible(*m_stopBtn);
    m_titleLabel=std::make_unique<juce::Label>("t",BM_TJ("widget.AudioPlayerControl.noTrack"));m_titleLabel->setFont(juce::Font(12.0f,juce::Font::bold));m_titleLabel->setColour(juce::Label::textColourId,Colors::textPrimary());addAndMakeVisible(*m_titleLabel);
    m_posLabel=std::make_unique<juce::Label>("p","0:00");m_posLabel->setFont(juce::Font(10.0f));m_posLabel->setColour(juce::Label::textColourId,Colors::textMuted());addAndMakeVisible(*m_posLabel);
    m_seekSlider=std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal,juce::Slider::NoTextBox);m_seekSlider->setRange(0,1000,1);m_seekSlider->setColour(juce::Slider::thumbColourId,Colors::primary());m_seekSlider->setColour(juce::Slider::trackColourId,Colors::bgLighter());
    m_seekSlider->onValueChange=[this]{m_listeners.call([this](Listener&l){l.seekRequested((int)m_seekSlider->getValue());});};addAndMakeVisible(*m_seekSlider);
    m_durationLabel=std::make_unique<juce::Label>("d","0:00");m_durationLabel->setFont(juce::Font(10.0f));m_durationLabel->setColour(juce::Label::textColourId,Colors::textMuted());addAndMakeVisible(*m_durationLabel);
    m_bpmLabel=std::make_unique<juce::Label>("bpm","BPM: -");m_bpmLabel->setFont(juce::Font(11.0f,juce::Font::bold));m_bpmLabel->setColour(juce::Label::textColourId,Colors::success());addAndMakeVisible(*m_bpmLabel);
    m_keyLabel=std::make_unique<juce::Label>("key","Key: -");m_keyLabel->setFont(juce::Font(11.0f,juce::Font::bold));m_keyLabel->setColour(juce::Label::textColourId,Colors::primary());addAndMakeVisible(*m_keyLabel);
    m_volumeKnob=std::make_unique<RotaryKnobWidget>();m_volumeKnob->setRange(0,100);m_volumeKnob->setValue(80);m_volumeKnob->setLabel("Vol");m_volumeKnob->setSuffix("%");
    m_volumeListener = std::make_unique<VolumeListener>(*this);
    m_volumeKnob->addListener(m_volumeListener.get());
    addAndMakeVisible(*m_volumeKnob);
}
void AudioPlayerControl::setTrackInfo(const juce::String& title,const juce::String& artist,double bpm,const juce::String& key){
    m_titleLabel->setText(artist+" - "+title,juce::dontSendNotification);
    m_bpmLabel->setText("BPM: "+juce::String(bpm,1),juce::dontSendNotification);
    m_keyLabel->setText("Key: "+key,juce::dontSendNotification);
}
void AudioPlayerControl::setPosition(int posMs,int totalMs){
    if(!m_seekSlider->isMouseButtonDown()&&totalMs>0){m_seekSlider->setRange(0,totalMs,1);m_seekSlider->setValue(posMs,juce::dontSendNotification);}
    int posS=posMs/1000,totS=totalMs/1000;
    m_posLabel->setText(juce::String(posS/60)+":"+juce::String(posS%60).paddedLeft('0',2),juce::dontSendNotification);
    m_durationLabel->setText(juce::String(totS/60)+":"+juce::String(totS%60).paddedLeft('0',2),juce::dontSendNotification);
}
void AudioPlayerControl::setPlaying(bool playing){m_playing=playing;m_playPauseBtn->setButtonText(playing?"||":">");}
void AudioPlayerControl::resized(){
    m_playPauseBtn->setBounds(8,4,36,36);m_stopBtn->setBounds(50,8,30,28);
    m_titleLabel->setBounds(90,4,300,16);m_posLabel->setBounds(90,22,40,14);m_seekSlider->setBounds(130,22,getWidth()-370,14);m_durationLabel->setBounds(getWidth()-238,22,40,14);
    m_bpmLabel->setBounds(getWidth()-190,4,80,18);m_keyLabel->setBounds(getWidth()-190,22,80,18);m_volumeKnob->setBounds(getWidth()-100,0,70,44);
}
} // namespace BeatMate::UI
