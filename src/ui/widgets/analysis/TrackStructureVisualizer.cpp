#include "TrackStructureVisualizer.h"
namespace BeatMate::UI {
TrackStructureVisualizer::TrackStructureVisualizer(){
    m_sections={{"Intro",0.0,0.08,juce::Colour(0xFF4A90E2)},{"Verse 1",0.08,0.22,juce::Colour(0xFF00FFA3)},{"Chorus",0.22,0.35,juce::Colour(0xFFFF6B9D)},{"Verse 2",0.35,0.48,juce::Colour(0xFF00FFA3)},{"Chorus",0.48,0.62,juce::Colour(0xFFFF6B9D)},{"Drop",0.62,0.78,juce::Colour(0xFFFFB800)},{"Breakdown",0.78,0.88,juce::Colour(0xFFB048FF)},{"Outro",0.88,1.0,juce::Colour(0xFF4A90E2)}};
}
void TrackStructureVisualizer::setSections(const std::vector<StructureSection>& sections){m_sections=sections;repaint();}
void TrackStructureVisualizer::paint(juce::Graphics& g){
    g.fillAll(juce::Colour(0xFF0A0A0A));int w=getWidth(),h=getHeight();
    for(auto&sec:m_sections){int x1=(int)(sec.start*w),x2=(int)(sec.end*w),barH=h-18;
        g.setColour(sec.color.withAlpha(0.7f));g.fillRoundedRectangle((float)(x1+1),2.0f,(float)(x2-x1-2),(float)barH,3.0f);
        g.setColour(sec.color);g.drawRoundedRectangle((float)(x1+1),2.0f,(float)(x2-x1-2),(float)barH,3.0f,1.0f);
        if(x2-x1>30){g.setColour(juce::Colours::white);g.setFont(juce::Font(8.0f));g.drawText(sec.label,x1+3,2,x2-x1-6,barH,juce::Justification::centred);}
    }
    g.setColour(juce::Colour(0xFF2A2A2A));g.drawHorizontalLine(h-1,0.0f,(float)w);
}
void TrackStructureVisualizer::mouseDown(const juce::MouseEvent& e){
    double pos=(double)e.x/getWidth();
    for(size_t i=0;i<m_sections.size();++i) if(pos>=m_sections[i].start&&pos<m_sections[i].end){m_listeners.call([i](Listener&l){l.sectionClicked((int)i);});break;}
}
} // namespace BeatMate::UI
