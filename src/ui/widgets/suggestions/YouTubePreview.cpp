#include "YouTubePreview.h"

#include "LivePreviewPlayer.h"
#include "../../../services/streaming/ChartsService.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>

namespace BeatMate::UI::Widgets {
namespace {

std::mutex ytCacheMutex;
std::map<std::string, std::string> ytVideoIdCache;
std::atomic<uint64_t> ytSearchGeneration { 0 };

bool isValidVideoId(const juce::String& id)
{
    if (id.length() != 11) return false;
    for (int i = 0; i < id.length(); ++i) {
        const auto c = id[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                     || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) return false;
    }
    return true;
}

juce::String extractVideoId(const juce::String& raw)
{
    auto s = raw.trim();
    if (s.isEmpty()) return {};
    auto cut = [](juce::String v) {
        for (auto stop : { "?", "&", "/", "#" }) {
            const auto i = v.indexOf(stop);
            if (i >= 0) v = v.substring(0, i);
        }
        return v;
    };
    if (s.containsIgnoreCase("youtu.be/"))
        s = cut(s.fromFirstOccurrenceOf("youtu.be/", false, true));
    else if (s.containsIgnoreCase("/shorts/"))
        s = cut(s.fromFirstOccurrenceOf("/shorts/", false, true));
    else if (s.containsIgnoreCase("/embed/"))
        s = cut(s.fromFirstOccurrenceOf("/embed/", false, true));
    else if (s.contains("v="))
        s = cut(s.fromFirstOccurrenceOf("v=", false, false));
    return isValidVideoId(s) ? s : juce::String();
}

juce::String searchVideoIdBlocking(const juce::String& artist, const juce::String& title)
{
    const auto key = Services::Streaming::ChartsService::matchKey(
        title.toStdString(), artist.toStdString());
    {
        std::lock_guard<std::mutex> g(ytCacheMutex);
        auto it = ytVideoIdCache.find(key);
        if (it != ytVideoIdCache.end()) return juce::String(it->second);
    }

    const auto term = (artist + " " + title).trim();
    const auto encoded = juce::URL::addEscapeChars(term, false);
    juce::URL u("https://www.youtube.com/results?search_query=" + encoded);
    int statusCode = 0;
    auto stream = u.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(8000)
            .withExtraHeaders("User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                              "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36\r\n"
                              "Accept-Language: fr-FR,fr;q=0.9,en;q=0.8\r\n"
                              "Cookie: CONSENT=YES+1; SOCS=CAI\r\n")
            .withStatusCode(&statusCode));
    if (!stream) {
        spdlog::warn("[YouTubePreview] cannot open results stream for '{}'", term.toStdString());
        return {};
    }
    const auto html = stream->readEntireStreamAsString();
    if (html.isEmpty() || (statusCode != 0 && (statusCode < 200 || statusCode >= 300))) {
        spdlog::warn("[YouTubePreview] results page status={} bodyLen={}", statusCode, html.length());
        return {};
    }

    juce::String found;
    auto idx = html.indexOf("\"videoId\":\"");
    while (idx >= 0) {
        const auto start = idx + 11;
        const auto end = html.indexOf(start, "\"");
        if (end < 0) break;
        const auto candidate = html.substring(start, end);
        if (isValidVideoId(candidate)) { found = candidate; break; }
        idx = html.indexOf(start, "\"videoId\":\"");
    }

    if (found.isNotEmpty()) {
        std::lock_guard<std::mutex> g(ytCacheMutex);
        ytVideoIdCache[key] = found.toStdString();
    }
    return found;
}

class YouTubeContent : public juce::Component {
public:
    YouTubeContent()
    {
        trackLabel.setFont(juce::Font(15.0f, juce::Font::bold));
        trackLabel.setJustificationType(juce::Justification::centredLeft);
        trackLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(trackLabel);

        closeBtn.setButtonText("Fermer");
        closeBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF8B1E1E));
        closeBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        closeBtn.onClick = [] { YouTubePreview::close(); };
        addAndMakeVisible(closeBtn);

        linkEdit.setTextToShowWhenEmpty(
            juce::String::fromUTF8("Coller un lien YouTube (watch?v=, youtu.be, shorts)..."),
            juce::Colours::grey);
        linkEdit.onReturnKey = [this] { playFromLinkField(); };
        addAndMakeVisible(linkEdit);

        playLinkBtn.setButtonText("Lire le lien");
        playLinkBtn.onClick = [this] { playFromLinkField(); };
        addAndMakeVisible(playLinkBtn);

