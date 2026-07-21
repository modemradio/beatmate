#include <algorithm>
#include "RotaryKnobWidget.h"
namespace BeatMate::UI {
RotaryKnobWidget::RotaryKnobWidget(){setSize(70,85);setMouseCursor(juce::MouseCursor::PointingHandCursor);}
void RotaryKnobWidget::setValue(double v){v=juce::jlimit(m_min,v,m_max);if(std::abs(m_value-v)<1e-9)return;m_value=v;m_listeners.call([v](Listener&l){l.valueChanged(v);});repaint();}
void RotaryKnobWidget::setRange(double min,double max){m_min=min;m_max=max;m_value=juce::jlimit(min,m_value,max);repaint();}
void RotaryKnobWidget::paint(juce::Graphics& g){
    g.setColour(juce::Colours::transparentBlack);g.fillAll();
    int knobSize=std::min(getWidth(),getHeight()-20);int cx=getWidth()/2,cy=knobSize/2+2,radius=knobSize/2-4;
    // Background arc
    juce::Path bgArc;bgArc.addCentredArc((float)cx,(float)cy,(float)radius,(float)radius,0,-juce::MathConstants<float>::pi*5.0f/4.0f,juce::MathConstants<float>::pi*1.0f/4.0f,true);
    g.setColour(juce::Colour(0xFF2A2A2A));g.strokePath(bgArc,juce::PathStrokeType(4.0f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
    // Value arc
    double normalized=(m_value-m_min)/(m_max-m_min);
    float startAngle=-juce::MathConstants<float>::pi*5.0f/4.0f;float endAngle=startAngle+(float)normalized*juce::MathConstants<float>::pi*3.0f/2.0f;
    juce::Path valArc;valArc.addCentredArc((float)cx,(float)cy,(float)radius,(float)radius,0,startAngle,endAngle,true);
    g.setColour(m_color);g.strokePath(valArc,juce::PathStrokeType(4.0f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
    // Knob body
    g.setColour(juce::Colour(0xFF2A2A2A));g.fillEllipse((float)(cx-radius+6),(float)(cy-radius+6),(float)(radius-6)*2,(float)(radius-6)*2);
    g.setColour(juce::Colour(0xFF333333));g.drawEllipse((float)(cx-radius+6),(float)(cy-radius+6),(float)(radius-6)*2,(float)(radius-6)*2,1.0f);
    // Pointer
    float angle=-juce::MathConstants<float>::pi*5.0f/4.0f+(float)normalized*juce::MathConstants<float>::pi*3.0f/2.0f;
    int lr=radius-10;g.setColour(m_color);
    g.drawLine((float)cx,(float)cy,(float)(cx+lr*std::cos(angle)),(float)(cy+lr*std::sin(angle)),2.0f);
    // Value text
    g.setColour(juce::Colours::white);g.setFont(juce::Font(9.0f,juce::Font::bold));
    juce::String text=juce::String(m_value,(m_max-m_min)>10?0:1)+m_suffix;
    g.drawText(text,0,cy-8,getWidth(),16,juce::Justification::centred);
    // Label
    if(m_label.isNotEmpty()){g.setColour(juce::Colour(0xFF999999));g.setFont(juce::Font(8.0f));g.drawText(m_label,0,getHeight()-14,getWidth(),14,juce::Justification::centred);}
}
void RotaryKnobWidget::mouseDown(const juce::MouseEvent& e){if(e.mods.isLeftButtonDown()){m_dragging=true;m_lastMousePos=e.getPosition();}}
void RotaryKnobWidget::mouseDrag(const juce::MouseEvent& e){
    if(m_dragging){int dy=m_lastMousePos.y-e.y;double step=(m_max-m_min)/200.0;setValue(m_value+dy*step);m_lastMousePos=e.getPosition();}
}
void RotaryKnobWidget::mouseDoubleClick(const juce::MouseEvent&){setValue(m_defaultValue);}
void RotaryKnobWidget::mouseWheelMove(const juce::MouseEvent&,const juce::MouseWheelDetails& w){double step=(m_max-m_min)/100.0;setValue(m_value+(w.deltaY>0?step:-step));}
} // namespace BeatMate::UI
