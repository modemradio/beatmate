#include "MiniPlayerDialog.h"
#include "../styles/ColorPalette.h"
#include "../../services/config/I18n.h"
namespace BeatMate::UI {
MiniPlayerDialog::MiniPlayerDialog(){
    setSize(320,120);
    m_playPauseBtn=std::make_unique<juce::TextButton>("||");
    m_playPauseBtn->setColour(juce::TextButton::buttonColourId,Colors::primary());
    m_playPauseBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primaryHover());
    m_playPauseBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_playPauseBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_playPauseBtn->onClick=[this]{
        m_playing=!m_playing;
        m_playPauseBtn->setButtonText(m_playing?"||":">");
        if(onPlayPauseClicked)onPlayPauseClicked();
    };
    addAndMakeVisible(*m_playPauseBtn);

    m_titleLabel=std::make_unique<juce::Label>("tt",BM_TJ("dialog.miniPlayer.noTrack"));
    m_titleLabel->setFont(juce::Font(13.0f,juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId,Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    m_artistLabel=std::make_unique<juce::Label>("ar","-");
    m_artistLabel->setFont(juce::Font(11.0f));
    m_artistLabel->setColour(juce::Label::textColourId,Colors::textMuted());
    addAndMakeVisible(*m_artistLabel);

    m_timeLabel=std::make_unique<juce::Label>("tm","0:00 / 0:00");
    m_timeLabel->setFont(juce::Font(10.0f));
    m_timeLabel->setColour(juce::Label::textColourId,Colors::textMuted());
    m_timeLabel->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*m_timeLabel);

    m_positionSlider=std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal,juce::Slider::NoTextBox);
    m_positionSlider->setRange(0,1000,1);
    m_positionSlider->setColour(juce::Slider::backgroundColourId,Colors::bgLighter());
    m_positionSlider->setColour(juce::Slider::trackColourId,Colors::primary());
    m_positionSlider->setColour(juce::Slider::thumbColourId,Colors::primary());
    m_positionSlider->onValueChange=[this]{
        if(m_positionSlider->isMouseButtonDown()&&onPositionChanged)
            onPositionChanged((int)m_positionSlider->getValue());
    };
    addAndMakeVisible(*m_positionSlider);
}
void MiniPlayerDialog::setTrackInfo(const juce::String& title,const juce::String& artist,const juce::String&){
    m_titleLabel->setText(title,juce::dontSendNotification);
    m_artistLabel->setText(artist,juce::dontSendNotification);
}
void MiniPlayerDialog::setPlaying(bool playing){
    m_playing=playing;
    m_playPauseBtn->setButtonText(playing?"||":">");
}
void MiniPlayerDialog::setPosition(int pos,int total){
    if(!m_positionSlider->isMouseButtonDown()){
        m_positionSlider->setRange(0,total,1);
        m_positionSlider->setValue(pos,juce::dontSendNotification);
    }
    int posMin=pos/60,posSec=pos%60;
    int totMin=total/60,totSec=total%60;
    m_timeLabel->setText(juce::String(posMin)+":"+juce::String(posSec).paddedLeft('0',2)
        +" / "+juce::String(totMin)+":"+juce::String(totSec).paddedLeft('0',2),juce::dontSendNotification);
}
void MiniPlayerDialog::paint(juce::Graphics& g){
    g.fillAll(Colors::bgDarkest());
    g.setColour(Colors::border());
    g.drawRect(getLocalBounds(),1);
}
void MiniPlayerDialog::resized(){
    auto area=getLocalBounds().reduced(12);
    auto topRow=area.removeFromTop(40);
    m_playPauseBtn->setBounds(topRow.removeFromLeft(36));
    topRow.removeFromLeft(8);
    m_timeLabel->setBounds(topRow.removeFromRight(80));
    m_titleLabel->setBounds(topRow.removeFromTop(20));
    m_artistLabel->setBounds(topRow);
    area.removeFromTop(6);
    m_positionSlider->setBounds(area.removeFromTop(20));
}
}
