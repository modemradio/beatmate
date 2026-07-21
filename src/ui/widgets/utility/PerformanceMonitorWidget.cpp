#include "PerformanceMonitorWidget.h"
#include "../../styles/ColorPalette.h"
namespace BeatMate::UI {
PerformanceMonitorWidget::PerformanceMonitorWidget(){
    m_cpuLabel=std::make_unique<juce::Label>("c","CPU: 0%");m_cpuLabel->setFont(juce::Font(10.0f));m_cpuLabel->setColour(juce::Label::textColourId,Colors::textMuted());addAndMakeVisible(*m_cpuLabel);
    m_ramLabel=std::make_unique<juce::Label>("r","RAM: 0MB");m_ramLabel->setFont(juce::Font(10.0f));m_ramLabel->setColour(juce::Label::textColourId,Colors::textMuted());addAndMakeVisible(*m_ramLabel);
    m_latencyLabel=std::make_unique<juce::Label>("l","Lat: 0ms");m_latencyLabel->setFont(juce::Font(10.0f));m_latencyLabel->setColour(juce::Label::textColourId,Colors::textMuted());addAndMakeVisible(*m_latencyLabel);
    startTimer(2000);
}
void PerformanceMonitorWidget::setCPU(double percent){m_cpu=percent;updateDisplay();}
void PerformanceMonitorWidget::setRAM(int mb){m_ram=mb;updateDisplay();}
void PerformanceMonitorWidget::setLatency(double ms){m_latency=ms;updateDisplay();}
void PerformanceMonitorWidget::timerCallback(){updateDisplay();}
void PerformanceMonitorWidget::updateDisplay(){
    auto cpuColor=m_cpu>80?Colors::error():m_cpu>50?Colors::warning():Colors::success();
    m_cpuLabel->setText("CPU: "+juce::String((int)m_cpu)+"%",juce::dontSendNotification);m_cpuLabel->setColour(juce::Label::textColourId,cpuColor);
    m_ramLabel->setText("RAM: "+juce::String(m_ram)+"MB",juce::dontSendNotification);
    m_latencyLabel->setText("Lat: "+juce::String(m_latency,1)+"ms",juce::dontSendNotification);
}
void PerformanceMonitorWidget::resized(){int w=getWidth()/3;m_cpuLabel->setBounds(0,0,w,getHeight());m_ramLabel->setBounds(w,0,w,getHeight());m_latencyLabel->setBounds(w*2,0,w,getHeight());}
} // namespace BeatMate::UI
