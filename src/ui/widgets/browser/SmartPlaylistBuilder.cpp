#include "SmartPlaylistBuilder.h"
#include "../../styles/ColorPalette.h"
#include "../../../services/config/I18n.h"
namespace BeatMate::UI {
SmartPlaylistBuilder::SmartPlaylistBuilder(){setupUI();}
void SmartPlaylistBuilder::setupUI(){
    m_titleLabel=std::make_unique<juce::Label>("t",BM_TJ("widget.SmartPlaylistBuilder.title"));m_titleLabel->setFont(juce::Font(13.0f,juce::Font::bold));m_titleLabel->setColour(juce::Label::textColourId,Colors::textPrimary());addAndMakeVisible(*m_titleLabel);
    m_addBtn=std::make_unique<juce::TextButton>(BM_TJ("widget.SmartPlaylistBuilder.add"));m_addBtn->setColour(juce::TextButton::buttonColourId,Colors::primary());m_addBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());m_addBtn->onClick=[this]{addRule();};addAndMakeVisible(*m_addBtn);
    m_removeBtn=std::make_unique<juce::TextButton>(BM_TJ("widget.SmartPlaylistBuilder.remove"));m_removeBtn->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());m_removeBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());m_removeBtn->onClick=[this]{removeSelectedRule();};addAndMakeVisible(*m_removeBtn);
    m_rulesModel=std::make_unique<RulesModel>();
    m_rulesTable=std::make_unique<juce::ListBox>("rules",m_rulesModel.get());m_rulesTable->setRowHeight(28);m_rulesTable->setColour(juce::ListBox::backgroundColourId,Colors::bgDark());addAndMakeVisible(*m_rulesTable);
}
void SmartPlaylistBuilder::addRule(){m_listeners.call(&Listener::rulesChanged);}
void SmartPlaylistBuilder::removeSelectedRule(){m_listeners.call(&Listener::rulesChanged);}
void SmartPlaylistBuilder::clearRules(){m_listeners.call(&Listener::rulesChanged);}
std::vector<SmartPlaylistBuilder::Rule> SmartPlaylistBuilder::rules() const{return{};}
void SmartPlaylistBuilder::resized(){m_titleLabel->setBounds(0,0,300,20);m_rulesTable->setBounds(0,24,getWidth(),getHeight()-56);m_addBtn->setBounds(0,getHeight()-28,90,24);m_removeBtn->setBounds(96,getHeight()-28,80,24);}
} // namespace BeatMate::UI
