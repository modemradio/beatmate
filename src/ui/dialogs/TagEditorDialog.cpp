#include "TagEditorDialog.h"
#include "../styles/ColorPalette.h"
#include "../../services/config/I18n.h"
namespace BeatMate::UI {
TagEditorDialog::TagEditorDialog(){
    setSize(550,550);
    m_headerLabel=std::make_unique<juce::Label>("h",BM_TJ("dialog.tagEditor.title"));
    m_headerLabel->setFont(juce::Font(18.0f,juce::Font::bold));
    m_headerLabel->setColour(juce::Label::textColourId,Colors::textPrimary());
    addAndMakeVisible(*m_headerLabel);

    auto makeLbl=[this](const juce::String& text){
        auto l=std::make_unique<juce::Label>("",text);
        l->setFont(juce::Font(12.0f));l->setColour(juce::Label::textColourId,Colors::textSecondary());
        addAndMakeVisible(*l);return l;
    };
    auto makeEdit=[this](){
        auto e=std::make_unique<juce::TextEditor>();
        e->setColour(juce::TextEditor::backgroundColourId,Colors::bgLight());
        e->setColour(juce::TextEditor::textColourId,Colors::textPrimary());
        e->setColour(juce::TextEditor::outlineColourId,Colors::border());
        addAndMakeVisible(*e);return e;
    };
    auto makeCombo=[this](){
        auto c=std::make_unique<juce::ComboBox>();
        c->setColour(juce::ComboBox::backgroundColourId,Colors::bgLight());
        c->setColour(juce::ComboBox::textColourId,Colors::textPrimary());
        c->setColour(juce::ComboBox::outlineColourId,Colors::border());
        addAndMakeVisible(*c);return c;
    };

    m_titleLbl=makeLbl(BM_TJ("dialog.tagEditor.titleField"));m_titleEdit=makeEdit();
    m_artistLbl=makeLbl(BM_TJ("dialog.tagEditor.artist"));m_artistEdit=makeEdit();
    m_albumLbl=makeLbl(BM_TJ("dialog.tagEditor.album"));m_albumEdit=makeEdit();

    m_genreLbl=makeLbl(BM_TJ("dialog.tagEditor.genre"));
    m_genreCombo=makeCombo();
    m_genreCombo->setEditableText(true);
    juce::StringArray genres={"House","Techno","Trance","Drum & Bass","Dubstep","Hip-Hop","Pop","Rock","Ambient","Deep House","Tech House","Progressive","Minimal","Melodic Techno","Electronica"};
    for(int i=0;i<genres.size();++i)m_genreCombo->addItem(genres[i],i+1);

    m_yearLbl=makeLbl(BM_TJ("dialog.tagEditor.year"));
    m_yearSlider=std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal,juce::Slider::TextBoxRight);
    m_yearSlider->setRange(1900,2030,1);m_yearSlider->setValue(2024);
    m_yearSlider->setColour(juce::Slider::backgroundColourId,Colors::bgLight());
    m_yearSlider->setColour(juce::Slider::trackColourId,Colors::primary());
    m_yearSlider->setColour(juce::Slider::thumbColourId,Colors::primary());
    m_yearSlider->setColour(juce::Slider::textBoxTextColourId,Colors::textPrimary());
    m_yearSlider->setColour(juce::Slider::textBoxBackgroundColourId,Colors::bgLight());
    m_yearSlider->setColour(juce::Slider::textBoxOutlineColourId,Colors::border());
    addAndMakeVisible(*m_yearSlider);

