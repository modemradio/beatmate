#include "ExportDialog.h"
#include "../styles/ColorPalette.h"
#include "../../services/config/I18n.h"
namespace BeatMate::UI {
ExportDialog::ExportDialog(){
    setSize(450,400);
    m_titleLabel=std::make_unique<juce::Label>("t",BM_TJ("dialog.export.title"));
    m_titleLabel->setFont(juce::Font(18.0f,juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId,Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    auto makeLabel=[this](const juce::String& text)->std::unique_ptr<juce::Label>{
        auto l=std::make_unique<juce::Label>("",text);
        l->setFont(juce::Font(12.0f));l->setColour(juce::Label::textColourId,Colors::textSecondary());
        addAndMakeVisible(*l);return l;
    };
    auto makeCombo=[this]()->std::unique_ptr<juce::ComboBox>{
        auto c=std::make_unique<juce::ComboBox>();
        c->setColour(juce::ComboBox::backgroundColourId,Colors::bgLight());
        c->setColour(juce::ComboBox::textColourId,Colors::textPrimary());
        c->setColour(juce::ComboBox::outlineColourId,Colors::border());
        addAndMakeVisible(*c);return c;
    };

    m_formatLabel=makeLabel(BM_TJ("dialog.export.format"));
    m_formatCombo=makeCombo();
    juce::StringArray fmts={"MP3","WAV","FLAC","OGG","AAC"};
    for(int i=0;i<fmts.size();++i)m_formatCombo->addItem(fmts[i],i+1);
    m_formatCombo->setSelectedId(1);

    m_bitrateLabel=makeLabel(BM_TJ("dialog.export.bitrate"));
    m_bitrateCombo=makeCombo();
    juce::StringArray brs={"128 kbps","192 kbps","256 kbps","320 kbps"};
    for(int i=0;i<brs.size();++i)m_bitrateCombo->addItem(brs[i],i+1);
    m_bitrateCombo->setSelectedId(4);

    m_sampleRateLabel=makeLabel(BM_TJ("dialog.export.sampleRate"));
    m_sampleRateCombo=makeCombo();
    juce::StringArray srs={"44100 Hz","48000 Hz","96000 Hz"};
    for(int i=0;i<srs.size();++i)m_sampleRateCombo->addItem(srs[i],i+1);
    m_sampleRateCombo->setSelectedId(1);

    m_destEdit=std::make_unique<juce::TextEditor>();
    m_destEdit->setColour(juce::TextEditor::backgroundColourId,Colors::bgLight());
    m_destEdit->setColour(juce::TextEditor::textColourId,Colors::textPrimary());
    m_destEdit->setColour(juce::TextEditor::outlineColourId,Colors::border());
    m_destEdit->setTextToShowWhenEmpty(BM_TJ("dialog.export.destPlaceholder"),Colors::textDim());
    addAndMakeVisible(*m_destEdit);

    m_browseBtn=std::make_unique<juce::TextButton>("...");
    m_browseBtn->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());
    m_browseBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primary());
    m_browseBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_browseBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_browseBtn->onClick=[this]{
        juce::FileChooser chooser(BM_TJ("dialog.export.destPrompt"),juce::File(),"",true);
        if(chooser.browseForDirectory())
            m_destEdit->setText(chooser.getResult().getFullPathName());
    };
    addAndMakeVisible(*m_browseBtn);

    auto makeCheck=[this](const juce::String& text,bool checked)->std::unique_ptr<juce::ToggleButton>{
        auto c=std::make_unique<juce::ToggleButton>(text);
        c->setColour(juce::ToggleButton::textColourId,Colors::textSecondary());
        c->setColour(juce::ToggleButton::tickColourId,Colors::primary());
        c->setToggleState(checked,juce::dontSendNotification);
        addAndMakeVisible(*c);return c;
    };
    m_metadataCheck=makeCheck(BM_TJ("dialog.export.includeMeta"),true);
    m_cuePointsCheck=makeCheck(BM_TJ("dialog.export.includeCues"),true);
    m_normalizeCheck=makeCheck(BM_TJ("dialog.export.normalize"),false);

    m_cancelBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.export.cancel"));
    m_cancelBtn->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());
    m_cancelBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primary());
    m_cancelBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_cancelBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_cancelBtn->onClick=[this]{if(auto*dw=findParentComponentOfClass<juce::DialogWindow>())dw->exitModalState(0);};
    addAndMakeVisible(*m_cancelBtn);

    m_okBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.export.do"));
    m_okBtn->setColour(juce::TextButton::buttonColourId,Colors::primary());
    m_okBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primaryHover());
    m_okBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_okBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_okBtn->onClick=[this]{if(auto*dw=findParentComponentOfClass<juce::DialogWindow>())dw->exitModalState(1);};
    addAndMakeVisible(*m_okBtn);
}
juce::String ExportDialog::format() const{return m_formatCombo->getText();}
int ExportDialog::bitrate() const{return m_bitrateCombo->getText().upToFirstOccurrenceOf(" ",false,false).getIntValue();}
juce::String ExportDialog::destination() const{return m_destEdit->getText();}
bool ExportDialog::exportMetadata() const{return m_metadataCheck->getToggleState();}
int ExportDialog::showDialog(juce::Component*){
    auto*content=new ExportDialog();
    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(content);opts.dialogTitle=BM_TJ("dialog.export.title");
    opts.dialogBackgroundColour=Colors::bgDarker();opts.resizable=false;opts.useNativeTitleBar=true;
    return opts.runModal();
}
void ExportDialog::paint(juce::Graphics& g){
    juce::ColourGradient bgGrad(juce::Colour(0xFF0D0D14),0.0f,0.0f,
        juce::Colour(0xFF08080E),0.0f,static_cast<float>(getHeight()),false);
    g.setGradientFill(bgGrad);
    g.fillRect(getLocalBounds());

    g.setColour(Colors::primary().withAlpha(0.03f));
    g.fillEllipse(-50.0f,-50.0f,200.0f,200.0f);

    g.setColour(Colors::accent().withAlpha(0.02f));
    g.fillEllipse(static_cast<float>(getWidth())-40.0f,
                  static_cast<float>(getHeight())*0.3f,120.0f,120.0f);

    g.setColour(Colors::primary().withAlpha(0.6f));
    g.fillRect(24.0f,30.0f,3.0f,16.0f);

    float panelW=(float)getWidth()-48.0f;
    auto fmtPanel=juce::Rectangle<float>(24.0f,54.0f,panelW,120.0f);

    juce::ColourGradient fmtGrad(Colors::glassWhite(),24.0f,54.0f,
        juce::Colours::transparentBlack,24.0f,174.0f,false);
    g.setGradientFill(fmtGrad);
    g.fillRoundedRectangle(fmtPanel,8.0f);
    g.setColour(Colors::bgMedium().withAlpha(0.6f));
    g.fillRoundedRectangle(fmtPanel,8.0f);
    g.setColour(Colors::glassBorder());
    g.drawRoundedRectangle(fmtPanel,8.0f,0.5f);
    g.setColour(Colors::primary().withAlpha(0.04f));
    g.drawRoundedRectangle(fmtPanel.expanded(1.0f),9.0f,1.0f);

    g.setColour(Colors::primary().withAlpha(0.5f));
    g.fillRect(34.0f,48.0f,3.0f,12.0f);
    g.setColour(Colors::textPrimary());g.setFont(juce::Font(12.0f,juce::Font::bold));
    g.drawText(BM_TJ("dialog.export.section"),42,46,100,16,juce::Justification::centredLeft);

    float optY=210.0f;
    float optH=90.0f;
    auto optPanel=juce::Rectangle<float>(24.0f,optY,panelW,optH);
    g.setColour(Colors::glassWhite());
    g.fillRoundedRectangle(optPanel,8.0f);
    g.setColour(Colors::glassBorder());
    g.drawRoundedRectangle(optPanel,8.0f,0.5f);

    float footerY=static_cast<float>(getHeight())-54.0f;
    g.setColour(Colors::primary().withAlpha(0.08f));
    g.fillRect(0.0f,footerY,static_cast<float>(getWidth()),1.0f);

    g.setColour(Colors::glassBorder());
    g.drawRect(getLocalBounds().toFloat(),0.5f);
}
void ExportDialog::resized(){
    auto area=getLocalBounds().reduced(24);
    m_titleLabel->setBounds(area.removeFromTop(26));area.removeFromTop(8);
    auto fmtArea=area.removeFromTop(120).reduced(12);
    int rowH=28;
    auto r1=fmtArea.removeFromTop(rowH);m_formatLabel->setBounds(r1.removeFromLeft(100));m_formatCombo->setBounds(r1);
    fmtArea.removeFromTop(4);
    auto r2=fmtArea.removeFromTop(rowH);m_bitrateLabel->setBounds(r2.removeFromLeft(100));m_bitrateCombo->setBounds(r2);
    fmtArea.removeFromTop(4);
    auto r3=fmtArea.removeFromTop(rowH);m_sampleRateLabel->setBounds(r3.removeFromLeft(100));m_sampleRateCombo->setBounds(r3);
    area.removeFromTop(12);
    auto destRow=area.removeFromTop(28);
    m_browseBtn->setBounds(destRow.removeFromRight(40));destRow.removeFromRight(4);
    m_destEdit->setBounds(destRow);
    area.removeFromTop(12);
    m_metadataCheck->setBounds(area.removeFromTop(24));area.removeFromTop(4);
    m_cuePointsCheck->setBounds(area.removeFromTop(24));area.removeFromTop(4);
    m_normalizeCheck->setBounds(area.removeFromTop(24));
    auto btnRow=area.removeFromBottom(30);
    m_okBtn->setBounds(btnRow.removeFromRight(100));btnRow.removeFromRight(8);
    m_cancelBtn->setBounds(btnRow.removeFromRight(100));
}
} // namespace BeatMate::UI
