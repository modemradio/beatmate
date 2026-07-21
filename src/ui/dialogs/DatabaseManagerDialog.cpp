#include "DatabaseManagerDialog.h"
#include "../styles/ColorPalette.h"
#include "../../services/config/I18n.h"
namespace BeatMate::UI {
DatabaseManagerDialog::DatabaseManagerDialog(){
    setSize(500,400);
    m_titleLabel=std::make_unique<juce::Label>("t",BM_TJ("dialog.db.title"));
    m_titleLabel->setFont(juce::Font(18.0f,juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId,Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    auto makeLabel=[this](const juce::String& text){
        auto l=std::make_unique<juce::Label>("",text);
        l->setFont(juce::Font(12.0f));l->setColour(juce::Label::textColourId,Colors::textSecondary());
        addAndMakeVisible(*l);return l;
    };
    auto makeVal=[this](const juce::String& text){
        auto l=std::make_unique<juce::Label>("",text);
        l->setFont(juce::Font(12.0f));l->setColour(juce::Label::textColourId,Colors::textPrimary());
        addAndMakeVisible(*l);return l;
    };
    m_dbSizeLabel=makeLabel(BM_TJ("dialog.db.size"));m_dbSizeVal=makeVal("12.4 MB");
    m_trackCountLabel=makeLabel(BM_TJ("dialog.db.tracks"));m_trackCountVal=makeVal("1,247");
    m_playlistCountLabel=makeLabel(BM_TJ("dialog.db.playlists"));m_playlistCountVal=makeVal("15");
    m_cueCountLabel=makeLabel(BM_TJ("dialog.db.cues"));m_cueCountVal=makeVal("3,891");

    auto makeBtn=[this](const juce::String& text){
        auto b=std::make_unique<juce::TextButton>(text);
        b->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());
        b->setColour(juce::TextButton::buttonOnColourId,Colors::primary());
        b->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
        b->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
        addAndMakeVisible(*b);return b;
    };
    m_cleanBtn=makeBtn(BM_TJ("dialog.db.clean"));
    m_cleanBtn->onClick=[this]{if(onCleanupRequested)onCleanupRequested();};
    m_optimizeBtn=makeBtn(BM_TJ("dialog.db.optimize"));
    m_optimizeBtn->onClick=[this]{if(onOptimizeRequested)onOptimizeRequested();};
    m_repairBtn=makeBtn(BM_TJ("dialog.db.repair"));
    m_repairBtn->onClick=[this]{if(onRepairRequested)onRepairRequested();};

    m_statusLabel=std::make_unique<juce::Label>("st",BM_TJ("dialog.db.ready"));
    m_statusLabel->setFont(juce::Font(12.0f));
    m_statusLabel->setColour(juce::Label::textColourId,Colors::textMuted());
    addAndMakeVisible(*m_statusLabel);

    m_closeBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.db.close"));
    m_closeBtn->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());
    m_closeBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primary());
    m_closeBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_closeBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_closeBtn->onClick=[this]{if(auto*dw=findParentComponentOfClass<juce::DialogWindow>())dw->exitModalState(1);};
    addAndMakeVisible(*m_closeBtn);
}
void DatabaseManagerDialog::paint(juce::Graphics& g){
    ProDraw::viewBackground(g, getWidth(), getHeight());
    auto area=getLocalBounds().reduced(24);
    float statsY=(float)area.getY()+38.0f;
    g.setColour(Colors::bgMedium());
    g.fillRoundedRectangle(24.0f,statsY,(float)getWidth()-48.0f,100.0f,8.0f);
    g.setColour(Colors::border());
    g.drawRoundedRectangle(24.0f,statsY,(float)getWidth()-48.0f,100.0f,8.0f,1.0f);
    g.setColour(Colors::textPrimary());g.setFont(juce::Font(12.0f,juce::Font::bold));
    g.drawText(BM_TJ("dialog.db.stats"),34,int(statsY)-8,120,16,juce::Justification::centredLeft);
    float actY=statsY+116.0f;
    g.setColour(Colors::bgMedium());
    g.fillRoundedRectangle(24.0f,actY,(float)getWidth()-48.0f,110.0f,8.0f);
    g.setColour(Colors::border());
    g.drawRoundedRectangle(24.0f,actY,(float)getWidth()-48.0f,110.0f,8.0f,1.0f);
    g.setColour(Colors::textPrimary());g.setFont(juce::Font(12.0f,juce::Font::bold));
    g.drawText(BM_TJ("dialog.db.actions"),34,int(actY)-8,120,16,juce::Justification::centredLeft);
    float progY=(float)getHeight()-80.0f;
    g.setColour(Colors::bgLight());
    g.fillRoundedRectangle(24.0f,progY,(float)getWidth()-48.0f,18.0f,4.0f);
    if(m_progress>0){
        g.setColour(Colors::primary());
        g.fillRoundedRectangle(24.0f,progY,(float)(getWidth()-48)*m_progress,18.0f,4.0f);
    }
}
void DatabaseManagerDialog::resized(){
    auto area=getLocalBounds().reduced(24);
    m_titleLabel->setBounds(area.removeFromTop(26));area.removeFromTop(12);
    auto statsArea=area.removeFromTop(100).reduced(12);
    int rowH=22;
    auto r1=statsArea.removeFromTop(rowH);m_dbSizeLabel->setBounds(r1.removeFromLeft(100));m_dbSizeVal->setBounds(r1);
    auto r2=statsArea.removeFromTop(rowH);m_trackCountLabel->setBounds(r2.removeFromLeft(100));m_trackCountVal->setBounds(r2);
    auto r3=statsArea.removeFromTop(rowH);m_playlistCountLabel->setBounds(r3.removeFromLeft(100));m_playlistCountVal->setBounds(r3);
    auto r4=statsArea.removeFromTop(rowH);m_cueCountLabel->setBounds(r4.removeFromLeft(100));m_cueCountVal->setBounds(r4);
    area.removeFromTop(16);
    auto actArea=area.removeFromTop(110).reduced(12);
    m_cleanBtn->setBounds(actArea.removeFromTop(28));actArea.removeFromTop(4);
    m_optimizeBtn->setBounds(actArea.removeFromTop(28));actArea.removeFromTop(4);
    m_repairBtn->setBounds(actArea.removeFromTop(28));
    area.removeFromTop(8);
    m_statusLabel->setBounds(area.removeFromTop(20));
    area.removeFromTop(22); // progress bar space
    auto btnRow=area.removeFromBottom(30);
    m_closeBtn->setBounds(btnRow.removeFromRight(100));
}
} // namespace BeatMate::UI
