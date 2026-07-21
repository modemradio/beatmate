#include "KeyboardShortcutsOverlay.h"
#include "../../styles/ColorPalette.h"
#include "../../../services/config/I18n.h"
namespace BeatMate::UI {
KeyboardShortcutsOverlay::KeyboardShortcutsOverlay(){
    setVisible(false);setWantsKeyboardFocus(true);
    m_groups={{BM_TJ("widget.KeyboardShortcutsOverlay.group.playback"),{{BM_TJ("widget.KeyboardShortcutsOverlay.key.space"),BM_TJ("widget.KeyboardShortcutsOverlay.action.playPause")},{BM_TJ("widget.KeyboardShortcutsOverlay.key.escape"),BM_TJ("widget.KeyboardShortcutsOverlay.action.stop")},{BM_TJ("widget.KeyboardShortcutsOverlay.key.leftRight"),BM_TJ("widget.KeyboardShortcutsOverlay.action.prevNextTrack")}}},
        {BM_TJ("widget.KeyboardShortcutsOverlay.group.navigation"),{{"Ctrl+F",BM_TJ("widget.KeyboardShortcutsOverlay.action.search")},{"F11",BM_TJ("widget.KeyboardShortcutsOverlay.action.fullscreen")},{"Ctrl+,",BM_TJ("widget.KeyboardShortcutsOverlay.action.preferences")}}},
        {BM_TJ("widget.KeyboardShortcutsOverlay.group.files"),{{"Ctrl+I",BM_TJ("widget.KeyboardShortcutsOverlay.action.import")},{"Ctrl+E",BM_TJ("widget.KeyboardShortcutsOverlay.action.export")},{"Ctrl+S",BM_TJ("widget.KeyboardShortcutsOverlay.action.save")}}},
        {BM_TJ("widget.KeyboardShortcutsOverlay.group.analysis"),{{"Ctrl+A",BM_TJ("widget.KeyboardShortcutsOverlay.action.analyze")},{"Ctrl+N",BM_TJ("widget.KeyboardShortcutsOverlay.action.normalize")}}}};
}
void KeyboardShortcutsOverlay::toggle(){if(isVisible())setVisible(false);else{if(auto*p=getParentComponent())setBounds(p->getLocalBounds());setVisible(true);toFront(true);grabKeyboardFocus();}}
void KeyboardShortcutsOverlay::paint(juce::Graphics& g){
    g.fillAll(juce::Colour((juce::uint8)0,0,0,(juce::uint8)200));
    g.setColour(juce::Colours::white);g.setFont(juce::Font(24.0f,juce::Font::bold));g.drawText(BM_TJ("widget.KeyboardShortcutsOverlay.title"),0,30,getWidth(),40,juce::Justification::centred);
    int cols=(int)m_groups.size(),colW=getWidth()/(cols+1);int startY=100;
    for(int gi=0;gi<(int)m_groups.size();++gi){
        int x=(gi+1)*colW-colW/2,y=startY;
        g.setColour(Colors::primary());g.setFont(juce::Font(14.0f,juce::Font::bold));g.drawText(m_groups[gi].title,x,y,200,20,juce::Justification::centredLeft);y+=30;
        for(auto&s:m_groups[gi].shortcuts){
            g.setColour(juce::Colour(0xFF1E1E1E));g.fillRoundedRectangle((float)x,(float)(y-12),90.0f,22.0f,4.0f);
            g.setColour(juce::Colour(0xFF555555));g.drawRoundedRectangle((float)x,(float)(y-12),90.0f,22.0f,4.0f,1.0f);
            g.setColour(Colors::success());g.setFont(juce::Font(10.0f,juce::Font::bold));g.drawText(s.first,x,y-12,90,22,juce::Justification::centred);
            g.setColour(Colors::textSecondary());g.setFont(juce::Font(11.0f));g.drawText(s.second,x+100,y-12,200,22,juce::Justification::centredLeft);
            y+=30;
        }
    }
    g.setColour(Colors::textDim());g.setFont(juce::Font(11.0f));g.drawText(BM_TJ("widget.KeyboardShortcutsOverlay.footer"),0,getHeight()-40,getWidth(),30,juce::Justification::centred);
}
} // namespace BeatMate::UI
