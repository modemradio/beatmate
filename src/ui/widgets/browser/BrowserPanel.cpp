#include "BrowserPanel.h"
#include "../../styles/ColorPalette.h"
#include "../../../services/config/I18n.h"

namespace BeatMate::UI {

void BrowserPanel::RootItem::paintItem(juce::Graphics& g, int w, int h)
{
    auto bounds = juce::Rectangle<float>(0, 0, (float)w, (float)h);

    if (isCategory)
    {
        juce::ColourGradient catGrad(
            Colors::bgElevated().brighter(0.06f), 0, 0,
            Colors::bgCard(), 0, (float)h, false);
        g.setGradientFill(catGrad);
        g.fillRect(bounds);

        g.setColour(Colors::border().withAlpha(0.5f));
        g.drawHorizontalLine(h - 1, 0.0f, (float)w);

        float iconX = 4.0f;
        float iconY = (h - 10.0f) * 0.5f;
        g.setColour(Colors::primary().withAlpha(0.7f));
        juce::Path folder;
        folder.addRoundedRectangle(iconX, iconY + 2.0f, 10.0f, 7.0f, 1.5f);
        folder.addRoundedRectangle(iconX, iconY, 5.0f, 4.0f, 1.0f, 1.0f, true, true, false, false);
        g.fillPath(folder);

        g.setColour(Colors::textPrimary());
        g.setFont(juce::Font(11.5f, juce::Font::bold));
        g.drawText(text, 18, 0, w - 22, h, juce::Justification::centredLeft);
    }
    else
    {

        if (isSelected())
        {
            juce::ColourGradient selGrad(
                Colors::primary().withAlpha(0.35f), 0, 0,
                Colors::primary().withAlpha(0.15f), (float)w, 0, false);
            g.setGradientFill(selGrad);
            g.fillRect(bounds);

            g.setColour(Colors::primary());
            g.fillRect(0.0f, 2.0f, 2.5f, (float)h - 4.0f);
        }


        float dotR = 2.5f;
        float dotCX = 8.0f;
        float dotCY = h * 0.5f;
        juce::Colour dotCol = isSelected() ? Colors::primary() : Colors::textDim();
        g.setColour(dotCol);
        g.fillEllipse(dotCX - dotR, dotCY - dotR, dotR * 2.0f, dotR * 2.0f);

        g.setColour(isSelected() ? Colors::textPrimary() : Colors::textSecondary());
        g.setFont(juce::Font(11.0f));
        g.drawText(text, 16, 0, w - 20, h, juce::Justification::centredLeft);

        g.setColour(Colors::border().withAlpha(0.15f));
        g.drawHorizontalLine(h - 1, 8.0f, (float)w - 4.0f);
    }
}

void BrowserPanel::RootItem::itemClicked(const juce::MouseEvent&)
{
    if (getNumSubItems() == 0 && owner && category.isNotEmpty())
    {
        auto cat = category;
        auto val = text;
        owner->m_listeners.call([cat, val](Listener& l) { l.filterChanged(cat, val); });
    }
}


BrowserPanel::FileSystemItem::FileSystemItem(const juce::File& file, BrowserPanel* owner, const juce::String& displayName)
    : m_file(file), m_owner(owner)
{
    if (displayName.isNotEmpty()) {
        m_displayName = displayName;
    } else {
        auto name = file.getFileName();
        if (name.isEmpty()) name = file.getFullPathName();
        if (name.isEmpty()) name = "(racine)";
        m_displayName = name;
    }
}

bool BrowserPanel::FileSystemItem::mightContainSubItems()
{
    return m_file.isDirectory();
}

juce::String BrowserPanel::FileSystemItem::getUniqueName() const
{
    return m_file.getFullPathName();
}

void BrowserPanel::FileSystemItem::paintItem(juce::Graphics& g, int w, int h)
{
    auto bounds = juce::Rectangle<float>(0, 0, (float)w, (float)h);

    if (isSelected())
    {
        juce::ColourGradient selGrad(
            Colors::primary().withAlpha(0.35f), 0, 0,
            Colors::primary().withAlpha(0.15f), (float)w, 0, false);
        g.setGradientFill(selGrad);
        g.fillRect(bounds);

        g.setColour(Colors::primary());
        g.fillRect(0.0f, 2.0f, 2.5f, (float)h - 4.0f);
    }

    float iconX = 4.0f;
    float iconY = (h - 9.0f) * 0.5f;
    g.setColour(isSelected() ? Colors::primary() : Colors::textDim().brighter(0.2f));
    juce::Path folder;
    folder.addRoundedRectangle(iconX, iconY + 2.0f, 9.0f, 6.0f, 1.2f);
    folder.addRoundedRectangle(iconX, iconY, 4.5f, 3.5f, 0.8f, 0.8f, true, true, false, false);
    g.fillPath(folder);

    g.setColour(isSelected() ? Colors::textPrimary() : Colors::textSecondary());
    g.setFont(juce::Font(11.0f));
    g.drawText(m_displayName, 16, 0, w - 20, h, juce::Justification::centredLeft);

    g.setColour(Colors::border().withAlpha(0.15f));
    g.drawHorizontalLine(h - 1, 8.0f, (float)w - 4.0f);
}

void BrowserPanel::FileSystemItem::itemOpennessChanged(bool isNowOpen)
{
    if (!isNowOpen || m_hasScanned)
        return;
    m_hasScanned = true;

    if (!m_file.isDirectory())
        return;

    auto dirs = m_file.findChildFiles(juce::File::findDirectories, false);
    dirs.sort();

    for (auto& dir : dirs)
    {
        if (dir.getFileName().startsWithChar('.'))
            continue;
        if (dir.getFileName().equalsIgnoreCase("$Recycle.Bin") ||
            dir.getFileName().equalsIgnoreCase("System Volume Information") ||
            dir.getFileName().equalsIgnoreCase("$WinREAgent"))
            continue;

        addSubItem(new FileSystemItem(dir, m_owner));
    }
}

void BrowserPanel::FileSystemItem::itemClicked(const juce::MouseEvent&)
{
    if (!m_file.isDirectory() || m_owner == nullptr)
        return;

    juce::String path = m_file.getFullPathName();
    m_owner->m_listeners.call([path](Listener& l) { l.filterChanged("Dossier", path); });
}

BrowserPanel::BrowserPanel()
{
    setupUI();
    populateTree();
}

void BrowserPanel::setupUI()
{
    m_titleLabel = std::make_unique<juce::Label>("t", BM_TJ("widget.BrowserPanel.title"));
    m_titleLabel->setFont(juce::Font(13.0f, juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId, Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    m_tree = std::make_unique<juce::TreeView>();
    m_tree->setColour(juce::TreeView::backgroundColourId, juce::Colours::transparentBlack);
    m_tree->setIndentSize(16);
    addAndMakeVisible(*m_tree);
}

void BrowserPanel::populateTree()
{
    m_rootItem = std::make_unique<RootItem>();
    m_rootItem->text = "Root";
    m_rootItem->owner = this;
    m_rootItem->setOpen(true);

    auto addCategory = [this](const juce::String& name, const juce::StringArray& items) {
        auto* cat = new RootItem();
        cat->text = name;
        cat->category = name;
        cat->owner = this;
        cat->isCategory = true;
        for (auto& item : items)
        {
            auto* child = new RootItem();
            child->text = item;
            child->category = name;
            child->owner = this;
            child->isCategory = false;
            cat->addSubItem(child);
        }
        m_rootItem->addSubItem(cat);
        cat->setOpen(true);
    };

    {
        auto* explorerCat = new RootItem();
        explorerCat->text = BM_TJ("widget.BrowserPanel.cat.explorer");
        explorerCat->category = "Explorateur";
        explorerCat->owner = this;
        explorerCat->isCategory = true;

        auto musicDir = juce::File::getSpecialLocation(juce::File::userMusicDirectory);
        if (musicDir.isDirectory())
            explorerCat->addSubItem(new FileSystemItem(musicDir, this, BM_TJ("widget.BrowserPanel.loc.music")));

        auto docsDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
        if (docsDir.isDirectory())
            explorerCat->addSubItem(new FileSystemItem(docsDir, this, BM_TJ("widget.BrowserPanel.loc.documents")));

        auto desktopDir = juce::File::getSpecialLocation(juce::File::userDesktopDirectory);
        if (desktopDir.isDirectory())
            explorerCat->addSubItem(new FileSystemItem(desktopDir, this, BM_TJ("widget.BrowserPanel.loc.desktop")));

        juce::Array<juce::File> roots;
        juce::File::findFileSystemRoots(roots);
        for (auto& root : roots)
            explorerCat->addSubItem(new FileSystemItem(root, this, root.getFullPathName()));

        m_rootItem->addSubItem(explorerCat);
        explorerCat->setOpen(true);
    }

    addCategory("Genres", {
        "House", "Tech House", "Deep House", "Afro House", "Melodic Techno",
        "Techno", "Trance", "Progressive", "D&B", "Dubstep", "Hip-Hop", "R&B",
        "Pop", "Rock", "Indie Rock", "Alternative", "Metal", "Punk",
        "Jazz", "Soul", "Funk", "Disco", "Reggae", "Dancehall", "Reggaeton",
        "Latin", "Afrobeat", "Amapiano", "EDM", "Electro", "Hardstyle",
        "UK Garage", "Bass House", "Minimal", "Ambient", "Lo-Fi",
        "Classical", "Country", "Folk", "Blues"
    });
    addCategory(BM_TJ("widget.BrowserPanel.cat.artists"), {});
    addCategory("BPM", {"< 100", "100-120", "120-130", "130-140", "> 140"});
    addCategory("Playlists", {"Warm Up", "Peak Time", "Closing"});

    addCategory(BM_TJ("widget.BrowserPanel.cat.djSources"), {"Rekordbox", "VirtualDJ", "Serato", "Traktor", "Engine DJ"});

    m_tree->setRootItem(m_rootItem.get());
    m_tree->setRootItemVisible(false);
}

void BrowserPanel::paint(juce::Graphics& g)
{
    g.fillAll(Colors::bgDarkest());

    juce::ColourGradient titleGrad(
        Colors::bgCard(), 0, 0,
        Colors::bgDarkest(), 0, 32.0f, false);
    g.setGradientFill(titleGrad);
    g.fillRect(0, 0, getWidth(), 32);

    g.setColour(Colors::border());
    g.drawHorizontalLine(31, 0.0f, (float)getWidth());

    g.setColour(Colors::border());
    g.drawVerticalLine(getWidth() - 1, 0.0f, (float)getHeight());
}

void BrowserPanel::resized()
{
    m_titleLabel->setBounds(8, 8, 180, 20);
    m_tree->setBounds(0, 32, getWidth(), getHeight() - 32);
}

}
