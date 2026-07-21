#include "CollapsiblePanel.h"
#include "../../styles/ColorPalette.h"
namespace BeatMate::UI {
CollapsiblePanel::CollapsiblePanel(const juce::String& title):m_title(title){
    m_headerBtn=std::make_unique<juce::TextButton>("v  "+title);m_headerBtn->setColour(juce::TextButton::buttonColourId,Colors::bgMedium());m_headerBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primary().withAlpha(0.3f));m_headerBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());m_headerBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_headerBtn->onClick=[this]{setExpanded(!m_expanded);};addAndMakeVisible(*m_headerBtn);
}
void CollapsiblePanel::setContentWidget(juce::Component* widget){m_contentWidget=widget;addAndMakeVisible(widget);resized();}
void CollapsiblePanel::setExpanded(bool expanded){m_expanded=expanded;if(m_contentWidget)m_contentWidget->setVisible(expanded);
    m_headerBtn->setButtonText(juce::String(expanded?"v":">")+juce::String("  ")+m_title);m_listeners.call([expanded](Listener&l){l.toggled(expanded);});resized();}
void CollapsiblePanel::paint(juce::Graphics& g){g.setColour(Colors::border());g.drawHorizontalLine(32,0.0f,(float)getWidth());}
void CollapsiblePanel::resized(){m_headerBtn->setBounds(0,0,getWidth(),32);if(m_contentWidget&&m_expanded)m_contentWidget->setBounds(0,34,getWidth(),getHeight()-34);}
} // namespace BeatMate::UI
