#include "UniversalImportPanel.h"
#include "../../styles/ColorPalette.h"
#include "../../../services/config/I18n.h"
namespace BeatMate::UI {
void UniversalImportPanel::FileModel::paintListBoxItem(int row,juce::Graphics& g,int w,int h,bool sel){
    if(row<0||row>=items.size())return;if(sel){g.setColour(Colors::primary());g.fillRect(0,0,w,h);}
    g.setColour(sel?Colors::textPrimary():Colors::textSecondary());g.setFont(juce::Font(12.0f));g.drawText(items[row],8,0,w-16,h,juce::Justification::centredLeft);
    g.setColour(Colors::bgLight());g.drawHorizontalLine(h-1,0.0f,(float)w);
}
UniversalImportPanel::UniversalImportPanel(){setupUI();}
void UniversalImportPanel::setupUI(){
    m_dropLabel=std::make_unique<juce::Label>("dz",BM_TJ("widget.UniversalImportPanel.dropzone"));
    m_dropLabel->setJustificationType(juce::Justification::centred);m_dropLabel->setFont(juce::Font(15.0f));m_dropLabel->setColour(juce::Label::textColourId,Colors::textDim());addAndMakeVisible(*m_dropLabel);
    m_browseBtn=std::make_unique<juce::TextButton>(BM_TJ("widget.UniversalImportPanel.browse"));m_browseBtn->setColour(juce::TextButton::buttonColourId,Colors::primary());m_browseBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_browseBtn->onClick=[this]{juce::FileChooser c(BM_TJ("widget.UniversalImportPanel.chooserTitle"),juce::File{},"*.mp3;*.wav;*.flac;*.aac;*.ogg;*.m4a;*.aiff");if(c.browseForMultipleFilesToOpen()){juce::StringArray paths;for(auto&f:c.getResults()){m_fileModel->items.add(f.getFileName());paths.add(f.getFullPathName());}m_fileList->updateContent();m_listeners.call([&paths](Listener&l){l.filesDropped(paths);});}};addAndMakeVisible(*m_browseBtn);
    m_autoAnalyze=std::make_unique<juce::ToggleButton>(BM_TJ("widget.UniversalImportPanel.autoAnalyze"));m_autoAnalyze->setToggleState(true,juce::dontSendNotification);m_autoAnalyze->setColour(juce::ToggleButton::textColourId,Colors::textSecondary());addAndMakeVisible(*m_autoAnalyze);
    m_importBtn=std::make_unique<juce::TextButton>(BM_TJ("widget.UniversalImportPanel.importAll"));m_importBtn->setColour(juce::TextButton::buttonColourId,Colors::success());m_importBtn->setColour(juce::TextButton::textColourOffId,juce::Colours::black);
    m_importBtn->onClick=[this]{juce::StringArray f;for(auto&s:m_fileModel->items)f.add(s);m_listeners.call([&f,this](Listener&l){l.importRequested(f,m_autoAnalyze->getToggleState());});};addAndMakeVisible(*m_importBtn);
    m_fileModel=std::make_unique<FileModel>();m_fileList=std::make_unique<juce::ListBox>("fl",m_fileModel.get());m_fileList->setRowHeight(26);m_fileList->setColour(juce::ListBox::backgroundColourId,Colors::bgDark());m_fileList->setColour(juce::ListBox::outlineColourId,Colors::border());addAndMakeVisible(*m_fileList);
}
void UniversalImportPanel::filesDropped(const juce::StringArray& files,int,int){
    juce::StringArray paths;for(auto&path:files){juce::File f(path);if(f.isDirectory()||f.existsAsFile()){m_fileModel->items.add(f.getFileName());paths.add(path);}}
    m_fileList->updateContent();if(!paths.isEmpty())m_listeners.call([&paths](Listener&l){l.filesDropped(paths);});
}
void UniversalImportPanel::paint(juce::Graphics& g){
    ProDraw::viewBackground(g, getWidth(), getHeight());
    g.setColour(Colors::bgDark());g.fillRoundedRectangle(24.0f,24.0f,(float)getWidth()-48.0f,110.0f,12.0f);
    g.setColour(Colors::bgLighter());g.drawRoundedRectangle(24.0f,24.0f,(float)getWidth()-48.0f,110.0f,12.0f,2.0f);
    int pbY=getHeight()-24;g.setColour(Colors::bgLight());g.fillRoundedRectangle(24.0f,(float)pbY,getWidth()-48.0f,16.0f,4.0f);
    if(m_progress>0){g.setColour(Colors::primary());g.fillRoundedRectangle(24.0f,(float)pbY,(getWidth()-48.0f)*(float)m_progress,16.0f,4.0f);}
}
void UniversalImportPanel::resized(){
    m_dropLabel->setBounds(24,40,getWidth()-48,80);
    m_browseBtn->setBounds(24,144,120,28);m_autoAnalyze->setBounds(152,144,200,28);m_importBtn->setBounds(getWidth()-140,144,120,28);
    m_fileList->setBounds(24,180,getWidth()-48,getHeight()-220);
}
} // namespace BeatMate::UI
