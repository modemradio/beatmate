#include "StereoVUMeterWidget.h"
#include "VUMeterWidget.h"
namespace BeatMate::UI {
StereoVUMeterWidget::StereoVUMeterWidget(){
    m_lLabel=std::make_unique<juce::Label>("l","L");m_lLabel->setFont(juce::Font(9.0f));m_lLabel->setColour(juce::Label::textColourId,juce::Colour(0xFF666666));m_lLabel->setJustificationType(juce::Justification::centred);addAndMakeVisible(*m_lLabel);
    m_leftMeter=std::make_unique<VUMeterWidget>();addAndMakeVisible(*m_leftMeter);
    m_rightMeter=std::make_unique<VUMeterWidget>();addAndMakeVisible(*m_rightMeter);
    m_rLabel=std::make_unique<juce::Label>("r","R");m_rLabel->setFont(juce::Font(9.0f));m_rLabel->setColour(juce::Label::textColourId,juce::Colour(0xFF666666));m_rLabel->setJustificationType(juce::Justification::centred);addAndMakeVisible(*m_rLabel);
}
void StereoVUMeterWidget::setLevels(float left,float right){m_leftMeter->setLevel(left);m_rightMeter->setLevel(right);}
void StereoVUMeterWidget::resized(){m_lLabel->setBounds(0,0,12,getHeight());m_leftMeter->setBounds(12,0,20,getHeight());m_rightMeter->setBounds(34,0,20,getHeight());m_rLabel->setBounds(56,0,12,getHeight());}
}
