#include "DockablePanel.h"
#include "../../styles/ColorPalette.h"
namespace BeatMate::UI {
DockablePanel::DockablePanel(const juce::String& title):m_title(title){}
void DockablePanel::setContentWidget(juce::Component* widget){m_content=widget;if(widget)addAndMakeVisible(widget);resized();}
void DockablePanel::saveState(){}void DockablePanel::restoreState(){}
void DockablePanel::paint(juce::Graphics& g){
    g.setColour(Colors::bgMedium());g.fillRect(0,0,getWidth(),28);g.setColour(Colors::border());g.drawHorizontalLine(28,0.0f,(float)getWidth());
    g.setColour(Colors::textPrimary());g.setFont(juce::Font(12.0f,juce::Font::bold));g.drawText(m_title,8,0,getWidth()-16,28,juce::Justification::centredLeft);
}
void DockablePanel::resized(){if(m_content)m_content->setBounds(0,30,getWidth(),getHeight()-30);}
} // namespace BeatMate::UI
