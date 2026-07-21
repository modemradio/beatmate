#include "EnergyGraphWidget.h"
#include "../../styles/ColorPalette.h"
namespace BeatMate::UI {
EnergyGraphWidget::EnergyGraphWidget(){m_data={3.0f,4.0f,5.0f,6.0f,7.0f,8.0f,9.0f,8.0f,7.0f,6.0f,5.0f,4.0f};}
void EnergyGraphWidget::setEnergyData(const std::vector<float>& data){m_data=data;repaint();}
void EnergyGraphWidget::setScale(int min,int max){m_scaleMin=min;m_scaleMax=max;repaint();}
void EnergyGraphWidget::paint(juce::Graphics& g){
    int w=getWidth(),h=getHeight(),margin=30;g.fillAll(juce::Colour(0xFF0A0A0A));
    g.setColour(juce::Colour((juce::uint8)255,255,255,(juce::uint8)20));float range=(float)(m_scaleMax-m_scaleMin);
    for(int i=m_scaleMin;i<=m_scaleMax;++i){int y=h-margin-(int)((i-m_scaleMin)/range*(h-margin*2));g.drawHorizontalLine(y,(float)margin,(float)(w-10));
        g.setColour(juce::Colour(0xFF666666));g.setFont(juce::Font(8.0f));g.drawText(juce::String(i),0,y-6,margin-4,12,juce::Justification::centredRight);g.setColour(juce::Colour((juce::uint8)255,255,255,(juce::uint8)20));}
    if(m_data.empty())return;
    juce::Path curve;
    for(size_t i=0;i<m_data.size();++i){
        float x=(float)margin+(float)i/(m_data.size()-1)*(w-margin-10);float norm=(m_data[i]-m_scaleMin)/range;float y=(float)(h-margin)-norm*(h-margin*2);
        if(i==0)curve.startNewSubPath(x,y);else curve.lineTo(x,y);
    }
    juce::Path fill=curve;fill.lineTo((float)(w-10),(float)(h-margin));fill.lineTo((float)margin,(float)(h-margin));fill.closeSubPath();
    juce::ColourGradient grad(Colors::success().withAlpha(0.4f),0,0,Colors::primary().withAlpha(0.12f),0,(float)h,false);
    grad.addColour(0.5,Colors::warning().withAlpha(0.24f));
    g.setGradientFill(grad);g.fillPath(fill);
    g.setColour(Colors::success());g.strokePath(curve,juce::PathStrokeType(2.0f));
    for(size_t i=0;i<m_data.size();++i){float x=(float)margin+(float)i/(m_data.size()-1)*(w-margin-10);float norm=(m_data[i]-m_scaleMin)/range;float y=(float)(h-margin)-norm*(h-margin*2);g.fillEllipse(x-4,y-4,8,8);}
    g.setColour(Colors::border());g.drawRect(getLocalBounds(),1);
}
} // namespace BeatMate::UI
