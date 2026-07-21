#include "ExportReportDialog.h"
#include "../../styles/ColorPalette.h"
#include "../../../services/config/I18n.h"

namespace BeatMate::UI {

namespace {
constexpr int kHeaderH = 96;
constexpr int kFooterH = 46;

juce::String statusLabel(Services::Export::TrackExportOutcome::Status s)
{
    using S = Services::Export::TrackExportOutcome::Status;
    switch (s)
    {
        case S::Copied:    return BM_TJ("export.report.statusCopied");
        case S::Converted: return BM_TJ("export.report.statusConverted");
        case S::Cancelled: return BM_TJ("export.report.statusCancelled");
        default:           return BM_TJ("export.report.statusFailed");
    }
}

juce::Colour statusColour(Services::Export::TrackExportOutcome::Status s)
{
    using S = Services::Export::TrackExportOutcome::Status;
    switch (s)
    {
        case S::Copied:
        case S::Converted: return Colors::success();
        case S::Cancelled: return Colors::warning();
        default:           return Colors::error();
    }
}
}

ExportReportDialog::ExportReportDialog(Services::Export::BatchExportReport report)
    : m_report(std::move(report))
{
    m_model = std::make_unique<RowModel>(*this);
    m_listBox = std::make_unique<juce::ListBox>("report", m_model.get());
    m_listBox->setRowHeight(40);
    m_listBox->setColour(juce::ListBox::backgroundColourId, Colors::bgDarker());
    m_listBox->setColour(juce::ListBox::outlineColourId, Colors::border());
    m_listBox->setOutlineThickness(1);
    addAndMakeVisible(*m_listBox);

    m_openFolderBtn = std::make_unique<juce::TextButton>(BM_TJ("export.report.openFolder"));
    m_openFolderBtn->setColour(juce::TextButton::buttonColourId, Colors::primary().withAlpha(0.4f));
    m_openFolderBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    m_openFolderBtn->onClick = [this] {
        juce::File dir(juce::String::fromUTF8(m_report.settings.destinationDir.c_str()));
        if (dir.isDirectory())
            dir.revealToUser();
    };
    addAndMakeVisible(*m_openFolderBtn);

    m_closeBtn = std::make_unique<juce::TextButton>(BM_TJ("export.report.close"));
    m_closeBtn->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    m_closeBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    m_closeBtn->onClick = [this] {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };
    addAndMakeVisible(*m_closeBtn);

    setSize(720, 520);
}

void ExportReportDialog::show(const Services::Export::BatchExportReport& report)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(new ExportReportDialog(report));
    options.dialogTitle = BM_TJ("export.report.title");
    options.dialogBackgroundColour = Colors::bg();
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

void ExportReportDialog::paint(juce::Graphics& g)
{
    g.fillAll(Colors::bg());

    g.setColour(Colors::primary());
    g.fillRoundedRectangle(14.0f, 14.0f, 4.0f, 18.0f, 2.0f);
    g.setFont(juce::Font("Segoe UI", 16.0f, juce::Font::bold));
    g.setColour(Colors::textPrimary());
    g.drawText(BM_TJ("export.report.title"), 26, 10, getWidth() - 40, 26,
               juce::Justification::centredLeft);

    float bx = 26.0f;
    const float by = 42.0f;
    auto badge = [&](const juce::String& label, int count, juce::Colour col) {
        const juce::String txt = juce::String(count) + " " + label;
        const float bw = juce::GlyphArrangement::getStringWidth(
                             juce::Font("Segoe UI", 11.0f, juce::Font::bold), txt) + 26.0f;
        ProDraw::badge(g, txt, bx, by, bw, 22.0f, col);
        bx += bw + 8.0f;
    };
    badge(BM_TJ("export.report.ok"), m_report.succeeded, Colors::success());
    badge(BM_TJ("export.report.failedShort"), m_report.failed, Colors::error());
    if (m_report.cancelledCount > 0)
        badge(BM_TJ("export.report.cancelledShort"), m_report.cancelledCount, Colors::warning());
    badge("Mo", static_cast<int>(m_report.totalBytes / (1024 * 1024)), Colors::primary());

    g.setFont(juce::Font(10.5f));
    g.setColour(Colors::textSecondary());
    juce::String djLine;
    if (!m_report.djLibraryFiles.empty())
    {
        djLine = BM_TJ("export.report.djFilesHeader") + " ";
        for (size_t i = 0; i < m_report.djLibraryFiles.size(); ++i)
        {
            if (i > 0) djLine += ", ";
            djLine += juce::File(juce::String::fromUTF8(m_report.djLibraryFiles[i].c_str())).getFileName();
        }
    }
    if (!m_report.m3uPath.empty())
    {
        if (djLine.isNotEmpty()) djLine += ", ";
        else djLine = BM_TJ("export.report.djFilesHeader") + " ";
        djLine += juce::File(juce::String::fromUTF8(m_report.m3uPath.c_str())).getFileName();
    }
    g.drawText(djLine, 26, 68, getWidth() - 52, 16, juce::Justification::centredLeft, true);
}

void ExportReportDialog::resized()
{
    m_listBox->setBounds(14, kHeaderH, getWidth() - 28, getHeight() - kHeaderH - kFooterH);
    m_openFolderBtn->setBounds(14, getHeight() - kFooterH + 8, 190, 28);
    m_closeBtn->setBounds(getWidth() - 118, getHeight() - kFooterH + 8, 104, 28);
}

int ExportReportDialog::RowModel::getNumRows()
{
    return static_cast<int>(m_owner.m_report.outcomes.size());
}

void ExportReportDialog::RowModel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool)
{
    if (row < 0 || row >= static_cast<int>(m_owner.m_report.outcomes.size()))
        return;
    const auto& o = m_owner.m_report.outcomes[static_cast<size_t>(row)];

    if (row % 2 == 1)
    {
        g.setColour(juce::Colour(0x06FFFFFF));
        g.fillRect(0, 0, w, h);
    }

    const juce::String fileName = o.outputPath.empty()
        ? juce::File(juce::String::fromUTF8(o.sourcePath.c_str())).getFileName()
        : juce::File(juce::String::fromUTF8(o.outputPath.c_str())).getFileName();

    g.setColour(Colors::textPrimary());
    g.setFont(juce::Font(11.5f, juce::Font::bold));
    g.drawText(fileName, 8, 3, w - 200, 16, juce::Justification::centredLeft, true);

    juce::String detail;
    if (o.normalized)
    {
        detail << juce::String(o.measuredLufs, 1) << " LUFS "
               << juce::String::fromUTF8("\xe2\x86\x92") << " "
               << (o.appliedGainDb >= 0 ? "+" : "")
               << juce::String(o.appliedGainDb, 1) << " dB";
        if (o.gainLimitedByPeak)
            detail += " " + BM_TJ("export.report.gainLimited");
    }
    else if (o.normalizationSkipped)
    {
        detail += BM_TJ("export.report.normSkipped");
    }
    if (o.tagsWritten)
        detail += juce::String(detail.isEmpty() ? "" : "  |  ") + BM_TJ("export.report.tagsOk");
    if (o.seratoWritten)
        detail += juce::String(detail.isEmpty() ? "" : "  |  ") + "Serato";
    if (o.message.length() > 0)
        detail += juce::String(detail.isEmpty() ? "" : "  |  ") + juce::String::fromUTF8(o.message.c_str());

    g.setColour(Colors::textSecondary());
    g.setFont(juce::Font(10.0f));
    g.drawText(detail, 8, 21, w - 200, 14, juce::Justification::centredLeft, true);

    ProDraw::badge(g, statusLabel(o.status), static_cast<float>(w - 130), h / 2.0f - 10.0f,
                   118.0f, 20.0f, statusColour(o.status));

    g.setColour(Colors::border().withAlpha(0.2f));
    g.drawHorizontalLine(h - 1, 4.0f, static_cast<float>(w - 4));
}

} // namespace BeatMate::UI