    m_bpmLbl=makeLbl(BM_TJ("dialog.tagEditor.bpm"));
    m_bpmSlider=std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal,juce::Slider::TextBoxRight);
    m_bpmSlider->setRange(0,300,0.1);m_bpmSlider->setValue(0);
    m_bpmSlider->setColour(juce::Slider::backgroundColourId,Colors::bgLight());
    m_bpmSlider->setColour(juce::Slider::trackColourId,Colors::primary());
    m_bpmSlider->setColour(juce::Slider::thumbColourId,Colors::primary());
    m_bpmSlider->setColour(juce::Slider::textBoxTextColourId,Colors::textPrimary());
    m_bpmSlider->setColour(juce::Slider::textBoxBackgroundColourId,Colors::bgLight());
    m_bpmSlider->setColour(juce::Slider::textBoxOutlineColourId,Colors::border());
    addAndMakeVisible(*m_bpmSlider);

    m_keyLbl=makeLbl(BM_TJ("dialog.tagEditor.key"));
    m_keyCombo=makeCombo();
    for(int i=1;i<=12;++i){m_keyCombo->addItem(juce::String(i)+"A",i*2-1);m_keyCombo->addItem(juce::String(i)+"B",i*2);}

    m_commentLbl=makeLbl(BM_TJ("dialog.tagEditor.comment"));
    m_commentEdit=std::make_unique<juce::TextEditor>();
    m_commentEdit->setMultiLine(true,true);
    m_commentEdit->setColour(juce::TextEditor::backgroundColourId,Colors::bgLight());
    m_commentEdit->setColour(juce::TextEditor::textColourId,Colors::textPrimary());
    m_commentEdit->setColour(juce::TextEditor::outlineColourId,Colors::border());
    addAndMakeVisible(*m_commentEdit);

    m_albumArtLabel=std::make_unique<juce::Label>("art",BM_TJ("dialog.tagEditor.noArt"));
    m_albumArtLabel->setFont(juce::Font(12.0f));
    m_albumArtLabel->setColour(juce::Label::textColourId,Colors::textDim());
    m_albumArtLabel->setColour(juce::Label::backgroundColourId,Colors::bgLight());
    m_albumArtLabel->setColour(juce::Label::outlineColourId,Colors::border());
    m_albumArtLabel->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*m_albumArtLabel);

    m_changeArtBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.tagEditor.changeArt"));
    m_changeArtBtn->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());
    m_changeArtBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primary());
    m_changeArtBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_changeArtBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_changeArtBtn->onClick=[this]{
        juce::FileChooser chooser(BM_TJ("norm.chooseImg"),juce::File(),"*.jpg;*.jpeg;*.png;*.bmp");
        if(chooser.browseForFileToOpen()){
            auto file=chooser.getResult();
            auto img=juce::ImageFileFormat::loadFrom(file);
            if(img.isValid()){
                m_albumArtLabel->setText("",juce::dontSendNotification);
            }
        }
    };
    addAndMakeVisible(*m_changeArtBtn);

    m_cancelBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.tagEditor.cancel"));
    m_cancelBtn->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());
    m_cancelBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primary());
    m_cancelBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_cancelBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_cancelBtn->onClick=[this]{if(auto*dw=findParentComponentOfClass<juce::DialogWindow>())dw->exitModalState(0);};
    addAndMakeVisible(*m_cancelBtn);

    m_saveBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.tagEditor.save"));
    m_saveBtn->setColour(juce::TextButton::buttonColourId,Colors::primary());
    m_saveBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primaryHover());
    m_saveBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_saveBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_saveBtn->onClick=[this]{
        if(onTagsChanged)onTagsChanged();
        if(auto*dw=findParentComponentOfClass<juce::DialogWindow>())dw->exitModalState(1);
    };
    addAndMakeVisible(*m_saveBtn);
}
void TagEditorDialog::setTrackInfo(const juce::String& titleStr,const juce::String& artistStr,
                                    const juce::String& albumStr,const juce::String& genreStr,
                                    int yearVal,double bpmVal,const juce::String& keyStr,
                                    const juce::String& commentStr){
    m_titleEdit->setText(titleStr);
    m_artistEdit->setText(artistStr);
    m_albumEdit->setText(albumStr);
    int idx=m_genreCombo->indexOfItemId(1);
    for(int i=0;i<m_genreCombo->getNumItems();++i){
        if(m_genreCombo->getItemText(i)==genreStr){m_genreCombo->setSelectedItemIndex(i);break;}
    }
    m_yearSlider->setValue(yearVal,juce::dontSendNotification);
    m_bpmSlider->setValue(bpmVal,juce::dontSendNotification);
    for(int i=0;i<m_keyCombo->getNumItems();++i){
        if(m_keyCombo->getItemText(i)==keyStr){m_keyCombo->setSelectedItemIndex(i);break;}
    }
    m_commentEdit->setText(commentStr);
}
juce::String TagEditorDialog::title() const{return m_titleEdit->getText();}
juce::String TagEditorDialog::artist() const{return m_artistEdit->getText();}
juce::String TagEditorDialog::album() const{return m_albumEdit->getText();}
juce::String TagEditorDialog::genre() const{return m_genreCombo->getText();}
int TagEditorDialog::year() const{return(int)m_yearSlider->getValue();}
double TagEditorDialog::bpm() const{return m_bpmSlider->getValue();}
juce::String TagEditorDialog::key() const{return m_keyCombo->getText();}
juce::String TagEditorDialog::comment() const{return m_commentEdit->getText();}
int TagEditorDialog::showDialog(juce::Component*){
    auto*content=new TagEditorDialog();
    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(content);opts.dialogTitle=BM_TJ("dialog.tagEditor.windowTitle");
    opts.dialogBackgroundColour=Colors::bgDarker();opts.resizable=false;opts.useNativeTitleBar=true;
    return opts.runModal();
}
void TagEditorDialog::paint(juce::Graphics& g){
    juce::ColourGradient bgGrad(juce::Colour(0xFF0D0D14),0.0f,0.0f,
        juce::Colour(0xFF08080E),0.0f,static_cast<float>(getHeight()),false);
    g.setGradientFill(bgGrad);
    g.fillRect(getLocalBounds());

    g.setColour(Colors::primary().withAlpha(0.03f));
    g.fillEllipse(-50.0f,-50.0f,200.0f,200.0f);

    g.setColour(Colors::secondary().withAlpha(0.02f));
    g.fillEllipse(static_cast<float>(getWidth())-60.0f,
                  static_cast<float>(getHeight())-60.0f,120.0f,120.0f);

    g.setColour(Colors::primary().withAlpha(0.6f));
    g.fillRect(24.0f,30.0f,3.0f,16.0f);

    float panelH=(float)getHeight()-120.0f;
    auto infoPanel=juce::Rectangle<float>(24.0f,54.0f,320.0f,panelH);

    juce::ColourGradient infoPanelGrad(Colors::glassWhite(),24.0f,54.0f,
        juce::Colours::transparentBlack,24.0f,54.0f+panelH,false);
    g.setGradientFill(infoPanelGrad);
    g.fillRoundedRectangle(infoPanel,8.0f);
    g.setColour(Colors::bgMedium().withAlpha(0.6f));
    g.fillRoundedRectangle(infoPanel,8.0f);
    g.setColour(Colors::glassBorder());
    g.drawRoundedRectangle(infoPanel,8.0f,0.5f);
    g.setColour(Colors::primary().withAlpha(0.05f));
    g.drawRoundedRectangle(infoPanel.expanded(1.0f),9.0f,1.0f);

    g.setColour(Colors::primary().withAlpha(0.5f));
    g.fillRect(34.0f,48.0f,3.0f,12.0f);
    g.setColour(Colors::textPrimary());g.setFont(juce::Font(13.0f,juce::Font::bold));
    g.drawText(BM_TJ("dialog.tagEditor.informations"),42,46,150,16,juce::Justification::centredLeft);

    float artX=354.0f;
    float artW=(float)getWidth()-artX-24.0f;
    auto artPanel=juce::Rectangle<float>(artX,54.0f,artW,panelH);

    juce::ColourGradient artPanelGrad(Colors::glassWhite(),artX,54.0f,
        juce::Colours::transparentBlack,artX,54.0f+panelH,false);
    g.setGradientFill(artPanelGrad);
    g.fillRoundedRectangle(artPanel,8.0f);
    g.setColour(Colors::bgMedium().withAlpha(0.6f));
    g.fillRoundedRectangle(artPanel,8.0f);
    g.setColour(Colors::glassBorder());
    g.drawRoundedRectangle(artPanel,8.0f,0.5f);
    g.setColour(Colors::secondary().withAlpha(0.05f));
    g.drawRoundedRectangle(artPanel.expanded(1.0f),9.0f,1.0f);

    g.setColour(Colors::secondary().withAlpha(0.5f));
    g.fillRect(artX+10.0f,48.0f,3.0f,12.0f);
    g.setColour(Colors::textPrimary());g.setFont(juce::Font(13.0f,juce::Font::bold));
    g.drawText(BM_TJ("dialog.tagEditor.pochette"),artX+18.0f,46,100,16,juce::Justification::centredLeft);

    float footerY=(float)getHeight()-58.0f;
    g.setColour(Colors::primary().withAlpha(0.08f));
    g.fillRect(0.0f,footerY,static_cast<float>(getWidth()),1.0f);

    g.setColour(Colors::glassBorder());
    g.drawRect(getLocalBounds().toFloat(),0.5f);
}
void TagEditorDialog::resized(){
    auto area=getLocalBounds().reduced(24);
    m_headerLabel->setBounds(area.removeFromTop(26));area.removeFromTop(8);
    auto btnRow=area.removeFromBottom(30);
    m_saveBtn->setBounds(btnRow.removeFromRight(110));btnRow.removeFromRight(8);
    m_cancelBtn->setBounds(btnRow.removeFromRight(80));
    area.removeFromBottom(8);
    auto formArea=area.removeFromLeft(320).reduced(12);
    int rowH=26,gap=4;
    auto layoutRow=[&](juce::Label* lbl,juce::Component* ed){
        auto row=formArea.removeFromTop(rowH);
        lbl->setBounds(row.removeFromLeft(90));ed->setBounds(row);
        formArea.removeFromTop(gap);
    };
    layoutRow(m_titleLbl.get(),m_titleEdit.get());
    layoutRow(m_artistLbl.get(),m_artistEdit.get());
    layoutRow(m_albumLbl.get(),m_albumEdit.get());
    layoutRow(m_genreLbl.get(),m_genreCombo.get());
    layoutRow(m_yearLbl.get(),m_yearSlider.get());
    layoutRow(m_bpmLbl.get(),m_bpmSlider.get());
    layoutRow(m_keyLbl.get(),m_keyCombo.get());
    m_commentLbl->setBounds(formArea.removeFromTop(rowH).removeFromLeft(90));
    m_commentEdit->setBounds(formArea);
    area.removeFromLeft(10);
    auto artArea=area.reduced(12);
    artArea.removeFromTop(8);
    m_albumArtLabel->setBounds(artArea.removeFromTop(juce::jmin(180,artArea.getHeight()-36)).withSizeKeepingCentre(juce::jmin(180,artArea.getWidth()),180));
    artArea.removeFromTop(8);
    m_changeArtBtn->setBounds(artArea.removeFromTop(28).withSizeKeepingCentre(juce::jmin(120,artArea.getWidth()),28));
}
} // namespace BeatMate::UI
