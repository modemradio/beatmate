#include "PlayHistoryDialog.h"
#include "../styles/ColorPalette.h"
#include "../../services/config/I18n.h"
#include <spdlog/spdlog.h>
namespace BeatMate::UI {
PlayHistoryDialog::PlayHistoryDialog(){
    setSize(600,450);
    m_titleLabel=std::make_unique<juce::Label>("t",BM_TJ("dialog.history.title"));
    m_titleLabel->setFont(juce::Font(18.0f,juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId,Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    auto makeLbl=[this](const juce::String& text){
        auto l=std::make_unique<juce::Label>("",text);
        l->setFont(juce::Font(12.0f));l->setColour(juce::Label::textColourId,Colors::textSecondary());
        addAndMakeVisible(*l);return l;
    };
    m_fromLabel=makeLbl(BM_TJ("dialog.history.from"));
    m_fromEdit=std::make_unique<juce::TextEditor>();
    m_fromEdit->setColour(juce::TextEditor::backgroundColourId,Colors::bgLight());
    m_fromEdit->setColour(juce::TextEditor::textColourId,Colors::textPrimary());
    m_fromEdit->setColour(juce::TextEditor::outlineColourId,Colors::border());
    m_fromEdit->setText("01/01/2026");
    addAndMakeVisible(*m_fromEdit);

    m_toLabel=makeLbl(BM_TJ("dialog.history.to"));
    m_toEdit=std::make_unique<juce::TextEditor>();
    m_toEdit->setColour(juce::TextEditor::backgroundColourId,Colors::bgLight());
    m_toEdit->setColour(juce::TextEditor::textColourId,Colors::textPrimary());
    m_toEdit->setColour(juce::TextEditor::outlineColourId,Colors::border());
    m_toEdit->setText("22/03/2026");
    addAndMakeVisible(*m_toEdit);

    m_filterBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.history.filter"));
    m_filterBtn->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());
    m_filterBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primary());
    m_filterBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_filterBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_filterBtn->onClick=[this]{ spdlog::warn("[PlayHistory] filter sans handler"); };
    addAndMakeVisible(*m_filterBtn);

    m_totalPlaysLabel=std::make_unique<juce::Label>("tp",BM_TJ("dialog.history.totalPrefix")+juce::String("247")+BM_TJ("dialog.history.playsSuffix"));
    m_totalPlaysLabel->setFont(juce::Font(12.0f,juce::Font::bold));
    m_totalPlaysLabel->setColour(juce::Label::textColourId,Colors::primary());
    addAndMakeVisible(*m_totalPlaysLabel);

    m_mostPlayedLabel=std::make_unique<juce::Label>("mp",BM_TJ("dialog.history.mostPlayed"));
    m_mostPlayedLabel->setFont(juce::Font(12.0f,juce::Font::bold));
    m_mostPlayedLabel->setColour(juce::Label::textColourId,Colors::success());
    m_mostPlayedLabel->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*m_mostPlayedLabel);

    m_historyTable=std::make_unique<juce::TableListBox>("history",this);
    m_historyTable->setColour(juce::ListBox::backgroundColourId,Colors::bgDarker());
    m_historyTable->setColour(juce::ListBox::outlineColourId,Colors::border());
    m_historyTable->getHeader().addColumn(BM_TJ("dialog.history.colDate"),1,150);
    m_historyTable->getHeader().addColumn(BM_TJ("dialog.history.colTitle"),2,200);
    m_historyTable->getHeader().addColumn(BM_TJ("dialog.history.colArtist"),3,150);
    m_historyTable->getHeader().addColumn(BM_TJ("dialog.history.colBpm"),4,60);
    m_historyTable->getHeader().addColumn(BM_TJ("dialog.history.colDuration"),5,80);
    m_historyTable->getHeader().setColour(juce::TableHeaderComponent::backgroundColourId,Colors::bgMedium());
    m_historyTable->getHeader().setColour(juce::TableHeaderComponent::textColourId,Colors::textMuted());
    addAndMakeVisible(*m_historyTable);

    m_closeBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.history.close"));
    m_closeBtn->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());
    m_closeBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primary());
    m_closeBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_closeBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_closeBtn->onClick=[this]{if(auto*dw=findParentComponentOfClass<juce::DialogWindow>())dw->exitModalState(1);};
    addAndMakeVisible(*m_closeBtn);
}
void PlayHistoryDialog::paintRowBackground(juce::Graphics& g,int rowNumber,int,int height,bool rowIsSelected){
    if(rowIsSelected)g.fillAll(Colors::primary().withAlpha(0.3f));
    else if(rowNumber%2)g.fillAll(Colors::bgDark());
}
void PlayHistoryDialog::paintCell(juce::Graphics& g,int,int,int width,int height,bool){
    g.setColour(Colors::textSecondary());g.setFont(12.0f);
    g.drawText("-",4,0,width-8,height,juce::Justification::centredLeft);
}
void PlayHistoryDialog::paint(juce::Graphics& g){ProDraw::viewBackground(g, getWidth(), getHeight());}
void PlayHistoryDialog::resized(){
    auto area=getLocalBounds().reduced(24);
    m_titleLabel->setBounds(area.removeFromTop(26));area.removeFromTop(12);
    auto filterRow=area.removeFromTop(28);
    m_fromLabel->setBounds(filterRow.removeFromLeft(30));
    m_fromEdit->setBounds(filterRow.removeFromLeft(100));filterRow.removeFromLeft(8);
    m_toLabel->setBounds(filterRow.removeFromLeft(30));
    m_toEdit->setBounds(filterRow.removeFromLeft(100));filterRow.removeFromLeft(8);
    m_filterBtn->setBounds(filterRow.removeFromLeft(70));
    area.removeFromTop(8);
    auto statsRow=area.removeFromTop(20);
    m_totalPlaysLabel->setBounds(statsRow.removeFromLeft(statsRow.getWidth()/2));
    m_mostPlayedLabel->setBounds(statsRow);
    area.removeFromTop(8);
    auto btnRow=area.removeFromBottom(30);
    m_closeBtn->setBounds(btnRow.removeFromRight(100));
    area.removeFromBottom(8);
    m_historyTable->setBounds(area);
}
} // namespace BeatMate::UI