        statusLabel.setFont(juce::Font(13.0f));
        statusLabel.setJustificationType(juce::Justification::centred);
        statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFB9C2CF));
        addAndMakeVisible(statusLabel);

       #if JUCE_WINDOWS
        _putenv_s("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",
                  "--autoplay-policy=no-user-gesture-required");
       #endif
        auto dataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                           .getChildFile("BeatMate").getChildFile("WebView2");
        dataDir.createDirectory();
        const auto options = juce::WebBrowserComponent::Options()
            .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
            .withKeepPageLoadedWhenBrowserIsHidden()
            .withWinWebView2Options(juce::WebBrowserComponent::Options::WinWebView2()
                                        .withUserDataFolder(dataDir)
                                        .withStatusBarDisabled());
        if (juce::WebBrowserComponent::areOptionsSupported(options))
            browser = std::make_unique<juce::WebBrowserComponent>(options);
        if (browser)
            addAndMakeVisible(*browser);
        else
            spdlog::warn("[YouTubePreview] WebView2 indisponible, fallback navigateur");

        previewToken = LivePreviewPlayer::getInstance().addListener([this] {
            if (LivePreviewPlayer::getInstance().currentState() != LivePreviewPlayer::State::Idle)
                silence();
        });
    }

    void setTrackText(const juce::String& artist, const juce::String& title)
    {
        trackLabel.setText(artist.isEmpty() ? title
                                            : artist + juce::String::fromUTF8(" \xe2\x80\x94 ") + title,
                           juce::dontSendNotification);
    }

    void showStatus(const juce::String& text)
    {
        statusLabel.setText(text, juce::dontSendNotification);
    }

    void loadVideo(const juce::String& videoId)
    {
        LivePreviewPlayer::getInstance().stop();
        if (browser) {
            browser->goToURL("https://www.youtube.com/embed/" + videoId + "?autoplay=1");
            showStatus({});
        } else {
            juce::URL("https://www.youtube.com/watch?v=" + videoId).launchInDefaultBrowser();
            juce::MessageManager::callAsync([] { YouTubePreview::close(); });
        }
    }

    void silence()
    {
        if (browser)
            browser->goToURL("about:blank");
    }

    void playFromLinkField()
    {
        const auto id = extractVideoId(linkEdit.getText());
        if (id.isEmpty()) {
            showStatus(juce::String::fromUTF8(
                "Lien YouTube invalide. Formats : youtube.com/watch?v=, youtu.be/, shorts."));
            return;
        }
        loadVideo(id);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xFF14161B));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(10);
        auto top = area.removeFromTop(28);
        closeBtn.setBounds(top.removeFromRight(86));
        top.removeFromRight(8);
        trackLabel.setBounds(top);
        area.removeFromTop(6);
        auto linkRow = area.removeFromTop(28);
        playLinkBtn.setBounds(linkRow.removeFromRight(110));
        linkRow.removeFromRight(6);
        linkEdit.setBounds(linkRow);
        area.removeFromTop(8);
        auto statusArea = area.removeFromBottom(24);
        statusLabel.setBounds(statusArea);
        if (browser)
            browser->setBounds(area);
        else
            statusLabel.setBounds(getLocalBounds().reduced(10).withTrimmedTop(80));
    }

private:
    juce::Label trackLabel;
    juce::TextButton closeBtn;
    juce::TextEditor linkEdit;
    juce::TextButton playLinkBtn;
    juce::Label statusLabel;
    std::unique_ptr<juce::WebBrowserComponent> browser;
    LivePreviewPlayer::ListenerToken previewToken;
};

class YouTubeWindow;
YouTubeWindow* ytWindow = nullptr;

class YouTubeWindow : public juce::DocumentWindow, private juce::DeletedAtShutdown {
public:
    YouTubeWindow()
        : juce::DocumentWindow(juce::String::fromUTF8("Pr\xc3\xa9\xc3\xa9" "coute YouTube"),
                               juce::Colour(0xFF14161B), juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        contentPanel = new YouTubeContent();
        setContentOwned(contentPanel, false);
        setResizable(true, true);
        setResizeLimits(420, 320, 1920, 1200);
        centreWithSize(760, 520);
        setVisible(true);
        setAlwaysOnTop(false);
    }

    ~YouTubeWindow() override
    {
        if (ytWindow == this) ytWindow = nullptr;
    }

    void closeButtonPressed() override
    {
        YouTubePreview::close();
    }

    YouTubeContent* contentPanel = nullptr;
};

YouTubeWindow& ensureWindow()
{
    if (ytWindow == nullptr)
        ytWindow = new YouTubeWindow();
    ytWindow->setVisible(true);
    ytWindow->toFront(true);
    return *ytWindow;
}

} // namespace

void YouTubePreview::showForTrack(const juce::String& artist, const juce::String& title)
{
    auto& win = ensureWindow();
    win.contentPanel->setTrackText(artist, title);
    win.contentPanel->showStatus("Recherche YouTube...");
    LivePreviewPlayer::getInstance().stop();

    const auto gen = ytSearchGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    juce::Thread::launch([artist, title, gen] {
        const auto id = searchVideoIdBlocking(artist, title);
        juce::MessageManager::callAsync([artist, title, id, gen] {
            if (gen != ytSearchGeneration.load(std::memory_order_acquire)) return;
            if (ytWindow == nullptr || ytWindow->contentPanel == nullptr) return;
            if (id.isEmpty()) {
                ytWindow->contentPanel->showStatus(
                    juce::String::fromUTF8("Aucun r\xc3\xa9sultat YouTube pour \xc2\xab ")
                    + artist + " - " + title
                    + juce::String::fromUTF8(" \xc2\xbb. Collez un lien ci-dessus."));
                return;
            }
            ytWindow->contentPanel->loadVideo(id);
        });
    });
}

void YouTubePreview::stopPlayback()
{
    if (ytWindow != nullptr && ytWindow->contentPanel != nullptr)
        ytWindow->contentPanel->silence();
}

void YouTubePreview::close()
{
    if (ytWindow == nullptr) return;
    auto* w = ytWindow;
    ytWindow = nullptr;
    w->setVisible(false);
    juce::MessageManager::callAsync([w] { delete w; });
}

} // namespace BeatMate::UI::Widgets
