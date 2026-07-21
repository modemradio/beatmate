#include "HelpView.h"
#include "../styles/ColorPalette.h"
#include "../../services/config/I18n.h"
#include "../../services/update/UpdateService.h"
#include <spdlog/spdlog.h>

namespace BeatMate::UI {


HelpView::FAQPanel::FAQPanel(const juce::String& question, const juce::String& answer)
    : m_question(question), m_answer(answer)
{
    m_questionLabel.setText(question, juce::dontSendNotification);
    m_questionLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    m_questionLabel.setColour(juce::Label::textColourId, Colors::textPrimary());
    m_questionLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(m_questionLabel);

    m_answerLabel.setText(answer, juce::dontSendNotification);
    m_answerLabel.setFont(juce::Font(13.0f));
    m_answerLabel.setColour(juce::Label::textColourId, Colors::textSecondary());
    m_answerLabel.setJustificationType(juce::Justification::topLeft);
    m_answerLabel.setInterceptsMouseClicks(false, false);
    m_answerLabel.setMinimumHorizontalScale(1.0f);
    addChildComponent(m_answerLabel);
}

void HelpView::FAQPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour(Colors::bgCard());
    g.fillRoundedRectangle(bounds.reduced(2.0f), 6.0f);

    g.setColour(m_expanded ? Colors::primary().withAlpha(0.4f) : Colors::border());
    g.drawRoundedRectangle(bounds.reduced(2.0f), 6.0f, 1.0f);

    auto arrowArea = bounds.removeFromLeft(36.0f).reduced(10.0f, 12.0f);
    g.setColour(Colors::primary());
    juce::Path arrow;
    if (m_expanded)
    {
        arrow.addTriangle(arrowArea.getX(), arrowArea.getY(),
                          arrowArea.getRight(), arrowArea.getY(),
                          arrowArea.getCentreX(), arrowArea.getBottom());
    }
    else
    {
        arrow.addTriangle(arrowArea.getX(), arrowArea.getY(),
                          arrowArea.getRight(), arrowArea.getCentreY(),
                          arrowArea.getX(), arrowArea.getBottom());
    }
    g.fillPath(arrow);
}

void HelpView::FAQPanel::resized()
{
    auto area = getLocalBounds().reduced(8);
    area.removeFromLeft(28);

    m_questionLabel.setBounds(area.removeFromTop(28));

    if (m_expanded)
    {
        area.removeFromTop(4);
        m_answerLabel.setBounds(area);
    }
}

void HelpView::FAQPanel::mouseUp(const juce::MouseEvent& /*e*/)
{
    m_expanded = !m_expanded;
    m_answerLabel.setVisible(m_expanded);

    setSize(getWidth(), getDesiredHeight());

    if (auto* parent = dynamic_cast<FAQContainer*>(getParentComponent()))
        parent->layoutPanels();
}

int HelpView::FAQPanel::getDesiredHeight() const
{
    if (!m_expanded)
        return 44;

    int lineCount = 1;
    for (int i = 0; i < m_answer.length(); ++i)
        if (m_answer[i] == '\n') ++lineCount;

    int estimatedWidth = getWidth() > 60 ? getWidth() - 60 : 400;
    juce::Font f(13.0f);
    int textWidth = f.getStringWidth(m_answer);
    int wrapLines = (textWidth / juce::jmax(1, estimatedWidth)) + 1;
    lineCount = juce::jmax(lineCount, wrapLines);

    return 44 + lineCount * 18 + 16;
}


void HelpView::FAQContainer::addPanel(FAQPanel* panel)
{
    panels.add(panel);
    addAndMakeVisible(panel);
}

void HelpView::FAQContainer::layoutPanels()
{
    int y = 8;
    int w = getWidth() - 16;

    for (auto* panel : panels)
    {
        panel->setBounds(8, y, w, panel->getDesiredHeight());
        y += panel->getDesiredHeight() + 6;
    }

    setSize(getWidth(), y + 8);
}

void HelpView::FAQContainer::resized()
{
    layoutPanels();
}


void HelpView::ShortcutsTableModel::paintRowBackground(
    juce::Graphics& g, int row, int w, int h, bool selected)
{
    if (selected)
        g.fillAll(Colors::primary().withAlpha(0.2f));
    else if (row % 2 == 0)
        g.fillAll(Colors::bgDark());
    else
        g.fillAll(Colors::bgSurface());
}

void HelpView::ShortcutsTableModel::paintCell(
    juce::Graphics& g, int row, int col, int w, int h, bool /*selected*/)
{
    if (row >= (int)entries.size()) return;

    const auto& entry = entries[(size_t)row];

    if (col == 0)
    {
        g.setColour(Colors::primary());
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText("  " + entry.category, 0, 0, w, h, juce::Justification::centredLeft);
    }
    else if (col == 1)
    {
        g.setColour(Colors::textPrimary());
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::bold));
        g.drawText("  " + entry.shortcut, 0, 0, w, h, juce::Justification::centredLeft);
    }
    else
    {
        g.setColour(Colors::textSecondary());
        g.setFont(juce::Font(13.0f));
        g.drawText("  " + entry.action, 0, 0, w, h, juce::Justification::centredLeft);
    }

    g.setColour(Colors::border());
    g.drawLine((float)w, 0.0f, (float)w, (float)h, 0.5f);
}

juce::TextEditor* HelpView::createStyledTextArea()
{
    auto* te = new juce::TextEditor();
    te->setMultiLine(true, true);
    te->setReadOnly(true);
    te->setScrollbarsShown(true);
    te->setCaretVisible(false);
    te->setColour(juce::TextEditor::backgroundColourId, Colors::bgDark());
    te->setColour(juce::TextEditor::textColourId, Colors::textPrimary());
    te->setColour(juce::TextEditor::outlineColourId, Colors::border());
    te->setColour(juce::ScrollBar::thumbColourId, Colors::bgLighter());
    te->setFont(juce::Font(14.0f));
    return te;
}

juce::Component* HelpView::createGuideTab()
{
    auto* te = createStyledTextArea();

    juce::String text;

    text << "============================================================\n";
    text << "   GUIDE D'UTILISATION - BeatMate V12 Professional\n";
    text << "   par Sebastien Sainte-Foi\n";
    text << "============================================================\n\n";

    text << "------------------------------------------------------------\n";
    text << " DEMARRAGE RAPIDE - 5 etapes essentielles\n";
    text << "------------------------------------------------------------\n\n";

    text << " ETAPE 1 : IMPORTER VOTRE MUSIQUE\n";
    text << "    BeatMate V12 supporte l'import de musique depuis plusieurs sources.\n";
    text << "    - Glissez-deposez vos fichiers ou dossiers directement dans la\n";
    text << "      bibliothèque. BeatMate detecte automatiquement le format.\n";
    text << "    - Utilisez Ctrl+I pour parcourir et selectionner des dossiers.\n";
    text << "    - Configurez des dossiers surveillés dans Paramètres > Bibliothèque\n";
    text << "      pour un import automatique en continu.\n";
    text << "    - Formats supportes : MP3, WAV, FLAC, AIFF, AAC, OGG, M4A, WMA\n";
    text << "    - L'import est 100% non-destructif : vos fichiers originaux ne sont\n";
    text << "      jamais modifies. BeatMate stocke les metadata dans sa propre base.\n";
    text << "    - Astuce : importez par lots pour gagner du temps. BeatMate gere\n";
    text << "      des bibliotheques de plus de 100 000 pistes sans ralentissement.\n\n";

    text << " ETAPE 2 : ANALYSER VOS PISTES\n";
    text << "    L'analyse est le coeur de BeatMate. Elle detecte automatiquement :\n";
    text << "    - BPM (Beats Per Minute) : Le tempo de chaque piste, avec une\n";
    text << "      precision de 0.1 BPM. Essentiel pour le beatmatching.\n";
    text << "    - Key (Tonalite) : La cle musicale en notation Camelot (1A-12B),\n";
    text << "      Open Key ou standard. Permet le mix harmonique.\n";
    text << "    - Energy (1-10) : Le niveau d'energie percu du morceau.\n";
    text << "      1 = ambient/chill, 5 = groove, 10 = peak time/hard.\n";
    text << "    - Structure : Detection des sections (intro, verse, drop, outro)\n";
    text << "      pour placer les hot cues automatiquement.\n";
    text << "    - Selectionnez les pistes, puis Ctrl+A pour analyser la selection.\n";
    text << "    - Ctrl+Shift+A pour analyser toute la bibliothèque d'un coup.\n";
    text << "    - L'analyse tourne en arriere-plan : vous pouvez continuer a\n";
    text << "      travailler pendant que BeatMate analyse vos pistes.\n";
    text << "    - Choisissez la qualité dans Paramètres > Analyse :\n";
    text << "      * Rapide    : ~2 sec/piste, precision correcte (DJ casual)\n";
    text << "      * Standard  : ~5 sec/piste, bonne precision (recommande)\n";
    text << "      * Haute     : ~10 sec/piste, precision maximale (DJ pro)\n";
    text << "      * Ultra     : ~20 sec/piste, analyse multi-passe (studio)\n\n";

    text << " ETAPE 3 : ORGANISER EN PLAYLISTS ET PREPARER VOS SETS\n";
    text << "    BeatMate offre trois modes de preparation :\n";
    text << "    - Playlists simples : Creez (Ctrl+N) et glissez vos pistes.\n";
    text << "    - Preparation Set : Mode avance avec scoring de transitions.\n";
    text << "    - Preparation Soiree : Planifiez toute une soiree par phases.\n";
    text << "    - Utilisez les filtres par genre, BPM, key, energy pour trouver\n";
    text << "      rapidement les bons morceaux.\n";
    text << "    - Notez vos pistes de 1 a 5 etoiles (Ctrl+1 a Ctrl+5).\n";
    text << "    - Triez par n'importe quelle colonne d'un simple clic.\n\n";

    text << juce::String::fromUTF8(" ÉTAPE 4 : UTILISER LIVE SUGGEST\n");
    text << juce::String::fromUTF8("    Live Suggest est votre assistant temps réel pendant le mix :\n");
    text << juce::String::fromUTF8("    - Lancez-le avec Ctrl+L avant ou pendant votre performance.\n");
    text << juce::String::fromUTF8("    - Il affiche les 5 meilleures suggestions pour la prochaine piste\n");
    text << juce::String::fromUTF8("      basées sur la compatibilité BPM + Key + Energy.\n");
    text << juce::String::fromUTF8("    - La courbe d'énergie de votre set s'affiche en temps réel.\n");
    text << juce::String::fromUTF8("    - Deux modes : Mon Style (basé sur vos habitudes) et Smart AI\n");
    text << juce::String::fromUTF8("      (algorithme BeatMate optimisé pour les transitions idéales).\n");
    text << juce::String::fromUTF8("    - Trending : découvrez les pistes tendance du moment.\n\n");

    text << " ETAPE 5 : EXPORTER VERS USB OU DJ SOFTWARE\n";
    text << "    - Ctrl+E pour ouvrir le panneau d'export.\n";
    text << "    - Export USB pret pour CDJ/XDJ avec metadata Pioneer.\n";
    text << "    - Synchronisation directe avec Rekordbox, Serato, Traktor,\n";
    text << "      VirtualDJ, Engine DJ et djay Pro.\n";
    text << "    - Les hot cues, grilles BPM et metadata sont preserves.\n";
    text << "    - Export M3U, CSV, PDF pour d'autres usages.\n\n";

    text << "============================================================\n";
    text << " PREPARATION SET - IA et scoring de transitions\n";
    text << "============================================================\n\n";

    text << " CREER UN SET\n";
    text << "    - Menu lateral > Preparation Set, ou Ctrl+N > Set.\n";
    text << "    - Glissez des pistes depuis la bibliothèque dans le set.\n";
    text << "    - L'indicateur de compatibilite s'affiche entre chaque piste.\n\n";

    text << " SCORING DE COMPATIBILITE\n";
    text << "    - Score de 0 a 100% base sur :\n";
    text << "      * Compatibilite BPM (difference de tempo)\n";
    text << "      * Compatibilite Key (harmonie Camelot)\n";
    text << "      * Courbe d'energy (progression logique)\n";
    text << "      * Genre (coherence stylistique)\n";
    text << "    - Vert (80-100%) : transition ideale\n";
    text << "    - Jaune (50-80%) : transition acceptable\n";
    text << "    - Rouge (0-50%) : transition risquee\n\n";

    text << " IA AUTO-ARRANGE\n";
    text << "    - Cliquez 'IA Auto-Arrange' pour que BeatMate reordonne\n";
    text << "      automatiquement votre set pour un score global optimal.\n";
    text << "    - L'IA prend en compte BPM, Key, Energy et Genre.\n";
    text << "    - Vous pouvez verrouiller certaines positions avant l'auto-arrange.\n\n";

    text << "============================================================\n";
    text << " PREPARATION SOIREE - Planifiez toute une nuit\n";
    text << "============================================================\n\n";

    text << " PHASES D'UNE SOIREE\n";
    text << "    BeatMate divise une soiree en phases typiques :\n";
    text << "    - Accueil (22h-23h) : Musique d'ambiance, BPM bas, energy 2-4\n";
    text << "    - Montee (23h-00h) : Montee progressive, BPM 118-126, energy 4-6\n";
    text << "    - Peak (00h-02h)   : Peak time, BPM 126-134, energy 7-10\n";
    text << "    - Descente (02h-03h) : Retour au calme, energy 5-7\n";
    text << "    - After (03h-05h)  : Ambient, deep, energy 2-4\n\n";

    text << " PROFILS ENERGIE\n";
    text << "    - Preset 'Club Standard' : montee classique avec peak a 1h\n";
    text << "    - Preset 'Festival' : peak prolonge de 2h\n";
    text << "    - Preset 'Lounge' : energie constante, ambiance chill\n";
    text << "    - Preset 'After Party' : descente lente et progressive\n";
    text << "    - Mode personnalise : dessinez votre propre courbe d'energie.\n\n";

    text << " IA AUTO-REMPLIR\n";
    text << "    - Definissez les phases et la duree de chaque phase.\n";
    text << "    - Cliquez 'IA Auto-Remplir' : BeatMate selectionne les meilleures\n";
    text << "      pistes de votre bibliothèque pour chaque phase.\n";
    text << "    - L'IA respecte les contraintes de BPM, Key et Energy.\n\n";

    text << "============================================================\n";
    text << " PLAYLISTS - Organisation et Smart Playlists\n";
    text << "============================================================\n\n";

    text << " PLAYLISTS MANUELLES\n";
    text << "    - Ctrl+N pour creer une nouvelle playlist.\n";
    text << "    - Glissez des pistes depuis la bibliothèque.\n";
    text << "    - Reordonnez par drag & drop.\n\n";

    text << " SMART PLAYLISTS\n";
    text << "    - Playlists dynamiques basees sur des regles :\n";
    text << "      Ex: 'Tech House + 124-128 BPM + Energy > 6 + Rating >= 4'\n";
    text << "    - Se mettent a jour automatiquement quand de nouvelles pistes\n";
    text << "      correspondent aux criteres.\n";
    text << "    - Combinez autant de regles que necessaire.\n\n";

    text << " IA BUILD\n";
    text << "    - Decrivez en langage naturel : 'Playlist tech house pour\n";
    text << "      warm-up de 45 minutes'\n";
    text << "    - BeatMate genere une playlist optimisee automatiquement.\n";
    text << "    - Score global de la playlist affiche en temps reel.\n\n";

    text << "============================================================\n";
    text << " BEATMATE LIVE - Assistant temps reel\n";
    text << "============================================================\n\n";

    text << " MONITORING\n";
    text << "    - Piste en cours avec BPM, Key, Energy affiches.\n";
    text << "    - Temps ecoule et temps restant.\n";
    text << "    - Waveform mini avec position de lecture.\n\n";

    text << " SUGGESTIONS\n";
    text << "    - Top 5 suggestions basees sur compatibilite harmonique.\n";
    text << "    - Score de compatibilite affiche pour chaque suggestion.\n";
    text << "    - Filtrez par genre, energy, mood.\n";
    text << "    - Mode 'Mon Style' : apprend de vos habitudes de mix.\n";
    text << "    - Mode 'Smart AI' : algorithme BeatMate optimise.\n\n";

    text << " TRENDING\n";
    text << "    - Decouvrez les pistes tendance jouees par d'autres DJs.\n";
    text << "    - Statistiques de popularite par genre et periode.\n";
    text << "    - Ajoutez directement les pistes trending a votre bibliothèque.\n\n";

    text << "============================================================\n";
    text << " EXPORT - Formats et options\n";
    text << "============================================================\n\n";

    text << " FORMATS D'EXPORT DJ\n";
    text << "    - Rekordbox XML (.xml) : playlists, hot cues, grilles BPM\n";
    text << "    - Serato Crates/Tags : crates, couleurs, BPM, Key\n";
    text << "    - Traktor NML (.nml) : playlists, cue points, tempo\n";
    text << "    - VirtualDJ Database : playlists, POI, BPM\n";
    text << "    - Engine DJ (Denon) : database, hot cues, grilles\n\n";

    text << " EXPORT USB (CDJ/XDJ)\n";
    text << "    - Structure Pioneer automatique (PIONEER/rekordbox/)\n";
    text << "    - Base de donnees compatible CDJ-3000, XDJ-RX3, etc.\n";
    text << "    - Hot cues, couleurs et metadata preserves.\n";
    text << "    - Waveforms exportes pour affichage sur l'ecran CDJ.\n\n";

    text << " AUTRES FORMATS\n";
    text << "    - M3U / M3U8 : playlists universelles.\n";
    text << "    - CSV : export tableur (Excel, Google Sheets).\n";
    text << "    - PDF : plan de soiree / setlist imprimable.\n";
    text << "    - JSON : export complet de la base de donnees.\n\n";

    text << "============================================================\n";
    text << " NORMALISATION AUDIO - Volume uniforme\n";
    text << "============================================================\n\n";

    text << " QU'EST-CE QUE LE LUFS ?\n";
    text << "    LUFS (Loudness Units Full Scale) mesure le volume percu.\n";
    text << "    C'est le standard international pour la mesure de loudness.\n";
    text << "    Contrairement aux dB peak, le LUFS reflete comment l'oreille\n";
    text << "    humaine percoit reellement le volume.\n\n";

    text << " PRESETS DE NORMALISATION\n";
    text << "    - Streaming (-14 LUFS) : Niveau Spotify/Apple Music\n";
    text << "    - Club (-8 LUFS) : Niveau club, plus fort\n";
    text << "    - Festival (-6 LUFS) : Niveau festival, maximum\n";
    text << "    - Personnalise : choisissez votre valeur cible\n";
    text << "    - Le traitement est non-destructif (gain de lecture ajuste).\n\n";

    text << "============================================================\n";
    text << " STREAMING - Tendances et decouverte\n";
    text << "============================================================\n\n";

    text << " TENDANCES\n";
    text << "    - Suivez les tendances musicales par genre et region.\n";
    text << "    - Charts mis a jour regulierement.\n";
    text << "    - Decouvrez les nouvelles sorties populaires.\n\n";

    text << " DECOUVRIR\n";
    text << "    - Recommandations basees sur votre bibliothèque.\n";
    text << "    - Artistes similaires a ceux que vous jouez.\n";
    text << "    - Integration avec les plateformes de streaming.\n\n";

    text << juce::String::fromUTF8("\n");
    text << juce::String::fromUTF8("============================================================\n");
    text << juce::String::fromUTF8(" BIBLIOTHÈQUE - Filtrer, trier et organiser\n");
    text << juce::String::fromUTF8("============================================================\n\n");
    text << juce::String::fromUTF8(" LA BARRE DE FACETTES\n");
    text << juce::String::fromUTF8("    Ce bandeau, sous la recherche et les filtres, filtre par chips.\n");
    text << juce::String::fromUTF8("    1. À gauche, choisissez la dimension parmi les six boutons\n");
    text << juce::String::fromUTF8("       Genre, BPM, Énergie, Année, Clé et État.\n");
    text << juce::String::fromUTF8("    2. Cliquez une pastille pour l'activer, recliquez pour la\n");
    text << juce::String::fromUTF8("       désactiver ; son compteur est recalculé à chaque clic.\n");
    text << juce::String::fromUTF8("    3. Les pastilles d'une même dimension se cumulent en OU,\n");
    text << juce::String::fromUTF8("       deux dimensions différentes se cumulent en ET.\n");
    text << juce::String::fromUTF8("    Les flèches du carrousel et la molette font défiler les\n");
    text << juce::String::fromUTF8("    pastilles ; « × Effacer », à droite, retire toutes les\n");
    text << juce::String::fromUTF8("    facettes actives. Les paliers vont de « < 95 » à « 142+ »,\n");
    text << juce::String::fromUTF8("    de Douce 1-3 à Max 9-10, de 70s- à 2020s et de 1A à 12B,\n");
    text << juce::String::fromUTF8("    chacun suivi de Sans BPM, Sans énergie, Sans année, Sans clé.\n\n");
    text << juce::String::fromUTF8(" LE REGROUPEMENT DES GENRES\n");
    text << juce::String::fromUTF8("    Quand la dimension Genre est active, un bouton apparaît à sa\n");
    text << juce::String::fromUTF8("    droite : cliquez-le pour basculer entre « Groupés » et\n");
    text << juce::String::fromUTF8("    « Détail ». En Groupés, les libellés bruts de vos fichiers\n");
    text << juce::String::fromUTF8("    (disco888, Disco/Funk, disco 444) sont rassemblés en\n");
    text << juce::String::fromUTF8("    familles : Zouk, Kompa / Compas, Kizomba, Reggae / Dancehall,\n");
    text << juce::String::fromUTF8("    Afro, Salsa / Latino, House, Techno, Trance, Disco / Funk,\n");
    text << juce::String::fromUTF8("    Soul / R&B, Hip-Hop / Rap, Métal, Rock, Électro / Dance,\n");
    text << juce::String::fromUTF8("    Slow / Ballade, Variété française, Pop, Jazz, Blues, Country,\n");
    text << juce::String::fromUTF8("    Classique, Gospel et Thème / B.O. Un genre non reconnu reste\n");
    text << juce::String::fromUTF8("    tel quel, les sans-genre vont sous « Sans genre ». En Détail,\n");
    text << juce::String::fromUTF8("    chaque libellé brut redevient une pastille.\n\n");
    text << juce::String::fromUTF8(" LA FACETTE ÉTAT (le ménage de la collection)\n");
    text << juce::String::fromUTF8("    Cliquez la dimension « État » pour obtenir les pastilles de\n");
    text << juce::String::fromUTF8("    contrôle qualité, dans cet ordre :\n");
    text << juce::String::fromUTF8("    - Fichiers manquants : le fichier n'est plus à son emplace-\n");
    text << juce::String::fromUTF8("      ment. Corrompus : fichier illisible ou tronqué.\n");
    text << juce::String::fromUTF8("    - Doublons : même artiste et même titre présents plusieurs fois,\n");
    text << juce::String::fromUTF8("      casse ignorée. Sans BPM, Sans clé, Sans genre : champ vide.\n");
    text << juce::String::fromUTF8("      Sans année : absente ou antérieure à 1950. Non analysés :\n");
    text << juce::String::fromUTF8("      jamais passés par l'analyse. Jamais joués : zéro lecture.\n");
    text << juce::String::fromUTF8("    Ces pastilles se cumulent en OU. « Réparer les corrompus »\n");
    text << juce::String::fromUTF8("    n'apparaît que si Corrompus est active, et « Fichiers\n");
    text << juce::String::fromUTF8("    manquants » passe en rouge si sa pastille l'est.\n\n");
    text << juce::String::fromUTF8(" COCHER LES MORCEAUX À TRAITER\n");
    text << juce::String::fromUTF8("    Chaque ligne a une case dans la gouttière, la bande étroite\n");
    text << juce::String::fromUTF8("    tout à gauche de la liste, avant la colonne WAVE.\n");
    text << juce::String::fromUTF8("    1. Cliquez la gouttière d'une ligne pour la cocher ; Maj+clic\n");
    text << juce::String::fromUTF8("       coche toute la plage depuis la dernière case cliquée.\n");
    text << juce::String::fromUTF8("    2. Cliquez la case d'en-tête, dans la gouttière de la ligne\n");
    text << juce::String::fromUTF8("       des titres de colonnes, pour tout cocher ou décocher.\n");
    text << juce::String::fromUTF8("    3. Le bouton « Tout cocher » fait de même et devient « Décocher\n");
    text << juce::String::fromUTF8("       (n) » ; Ctrl+A coche tout l'affichage, Ctrl+D décoche tout.\n");
    text << juce::String::fromUTF8("    Les cochés sont ceux sur lesquels agissent l'éditeur de tags,\n");
    text << juce::String::fromUTF8("    l'analyse et les exports. Sans aucun coché, l'éditeur de tags\n");
    text << juce::String::fromUTF8("    prend la sélection, à défaut toute la liste affichée.\n\n");
    text << juce::String::fromUTF8(" LES VUES SAUVEGARDÉES\n");
    text << juce::String::fromUTF8("    Une vue mémorise d'un bloc la recherche, les facettes, le tri,\n");
    text << juce::String::fromUTF8("    la largeur et la visibilité des colonnes.\n");
    text << juce::String::fromUTF8("    1. Réglez la bibliothèque comme vous le souhaitez.\n");
    text << juce::String::fromUTF8("    2. Cliquez « Vues », puis « Enregistrer la vue actuelle... »,\n");
    text << juce::String::fromUTF8("       nommez-la et validez ; un nom existant remplace la vue.\n");
    text << juce::String::fromUTF8("    3. Pour rappeler une vue, rouvrez « Vues » et cliquez son nom ;\n");
    text << juce::String::fromUTF8("       pour en retirer une, sous-menu « Supprimer une vue ».\n\n");
    text << juce::String::fromUTF8(" LES COLONNES\n");
    text << juce::String::fromUTF8("    La liste compte 25 colonnes : WAVE, ART, TITRE, ARTISTE,\n");
    text << juce::String::fromUTF8("    ALBUM, BPM, KEY, CAMELOT, ENERGY, DUREE, GENRE, NOTE, COL,\n");
    text << juce::String::fromUTF8("    CUES, STEMS, ANNEE, PLAYS, LUFS, MOOD, DANSE, LABEL, COMMENT,\n");
    text << juce::String::fromUTF8("    KBPS, JOUE LE et AJOUTE.\n");
    text << juce::String::fromUTF8("    - Cliquez un titre pour trier, recliquez pour inverser. Saisis-\n");
    text << juce::String::fromUTF8("      sez son bord droit et glissez pour redimensionner, de 16 à\n");
    text << juce::String::fromUTF8("      600 pixels.\n");
    text << juce::String::fromUTF8("    - Un clic droit sur la ligne des titres ouvre « Colonnes\n");
    text << juce::String::fromUTF8("      affichées » : cochez ou décochez, le menu se rouvre pour en\n");
    text << juce::String::fromUTF8("      enchaîner plusieurs. TITRE ne peut pas être masquée.\n");
    text << juce::String::fromUTF8("    Largeurs et visibilité sont conservées au redémarrage.\n\n");
    text << juce::String::fromUTF8(" CLIQUER DIRECTEMENT DANS UNE CELLULE\n");
    text << juce::String::fromUTF8("    - ARTISTE et GENRE : filtrent sur la valeur. BPM : cale les deux\n");
    text << juce::String::fromUTF8("      curseurs dessus. KEY et CAMELOT : règlent le filtre de clé.\n");
    text << juce::String::fromUTF8("    - NOTE : cliquez la n-ième étoile pour donner n étoiles ;\n");
    text << juce::String::fromUTF8("      recliquer la même étoile remet la note à zéro.\n");
    text << juce::String::fromUTF8("    - COL : ouvre « Couleur du morceau » avec Rouge, Orange,\n");
    text << juce::String::fromUTF8("      Jaune, Vert, Cyan, Bleu, Violet, Rose et « Aucune ».\n");
    text << juce::String::fromUTF8("    Ctrl+F place le curseur dans la recherche, Espace pré-écoute\n");
    text << juce::String::fromUTF8("    la sélection, Suppr la retire après confirmation.\n\n");
    text << juce::String::fromUTF8(" LE MENU DU CLIC DROIT SUR UN MORCEAU\n");
    text << juce::String::fromUTF8("    Il propose, dans l'ordre : Lire / Pre-ecouter, Ouvrir dans Hot\n");
    text << juce::String::fromUTF8("    Cues, Analyser, Normaliser, Modifier les metadonnees..., Ajouter\n");
    text << juce::String::fromUTF8("    au set actuel, Ajouter a la playlist... (avec « + Nouvelle\n");
    text << juce::String::fromUTF8("    playlist... »), Verifier l'integrite du fichier, Reparer le\n");
    text << juce::String::fromUTF8("    fichier (sauvegarde de l'original), Localiser le fichier, Copier\n");
    text << juce::String::fromUTF8("    les infos, Exporter vers..., Trouver les doublons, Relier les\n");
    text << juce::String::fromUTF8("    fichiers manquants... et Supprimer de la collection.\n\n");
    text << juce::String::fromUTF8(" RETROUVER LES FICHIERS MANQUANTS\n");
    text << juce::String::fromUTF8("    Le bouton « Fichiers manquants » ouvre la fenêtre « Relier les\n");
    text << juce::String::fromUTF8("    fichiers manquants ». L'analyse démarre seule ; relancez-la\n");
    text << juce::String::fromUTF8("    avec « Rechercher les fichiers manquants ». Chaque ligne donne\n");
    text << juce::String::fromUTF8("    le titre, la confiance, l'ancien chemin en rouge et le nouveau\n");
    text << juce::String::fromUTF8("    en vert ; cliquez-la pour cocher la proposition. « Chercher\n");
    text << juce::String::fromUTF8("    dans... » ajoute un dossier à explorer, « Relier la selection »\n");
    text << juce::String::fromUTF8("    applique les remplacements cochés.\n\n");
    text << juce::String::fromUTF8(" TRAITER LES DOUBLONS\n");
    text << juce::String::fromUTF8("    L'entrée « Trouver les doublons » du clic droit ouvre la\n");
    text << juce::String::fromUTF8("    fenêtre « Doublons de la bibliothèque ». Choisissez la méthode\n");
    text << juce::String::fromUTF8("    (Par nom de fichier, Par metadonnees (titre/artiste/duree),\n");
    text << juce::String::fromUTF8("    proposée par défaut, ou Par empreinte audio) puis cliquez\n");
    text << juce::String::fromUTF8("    « Analyser ». Chaque ligne montre la piste GARDER, la piste\n");
    text << juce::String::fromUTF8("    RETIRER et la confiance ; cliquez-la pour la cocher.\n");
    text << juce::String::fromUTF8("    « Fusionner la selection » reporte cues, note et playlists sur la\n");
    text << juce::String::fromUTF8("    piste conservée et retire le doublon, sans supprimer le fichier.\n\n");
    text << juce::String::fromUTF8(" MODIFIER PLUSIEURS MORCEAUX D'UN COUP\n");
    text << juce::String::fromUTF8("    Sélectionnez les lignes puis Ctrl+E, ou choisissez « Modifier\n");
    text << juce::String::fromUTF8("    les metadonnees... » : le panneau EDITION EN LOT s'affiche au\n");
    text << juce::String::fromUTF8("    centre. Cochez uniquement les champs à écraser (Titre,\n");
    text << juce::String::fromUTF8("    Artiste, Album, Genre, BPM, Cle, Commentaire, Label, Energie,\n");
    text << juce::String::fromUTF8("    Note, Couleur) : un champ non coché n'est pas touché. Curseur\n");
    text << juce::String::fromUTF8("    de 1 à 10 pour Energie, de 0 à 5 pour Note, « Choisir... »\n");
    text << juce::String::fromUTF8("    pour la couleur, puis « Appliquer » ou « Annuler ».\n\n");
    text << juce::String::fromUTF8(" VÉRIFIER ET RÉPARER LES FICHIERS\n");
    text << juce::String::fromUTF8("    Le bouton « Vérifier / Réparer » ouvre le menu « Intégrité des\n");
    text << juce::String::fromUTF8("    fichiers » à trois portées : « La sélection (n) », « Les\n");
    text << juce::String::fromUTF8("    morceaux affichés (n) » et « Toute la bibliothèque ». Cochez\n");
    text << juce::String::fromUTF8("    les fichiers à réparer puis lancez : l'original est sauvegardé\n");
    text << juce::String::fromUTF8("    dans integrity_backups.\n\n");
    text << juce::String::fromUTF8(" L'ARBRE DE GAUCHE\n");
    text << juce::String::fromUTF8("    Cliquez un nœud pour charger son contenu dans la liste.\n");
    text << juce::String::fromUTF8("    - Ma bibliothèque : la collection BeatMate complète.\n");
    text << juce::String::fromUTF8("    - Smartlists : vos listes intelligentes ; clic droit pour\n");
    text << juce::String::fromUTF8("      Nouvelle smartlist..., Modifier les regles..., Supprimer.\n");
    text << juce::String::fromUTF8("    - Rekordbox : Playlists et Historique. Serato : Crates et\n");
    text << juce::String::fromUTF8("      Smart Crates. Traktor, VirtualDJ et Engine DJ : Playlists.\n");
    text << juce::String::fromUTF8("    Les cinq logiciels sont toujours affichés ; non détecté, l'un\n");
    text << juce::String::fromUTF8("    d'eux porte « (non disponible) ». « Re-sync DJ playlists » les\n");
    text << juce::String::fromUTF8("    relit tous les cinq.\n\n");
    text << juce::String::fromUTF8(" EXPORTER VERS UN LOGICIEL DJ\n");
    text << juce::String::fromUTF8("    1. Sélectionnez les morceaux, clic droit, sous-menu « Exporter\n");
    text << juce::String::fromUTF8("       vers... », puis Rekordbox XML, Traktor NML ou Serato Crates.\n");
    text << juce::String::fromUTF8("    2. Indiquez le fichier de destination ; les noms proposés sont\n");
    text << juce::String::fromUTF8("       BeatMate_rekordbox.xml, BeatMate_traktor.nml et\n");
    text << juce::String::fromUTF8("       BeatMate_serato_crate.m3u8, dans vos Documents.\n");
    text << juce::String::fromUTF8("    L'export Rekordbox XML emporte vos cues et vos boucles et crée\n");
    text << juce::String::fromUTF8("    une playlist BeatMate Export. Pour envoyer des hot cues vers\n");
    text << juce::String::fromUTF8("    VirtualDJ ou Engine DJ, passez par Hot Cues.\n\n");
    text << juce::String::fromUTF8("\n");
    text << juce::String::fromUTF8("============================================================\n");
    text << juce::String::fromUTF8(" ANALYSE - Mesurer BPM, clé, énergie et structure\n");
    text << juce::String::fromUTF8("============================================================\n\n");
    text << juce::String::fromUTF8(" CHOISIR CE QUI EST MESURÉ\n");
    text << juce::String::fromUTF8("    La rangée de cases sous les filtres décide de ce que l'analyse\n");
    text << juce::String::fromUTF8("    calcule ; vos choix sont mémorisés. BPM, Key (Tonalite),\n");
    text << juce::String::fromUTF8("    Energie, Structure et Waveform sont cochés par défaut. Mood,\n");
    text << juce::String::fromUTF8("    l'ambiance du morceau, est décoché. Stems Ultra (MDX-GPU),\n");
    text << juce::String::fromUTF8("    décoché lui aussi, pré-sépare les stems en haute qualité\n");
    text << juce::String::fromUTF8("    pendant l'analyse pour les avoir instantanément au Studio ;\n");
    text << juce::String::fromUTF8("    il demande le moteur stemsep.\n\n");
    text << juce::String::fromUTF8(" RESTREINDRE LA LISTE\n");
    text << juce::String::fromUTF8("    La barre du haut offre quatre filtres cumulables :\n");
    text << juce::String::fromUTF8("    « Recherche: » sur le titre et l'artiste, « Genre: »,\n");
    text << juce::String::fromUTF8("    « Statut: » avec Tous, Non analyses, Analyses et Erreur, et\n");
    text << juce::String::fromUTF8("    deux curseurs qui bornent la plage de BPM. Non analyses est le\n");
    text << juce::String::fromUTF8("    choix par défaut, celui qui sert à traiter un import récent.\n");
    text << juce::String::fromUTF8("    « Ajouter des pistes... » injecte des fichiers du disque sans\n");
    text << juce::String::fromUTF8("    passer par la bibliothèque, « Vider la liste » remet la file\n");
    text << juce::String::fromUTF8("    à zéro.\n\n");
    text << juce::String::fromUTF8(" LANCER ET ARRÊTER\n");
    text << juce::String::fromUTF8("    1. Cochez les morceaux dans la première colonne, ou utilisez\n");
    text << juce::String::fromUTF8("       « Tout selectionner », qui devient « Tout deselectionner »\n");
    text << juce::String::fromUTF8("       quand tout est coché.\n");
    text << juce::String::fromUTF8("    2. Cliquez « Analyser selection » pour les morceaux cochés, ou\n");
    text << juce::String::fromUTF8("       « Analyser tout » pour toute la file. Sans rien de coché,\n");
    text << juce::String::fromUTF8("       la notification « Aucune piste selectionnee » s'affiche et\n");
    text << juce::String::fromUTF8("       rien ne démarre.\n");
    text << juce::String::fromUTF8("    3. Pour interrompre, cliquez « Annuler » : il affiche\n");
    text << juce::String::fromUTF8("       « Annulation... » le temps que les pistes en cours se\n");
    text << juce::String::fromUTF8("       terminent proprement.\n\n");
    text << juce::String::fromUTF8(" SUIVRE LA PROGRESSION\n");
    text << juce::String::fromUTF8("    La carte au-dessus de la liste affiche une pastille d'état\n");
    text << juce::String::fromUTF8("    (Pret a analyser, Analyse en cours, Analyse terminee, Analyse\n");
    text << juce::String::fromUTF8("    annulee), un anneau avec le pourcentage remplacé par une coche\n");
    text << juce::String::fromUTF8("    verte à la fin, le compteur « Analysees : n / total »,\n");
    text << juce::String::fromUTF8("    « Ignorees : n » en orange s'il y a eu des sauts, « Temps\n");
    text << juce::String::fromUTF8("    restant : » suivi d'une estimation calculée sur la cadence\n");
    text << juce::String::fromUTF8("    observée, et le titre des trois pistes en cours. Chaque ligne\n");
    text << juce::String::fromUTF8("    porte son état, En attente, En cours, Analysee ou Erreur,\n");
    text << juce::String::fromUTF8("    avec sa barre de progression.\n\n");
    text << juce::String::fromUTF8(" LIRE LES RÉSULTATS\n");
    text << juce::String::fromUTF8("    Les colonnes sont Titre, Artiste, Statut, BPM, Key, Energie,\n");
    text << juce::String::fromUTF8("    LUFS et Progression ; cliquez un titre pour trier. La pastille\n");
    text << juce::String::fromUTF8("    à droite du BPM donne la confiance : verte au-delà de 75 %,\n");
    text << juce::String::fromUTF8("    orange au-delà de 45 %, rouge en dessous ; une pastille rouge\n");
    text << juce::String::fromUTF8("    mérite vérification. La colonne Energie dessine la courbe\n");
    text << juce::String::fromUTF8("    d'énergie en miniature, et un tiret signifie que la mesure\n");
    text << juce::String::fromUTF8("    n'existe pas encore.\n\n");
    text << juce::String::fromUTF8(" LE PANNEAU DE DÉTAIL\n");
    text << juce::String::fromUTF8("    Cliquez une ligne pour ouvrir sa fiche à droite. Le panneau\n");
    text << juce::String::fromUTF8("    n'apparaît que si la fenêtre fait au moins 1080 pixels de\n");
    text << juce::String::fromUTF8("    large : agrandissez-la s'il manque. Il contient MESURES (BPM\n");
    text << juce::String::fromUTF8("    et tonalité, chacun avec sa confiance), WAVEFORM, STRUCTURE,\n");
    text << juce::String::fromUTF8("    LOUDNESS avec les trois tuiles LUFS, LRA en LU et PEAK en\n");
    text << juce::String::fromUTF8("    dBTP, ROUE CAMELOT, puis FICHIER avec Duree, Format et\n");
    text << juce::String::fromUTF8("    « Analysee le ». La mention « Non disponible - relancer\n");
    text << juce::String::fromUTF8("    l'analyse » signale une section jamais calculée : cochez\n");
    text << juce::String::fromUTF8("    l'option correspondante et relancez. Le bouton « Vérifier /\n");
    text << juce::String::fromUTF8("    Réparer » est aussi présent ici, car un fichier corrompu\n");
    text << juce::String::fromUTF8("    fausse toutes les mesures.\n\n");
    text << juce::String::fromUTF8("\n");
    text << juce::String::fromUTF8("============================================================\n");
    text << juce::String::fromUTF8(" HOT CUES - Poser et régler vos points de repère\n");
    text << juce::String::fromUTF8("============================================================\n\n");
    text << juce::String::fromUTF8(" CHARGER UN MORCEAU\n");
    text << juce::String::fromUTF8("    Dans la colonne de gauche, filtrez avec « Rechercher titre/\n");
    text << juce::String::fromUTF8("    artiste... », la liste « Genre: Tous » et la liste d'état des\n");
    text << juce::String::fromUTF8("    cues (Tous, Sans cues, Avec cues), basculez entre les onglets\n");
    text << juce::String::fromUTF8("    « Liste » et « Dossiers », puis double-cliquez une piste. Le\n");
    text << juce::String::fromUTF8("    bouton « Bibliothèque » ouvre la même liste en fenêtre flottante.\n\n");
    text << juce::String::fromUTF8(" LES HUIT PADS\n");
    text << juce::String::fromUTF8("    Les pads forment une grille 4 × 2, soit huit cues de A à H.\n");
    text << juce::String::fromUTF8("    - Cliquez un pad vide : le cue est posé à la position de lecture\n");
    text << juce::String::fromUTF8("      courante. Cliquez un pad occupé : la lecture saute au cue et\n");
    text << juce::String::fromUTF8("      démarre. Les touches 1 à 8 font de même.\n");
    text << juce::String::fromUTF8("    - Sur la forme d'onde, cliquez un marqueur pour sélectionner le\n");
    text << juce::String::fromUTF8("      cue, ou faites-le glisser pour le déplacer.\n");
    text << juce::String::fromUTF8("    En bas à droite, la zone DETAILS CUE aligne les huit cues : tapez\n");
    text << juce::String::fromUTF8("    le nom à gauche, la position exacte est rappelée à droite.\n\n");
    text << juce::String::fromUTF8(" LE POPOVER D'ÉDITION D'UN CUE\n");
    text << juce::String::fromUTF8("    Faites un clic droit sur un pad occupé, ou sur un marqueur de\n");
    text << juce::String::fromUTF8("    la forme d'onde : une fenêtre s'ouvre à côté.\n");
    text << juce::String::fromUTF8("    1. « Nom du cue » suivi de la lettre du pad : tapez le nom et\n");
    text << juce::String::fromUTF8("       validez par Entrée.\n");
    text << juce::String::fromUTF8("    2. « Couleur » : 16 pastilles sur deux rangées de huit.\n");
    text << juce::String::fromUTF8("    3. « Caler transitoire » recale le cue sur le transitoire le\n");
    text << juce::String::fromUTF8("       plus proche, dans une fenêtre de 200 ms de part et d'autre.\n");
    text << juce::String::fromUTF8("       « Supprimer » efface le cue et referme la fenêtre.\n\n");
    text << juce::String::fromUTF8(" QUANTIZE, NUDGE ET ZOOM\n");
    text << juce::String::fromUTF8("    Le bouton « Quantize » est un interrupteur, actif par défaut et\n");
    text << juce::String::fromUTF8("    mémorisé ; la touche Q le bascule aussi. Allumé, tout cue posé ou\n");
    text << juce::String::fromUTF8("    déplacé se cale sur le beat le plus proche de la grille ; sans\n");
    text << juce::String::fromUTF8("    BPM connu la position reste telle quelle, alors analysez d'abord\n");
    text << juce::String::fromUTF8("    le morceau. Les quatre boutons de nudge décalent le cue\n");
    text << juce::String::fromUTF8("    sélectionné de « -1 beat », « -10 ms », « +10 ms » ou « +1 beat ».\n");
    text << juce::String::fromUTF8("    Le curseur « Zoom: » va de 1x à 100x et s'ouvre à 50x.\n\n");
    text << juce::String::fromUTF8(" AUTO-GÉNÉRER PUIS EXPORTER\n");
    text << juce::String::fromUTF8("    1. Chargez un morceau, sinon le message « Chargez une piste\n");
    text << juce::String::fromUTF8("       d'abord. » s'affiche.\n");
    text << juce::String::fromUTF8("    2. Cliquez « Auto-generer (IA) », sous la liste de gauche : il\n");
    text << juce::String::fromUTF8("       passe en « Analyse en cours... », privilégie les drops et cale\n");
    text << juce::String::fromUTF8("       les cues sur le premier temps. Les cues existants sont\n");
    text << juce::String::fromUTF8("       remplacés et nommés d'après la section détectée ; « Tout\n");
    text << juce::String::fromUTF8("       effacer » les retire tous.\n");
    text << juce::String::fromUTF8("    3. Choisissez la cible en bas à droite : Rekordbox, Serato,\n");
    text << juce::String::fromUTF8("       Traktor, VirtualDJ ou Engine DJ, puis cliquez « Exporter\n");
    text << juce::String::fromUTF8("       vers DJ Software ». Sans cue posé, le message « Aucun cue\n");
    text << juce::String::fromUTF8("       point actif à exporter. » apparaît.\n\n");
    text << juce::String::fromUTF8(" LES RACCOURCIS DES HOT CUES\n");
    text << juce::String::fromUTF8("    1 à 8        pose le cue, ou saute dessus s'il existe\n");
    text << juce::String::fromUTF8("    Flèches      décalent le cue sélectionné de 10 ms\n");
    text << juce::String::fromUTF8("    Maj+flèches  décalent d'un beat\n");
    text << juce::String::fromUTF8("    Q            active ou coupe Quantize\n");
    text << juce::String::fromUTF8("    Suppr        supprime le cue sélectionné\n");
    text << juce::String::fromUTF8("    Espace       lecture / pause\n");
    text << juce::String::fromUTF8("    Clic droit   ouvre le popover d'édition\n\n");
    text << juce::String::fromUTF8(" LIMITE DE LA VERSION D'ESSAI\n");
    text << juce::String::fromUTF8("    En version d'essai, la pré-écoute est limitée à 5 minutes de\n");
    text << juce::String::fromUTF8("    lecture cumulée par morceau. Au-delà, la lecture s'arrête, le\n");
    text << juce::String::fromUTF8("    bouton devient « ESSAI TERMINE » et le message « Chargez un autre\n");
    text << juce::String::fromUTF8("    morceau pour continuer (5 min par morceau). » s'affiche. Chargez\n");
    text << juce::String::fromUTF8("    une autre piste pour repartir ; poser et éditer restent libres.\n\n");

    text << juce::String::fromUTF8("\n");
    text << juce::String::fromUTF8("============================================================\n");
    text << juce::String::fromUTF8(" AGENDA - Vos dates et prestations\n");
    text << juce::String::fromUTF8("============================================================\n\n");
    text << juce::String::fromUTF8(" OUVRIR L'AGENDA\n");
    text << juce::String::fromUTF8("    Cliquez sur « Agenda » dans le menu latéral, ou sur le\n");
    text << juce::String::fromUTF8("    bouton « Agenda » de la barre d'outils.\n\n");
    text << juce::String::fromUTF8(" CHOISIR L'AFFICHAGE\n");
    text << juce::String::fromUTF8("    Trois vues, en haut à gauche du calendrier :\n");
    text << juce::String::fromUTF8("    - « Mois »    : une pastille de couleur par prestation.\n");
    text << juce::String::fromUTF8("    - « Semaine » : sept colonnes, horaires et lieux visibles.\n");
    text << juce::String::fromUTF8("    - « Année »   : les 12 mois, un point coloré par date.\n");
    text << juce::String::fromUTF8("    Les flèches « ◀ » et « ▶ » reculent ou avancent d'un mois,\n");
    text << juce::String::fromUTF8("    d'une semaine ou d'une année selon la vue active.\n");
    text << juce::String::fromUTF8("    « Aujourd'hui » ramène le calendrier à la date du jour.\n");
    text << juce::String::fromUTF8("    En vue Année, un clic sur un mois bascule en vue Mois.\n\n");
    text << juce::String::fromUTF8(" CRÉER UNE DATE\n");
    text << juce::String::fromUTF8("    1. Cliquez sur le jour voulu dans le calendrier.\n");
    text << juce::String::fromUTF8("    2. Cliquez sur « + Nouvelle date » (ou double-cliquez\n");
    text << juce::String::fromUTF8("       directement sur un jour libre).\n");
    text << juce::String::fromUTF8("    3. Renseignez la fenêtre « Nouvelle date » :\n");
    text << juce::String::fromUTF8("       * Titre / soirée      (obligatoire)\n");
    text << juce::String::fromUTF8("       * Date (AAAA-MM-JJ)   (obligatoire)\n");
    text << juce::String::fromUTF8("       * Début (HH:MM)       proposé à 22:00\n");
    text << juce::String::fromUTF8("       * Fin (HH:MM)         proposée à 04:00\n");
    text << juce::String::fromUTF8("       * Lieu / club, Ville, Cachet (€), Style / genre\n");
    text << juce::String::fromUTF8("       * Statut : À confirmer, Confirmé, Passé, Annulé\n");
    text << juce::String::fromUTF8("       * Notes\n");
    text << juce::String::fromUTF8("    4. Cochez les rappels souhaités dans « Rappels avant la\n");
    text << juce::String::fromUTF8("       prestation » : 15 min, 30 min, 1 h, 2 h, 1 jour,\n");
    text << juce::String::fromUTF8("       1 sem., 1 mois, 2 mois. Plusieurs délais possibles.\n");
    text << juce::String::fromUTF8("    5. Cliquez sur « Enregistrer » (ou touche Entrée).\n");
    text << juce::String::fromUTF8("    Si l'heure de fin est antérieure à l'heure de début, la\n");
    text << juce::String::fromUTF8("    prestation est automatiquement considérée comme finissant\n");
    text << juce::String::fromUTF8("    le lendemain.\n");
    text << juce::String::fromUTF8("    Attention : sans titre ou sans date valide, la fenêtre se\n");
    text << juce::String::fromUTF8("    ferme sans rien enregistrer.\n\n");
    text << juce::String::fromUTF8(" MODIFIER OU SUPPRIMER\n");
    text << juce::String::fromUTF8("    1. Sélectionnez la date dans la liste sous le calendrier.\n");
    text << juce::String::fromUTF8("    2. Cliquez sur « Modifier » (ou double-cliquez sur la\n");
    text << juce::String::fromUTF8("       ligne, ou sur le jour concerné dans le calendrier).\n");
    text << juce::String::fromUTF8("    3. Pour effacer, cliquez sur « Supprimer » et confirmez.\n");
    text << juce::String::fromUTF8("    La couleur des pastilles est déduite du style, à défaut du\n");
    text << juce::String::fromUTF8("    lieu puis du titre. Une date annulée passe en gris.\n\n");
    text << juce::String::fromUTF8(" RÉGLER LES RAPPELS PAR DÉFAUT\n");
    text << juce::String::fromUTF8("    Le bouton « Rappels ▾ », à droite de la barre de vues,\n");
    text << juce::String::fromUTF8("    fixe les délais appliqués à toute nouvelle date : de\n");
    text << juce::String::fromUTF8("    « 15 min avant » à « 2 mois avant ». Cochez-en autant que\n");
    text << juce::String::fromUTF8("    nécessaire, ou choisissez « Tout désactiver ».\n\n");
    text << juce::String::fromUTF8(" EXPORTER L'AGENDA\n");
    text << juce::String::fromUTF8("    1. Cliquez sur « Exporter ▾ » (en haut à droite).\n");
    text << juce::String::fromUTF8("    2. Choisissez le format :\n");
    text << juce::String::fromUTF8("       * Calendrier .ics  - Google, Outlook, Apple\n");
    text << juce::String::fromUTF8("       * Page HTML  - imprimable\n");
    text << juce::String::fromUTF8("       * Document PDF\n");
    text << juce::String::fromUTF8("       * Document Word  (.docx)\n");
    text << juce::String::fromUTF8("       * Tableur CSV  - Excel\n");
    text << juce::String::fromUTF8("    3. Le fichier est proposé sur le Bureau (agenda-dj.ics,\n");
    text << juce::String::fromUTF8("       agenda-dj.html, agenda-dj.pdf, agenda-dj.docx,\n");
    text << juce::String::fromUTF8("       agenda-dj.csv), puis ouvert automatiquement.\n");
    text << juce::String::fromUTF8("    Les documents HTML, PDF et Word reprennent date, horaire,\n");
    text << juce::String::fromUTF8("    prestation, lieu, ville, cachet et statut, avec le nombre\n");
    text << juce::String::fromUTF8("    de dates et le cachet total. Le CSV est séparé par des\n");
    text << juce::String::fromUTF8("    points-virgules et s'ouvre directement dans Excel.\n");
    text << juce::String::fromUTF8("    Pour le .ics : dans Google Agenda, Paramètres ›\n");
    text << juce::String::fromUTF8("    Importer & exporter.\n\n");
    text << juce::String::fromUTF8(" IMPORTER UN CALENDRIER\n");
    text << juce::String::fromUTF8("    Cliquez sur « Importer .ics », choisissez le fichier :\n");
    text << juce::String::fromUTF8("    les dates sont ajoutées à l'agenda existant (rien n'est\n");
    text << juce::String::fromUTF8("    écrasé) et le nombre de dates importées s'affiche.\n\n");

    text << juce::String::fromUTF8("\n");
    text << juce::String::fromUTF8("============================================================\n");
    text << juce::String::fromUTF8(" COMPARAISON - Deux dossiers côte à côte\n");
    text << juce::String::fromUTF8("============================================================\n\n");
    text << juce::String::fromUTF8("    Ce module compare le contenu de deux dossiers (disque dur,\n");
    text << juce::String::fromUTF8("    clé USB, sauvegarde) et permet de les remettre à niveau\n");
    text << juce::String::fromUTF8("    dans les deux sens.\n\n");
    text << juce::String::fromUTF8(" CHOISIR LES DEUX DOSSIERS\n");
    text << juce::String::fromUTF8("    1. Ouvrez « Comparaison » dans le menu latéral.\n");
    text << juce::String::fromUTF8("    2. Cliquez sur « Dossier A… » et sélectionnez le premier\n");
    text << juce::String::fromUTF8("       dossier (le dossier de référence, en général).\n");
    text << juce::String::fromUTF8("    3. Cliquez sur « Dossier B… » et sélectionnez le second.\n");
    text << juce::String::fromUTF8("    Les deux chemins restent mémorisés d'une session à\n");
    text << juce::String::fromUTF8("    l'autre. Les petits boutons « A » et « B », à droite de\n");
    text << juce::String::fromUTF8("    chaque chemin, ouvrent le dossier dans l'explorateur.\n\n");
    text << juce::String::fromUTF8(" RÉGLER LA COMPARAISON\n");
    text << juce::String::fromUTF8("    - « Sous-dossiers » (coché par défaut) : parcourt aussi\n");
    text << juce::String::fromUTF8("      toute l'arborescence, pas seulement la racine.\n");
    text << juce::String::fromUTF8("    - « Audio seulement » (coché par défaut) : ne retient que\n");
    text << juce::String::fromUTF8("      les fichiers mp3, wav, flac, aac, m4a, ogg, aiff, aif\n");
    text << juce::String::fromUTF8("      et wma. Décochez pour comparer tous les fichiers.\n");
    text << juce::String::fromUTF8("    - La liste déroulante fixe le critère d'identité :\n");
    text << juce::String::fromUTF8("      * « Nom »                 : même chemin relatif.\n");
    text << juce::String::fromUTF8("      * « Nom + taille »        : réglage par défaut.\n");
    text << juce::String::fromUTF8("      * « Nom + taille + date » : le plus strict, avec une\n");
    text << juce::String::fromUTF8("        tolérance de 2 secondes sur la date (disques FAT).\n");
    text << juce::String::fromUTF8("    Les fichiers cachés sont toujours ignorés.\n\n");
    text << juce::String::fromUTF8(" LANCER ET LIRE LE RÉSULTAT\n");
    text << juce::String::fromUTF8("    1. Cliquez sur « Comparer ». Le bouton devient « Stop » :\n");
    text << juce::String::fromUTF8("       vous pouvez interrompre à tout moment.\n");
    text << juce::String::fromUTF8("    2. Le bilan s'affiche en fin d'analyse (A-seul, B-seul,\n");
    text << juce::String::fromUTF8("       différents, identiques).\n");
    text << juce::String::fromUTF8("    3. Les quatre pastilles sous la barre d'options affichent\n");
    text << juce::String::fromUTF8("       le compte par état et servent de filtres :\n");
    text << juce::String::fromUTF8("       * « A seulement »  : présent dans A, absent de B.\n");
    text << juce::String::fromUTF8("       * « B seulement »  : présent dans B, absent de A.\n");
    text << juce::String::fromUTF8("       * « Différents »   : même nom, contenu différent.\n");
    text << juce::String::fromUTF8("       * « Identiques »   : rien à faire.\n");
    text << juce::String::fromUTF8("       Cliquez une pastille pour masquer ou réafficher sa\n");
    text << juce::String::fromUTF8("       catégorie dans la liste.\n");
    text << juce::String::fromUTF8("    Chaque ligne indique le chemin relatif, la taille côté A,\n");
    text << juce::String::fromUTF8("    la taille côté B, puis les dates de modification.\n");
    text << juce::String::fromUTF8("    Un double-clic sur une ligne ouvre le fichier dans\n");
    text << juce::String::fromUTF8("    l'explorateur.\n\n");
    text << juce::String::fromUTF8(" COPIER DANS UN SENS OU DANS L'AUTRE\n");
    text << juce::String::fromUTF8("    1. Sélectionnez les lignes à traiter (Ctrl+clic ou\n");
    text << juce::String::fromUTF8("       Maj+clic). Sans sélection, toutes les lignes affichées\n");
    text << juce::String::fromUTF8("       sont prises en compte.\n");
    text << juce::String::fromUTF8("    2. Cliquez sur « Copier A → B » ou « Copier B → A ».\n");
    text << juce::String::fromUTF8("    3. Confirmez la fenêtre qui annonce le nombre de fichiers.\n");
    text << juce::String::fromUTF8("    Seuls les fichiers pertinents sont traités : « A → B »\n");
    text << juce::String::fromUTF8("    copie les A-seul et les différents, « B → A » copie les\n");
    text << juce::String::fromUTF8("    B-seul et les différents. Les dossiers manquants sont\n");
    text << juce::String::fromUTF8("    créés. Une nouvelle comparaison est relancée à la fin.\n");
    text << juce::String::fromUTF8("    Attention : pour un fichier « Différent », la version de\n");
    text << juce::String::fromUTF8("    destination est supprimée puis remplacée, sans copie de\n");
    text << juce::String::fromUTF8("    sauvegarde. Vérifiez le sens de la copie avant de valider.\n\n");
    text << juce::String::fromUTF8(" GARDER UNE TRACE\n");
    text << juce::String::fromUTF8("    « Rapport CSV » enregistre le tableau complet (colonnes\n");
    text << juce::String::fromUTF8("    État, Fichier, Taille A, Taille B, Date A, Date B), les\n");
    text << juce::String::fromUTF8("    deux chemins comparés figurant sur les premières lignes.\n");
    text << juce::String::fromUTF8("    Le fichier comparaison.csv est proposé sur le Bureau puis\n");
    text << juce::String::fromUTF8("    affiché dans l'explorateur.\n\n");

    text << juce::String::fromUTF8("\n");
    text << juce::String::fromUTF8("============================================================\n");
    text << juce::String::fromUTF8(" ÉDITEUR DE TAGS - Corriger vos métadonnées en lot\n");
    text << juce::String::fromUTF8("============================================================\n\n");
    text << juce::String::fromUTF8(" OUVRIR L'ÉDITEUR\n");
    text << juce::String::fromUTF8("    1. Ouvrez la Bibliothèque.\n");
    text << juce::String::fromUTF8("    2. Cochez les morceaux à corriger (ou « Tout cocher »).\n");
    text << juce::String::fromUTF8("    3. Cliquez sur « Éditeur de tags » ; le bouton affiche le\n");
    text << juce::String::fromUTF8("       nombre de morceaux cochés, par exemple\n");
    text << juce::String::fromUTF8("       « Éditeur de tags (24) ».\n");
    text << juce::String::fromUTF8("    Sans aucune case cochée, tous les morceaux affichés sont\n");
    text << juce::String::fromUTF8("    chargés. « ← Bibliothèque » revient à la liste.\n\n");
    text << juce::String::fromUTF8(" COMPRENDRE LES DEUX SÉLECTIONS\n");
    text << juce::String::fromUTF8("    - Cliquer sur le NOM d'un fichier affiche ses champs à\n");
    text << juce::String::fromUTF8("      droite, pour une édition ligne par ligne.\n");
    text << juce::String::fromUTF8("    - Cocher la CASE à gauche du fichier l'inclut dans le\n");
    text << juce::String::fromUTF8("      lot. Maj+clic sur une case coche toute une plage.\n");
    text << juce::String::fromUTF8("    Toutes les opérations par lot (écriture, casse, nettoyage,\n");
    text << juce::String::fromUTF8("    renommage, pochettes, recherche en ligne) n'agissent que\n");
    text << juce::String::fromUTF8("    sur les fichiers cochés. « Tout cocher » / « Tout\n");
    text << juce::String::fromUTF8("    décocher » traite la liste entière.\n\n");
    text << juce::String::fromUTF8(" MODIFIER LES CHAMPS\n");
    text << juce::String::fromUTF8("    Douze champs sont disponibles à droite : Titre, Artiste,\n");
    text << juce::String::fromUTF8("    Album, Genre, Année, BPM, Clé (8A, Am…), Énergie (1-10),\n");
    text << juce::String::fromUTF8("    Label, Mood, Commentaire et Tags perso (virgules).\n");
    text << juce::String::fromUTF8("    Quand plusieurs morceaux sont sélectionnés et que leurs\n");
    text << juce::String::fromUTF8("    valeurs diffèrent, le champ affiche <multiple> : laissez\n");
    text << juce::String::fromUTF8("    cette mention pour conserver la valeur propre à chaque\n");
    text << juce::String::fromUTF8("    fichier, remplacez-la pour imposer la même valeur à tous.\n");
    text << juce::String::fromUTF8("    Un point orange devant un fichier signale des\n");
    text << juce::String::fromUTF8("    modifications non encore écrites sur le disque.\n\n");
    text << juce::String::fromUTF8(" ENREGISTRER\n");
    text << juce::String::fromUTF8("    Cliquez sur « Enregistrer » (le bouton indique le nombre\n");
    text << juce::String::fromUTF8("    de fichiers concernés). Les tags sont écrits dans les\n");
    text << juce::String::fromUTF8("    fichiers ET dans la base BeatMate. Un fichier en lecture\n");
    text << juce::String::fromUTF8("    seule est signalé comme échec.\n\n");
    text << juce::String::fromUTF8(" OUTILS DE NETTOYAGE\n");
    text << juce::String::fromUTF8("    - « Tags depuis le nom » : découpe le nom de fichier sur\n");
    text << juce::String::fromUTF8("      le séparateur « - » pour remplir Artiste et Titre.\n");
    text << juce::String::fromUTF8("      Vérifiez le résultat, puis enregistrez.\n");
    text << juce::String::fromUTF8("    - « Casse ▾ » : Title Case (Chaque Mot), MAJUSCULES,\n");
    text << juce::String::fromUTF8("      minuscules, Première lettre seulement. S'applique au\n");
    text << juce::String::fromUTF8("      titre, à l'artiste et à l'album.\n");
    text << juce::String::fromUTF8("    - « Nettoyer ▾ » : remplace les tirets bas par des\n");
    text << juce::String::fromUTF8("      espaces, supprime les doubles espaces et les suffixes\n");
    text << juce::String::fromUTF8("      promotionnels (Official Video, HD, Clip Officiel…).\n\n");
    text << juce::String::fromUTF8(" RENOMMER LES FICHIERS\n");
    text << juce::String::fromUTF8("    1. Saisissez le masque dans le champ situé à droite du\n");
    text << juce::String::fromUTF8("       bouton « Enregistrer ». Par défaut : %artist% - %title%\n");
    text << juce::String::fromUTF8("    2. Variables acceptées : %artist%, %title%, %album%,\n");
    text << juce::String::fromUTF8("       %genre%, %year%, %bpm%, %key%.\n");
    text << juce::String::fromUTF8("    3. Cliquez sur « Renommer les fichiers ».\n");
    text << juce::String::fromUTF8("    Les caractères interdits par Windows sont remplacés par\n");
    text << juce::String::fromUTF8("    un tiret. Un fichier dont le nom cible existe déjà est\n");
    text << juce::String::fromUTF8("    ignoré. Le renommage agit immédiatement sur le disque et\n");
    text << juce::String::fromUTF8("    n'est pas annulable : commencez par un seul fichier.\n\n");
    text << juce::String::fromUTF8(" POCHETTES\n");
    text << juce::String::fromUTF8("    Le bouton « Pochette ▾ » propose :\n");
    text << juce::String::fromUTF8("    - « Chercher sur Internet… »\n");
    text << juce::String::fromUTF8("    - « Importer une image… »  (jpg, jpeg, png)\n");
    text << juce::String::fromUTF8("    - « Exporter la pochette… »\n");
    text << juce::String::fromUTF8("    - « Retirer la pochette »\n");
    text << juce::String::fromUTF8("    Avec un seul morceau coché, la recherche affiche jusqu'à\n");
    text << juce::String::fromUTF8("    huit pochettes dans une fenêtre « Choisir une pochette » :\n");
    text << juce::String::fromUTF8("    cliquez sur l'image voulue pour l'appliquer. Avec\n");
    text << juce::String::fromUTF8("    plusieurs morceaux, la meilleure pochette trouvée est\n");
    text << juce::String::fromUTF8("    appliquée automatiquement à chacun. La vignette en haut à\n");
    text << juce::String::fromUTF8("    droite montre la pochette du morceau sélectionné.\n\n");
    text << juce::String::fromUTF8(" COMPLÉTION EN LIGNE\n");
    text << juce::String::fromUTF8("    Le bouton « En ligne ▾ » propose :\n");
    text << juce::String::fromUTF8("    - « Tout automatique (tags + BPM + pochettes) »\n");
    text << juce::String::fromUTF8("    - « Compléter les tags + BPM »\n");
    text << juce::String::fromUTF8("    - « Chercher les pochettes »\n");
    text << juce::String::fromUTF8("    - « Analyser avec BeatMate (BPM précis, clé, énergie,\n");
    text << juce::String::fromUTF8("      mood IA) »\n");
    text << juce::String::fromUTF8("    Les trois premières interrogent Deezer et iTunes. Un\n");
    text << juce::String::fromUTF8("    score de correspondance écarte les résultats douteux :\n");
    text << juce::String::fromUTF8("    les versions karaoké, tribute ou « in the style of » sont\n");
    text << juce::String::fromUTF8("    rejetées, et un morceau non reconnu reste intact plutôt\n");
    text << juce::String::fromUTF8("    que de recevoir un faux tag. Le BPM en ligne n'est\n");
    text << juce::String::fromUTF8("    appliqué que si le morceau n'en a aucun.\n");
    text << juce::String::fromUTF8("    Une fenêtre de progression indique l'avancement et\n");
    text << juce::String::fromUTF8("    comporte un bouton « Annuler ».\n");
    text << juce::String::fromUTF8("    Les valeurs récupérées ne sont pas encore écrites :\n");
    text << juce::String::fromUTF8("    relisez-les, puis cliquez sur « Enregistrer ».\n\n");

    text << juce::String::fromUTF8("\n");
    text << juce::String::fromUTF8("============================================================\n");
    text << juce::String::fromUTF8(" ÉDITEUR AUDIO - Couper, rogner, fondus (Normalisation)\n");
    text << juce::String::fromUTF8("============================================================\n\n");
    text << juce::String::fromUTF8(" OUVRIR UN FICHIER\n");
    text << juce::String::fromUTF8("    1. Ouvrez « Normalisation » dans le menu latéral.\n");
    text << juce::String::fromUTF8("    2. Alimentez la liste avec « Ajouter des pistes… » ou\n");
    text << juce::String::fromUTF8("       « Ajouter tout ».\n");
    text << juce::String::fromUTF8("    3. Sélectionnez la ligne à retoucher.\n");
    text << juce::String::fromUTF8("    4. Cliquez sur « Éditer le fichier ».\n");
    text << juce::String::fromUTF8("    L'éditeur s'ouvre en plein écran par-dessus la vue. Le\n");
    text << juce::String::fromUTF8("    bandeau rappelle le nom du fichier, sa durée, sa fréquence\n");
    text << juce::String::fromUTF8("    d'échantillonnage et mono/stéréo. Un point à côté du titre\n");
    text << juce::String::fromUTF8("    « Éditeur audio » signale des modifications non\n");
    text << juce::String::fromUTF8("    enregistrées.\n\n");
    text << juce::String::fromUTF8(" NAVIGUER ET SÉLECTIONNER\n");
    text << juce::String::fromUTF8("    - Glissez la souris sur la forme d'onde pour sélectionner\n");
    text << juce::String::fromUTF8("      une portion ; un simple clic place le curseur.\n");
    text << juce::String::fromUTF8("    - Ctrl+A sélectionne tout le fichier.\n");
    text << juce::String::fromUTF8("    - La molette zoome autour du pointeur ; les boutons « + »,\n");
    text << juce::String::fromUTF8("      « − » et « Zoom tout » font de même (touches + et -).\n");
    text << juce::String::fromUTF8("    - « ▶ Écouter » (ou la barre d'espace) joue la sélection,\n");
    text << juce::String::fromUTF8("      ou tout depuis le curseur ; le bouton devient « ■ Stop ».\n");
    text << juce::String::fromUTF8("    Le pied de page indique la sélection (début, fin, durée)\n");
    text << juce::String::fromUTF8("    ou la position du curseur.\n\n");
    text << juce::String::fromUTF8(" LIRE LES NIVEAUX\n");
    text << juce::String::fromUTF8("    À droite du pied de page, BeatMate affiche la crête et le\n");
    text << juce::String::fromUTF8("    RMS de la zone analysée, suivis d'un verdict :\n");
    text << juce::String::fromUTF8("    - « trop fort (écrêtage possible) » au-dessus de -1 dB ;\n");
    text << juce::String::fromUTF8("    - « niveau faible » en dessous de -12 dB ;\n");
    text << juce::String::fromUTF8("    - « niveau correct » entre les deux.\n");
    text << juce::String::fromUTF8("    Les bandes rouges en haut et en bas de la forme d'onde\n");
    text << juce::String::fromUTF8("    matérialisent la zone d'écrêtage.\n\n");
    text << juce::String::fromUTF8(" LES OUTILS D'ÉDITION\n");
    text << juce::String::fromUTF8("    - « Couper » (touche Suppr) : supprime la sélection et\n");
    text << juce::String::fromUTF8("      recolle le reste.\n");
    text << juce::String::fromUTF8("    - « Rogner » : ne conserve que la sélection.\n");
    text << juce::String::fromUTF8("    - « Silence » : remplace la sélection par du silence.\n");
    text << juce::String::fromUTF8("    - « Gain… » : ouvre une fenêtre, saisissez la valeur en\n");
    text << juce::String::fromUTF8("      dB (négatif pour baisser), puis « Appliquer ».\n");
    text << juce::String::fromUTF8("    - « Fondu ↗ ▾ » et « Fondu ↘ ▾ » : choisissez la courbe\n");
    text << juce::String::fromUTF8("      parmi Linéaire, Puissance constante (recommandé),\n");
    text << juce::String::fromUTF8("      Exponentiel (départ doux), Logarithmique (départ\n");
    text << juce::String::fromUTF8("      rapide), Courbe en S (progressif), Rapide, Lent.\n");
    text << juce::String::fromUTF8("    - « Normaliser » : remonte la crête du fichier entier à\n");
    text << juce::String::fromUTF8("      -0,3 dBFS ; le gain appliqué est indiqué.\n");
    text << juce::String::fromUTF8("    - « Inverser » : lecture inversée de la sélection.\n");
    text << juce::String::fromUTF8("    « Couper » et « Rogner » exigent une sélection. Les\n");
    text << juce::String::fromUTF8("    autres outils s'appliquent au fichier entier lorsqu'il n'y\n");
    text << juce::String::fromUTF8("    a pas de sélection.\n\n");
    text << juce::String::fromUTF8(" ANNULER\n");
    text << juce::String::fromUTF8("    « ↶ Annuler » (Ctrl+Z) et « ↷ Rétablir » (Ctrl+Y)\n");
    text << juce::String::fromUTF8("    remontent jusqu'à 40 opérations. Le bouton Annuler\n");
    text << juce::String::fromUTF8("    affiche le nombre d'étapes disponibles.\n\n");
    text << juce::String::fromUTF8(" ENREGISTRER LE RÉSULTAT\n");
    text << juce::String::fromUTF8("    Deux possibilités, en bas à droite :\n");
    text << juce::String::fromUTF8("    - « Enregistrer sous… » : crée un nouveau fichier,\n");
    text << juce::String::fromUTF8("      proposé sous le nom « <fichier> (edit) » dans le même\n");
    text << juce::String::fromUTF8("      dossier. Formats wav, mp3, flac, ogg.\n");
    text << juce::String::fromUTF8("    - « Remplacer l'original » : après confirmation, le\n");
    text << juce::String::fromUTF8("      fichier d'origine est d'abord copié dans le dossier\n");
    text << juce::String::fromUTF8("      audio_backups (dossier BeatMate de vos données\n");
    text << juce::String::fromUTF8("      d'application), horodaté, puis remplacé par la version\n");
    text << juce::String::fromUTF8("      éditée. Le nom de la sauvegarde est rappelé à la fin.\n");
    text << juce::String::fromUTF8("    L'export se fait en 24 bits, et en 320 kbps pour le MP3.\n");
    text << juce::String::fromUTF8("    « ← Retour » ferme l'éditeur et arrête la lecture.\n");
    text << juce::String::fromUTF8("    Attention : les modifications non enregistrées sont\n");
    text << juce::String::fromUTF8("    perdues à la fermeture.\n\n");

    text << juce::String::fromUTF8("\n");
    text << juce::String::fromUTF8("============================================================\n");
    text << juce::String::fromUTF8(" INTÉGRITÉ DES FICHIERS - Vérifier et réparer\n");
    text << juce::String::fromUTF8("============================================================\n\n");
    text << juce::String::fromUTF8("    Ce module détecte les fichiers audio tronqués, corrompus\n");
    text << juce::String::fromUTF8("    ou illisibles, puis les répare sans réencodage.\n\n");
    text << juce::String::fromUTF8(" LANCER UN CONTRÔLE\n");
    text << juce::String::fromUTF8("    1. Ouvrez la Bibliothèque.\n");
    text << juce::String::fromUTF8("    2. Cliquez sur « Vérifier / Réparer ▾ ».\n");
    text << juce::String::fromUTF8("    3. Choisissez l'étendue du contrôle :\n");
    text << juce::String::fromUTF8("       * « La sélection (n) »        : les lignes sélectionnées\n");
    text << juce::String::fromUTF8("       * « Les morceaux affichés (n) » : le résultat des\n");
    text << juce::String::fromUTF8("         filtres en cours\n");
    text << juce::String::fromUTF8("       * « Toute la bibliothèque »\n");
    text << juce::String::fromUTF8("    La fenêtre « Intégrité des fichiers » s'ouvre et vérifie\n");
    text << juce::String::fromUTF8("    immédiatement les morceaux encore non contrôlés, puis\n");
    text << juce::String::fromUTF8("    bascule d'elle-même sur les problèmes détectés.\n");
    text << juce::String::fromUTF8("    Lorsque la facette « État » de la bibliothèque est réglée\n");
    text << juce::String::fromUTF8("    sur les fichiers corrompus, un bouton « Réparer les\n");
    text << juce::String::fromUTF8("    corrompus » apparaît et ouvre directement cette fenêtre.\n\n");
    text << juce::String::fromUTF8(" LIRE LES ÉTATS\n");
    text << juce::String::fromUTF8("    - « Intact »       : décodage complet sans erreur.\n");
    text << juce::String::fromUTF8("    - « Suspect »      : décodable, mais avec des\n");
    text << juce::String::fromUTF8("      avertissements (trames abîmées, fin douteuse).\n");
    text << juce::String::fromUTF8("    - « Corrompu »     : le décodage échoue.\n");
    text << juce::String::fromUTF8("    - « Illisible »    : fichier introuvable ou inaccessible.\n");
    text << juce::String::fromUTF8("    - « Réparé »       : remis en état par BeatMate.\n");
    text << juce::String::fromUTF8("    - « Non vérifié »  : pas encore contrôlé.\n");
    text << juce::String::fromUTF8("    L'en-tête récapitule le nombre de morceaux intacts,\n");
    text << juce::String::fromUTF8("    suspects, corrompus et non vérifiés. Le détail de\n");
    text << juce::String::fromUTF8("    l'erreur s'affiche sous le titre de chaque ligne.\n\n");
    text << juce::String::fromUTF8(" FILTRER ET COCHER\n");
    text << juce::String::fromUTF8("    Les quatre cases du haut (« Corrompus », « Suspects »,\n");
    text << juce::String::fromUTF8("    « Non vérifiés », « Intacts ») affichent ou masquent les\n");
    text << juce::String::fromUTF8("    catégories. Pour choisir les fichiers à traiter :\n");
    text << juce::String::fromUTF8("    - cliquez sur la case à gauche d'une ligne ;\n");
    text << juce::String::fromUTF8("    - ou utilisez « Tout cocher », « Tout décocher »,\n");
    text << juce::String::fromUTF8("      « Cocher les corrompus ».\n");
    text << juce::String::fromUTF8("    « Vérifier » relance le contrôle sur les fichiers cochés\n");
    text << juce::String::fromUTF8("    et devient « Arrêter » pendant le travail. Une barre de\n");
    text << juce::String::fromUTF8("    progression et le nom du fichier en cours s'affichent en\n");
    text << juce::String::fromUTF8("    bas de la fenêtre.\n\n");
    text << juce::String::fromUTF8(" RÉPARER\n");
    text << juce::String::fromUTF8("    1. Cochez les fichiers corrompus ou illisibles à traiter\n");
    text << juce::String::fromUTF8("       (« Cocher les corrompus » va au plus vite).\n");
    text << juce::String::fromUTF8("    2. Cliquez sur « Réparer (n) ». Le bouton reste inactif\n");
    text << juce::String::fromUTF8("       tant qu'aucun fichier réparable n'est coché.\n");
    text << juce::String::fromUTF8("    3. Confirmez la fenêtre d'avertissement.\n");
    text << juce::String::fromUTF8("    Chaque original est copié, horodaté, dans le dossier\n");
    text << juce::String::fromUTF8("    integrity_backups (dossier BeatMate de vos données\n");
    text << juce::String::fromUTF8("    d'application) AVANT toute modification. Le fichier est\n");
    text << juce::String::fromUTF8("    ensuite reconstruit sans réencodage : la qualité audio\n");
    text << juce::String::fromUTF8("    est conservée. BeatMate le revérifie aussitôt et le passe\n");
    text << juce::String::fromUTF8("    en « Réparé » si le résultat est propre.\n");
    text << juce::String::fromUTF8("    Si la réparation ne produit rien d'exploitable, le\n");
    text << juce::String::fromUTF8("    fichier d'origine reste en place, intouché.\n\n");
    text << juce::String::fromUTF8(" CONSULTER ET EXPORTER\n");
    text << juce::String::fromUTF8("    - « Ouvrir le dossier » (ou un double-clic sur la ligne)\n");
    text << juce::String::fromUTF8("      montre le fichier dans l'explorateur.\n");
    text << juce::String::fromUTF8("    - « Rapport CSV » enregistre les colonnes Statut, Titre,\n");
    text << juce::String::fromUTF8("      Fichier, Détails dans integrite-beatmate.csv, proposé\n");
    text << juce::String::fromUTF8("      sur le Bureau.\n");
    text << juce::String::fromUTF8("    - « Fermer » quitte la fenêtre et interrompt le travail\n");
    text << juce::String::fromUTF8("      en cours.\n");
    text << juce::String::fromUTF8("    Les résultats sont conservés d'une session à l'autre. Si\n");
    text << juce::String::fromUTF8("    un fichier change de taille ou de date, son résultat est\n");
    text << juce::String::fromUTF8("    invalidé et il sera revérifié.\n");
    text << juce::String::fromUTF8("    Si le message « L'outil d'analyse audio est introuvable »\n");
    text << juce::String::fromUTF8("    apparaît, réinstallez BeatMate : le moteur de décodage\n");
    text << juce::String::fromUTF8("    livré avec la suite est absent.\n\n");

    text << juce::String::fromUTF8("\n");
    text << juce::String::fromUTF8("============================================================\n");
    text << juce::String::fromUTF8(" MIX STUDIO - Construire un mix sur une timeline\n");
    text << juce::String::fromUTF8("============================================================\n\n");
    text << juce::String::fromUTF8(" LANCER MIX STUDIO\n");
    text << juce::String::fromUTF8("    Cliquez sur « Mix » dans le menu latéral. Mix Studio est\n");
    text << juce::String::fromUTF8("    une application distincte : elle s'ouvre dans sa propre\n");
    text << juce::String::fromUTF8("    fenêtre et nécessite la version Professional ou Premium.\n");
    text << juce::String::fromUTF8("    Le message « Module introuvable » signifie que\n");
    text << juce::String::fromUTF8("    l'application n'est pas installée : réinstallez la suite.\n\n");
    text << juce::String::fromUTF8(" LES MENUS\n");
    text << juce::String::fromUTF8("    Six menus : « Fichier », « Edition », « Effets »,\n");
    text << juce::String::fromUTF8("    « Licence », « Mises a jour », « Aide ».\n");
    text << juce::String::fromUTF8("    Fichier réunit « Nouveau », « Ouvrir... », « Recents »,\n");
    text << juce::String::fromUTF8("    « Enregistrer... », « Exporter le mix... », « Importer\n");
    text << juce::String::fromUTF8("    des morceaux... » et « Enregistrer une voix off... ».\n\n");
    text << juce::String::fromUTF8(" MONTER UN MIX\n");
    text << juce::String::fromUTF8("    1. Cliquez sur « Importer » (barre du haut) ou glissez\n");
    text << juce::String::fromUTF8("       vos fichiers dans la fenêtre.\n");
    text << juce::String::fromUTF8("    2. Le bouton « BIBLIOTHEQUE » ouvre un panneau latéral :\n");
    text << juce::String::fromUTF8("       glissez un morceau vers la timeline pour l'ajouter.\n");
    text << juce::String::fromUTF8("    3. L'onglet vertical « SET » affiche la liste du set.\n");
    text << juce::String::fromUTF8("    4. Déplacez les clips à la souris ; tirez leurs bords\n");
    text << juce::String::fromUTF8("       pour les rogner (Alt pour un rognage libre).\n");
    text << juce::String::fromUTF8("    5. « Auto-mix » ordonne et enchaîne tout le set.\n");
    text << juce::String::fromUTF8("    Le bouton « Snap » aimante les clips sur la grille\n");
    text << juce::String::fromUTF8("    (touche G ; Shift+G change la granularité : 1/4, 1/2,\n");
    text << juce::String::fromUTF8("    1 temps, 1 mesure).\n\n");
    text << juce::String::fromUTF8(" TEMPO ET VOLUME\n");
    text << juce::String::fromUTF8("    Le champ BPM de la barre du haut a deux comportements :\n");
    text << juce::String::fromUTF8("    avec un morceau sélectionné, il accélère ou ralentit ce\n");
    text << juce::String::fromUTF8("    morceau, clé verrouillée (master tempo) ; sans sélection,\n");
    text << juce::String::fromUTF8("    il fixe le tempo du projet, donc la grille.\n");
    text << juce::String::fromUTF8("    Le curseur « MASTER » règle le volume général ; un\n");
    text << juce::String::fromUTF8("    double-clic le remet à sa valeur d'origine.\n\n");
    text << juce::String::fromUTF8(" TRANSITIONS ET EFFETS\n");
    text << juce::String::fromUTF8("    - Cliquez sur la bande de transition entre deux clips\n");
    text << juce::String::fromUTF8("      pour ouvrir l'éditeur de transition. Double-cliquez sur\n");
    text << juce::String::fromUTF8("      la courbe pour ajouter ou supprimer un point ;\n");
    text << juce::String::fromUTF8("      Alt+glisser courbe le segment.\n");
    text << juce::String::fromUTF8("    - Le menu « Effets » s'applique à la région ou au clip\n");
    text << juce::String::fromUTF8("      sélectionné : sélectionnez-le d'abord, sinon les\n");
    text << juce::String::fromUTF8("      familles d'effets restent grisées. Shift + glisser à\n");
    text << juce::String::fromUTF8("      l'intérieur d'un clip limite l'effet à cette portion.\n");
    text << juce::String::fromUTF8("    - « Retirer les effets » nettoie la cible ; « Comment ca\n");
    text << juce::String::fromUTF8("      marche... » rappelle la procédure.\n\n");
    text << juce::String::fromUTF8(" GÉNÉRATION ASSISTÉE (IA : GÉNÉRER)\n");
    text << juce::String::fromUTF8("    1. Sélectionnez les clips à utiliser (sans sélection,\n");
    text << juce::String::fromUTF8("       tout le set sert de source).\n");
    text << juce::String::fromUTF8("    2. Cliquez sur « IA : générer », puis choisissez dans\n");
    text << juce::String::fromUTF8("       « Quoi générer » :\n");
    text << juce::String::fromUTF8("       * Medley  - les meilleurs passages, enchaînés\n");
    text << juce::String::fromUTF8("       * Mashup  - la voix d'un titre sur l'instru d'un autre\n");
    text << juce::String::fromUTF8("       * Remix   - un titre réarrangé (structure club)\n");
    text << juce::String::fromUTF8("       * Megamix - hooks courts, tempo unifié, cuts rapides\n");
    text << juce::String::fromUTF8("    3. Réglez la longueur : « Longueur des extraits » (16,\n");
    text << juce::String::fromUTF8("       32 ou 64 mesures), « Longueur des blocs (remix) »\n");
    text << juce::String::fromUTF8("       (8 ou 16), « Longueur des accroches (megamix) »\n");
    text << juce::String::fromUTF8("       (8, 16 ou 24). Cliquez sur « Générer ».\n");
    text << juce::String::fromUTF8("    Le mashup exige les stems des deux morceaux : s'ils\n");
    text << juce::String::fromUTF8("    manquent, BeatMate propose de les « Séparer » d'abord.\n");
    text << juce::String::fromUTF8("    Le résultat s'ajoute à la suite du mix ; Ctrl+Z annule.\n\n");
    text << juce::String::fromUTF8(" EXPORTER LE MIX\n");
    text << juce::String::fromUTF8("    1. Menu « Fichier » › « Exporter le mix... ».\n");
    text << juce::String::fromUTF8("    2. Réglez le format (WAV, MP3, FLAC, OGG), la fréquence\n");
    text << juce::String::fromUTF8("       (44100, 48000, 96000 Hz), la résolution (16 ou\n");
    text << juce::String::fromUTF8("       24 bits) ou le débit (192, 256, 320 kbps).\n");
    text << juce::String::fromUTF8("    3. Choisissez la plage : « Tout le mix » ou « Boucle /\n");
    text << juce::String::fromUTF8("       selection ».\n");
    text << juce::String::fromUTF8("    4. Au besoin, cochez « Normaliser le volume (LUFS) » et\n");
    text << juce::String::fromUTF8("       la cible : -9 LUFS (club), -14 LUFS (streaming) ou\n");
    text << juce::String::fromUTF8("       -16 LUFS (podcast), et « Exporter une cue sheet\n");
    text << juce::String::fromUTF8("       (.cue) ».\n");
    text << juce::String::fromUTF8("    5. Définissez « Destination... », puis « Exporter ».\n");
    text << juce::String::fromUTF8("    La même fenêtre exporte la liste du set : « M3U8 »,\n");
    text << juce::String::fromUTF8("    « Rekordbox XML », « Tracklist TXT/CSV », « Serato CSV »\n");
    text << juce::String::fromUTF8("    et « Engine / Traktor / VirtualDJ », en « Hot cues » ou\n");
    text << juce::String::fromUTF8("    en « Memory cues ».\n\n");
    text << juce::String::fromUTF8(" RACCOURCIS ESSENTIELS\n");
    text << juce::String::fromUTF8("    Espace          Lecture / pause\n");
    text << juce::String::fromUTF8("    Molette         Zoom (ancré sous la souris)\n");
    text << juce::String::fromUTF8("    Ctrl+C / X / V  Copier / couper / coller\n");
    text << juce::String::fromUTF8("    Ctrl+D          Dupliquer\n");
    text << juce::String::fromUTF8("    Suppr           Supprimer la sélection\n");
    text << juce::String::fromUTF8("    S               Scinder à la tête de lecture\n");
    text << juce::String::fromUTF8("    Ctrl+Z / Ctrl+Y Annuler / rétablir\n");
    text << juce::String::fromUTF8("    G / Shift+G     Snap on-off / granularité\n");
    text << juce::String::fromUTF8("    1 à 8           Poser un hot cue (Shift+1-8 : y sauter)\n");
    text << juce::String::fromUTF8("    L / Shift+L     Boucle de 4 ou 8 temps\n");
    text << juce::String::fromUTF8("    Flèches G/D     Saut d'un temps (Shift : quatre temps)\n");
    text << juce::String::fromUTF8("    La liste complète est dans « Aide » › « Raccourcis\n");
    text << juce::String::fromUTF8("    clavier... ».\n\n");
    text << juce::String::fromUTF8(" VOIX OFF\n");
    text << juce::String::fromUTF8("    Menu « Fichier » › « Enregistrer une voix off... » : le\n");
    text << juce::String::fromUTF8("    micro démarre après un décompte et la musique est\n");
    text << juce::String::fromUTF8("    automatiquement atténuée sous la voix.\n\n");

    text << "============================================================\n";
    text << " FORMATS AUDIO SUPPORTES\n";
    text << "============================================================\n\n";

    text << "   Format    Extension    Qualité           Notes\n";
    text << "   -------   ---------    --------          -----\n";
    text << "   MP3       .mp3         Lossy             Format le plus repandu\n";
    text << "   WAV       .wav         Lossless          Qualité CD, fichiers volumineux\n";
    text << "   FLAC      .flac        Lossless compresse Ideal : qualité + taille reduite\n";
    text << "   OGG       .ogg         Lossy             Alternative open-source au MP3\n";
    text << "   AAC       .aac/.m4a    Lossy             Meilleur que MP3 a debit egal\n";
    text << "   AIFF      .aiff/.aif   Lossless          Standard Apple, qualité CD\n";
    text << "   WMA       .wma         Lossy             Format Windows (support basique)\n\n";

    text << "   Note : Les fichiers proteges par DRM ne sont pas supportes.\n\n";

    text << "============================================================\n";
    text << " BeatMate V12 Professional - par Sebastien Sainte-Foi\n";
    text << " (c) 2024-2026 BeatMate. Tous droits reserves.\n";
    text << "============================================================\n";

    te->setText(text, false);
    return te;
}

juce::Component* HelpView::createShortcutsTab()
{
    auto* page = new juce::Component();

    m_shortcutsModel.entries = {
        { juce::String::fromUTF8("Navigation"), juce::String::fromUTF8("Ctrl+I"), juce::String::fromUTF8("Ouvrir le module Import") },
        { juce::String::fromUTF8("Navigation"), juce::String::fromUTF8("Ctrl+F"), juce::String::fromUTF8("Ouvrir la Bibliothèque") },
        { juce::String::fromUTF8("Navigation"), juce::String::fromUTF8("Ctrl+E"), juce::String::fromUTF8("Ouvrir le module Export") },
        { juce::String::fromUTF8("Navigation"), juce::String::fromUTF8("Ctrl+L"), juce::String::fromUTF8("Ouvrir le module Live") },
        { juce::String::fromUTF8("Navigation"), juce::String::fromUTF8("F1"), juce::String::fromUTF8("Ouvrir l'Aide") },
        { juce::String::fromUTF8("Navigation"), juce::String::fromUTF8("F5"), juce::String::fromUTF8("Rafraîchir l'affichage") },
        { juce::String::fromUTF8("Bibliothèque"), juce::String::fromUTF8("Ctrl+F"), juce::String::fromUTF8("Placer le curseur dans la recherche") },
        { juce::String::fromUTF8("Bibliothèque"), juce::String::fromUTF8("Ctrl+A"), juce::String::fromUTF8("Cocher et sélectionner tous les morceaux affichés") },
        { juce::String::fromUTF8("Bibliothèque"), juce::String::fromUTF8("Ctrl+D"), juce::String::fromUTF8("Tout décocher et désélectionner") },
        { juce::String::fromUTF8("Bibliothèque"), juce::String::fromUTF8("Ctrl+E"), juce::String::fromUTF8("Ouvrir l'éditeur de tags") },
        { juce::String::fromUTF8("Bibliothèque"), juce::String::fromUTF8("Espace"), juce::String::fromUTF8("Préécouter le morceau sélectionné") },
        { juce::String::fromUTF8("Bibliothèque"), juce::String::fromUTF8("Suppr"), juce::String::fromUTF8("Retirer les morceaux sélectionnés (confirmation)") },
        { juce::String::fromUTF8("Hot Cues"), juce::String::fromUTF8("1 à 8"), juce::String::fromUTF8("Poser le hot cue s'il est vide, sinon le jouer") },
        { juce::String::fromUTF8("Hot Cues"), juce::String::fromUTF8("Q"), juce::String::fromUTF8("Activer / désactiver la quantification") },
        { juce::String::fromUTF8("Hot Cues"), juce::String::fromUTF8("Gauche / Droite"), juce::String::fromUTF8("Décaler le hot cue sélectionné de 10 ms") },
        { juce::String::fromUTF8("Hot Cues"), juce::String::fromUTF8("Maj+Gauche / Maj+Droite"), juce::String::fromUTF8("Décaler le hot cue d'un temps") },
        { juce::String::fromUTF8("Hot Cues"), juce::String::fromUTF8("Suppr"), juce::String::fromUTF8("Effacer le hot cue sélectionné") },
        { juce::String::fromUTF8("Hot Cues"), juce::String::fromUTF8("Espace"), juce::String::fromUTF8("Lecture / Pause") },
        { juce::String::fromUTF8("Éditeur audio"), juce::String::fromUTF8("Espace"), juce::String::fromUTF8("Écouter la sélection") },
        { juce::String::fromUTF8("Éditeur audio"), juce::String::fromUTF8("Suppr"), juce::String::fromUTF8("Couper la sélection") },
        { juce::String::fromUTF8("Éditeur audio"), juce::String::fromUTF8("Retour arrière"), juce::String::fromUTF8("Couper la sélection") },
        { juce::String::fromUTF8("Éditeur audio"), juce::String::fromUTF8("+"), juce::String::fromUTF8("Zoom avant") },
        { juce::String::fromUTF8("Éditeur audio"), juce::String::fromUTF8("-"), juce::String::fromUTF8("Zoom arrière") },
        { juce::String::fromUTF8("Éditeur audio"), juce::String::fromUTF8("Ctrl+Z"), juce::String::fromUTF8("Annuler la dernière modification") },
        { juce::String::fromUTF8("Éditeur audio"), juce::String::fromUTF8("Ctrl+Y"), juce::String::fromUTF8("Rétablir la modification annulée") },
        { juce::String::fromUTF8("Éditeur audio"), juce::String::fromUTF8("Ctrl+A"), juce::String::fromUTF8("Sélectionner tout le fichier") },
        { juce::String::fromUTF8("Fenêtres"), juce::String::fromUTF8("Entrée"), juce::String::fromUTF8("Valider la fenêtre de dialogue") },
        { juce::String::fromUTF8("Fenêtres"), juce::String::fromUTF8("Échap"), juce::String::fromUTF8("Annuler / fermer la fenêtre de dialogue") },
    };

    m_shortcutsTable = std::make_unique<juce::TableListBox>("shortcuts", &m_shortcutsModel);
    m_shortcutsTable->setColour(juce::ListBox::backgroundColourId, Colors::bgDark());
    m_shortcutsTable->setColour(juce::ListBox::outlineColourId, Colors::border());
    m_shortcutsTable->setRowHeight(28);
    m_shortcutsTable->getHeader().addColumn("Categorie", 1, 130, 80, 200);
    m_shortcutsTable->getHeader().addColumn("Raccourci", 2, 180, 120, 250);
    m_shortcutsTable->getHeader().addColumn("Action",    3, 320, 200, 500);
    m_shortcutsTable->getHeader().setColour(juce::TableHeaderComponent::backgroundColourId, Colors::bgLighter());
    m_shortcutsTable->getHeader().setColour(juce::TableHeaderComponent::textColourId, Colors::textPrimary());
    page->addAndMakeVisible(*m_shortcutsTable);

    return page;
}

juce::Component* HelpView::createFAQTab()
{
    auto* page = new juce::Component();

    m_faqContainer = std::make_unique<FAQContainer>();
    m_faqViewport = std::make_unique<juce::Viewport>();
    m_faqViewport->setViewedComponent(m_faqContainer.get(), false);
    m_faqViewport->setScrollBarsShown(true, false);
    m_faqViewport->setColour(juce::ScrollBar::thumbColourId, Colors::bgLighter());
    page->addAndMakeVisible(*m_faqViewport);

    struct FAQ { const char* q; const char* a; };
    FAQ faqs[] = {
        { "Q1 : Comment importer ma musique ?",
          "Vous avez trois méthodes pour importer votre musique dans BeatMate V12 :\n\n"
          "1. Glisser-déposer (Drag & Drop) :\n"
          "   - Ouvrez l'Explorateur Windows\n"
          "   - Sélectionnez vos fichiers ou dossiers audio\n"
          "   - Faites-les glisser directement dans la fenêtre BeatMate\n"
          "   - BeatMate importe les fichiers et crée les entrées dans la base de données\n\n"
          "2. Bouton Importer (Ctrl+I) :\n"
          "   - Utilisez le raccourci Ctrl+I ou le bouton « Importer »\n"
          "   - Parcourez et sélectionnez un ou plusieurs dossiers\n"
          "   - BeatMate scanne récursivement tous les sous-dossiers\n\n"
          "3. Dossiers surveillés (automatique) :\n"
          "   - Configurez dans Paramètres > Bibliothèque > Dossiers surveillés\n"
          "   - BeatMate détecte automatiquement les nouveaux fichiers ajoutés\n"
          "   - Idéal pour les DJs qui téléchargent régulièrement de la musique\n\n"
          "L'import est toujours non-destructif : vos fichiers originaux ne sont jamais modifiés." },

        { "Q2 : Quels formats audio sont pris en charge ?",
          "BeatMate V12 prend en charge les formats audio suivants :\n\n"
          "- MP3 (.mp3) : format le plus répandu, lossy, 128-320 kbps\n"
          "- WAV (.wav) : lossless, qualité CD, fichiers volumineux\n"
          "- FLAC (.flac) : lossless compressé, idéal qualité/taille\n"
          "- AIFF (.aiff, .aif) : lossless, standard Apple\n"
          "- AAC (.aac, .m4a) : lossy, meilleur que MP3 à débit égal\n"
          "- OGG (.ogg) : lossy, alternative open source\n"
          "- WMA (.wma) : format Windows, prise en charge basique\n\n"
          "Note importante : les fichiers protégés par DRM (Digital Rights Management)\n"
          "ne sont pas pris en charge. Cela inclut certains fichiers achetés sur des\n"
          "plateformes anciennes avec protection anti-copie." },

        { "Q3 : Comment fonctionne la détection de BPM ?",
          "BeatMate utilise un algorithme multi-bandes avancé pour détecter le BPM :\n\n"
          "1. Décomposition spectrale du signal audio en bandes de fréquence\n"
          "2. Détection des transitoires (attaques) dans chaque bande\n"
          "3. Analyse de l'enveloppe d'amplitude et des motifs rythmiques\n"
          "4. Corrélation croisée pour déterminer la périodicité dominante\n"
          "5. Vérification par analyse de la grille de beats\n\n"
          "Précision : 0,1 BPM en mode Standard, encore meilleure en mode Haute/Ultra.\n"
          "BeatMate détecte aussi les changements de tempo (BPM dynamique) et peut\n"
          "gérer les morceaux à tempo variable (enregistrements live, etc.).\n\n"
          "Quatre niveaux de qualité sont disponibles dans Paramètres > Analyse :\n"
          "- Rapide : quelques secondes par piste, précision correcte\n"
          "- Standard : bonne précision, recommandé pour la plupart des cas\n"
          "- Haute : précision maximale, plus long\n"
          "- Ultra : analyse multi-passe, pour les perfectionnistes" },

        { "Q4 : Qu'est-ce que le Camelot Wheel et le mix harmonique ?",
          "Le Camelot Wheel est un système de notation des tonalités pour les DJs.\n"
          "Il simplifie la théorie musicale en attribuant un code de 1A à 12B :\n\n"
          "Règles de compatibilité :\n"
          "- Même code = parfaitement compatible (ex. : 8A -> 8A)\n"
          "- +/- 1 dans le même cercle = transition fluide (ex. : 8A -> 7A ou 9A)\n"
          "- A <-> B avec le même numéro = relatif majeur/mineur (ex. : 8A <-> 8B)\n"
          "- +7 demi-tons = « Energy Boost » (ex. : 8A -> 3A)\n\n"
          "Exemple concret :\n"
          "Si votre piste actuelle est en 8A (Am - La mineur), les meilleures\n"
          "transitions sont vers 7A, 9A, 8B ou 3A (energy boost).\n\n"
          "BeatMate affiche les codes Camelot directement dans la bibliothèque\n"
          "et calcule automatiquement la compatibilité dans les suggestions." },

        { "Q5 : Comment préparer un set DJ professionnel ?",
          "Voici la méthode professionnelle pour préparer un set avec BeatMate :\n\n"
          "1. Définissez le contexte :\n"
          "   - Durée du set (1 h, 2 h, 4 h...)\n"
          "   - Style de musique\n"
          "   - Type d'événement (club, festival, bar, privé)\n\n"
          "2. Sélectionnez vos pistes :\n"
          "   - Filtrez votre bibliothèque par genre/BPM/énergie\n"
          "   - Prévoyez 2 à 3 fois plus de pistes que nécessaire\n"
          "   - Règle : environ 15 pistes par heure pour la house\n\n"
          "3. Créez votre set (Ctrl+N > Set) :\n"
          "   - Glissez les pistes dans l'ordre souhaité\n"
          "   - Vérifiez les indicateurs de compatibilité BPM/clé\n"
          "   - Utilisez « IA Auto-Arrange » pour optimiser l'ordre\n\n"
          "4. Affinez :\n"
          "   - Vérifiez la courbe d'énergie (montée progressive)\n"
          "   - Ajustez les points problématiques (transitions rouges)\n"
          "   - Préparez des alternatives pour chaque moment clé\n\n"
          "5. Exportez :\n"
          "   - Ctrl+E > Export USB ou Export logiciel DJ\n"
          "   - Testez sur votre équipement avant le jour J" },

        { "Q6 : Comment utiliser Live Suggest pendant un mix ?",
          "Live Suggest est votre assistant intelligent en temps réel :\n\n"
          "1. Avant le mix :\n"
          "   - Lancez Live Suggest (Ctrl+L)\n"
          "   - Sélectionnez votre playlist source\n"
          "   - Choisissez le mode : Mon Style ou Smart AI\n\n"
          "2. Pendant le mix :\n"
          "   - BeatMate détecte la piste en cours de lecture\n"
          "   - 5 suggestions apparaissent avec score de compatibilité\n"
          "   - La courbe d'énergie de votre set se dessine en temps réel\n"
          "   - L'historique des pistes jouées est enregistré\n\n"
          "3. Modes de suggestion :\n"
          "   - Mon Style : analyse vos habitudes de mix et suggère\n"
          "     des pistes selon votre style personnel\n"
          "   - Smart AI : algorithme optimisé pour les transitions\n"
          "     idéales en termes de BPM, Key et Energy\n\n"
          "4. Astuces :\n"
          "   - Filtrez les suggestions par genre si vous changez de style\n"
          "   - Utilisez le bouton « skip » pour écarter une suggestion\n"
          "   - Consultez l'historique pour éviter les répétitions" },

        { "Q7 : Comment préparer une clé USB pour mes platines ?",
          "Ouvrez l'onglet Export, puis :\n\n"
          "1. Cochez les morceaux à exporter dans la Bibliothèque.\n"
          "2. Choisissez le format, la qualité, la fréquence et la\n"
          "   profondeur de bits.\n"
          "3. Cochez « Normaliser » et fixez la cible LUFS si vous voulez\n"
          "   des niveaux homogènes sur toute la clé.\n"
          "4. Choisissez la structure de dossiers :\n"
          "   - Générique : tous les fichiers à la racine du dossier\n"
          "   - Rekordbox : sous-dossier Contents/Tracks\n"
          "   - Engine DJ : sous-dossier Engine Library/Music\n"
          "5. Cochez « Écrire les tags » et « Créer un fichier M3U » pour\n"
          "   que la playlist soit reconnue.\n"
          "6. Vérifiez la taille estimée affichée avant de lancer.\n\n"
          "Important : BeatMate copie les fichiers et écrit la playlist,\n"
          "mais ne génère pas la base analysée du CDJ. Pour que les\n"
          "platines affichent les formes d'onde et les hot cues, importez\n"
          "ensuite la clé dans rekordbox ou Engine DJ, ou exportez\n"
          "directement vers ce logiciel depuis la Bibliothèque." },

        { "Q8 : Comment connecter mon logiciel DJ (Rekordbox, Serato, Traktor) ?",
          "BeatMate détecte et se synchronise avec vos logiciels DJ :\n\n"
          "1. Allez dans Paramètres > DJ Software.\n"
          "2. Cliquez sur « Scanner maintenant » pour détecter les logiciels.\n"
          "3. Les logiciels détectés s'affichent en vert.\n\n"
          "Logiciels pris en charge :\n"
          "- Rekordbox : XML, hot cues, grilles\n"
          "- Serato DJ : crates, tags, BPM, clé\n"
          "- Traktor Pro : NML, points de cue, tempo\n"
          "- VirtualDJ : base de données, POI\n"
          "- Engine DJ (Denon) : base, hot cues\n\n"
          "Depuis la Bibliothèque, le bouton d'export envoie directement\n"
          "la sélection vers le logiciel voulu, sans passer par l'onglet\n"
          "Export.\n\n"
          "BeatMate lit les analyses déjà faites par ces logiciels et les\n"
          "reprend à son compte, en respectant l'ordre de priorité\n"
          "rekordbox, Serato, Engine DJ, Traktor, VirtualDJ." },

        { "Q9 : Puis-je corriger le BPM ou la clé manuellement ?",
          "Oui, de deux façons.\n\n"
          "Pour un seul morceau, ouvrez l'Éditeur de tags depuis la\n"
          "Bibliothèque : les champs BPM, Clé et Énergie sont modifiables\n"
          "directement, et « Écrire les tags » enregistre la correction\n"
          "dans la base et dans le fichier.\n\n"
          "Pour plusieurs morceaux d'un coup, cochez-les dans la\n"
          "Bibliothèque puis utilisez l'édition par lot (Ctrl+E) : cochez\n"
          "la case du champ à écraser (Titre, Artiste, Album, Genre, BPM,\n"
          "Clé, Énergie, Note, Couleur, Label ou Commentaire), saisissez\n"
          "la valeur, puis appliquez. Seuls les champs cochés sont\n"
          "modifiés ; les autres restent intacts.\n\n"
          "Si la détection a pris un harmonique (BPM doublé ou divisé),\n"
          "corrigez la valeur puis bornez la plage BPM dans\n"
          "Paramètres > Analyse pour éviter que cela se reproduise." },

        { "Q10 : Comment fonctionne la normalisation audio ?",
          "La normalisation ajuste le volume perçu pour un mix uniforme :\n\n"
          "Qu'est-ce que le LUFS ?\n"
          "LUFS (Loudness Units Full Scale) mesure la loudness perçue,\n"
          "c'est le standard broadcast international (EBU R128).\n\n"
          "Préréglages disponibles :\n"
          "- Streaming (-14 LUFS) : niveau Spotify, Apple Music, YouTube\n"
          "- Club (-8 LUFS) : niveau club, plus fort\n"
          "- Festival (-6 LUFS) : niveau festival, maximum\n"
          "- Personnalisé : choisissez votre valeur cible\n\n"
          "Le traitement est non destructif : BeatMate ajuste le gain de\n"
          "lecture sans modifier vos fichiers, et vous pouvez le désactiver\n"
          "à tout moment.\n\n"
          "À ne pas confondre avec l'éditeur audio, accessible depuis le\n"
          "même écran par « Éditer le fichier ». Celui-ci, lui, modifie\n"
          "réellement l'audio (coupe, gain, fondus, normalisation crête à\n"
          "-0,3 dBFS) et propose « Remplacer l'original », qui sauvegarde\n"
          "d'abord le fichier source dans le dossier audio_backups." },

        { "Q11 : Mes fichiers audio sont-ils modifiés par BeatMate ?",
          "Non, jamais. BeatMate est 100 % non-destructif.\n\n"
          "Toutes les informations (BPM, clé, hot cues, notes, notations,\n"
          "grilles de beats) sont stockées dans la base de données interne\n"
          "de BeatMate, pas dans vos fichiers audio.\n\n"
          "Option avancée : vous pouvez choisir d'écrire les tags ID3\n"
          "(BPM, clé) directement dans les fichiers si vous le souhaitez.\n"
          "Cette option est désactivée par défaut et doit être activée\n"
          "explicitement dans Paramètres > Bibliothèque.\n\n"
          "Même l'export ne modifie pas vos originaux : BeatMate copie\n"
          "les fichiers vers la destination d'export." },

        { "Q12 : Comment sauvegarder et restaurer ma bibliothèque ?",
          "BeatMate offre plusieurs niveaux de sauvegarde :\n\n"
          "1. Sauvegarde automatique :\n"
          "   - Activée par défaut (Paramètres > Sauvegarde)\n"
          "   - Intervalle configurable : horaire, quotidien, hebdomadaire\n"
          "   - Nombre maximum de sauvegardes configurable (1 à 50)\n\n"
          "2. Sauvegarde manuelle :\n"
          "   - Paramètres > Sauvegarde > « Créer un backup maintenant »\n"
          "   - Crée un instantané complet de toutes vos données\n\n"
          "3. Restauration :\n"
          "   - Sélectionnez une sauvegarde dans la liste\n"
          "   - Cliquez sur « Restaurer »\n"
          "   - Toutes vos données seront restaurées à l'état de la sauvegarde\n\n"
          "Les sauvegardes incluent : base de données, playlists, sets,\n"
          "paramètres, hot cues, notations, tags - tout sauf les fichiers\n"
          "audio eux-mêmes (qui restent sur votre disque)." },

        { "Q13 : Combien de pistes BeatMate peut-il gérer ?",
          "BeatMate V12 est optimisé pour des bibliothèques massives :\n\n"
          "- Testé avec plus de 100 000 pistes sans ralentissement\n"
          "- Indexation SQLite haute performance pour la recherche\n"
          "- Cache intelligent pour le chargement instantané\n"
          "- Filtrage et tri en temps réel même sur de grosses collections\n\n"
          "Configuration recommandée pour les grandes bibliothèques :\n"
          "- SSD pour le stockage de la base de données BeatMate\n"
          "- 8 Go de RAM minimum (16 Go recommandés)\n"
          "- Les fichiers audio peuvent rester sur un disque dur externe" },

        { "Q14 : Comment activer ma licence BeatMate ?",
          "Pour activer votre licence BeatMate V12 :\n\n"
          "1. Allez dans Paramètres > Licence\n"
          "2. Entrez votre clé au format XXXXX-XXXXX-XXXXX-XXXXX-XXXXX\n"
          "3. Cliquez sur « Activer la licence »\n"
          "4. Votre licence est liée à votre machine (Machine ID affiché)\n\n"
          "Types de licence disponibles :\n"
          "- Trial : 30 jours, fonctionnalités limitées\n"
          "- Personal : 1 machine, usage personnel\n"
          "- Professional : 2 machines, usage commercial\n"
          "- Family : 5 machines, usage familial\n"
          "- Enterprise : illimité, support prioritaire\n\n"
          "Pour transférer votre licence sur un autre ordinateur :\n"
          "contactez support@beatmate.fr avec votre clé et l'ancien Machine ID." },

        { "Q15 : Où trouver de l'aide supplémentaire ?",
          "Plusieurs ressources sont disponibles :\n\n"
          "- Documentation en ligne : https://beatmate.fr/docs\n"
          "- Forum communautaire : https://beatmate.fr/community\n"
          "- Support par e-mail : support@beatmate.fr\n"
          "- Tutoriels vidéo : https://beatmate.fr/tutorials\n"
          "- FAQ en ligne : https://beatmate.fr/faq\n"
          "- Discord BeatMate : https://discord.gg/beatmate\n\n"
          "Temps de réponse du support :\n"
          "- Enterprise : < 4 heures\n"
          "- Professional : < 24 heures\n"
          "- Personal : < 48 heures\n"
          "- Trial : forum communautaire" },

        { "Q16 : Comment préparer un set de 2 heures ?",
          "Guide étape par étape pour un set de 2 heures :\n\n"
          "1. Estimation : ~30 pistes pour 2 h (4 min/piste en moyenne)\n"
          "   Préparez 50 à 60 pistes pour avoir des alternatives.\n\n"
          "2. Structure recommandée :\n"
          "   - 0-20 min : Warm-up (BPM 118-124, énergie 3-5)\n"
          "   - 20-50 min : Montée (BPM 124-128, énergie 5-7)\n"
          "   - 50-80 min : Peak (BPM 128-132, énergie 7-9)\n"
          "   - 80-100 min : Plateau haut (BPM 130-134, énergie 8-10)\n"
          "   - 100-115 min : Descente (BPM 128-130, énergie 6-8)\n"
          "   - 115-120 min : Closing (BPM 124-128, énergie 4-6)\n\n"
          "3. Dans BeatMate :\n"
          "   - Créez un set (Ctrl+N)\n"
          "   - Utilisez IA Auto-Arrange pour l'optimiser\n"
          "   - Vérifiez la courbe d'énergie\n"
          "   - Exportez vers USB (Ctrl+E)" },

        { "Q17 : Comment utiliser les Smart Playlists ?",
          "Les Smart Playlists sont des playlists dynamiques basées sur des règles :\n\n"
          "1. Créez une Smart Playlist (Ctrl+N > Smart Playlist)\n"
          "2. Définissez vos règles, par exemple :\n"
          "   - Genre = « Tech House »\n"
          "   - BPM entre 124 et 130\n"
          "   - Énergie >= 6\n"
          "   - Note >= 4 étoiles\n"
          "   - Date d'ajout < 30 jours (nouveautés)\n\n"
          "3. La playlist se remplit automatiquement avec les pistes\n"
          "   correspondant à TOUTES les règles.\n\n"
          "4. Mise à jour dynamique : quand vous importez de nouvelles\n"
          "   pistes qui correspondent aux critères, elles sont ajoutées\n"
          "   automatiquement à la Smart Playlist.\n\n"
          "Exemples utiles :\n"
          "- « Nouveautés du mois » : date d'ajout < 30 jours\n"
          "- « Favoris Peak Time » : note 5 + énergie 8-10\n"
          "- « Warm-up House » : genre House + BPM 118-124 + énergie 3-5" },

        { "Q18 : Quelle est la différence entre Préparation Set et Préparation Soirée ?",
          "Ce sont deux modes de préparation complémentaires :\n\n"
          "PRÉPARATION SET :\n"
          "- Pour un set DJ classique (1 à 3 heures)\n"
          "- Vous choisissez les pistes et l'ordre\n"
          "- Score de compatibilité entre chaque piste\n"
          "- IA Auto-Arrange pour optimiser l'ordre\n"
          "- Courbe d'énergie du set\n"
          "- Idéal pour : club nights, warm-up, closing sets\n\n"
          "PRÉPARATION SOIRÉE :\n"
          "- Pour une soirée complète (4 à 8 heures)\n"
          "- Divisée en phases (Accueil, Montée, Peak, Descente, After)\n"
          "- Chaque phase a ses propres paramètres (BPM, énergie)\n"
          "- IA Auto-Remplir : BeatMate sélectionne les pistes pour chaque phase\n"
          "- Profils d'énergie prédéfinis (Club, Festival, Lounge, After)\n"
          "- Idéal pour : soirées complètes, événements, mariages" },

        { "Q19 : Comment optimiser les performances de BeatMate ?",
          "Conseils pour des performances optimales :\n\n"
          "1. Stockage :\n"
          "   - Installez BeatMate et sa base de données sur un SSD\n"
          "   - Les fichiers audio peuvent rester sur un disque externe\n\n"
          "2. Analyse :\n"
          "   - Utilisez le mode « Rapide » pour les grandes importations\n"
          "   - Ré-analysez en « Haute » uniquement les pistes importantes\n"
          "   - Configurez le nombre de threads (Paramètres > Analyse)\n\n"
          "3. Mémoire :\n"
          "   - 8 Go minimum, 16 Go recommandés au-delà de 50 000 pistes\n"
          "   - Fermez les applications gourmandes pendant l'analyse\n\n"
          "4. Audio :\n"
          "   - Mémoire tampon de 512 échantillons pour un bon compromis\n"
          "   - Augmentez à 1024 si vous avez des coupures audio\n"
          "   - Utilisez ASIO si disponible pour la plus basse latence" },

        { "Q20 : Comment utiliser la fonctionnalité de streaming ?",
          "Le module Streaming de BeatMate vous permet de :\n\n"
          "1. Suivre les tendances :\n"
          "   - Classements par genre et par région\n"
          "   - Mise à jour régulière des classements\n"
          "   - Historique des tendances sur 30/60/90 jours\n\n"
          "2. Découvrir de la musique :\n"
          "   - Recommandations basées sur votre bibliothèque\n"
          "   - Artistes similaires à ceux que vous jouez\n"
          "   - Nouvelles sorties dans vos genres favoris\n\n"
          "3. Intégration :\n"
          "   - Connexion avec Spotify, Beatport, SoundCloud\n"
          "   - Préécoute des pistes directement dans BeatMate\n"
          "   - Ajout à votre bibliothèque en un clic" },

        { "Q21 : Comment gérer les doublons dans ma bibliothèque ?",
          "Tout se passe depuis la Bibliothèque :\n\n"
          "1. Dans la barre de facettes, choisissez la dimension « État »\n"
          "   et cliquez sur la facette « Doublons ». La liste ne montre\n"
          "   plus que les morceaux présents en plusieurs exemplaires.\n"
          "2. Le bouton « Doublons » de la barre d'outils ouvre la fenêtre\n"
          "   dédiée : les fichiers y sont regroupés par titre et artiste,\n"
          "   avec leur durée, leur débit et leur chemin.\n"
          "3. Cochez les exemplaires à écarter, puis validez.\n\n"
          "La suppression retire l'entrée de la base BeatMate. Vérifiez\n"
          "toujours le chemin affiché avant de valider : deux fichiers de\n"
          "même titre peuvent être deux masters différents." },

        { "Q22 : Puis-je utiliser BeatMate sur plusieurs ordinateurs ?",
          "Oui, selon votre type de licence :\n\n"
          "- Personal : 1 machine\n"
          "- Professional : 2 machines\n"
          "- Family : 5 machines\n"
          "- Enterprise : illimité\n\n"
          "Pour synchroniser entre machines :\n"
          "1. Exportez une sauvegarde depuis la première machine\n"
          "   (Paramètres > Sauvegarde > Créer un backup)\n"
          "2. Copiez le fichier de sauvegarde sur l'autre machine\n"
          "3. Importez-le via Paramètres > Sauvegarde > Restaurer\n\n"
          "Note : les chemins des fichiers audio doivent être accessibles\n"
          "sur les deux machines (même structure de dossiers ou lecteur réseau)." },

        { "Q23 : Quels périphériques audio sont compatibles ?",
          "BeatMate utilise les pilotes audio standards de Windows :\n\n"
          "- Sortie système par défaut (carte intégrée, Realtek…)\n"
          "- Interfaces USB (Focusrite, Native Instruments, Pioneer…)\n"
          "- WASAPI et DirectSound\n\n"
          "Réglage dans Paramètres > Audio :\n"
          "- Choisissez le périphérique de sortie, et l'entrée si vous en\n"
          "  utilisez une\n"
          "- Ajustez la taille de la mémoire tampon : plus elle est petite,\n"
          "  plus la réponse est immédiate, mais plus le risque de coupures\n"
          "  augmente sur une machine chargée\n"
          "- Choisissez la fréquence d'échantillonnage (44 100 Hz convient\n"
          "  dans la quasi-totalité des cas)\n"
          "- La latence estimée s'affiche sous les réglages\n"
          "- « Test Audio » joue trois notes sur la sortie choisie : si vous\n"
          "  n'entendez rien, le problème vient du périphérique sélectionné\n"
          "  ou du volume de Windows, pas de BeatMate" },

        { "Q24 : Comment personnaliser l'interface de BeatMate ?",
          "BeatMate offre de nombreuses options de personnalisation :\n\n"
          "Paramètres > Général :\n"
          "- Thème : Dark, Light, Nord, Dracula, Contraste élevé\n"
          "  ou Personnalisé\n"
          "- Langue : français ou anglais, appliquée immédiatement\n"
          "  sans redémarrage\n"
          "- Démarrage : plein écran, dernière taille utilisée ou\n"
          "  taille par défaut\n\n"
          "Bibliothèque :\n"
          "- Clic droit sur l'en-tête des colonnes pour choisir celles\n"
          "  qui sont affichées\n"
          "- Glissez la bordure d'une colonne pour la redimensionner\n"
          "- Bouton « Vues » pour enregistrer et rappeler un affichage\n"
          "  complet (recherche, facettes, tri, colonnes)" },

        { "Q25 : BeatMate est-il disponible sur Mac ?",
          "Non. BeatMate est aujourd'hui un logiciel Windows uniquement :\n\n"
          "- Windows 10 et Windows 11 (64 bits)\n\n"
          "L'installeur, le système de mise à jour et les modules audio "
          "reposent sur des composants Windows. Aucune version macOS ni "
          "Linux n'est distribuée.\n\n"
          "Configuration recommandée :\n"
          "- Processeur : Intel Core i5 / AMD Ryzen 5 ou mieux\n"
          "- Mémoire : 8 Go minimum, 16 Go conseillés si vous utilisez\n"
          "  les stems ou Mix Studio\n"
          "- Disque : 1 Go pour l'application, plus l'espace des caches\n"
          "  (formes d'onde, pochettes, sauvegardes)\n"
          "- Écran : 1280x720 minimum, 1920x1080 conseillé" },
        { "Q26 : Comment ajouter une date dans l'Agenda ?",
          "Ouvrez Agenda dans le menu latéral, puis :\n\n"
          "1. Cliquez sur le jour voulu dans le calendrier.\n"
          "2. Cliquez sur « + Nouvelle date » (ou double-cliquez sur le jour).\n"
          "3. Renseignez les champs : Titre / soirée, Date (AAAA-MM-JJ),\n"
          "   Début (HH:MM), Fin (HH:MM), Lieu / club, Ville, Cachet,\n"
          "   Style / genre, Statut et Notes.\n"
          "4. Cochez les rappels souhaités en bas de la fenêtre.\n"
          "5. Cliquez sur « Enregistrer ».\n\n"
          "Par défaut la prestation est proposée de 22:00 à 04:00. Si l'heure\n"
          "de fin est antérieure à l'heure de début, BeatMate comprend que la\n"
          "soirée se termine le lendemain et décale la fin de 24 heures.\n\n"
          "La date apparaît immédiatement sur le calendrier et dans la liste\n"
          "du bas. Pour la modifier ensuite : sélectionnez-la et cliquez sur\n"
          "« Modifier », ou double-cliquez sur la ligne." },

        { "Q27 : Quelles vues propose l'Agenda (mois, semaine, année) ?",
          "Trois vues, sélectionnées par les boutons en haut à gauche du\n"
          "calendrier :\n\n"
          "- « Mois » : grille de 7 colonnes. Chaque prestation apparaît en\n"
          "  pastille colorée avec son heure et son titre. Si le jour contient\n"
          "  plus de dates que la place disponible, un compteur « +2 » s'affiche.\n"
          "- « Semaine » : sept colonnes du lundi au dimanche, chaque\n"
          "  prestation occupant un bloc avec l'horaire et le lieu.\n"
          "- « Année » : les douze mois côte à côte, un point coloré sur\n"
          "  chaque jour occupé. Cliquez sur un mois pour l'ouvrir en vue Mois.\n\n"
          "Les flèches situées de part et d'autre du titre reculent ou avancent\n"
          "d'un mois, d'une semaine ou d'une année selon la vue active. Le\n"
          "bouton « Aujourd'hui » revient à la date du jour, qui est toujours\n"
          "entourée d'une pastille de couleur." },

        { "Q28 : À quoi servent les statuts et les couleurs de l'Agenda ?",
          "Chaque date porte un statut, choisi dans la liste « Statut » de la\n"
          "fiche. Il s'affiche en pastille à droite de la ligne :\n\n"
          "- « À confirmer » : orange, la date n'est pas encore ferme.\n"
          "- « Confirmé » : vert, c'est le statut par défaut.\n"
          "- « Passé » : gris, la prestation a eu lieu.\n"
          "- « Annulé » : rouge, et la pastille du calendrier devient grise\n"
          "  et translucide.\n\n"
          "Dans le calendrier, la couleur des pastilles ne dépend pas du statut\n"
          "mais du style musical ; à défaut, du lieu ; à défaut, du titre. Deux\n"
          "soirées du même style reçoivent donc automatiquement la même couleur,\n"
          "ce qui permet de repérer d'un coup d'oeil vos résidences.\n\n"
          "Les dates annulées sont ignorées par les rappels." },

        { "Q29 : Comment fonctionnent les rappels de prestation ?",
          "Vous pouvez cumuler plusieurs rappels pour une même date. Huit\n"
          "délais sont proposés : 15 min, 30 min, 1 h, 2 h, 1 jour, 1 sem.,\n"
          "1 mois et 2 mois avant le début.\n\n"
          "1. Dans la fiche d'une date, cochez les délais voulus.\n"
          "2. Pour définir des rappels valables pour toutes vos dates,\n"
          "   utilisez le bouton « Rappels » situé à droite au-dessus du\n"
          "   calendrier, puis cochez les délais dans le menu. « Tout\n"
          "   désactiver » les retire tous.\n\n"
          "Les rappels propres à une date remplacent le réglage général.\n\n"
          "Une cloche est affichée en permanence en haut à droite de BeatMate.\n"
          "Quand une prestation approche, elle s'élargit et affiche le nom de\n"
          "la soirée et le temps restant, en orange puis en rouge à moins de\n"
          "quinze minutes. Un clic ouvre l'Agenda. À chaque palier franchi,\n"
          "BeatMate joue le son d'alerte du système et affiche une notification\n"
          "« Prestation dans moins de… » pendant douze secondes. Chaque palier\n"
          "n'est signalé qu'une seule fois." },

        { "Q30 : Comment envoyer mon agenda vers Google, Outlook ou Apple ?",
          "L'Agenda exporte au format .ics, lu par tous les calendriers :\n\n"
          "1. Ouvrez Agenda.\n"
          "2. Cliquez sur « Exporter » en haut à droite.\n"
          "3. Choisissez « Calendrier .ics — Google, Outlook, Apple ».\n"
          "4. Enregistrez le fichier (agenda-dj.ics est proposé sur le Bureau).\n\n"
          "Dans Google Agenda, allez ensuite dans Paramètres puis Importer &\n"
          "exporter, et sélectionnez le fichier. Outlook et le calendrier\n"
          "d'Apple acceptent également ce fichier par simple ouverture.\n\n"
          "Le fichier contient toutes vos dates non filtrées, avec le titre,\n"
          "l'horaire, le lieu et les notes. Il s'agit d'une copie : les\n"
          "modifications faites ensuite dans BeatMate ne remontent pas\n"
          "automatiquement, il faut réexporter." },

        { "Q31 : Puis-je imprimer mon agenda ou l'importer depuis un autre outil ?",
          "Oui, dans les deux sens.\n\n"
          "Pour sortir un document, cliquez sur « Exporter » puis choisissez :\n"
          "- « Page HTML — imprimable » : s'ouvre dans votre navigateur.\n"
          "- « Document PDF » : pour l'envoyer à un organisateur.\n"
          "- « Document Word (.docx) » : modifiable dans Word.\n"
          "- « Tableur CSV — Excel » : pour votre comptabilité.\n\n"
          "Le fichier est proposé sur le Bureau puis ouvert automatiquement\n"
          "après l'enregistrement.\n\n"
          "Pour récupérer des dates venant d'un autre calendrier, cliquez sur\n"
          "« Importer .ics » et sélectionnez le fichier exporté depuis Google,\n"
          "Outlook ou Apple. BeatMate ajoute chaque événement comme une\n"
          "nouvelle date et affiche le nombre de dates importées. L'import\n"
          "ajoute toujours : il ne remplace ni ne fusionne les dates\n"
          "existantes, vérifiez donc les doublons après un second import." },

        { "Q32 : Comment ouvrir l'éditeur de tags ?",
          "L'éditeur de tags se lance depuis la Bibliothèque :\n\n"
          "1. Ouvrez Bibliothèque.\n"
          "2. Cochez les morceaux à modifier dans la colonne de gauche.\n"
          "3. Cliquez sur « Éditeur de tags » dans la barre d'actions.\n\n"
          "Le bouton indique le nombre de titres concernés, par exemple\n"
          "« Éditeur de tags (7) ».\n\n"
          "Si aucun morceau n'est coché, l'éditeur prend la sélection en cours.\n"
          "Si rien n'est sélectionné non plus, il charge tous les morceaux\n"
          "actuellement affichés : pensez donc à filtrer avant d'ouvrir.\n\n"
          "Douze champs sont éditables à droite : Titre, Artiste, Album,\n"
          "Genre, Année, BPM, Clé, Énergie (1-10), Label, Mood, Commentaire\n"
          "et Tags perso. Quand plusieurs titres sélectionnés n'ont pas la\n"
          "même valeur, le champ affiche <multiple> et n'est pas écrit tant\n"
          "que vous ne le remplacez pas.\n\n"
          "Le bouton « Bibliothèque » en haut à droite referme l'éditeur." },

        { "Q33 : Comment modifier plusieurs morceaux d'un coup ?",
          "Tout repose sur les cases à cocher de la liste de gauche :\n\n"
          "1. Cliquez dans la case, à gauche du nom de fichier.\n"
          "2. Pour cocher une plage entière, cochez la première ligne puis\n"
          "   maintenez Maj et cliquez sur la dernière.\n"
          "3. « Tout cocher » coche l'ensemble ; le bouton devient alors\n"
          "   « Tout décocher ».\n\n"
          "Seuls les titres cochés sont modifiés par les opérations par lot :\n"
          "écriture des tags, casse, nettoyage, renommage, pochettes et\n"
          "recherche en ligne.\n\n"
          "Attention à la différence entre cliquer et cocher : cliquer sur\n"
          "une ligne la sélectionne et affiche ses champs à droite, mais ne\n"
          "la coche pas. Le compteur en haut de l'éditeur rappelle en\n"
          "permanence le nombre de morceaux chargés et le nombre de morceaux\n"
          "cochés pour le lot.\n\n"
          "Un point orange devant un nom de fichier signale une modification\n"
          "non encore écrite dans le fichier." },

        { "Q34 : Comment écrire les tags dans mes fichiers MP3 ?",
          "Les modifications faites dans l'éditeur restent en mémoire tant\n"
          "que vous ne les avez pas enregistrées.\n\n"
          "1. Cochez les morceaux concernés.\n"
          "2. Corrigez les champs à droite (Titre, Artiste, Album, Genre,\n"
          "   Année, BPM, Clé, Énergie, Label, Mood, Commentaire, Tags perso).\n"
          "3. Cliquez sur « Enregistrer » en bas à gauche. Le bouton indique\n"
          "   « Enregistrer (N) » quand plusieurs titres sont cochés.\n\n"
          "BeatMate écrit alors les tags dans le fichier audio lui-même et\n"
          "met à jour la base de données. Une notification indique le nombre\n"
          "de fichiers écrits, et le cas échéant le nombre d'échecs, qui\n"
          "surviennent presque toujours sur un fichier en lecture seule ou\n"
          "ouvert dans un autre logiciel.\n\n"
          "Pour la clé, saisissez la notation Camelot (par exemple 8A) : elle\n"
          "est alors reconnue comme telle et mise en majuscules. L'énergie\n"
          "est ramenée à l'intervalle 1-10. Les tags perso se saisissent\n"
          "séparés par des virgules." },

        { "Q35 : Comment gérer les pochettes d'album ?",
          "Dans l'éditeur de tags, la pochette du morceau sélectionné est\n"
          "affichée en haut à droite. Le bouton « Pochette » ouvre quatre\n"
          "actions :\n\n"
          "- « Chercher sur Internet… » : télécharge jusqu'à huit propositions\n"
          "  et les présente en grille ; cliquez sur celle qui convient.\n"
          "- « Importer une image… » : choisissez un fichier JPG ou PNG sur\n"
          "  votre disque. Il est appliqué à tous les titres cochés.\n"
          "- « Exporter la pochette… » : enregistre la pochette du morceau\n"
          "  sélectionné sur le Bureau.\n"
          "- « Retirer la pochette » : supprime l'image des titres cochés.\n\n"
          "Les pochettes sont écrites directement dans les fichiers audio,\n"
          "sans passer par le bouton « Enregistrer ». La vignette affichée\n"
          "dans la colonne ART de la Bibliothèque est régénérée\n"
          "automatiquement après l'opération.\n\n"
          "Sur une sélection de plusieurs titres, BeatMate applique sans vous\n"
          "demander la première pochette trouvée pour chaque morceau." },

        { "Q36 : Comment compléter mes tags automatiquement depuis Internet ?",
          "Le bouton « En ligne » de l'éditeur de tags propose quatre modes :\n\n"
          "- « Tout automatique (tags + BPM + pochettes) » : enchaîne la\n"
          "  recherche des tags puis celle des pochettes.\n"
          "- « Compléter les tags + BPM » : titre, artiste, album, genre,\n"
          "  année et, si le BPM est absent, le BPM du service en ligne.\n"
          "- « Chercher les pochettes ».\n"
          "- « Analyser avec BeatMate » : n'utilise pas Internet et calcule\n"
          "  sur votre machine le BPM précis, la clé, l'énergie et le mood.\n\n"
          "Les données proviennent de Deezer et d'iTunes. Une fenêtre de\n"
          "progression annulable indique le morceau en cours.\n\n"
          "BeatMate note la ressemblance entre votre morceau et chaque\n"
          "résultat, et rejette ceux qui sont trop éloignés ainsi que les\n"
          "versions karaoké, reprises ou instrumentales. Un morceau non\n"
          "reconnu est laissé intact plutôt que mal renseigné.\n\n"
          "Les valeurs récupérées ne sont pas encore dans vos fichiers :\n"
          "vérifiez-les, puis cliquez sur « Enregistrer »." },

        { "Q37 : Comment renommer mes fichiers ou récupérer les tags depuis leur nom ?",
          "Deux opérations complémentaires, dans l'éditeur de tags.\n\n"
          "Pour renommer les fichiers à partir des tags :\n"
          "1. Cochez les morceaux.\n"
          "2. Saisissez le masque dans le champ situé à droite du bouton\n"
          "   « Enregistrer ». Le masque par défaut est %artist% - %title%.\n"
          "3. Cliquez sur « Renommer les fichiers ».\n\n"
          "Les variables disponibles sont %artist%, %title%, %album%,\n"
          "%genre%, %year%, %bpm% et %key%. Les caractères interdits par\n"
          "Windows sont remplacés par un tiret. Un fichier est ignoré si le\n"
          "nom cible existe déjà. Les chemins sont mis à jour dans la base.\n\n"
          "Pour l'opération inverse, quand vos fichiers sont bien nommés mais\n"
          "mal tagués, cliquez sur « Tags depuis le nom ». BeatMate coupe le\n"
          "nom du fichier au premier « - » : ce qui précède devient l'artiste,\n"
          "ce qui suit devient le titre. Les fichiers sans ce séparateur sont\n"
          "ignorés. Cliquez ensuite sur « Enregistrer »." },

        { "Q38 : À quoi servent les boutons Casse et Nettoyer ?",
          "Ils corrigent en masse la présentation des champs Titre, Artiste\n"
          "et Album des morceaux cochés.\n\n"
          "« Casse » propose quatre transformations :\n"
          "- « Title Case (Chaque Mot) » : une majuscule à chaque mot.\n"
          "- « MAJUSCULES ».\n"
          "- « minuscules ».\n"
          "- « Première lettre seulement ».\n\n"
          "« Nettoyer » remplace les traits de soulignement par des espaces,\n"
          "supprime les espaces en double et retire les suffixes promotionnels\n"
          "les plus courants tels que (Official Video), (Official Audio),\n"
          "(Lyric Video), (Lyrics), (HD), (HQ) ou (Clip Officiel).\n\n"
          "Ces deux actions modifient l'affichage immédiatement mais pas\n"
          "encore vos fichiers : les lignes concernées passent en orange avec\n"
          "un point. Cliquez sur « Enregistrer » pour valider, ou refermez\n"
          "l'éditeur sans enregistrer pour tout abandonner." },

        { "Q39 : Comment repérer les fichiers audio corrompus ?",
          "La Bibliothèque possède une facette dédiée :\n\n"
          "1. Ouvrez Bibliothèque.\n"
          "2. Dans la barre de facettes, choisissez la facette « État ».\n"
          "3. Cliquez sur la pastille « Corrompus ». La liste ne montre plus\n"
          "   que les fichiers défectueux, et le bouton « Réparer les\n"
          "   corrompus » apparaît.\n\n"
          "La même facette « État » propose aussi « Fichiers manquants »,\n"
          "« Doublons », « Sans BPM », « Sans clé », « Sans genre », « Sans\n"
          "année », « Non analysés » et « Jamais joués ».\n\n"
          "Un morceau est classé corrompu quand un contrôle d'intégrité a\n"
          "déjà été lancé et a échoué sur ce fichier. Tant qu'aucun contrôle\n"
          "n'a été fait, le fichier est simplement considéré comme non\n"
          "vérifié : lancez d'abord une vérification (voir la question\n"
          "suivante). Le résultat est mémorisé et réévalué si la taille ou la\n"
          "date du fichier change." },

        { "Q40 : Comment vérifier et réparer un fichier audio abîmé ?",
          "1. Dans la Bibliothèque, cliquez sur « Vérifier / Réparer ».\n"
          "2. Choisissez l'étendue : « La sélection », « Les morceaux\n"
          "   affichés » ou « Toute la bibliothèque ».\n"
          "3. La fenêtre « Intégrité des fichiers » s'ouvre et lance\n"
          "   automatiquement le contrôle des morceaux non encore vérifiés.\n\n"
          "Chaque morceau reçoit un état : Intact, Suspect, Corrompu,\n"
          "Illisible ou Réparé. À la fin du contrôle, l'affichage se\n"
          "concentre de lui-même sur les problèmes et coche les fichiers\n"
          "corrompus.\n\n"
          "4. Ajustez les cases si besoin, ou utilisez « Cocher les\n"
          "   corrompus », « Tout cocher » et « Tout décocher ».\n"
          "5. Cliquez sur « Réparer ». Le bouton indique le nombre de\n"
          "   fichiers concernés et confirme avant d'agir.\n\n"
          "La réparation reconstruit le fichier sans réencoder l'audio : la\n"
          "qualité n'est pas dégradée. Les quatre filtres du haut\n"
          "(« Corrompus », « Suspects », « Non vérifiés », « Intacts »)\n"
          "permettent de n'afficher que ce qui vous intéresse." },

        { "Q41 : Où va mon fichier d'origine après une réparation ?",
          "Avant toute modification, BeatMate copie l'original dans un\n"
          "dossier de sauvegarde :\n\n"
          "%APPDATA%\\BeatMate\\integrity_backups\n\n"
          "Chaque copie est préfixée par la date et l'heure, par exemple\n"
          "20260716_224500_MonMorceau.mp3. La réparation n'écrase le fichier\n"
          "de travail qu'une fois cette copie réussie : si la sauvegarde\n"
          "échoue, l'opération est abandonnée et rien n'est modifié.\n\n"
          "Après une réparation réussie, l'état du morceau passe à « Réparé »\n"
          "et le détail affiché indique le chemin exact de la sauvegarde.\n\n"
          "Ces sauvegardes ne sont jamais supprimées automatiquement : pensez\n"
          "à vider ce dossier de temps en temps si vous réparez beaucoup de\n"
          "fichiers.\n\n"
          "Le bouton « Rapport CSV » de la fenêtre d'intégrité enregistre la\n"
          "liste complète (statut, titre, chemin, détails) pour la conserver\n"
          "ou l'ouvrir dans Excel. « Ouvrir le dossier » affiche le fichier\n"
          "sélectionné dans l'Explorateur ; un double-clic fait de même." },

        { "Q42 : Un morceau est introuvable, comment le retrouver ?",
          "Quand vous déplacez ou renommez vos fichiers hors de BeatMate, la\n"
          "bibliothèque pointe vers des chemins qui n'existent plus.\n\n"
          "1. Ouvrez Bibliothèque.\n"
          "2. Cliquez sur « Fichiers manquants ». La fenêtre « Relier les\n"
          "   fichiers manquants » s'ouvre et lance immédiatement l'analyse.\n"
          "3. BeatMate liste les correspondances trouvées : l'ancien chemin\n"
          "   apparaît en rouge, le nouveau en vert, avec un pourcentage de\n"
          "   confiance.\n"
          "4. Cliquez sur une ligne pour la cocher ou la décocher.\n"
          "5. Cliquez sur « Relier la selection ».\n\n"
          "La recherche parcourt d'abord les dossiers parents de vos morceaux\n"
          "déjà valides ainsi que votre dossier Musique. Si vos fichiers ont\n"
          "été déplacés ailleurs, cliquez sur « Chercher dans... » et\n"
          "désignez le nouveau dossier : l'analyse est relancée en l'incluant.\n\n"
          "Quand tout est en ordre, le message indique qu'aucun fichier n'est\n"
          "manquant. Vous pouvez aussi repérer ces morceaux en activant la\n"
          "pastille « Fichiers manquants » de la facette « État »." },

        { "Q43 : Comment ouvrir l'éditeur audio ?",
          "L'éditeur audio se trouve dans la vue Normalisation :\n\n"
          "1. Ouvrez Normalisation dans le menu latéral.\n"
          "2. Ajoutez les morceaux à la liste.\n"
          "3. Sélectionnez le morceau à retoucher.\n"
          "4. Cliquez sur « Éditer le fichier ».\n\n"
          "L'éditeur occupe alors toute la vue. Le titre rappelle le nom du\n"
          "fichier, sa durée, sa fréquence d'échantillonnage et s'il est mono\n"
          "ou stéréo. Un point à côté du titre signale des modifications non\n"
          "enregistrées.\n\n"
          "Pour sélectionner une portion, glissez la souris sur la forme\n"
          "d'onde. Un simple clic place seulement le curseur. Ctrl+A\n"
          "sélectionne tout le morceau. Le bandeau du bas affiche le début,\n"
          "la fin et la durée de la sélection.\n\n"
          "Le bouton « Écouter », ou la barre d'espace, lit la sélection, ou\n"
          "bien la suite du morceau à partir du curseur. Le bouton « Retour »\n"
          "referme l'éditeur." },

        { "Q44 : Comment couper, rogner ou modifier le volume d'un passage ?",
          "Sélectionnez d'abord la portion concernée, puis :\n\n"
          "- « Couper » supprime la sélection et recolle le reste. La touche\n"
          "  Suppr fait la même chose.\n"
          "- « Rogner » fait l'inverse : seule la sélection est conservée,\n"
          "  tout le reste est supprimé.\n"
          "- « Silence » remplace la sélection par du silence sans changer la\n"
          "  durée du morceau. Pratique pour retirer un blanc parasite ou une\n"
          "  annonce.\n"
          "- « Gain… » demande une valeur en décibels et l'applique à la\n"
          "  sélection. Une valeur négative baisse le niveau. Le résultat est\n"
          "  limité pour éviter la saturation.\n"
          "- « Inverser » lit la sélection à l'envers.\n\n"
          "« Couper », « Rogner », « Silence » et « Inverser » exigent une\n"
          "sélection. « Gain… » et « Normaliser » s'appliquent au morceau\n"
          "entier si rien n'est sélectionné.\n\n"
          "Toutes ces opérations travaillent en mémoire : votre fichier sur\n"
          "le disque n'est touché qu'au moment de l'enregistrement." },

        { "Q45 : Comment faire un fondu et quelle courbe choisir ?",
          "Sélectionnez la zone à traiter puis cliquez sur « Fondu » entrant\n"
          "ou « Fondu » sortant. Sept courbes sont proposées :\n\n"
          "- « Linéaire » : progression régulière.\n"
          "- « Puissance constante (recommandé) » : conserve un volume\n"
          "  perçu stable, c'est le choix habituel pour un enchaînement.\n"
          "- « Exponentiel (départ doux) » : démarre très bas puis accélère.\n"
          "- « Logarithmique (départ rapide) » : monte vite puis s'adoucit.\n"
          "- « Courbe en S (progressif) » : douce au début et à la fin.\n"
          "- « Rapide » : atteint le niveau plein très tôt.\n"
          "- « Lent » : reste bas longtemps avant de monter.\n\n"
          "Le fondu s'applique sur toute la durée de la sélection : pour un\n"
          "fondu de cinq secondes, sélectionnez cinq secondes.\n\n"
          "Si aucune sélection n'est active, le fondu porte sur l'ensemble du\n"
          "morceau, ce qui est rarement l'effet recherché. En cas d'erreur,\n"
          "utilisez « Annuler »." },

        { "Q46 : Comment normaliser un morceau et lire les niveaux affichés ?",
          "Le bouton « Normaliser » amène la crête du morceau entier à\n"
          "-0,3 dBFS, c'est-à-dire juste sous le maximum numérique. Une\n"
          "notification indique le gain appliqué. La normalisation porte\n"
          "toujours sur l'intégralité du fichier, pas sur la sélection.\n\n"
          "En bas à droite, l'éditeur affiche en permanence deux mesures de\n"
          "la sélection, ou du morceau entier si rien n'est sélectionné :\n\n"
          "- « Crête » : le point le plus fort. C'est lui qui détermine le\n"
          "  risque de saturation.\n"
          "- « RMS » : le niveau moyen perçu, meilleur indicateur du volume\n"
          "  ressenti.\n\n"
          "Un verdict accompagne ces valeurs : « trop fort (écrêtage\n"
          "possible) » au-dessus de -1 dB, « niveau faible » en dessous de\n"
          "-12 dB, et « niveau correct » entre les deux.\n\n"
          "La forme d'onde reprend cette lecture : des repères horizontaux\n"
          "marquent 0, -3, -6, -12, -18, -24 et -36 dB, la zone au-delà de\n"
          "-1 dB est teintée en rouge, et les passages qui la dépassent sont\n"
          "dessinés en rouge. Le tracé clair correspond aux crêtes, le tracé\n"
          "plein central au niveau RMS." },

        { "Q47 : Comment annuler mes retouches et enregistrer le résultat ?",
          "L'éditeur conserve les quarante dernières opérations :\n\n"
          "- « Annuler » ou Ctrl+Z revient en arrière. Le bouton indique le\n"
          "  nombre d'étapes disponibles.\n"
          "- « Rétablir » ou Ctrl+Y refait l'opération annulée.\n\n"
          "Pour naviguer : « + » et « - » (ou les touches + et -) zooment\n"
          "autour de la sélection ou du curseur, la molette zoome à\n"
          "l'endroit pointé, et « Zoom tout » réaffiche le morceau entier.\n\n"
          "Deux façons d'enregistrer :\n\n"
          "1. « Enregistrer sous… » crée un nouveau fichier et laisse\n"
          "   l'original intact. BeatMate propose le nom d'origine suivi de\n"
          "   « (edit) », en WAV, MP3, FLAC ou OGG.\n"
          "2. « Remplacer l'original » écrase le fichier de départ, après\n"
          "   confirmation. L'original est d'abord copié dans\n"
          "   %APPDATA%\\BeatMate\\audio_backups, avec la date et l'heure\n"
          "   dans le nom. Si cette copie échoue, rien n'est écrasé.\n\n"
          "Tant que vous n'avez pas enregistré, fermer l'éditeur abandonne\n"
          "les modifications sans avertissement." },

        { "Q48 : Comment fonctionnent les facettes de la Bibliothèque ?",
          "Les facettes filtrent la liste en un clic, sans passer par la\n"
          "recherche. La barre située sous les filtres propose six axes :\n\n"
          "« Genre », « BPM », « Énergie », « Année », « Clé » et « État ».\n\n"
          "1. Cliquez sur l'axe voulu.\n"
          "2. Cliquez sur une ou plusieurs pastilles de valeurs. Chaque\n"
          "   pastille indique combien de morceaux elle contient.\n\n"
          "Plusieurs pastilles d'un même axe s'additionnent, alors que des\n"
          "axes différents se combinent : choisir « House » puis, dans l'axe\n"
          "BPM, « 122-126 » et « 126-130 » donne la house entre 122 et\n"
          "130 BPM.\n\n"
          "Les tranches sont prédéfinies : le BPM va de « < 95 » à « 142+ »,\n"
          "l'énergie se lit « Douce 1-3 », « Moyenne 4-6 », « Forte 7-8 » et\n"
          "« Max 9-10 », les années vont de « 70s- » à « 2020s ». Chaque axe\n"
          "possède une valeur pour les morceaux non renseignés, par exemple\n"
          "« Sans BPM ».\n\n"
          "Le bouton rouge « × Effacer », à droite, remet toutes les facettes\n"
          "à zéro. Il n'apparaît que si un filtre est actif." },

        { "Q49 : Pourquoi mes genres sont-ils regroupés, et comment voir le détail ?",
          "Quand la facette « Genre » est active, une pastille supplémentaire\n"
          "s'affiche à droite des axes. Son libellé indique le mode en cours :\n\n"
          "- « Groupés » : les variantes d'un même genre sont réunies sous un\n"
          "  nom commun. « disco », « Disco/Funk » et « disco 444 » se\n"
          "  retrouvent ensemble sous « Disco / Funk ».\n"
          "- « Détail » : chaque valeur de genre est affichée telle qu'elle\n"
          "  est écrite dans vos fichiers.\n\n"
          "Cliquez sur la pastille pour basculer d'un mode à l'autre. Le\n"
          "réglage est conservé d'une session à l'autre ; le mode groupé est\n"
          "actif par défaut.\n\n"
          "Le regroupement reconnaît une vingtaine de familles adaptées au\n"
          "métier : Zouk, Kompa / Compas, Kizomba, Reggae / Dancehall, Afro,\n"
          "Salsa / Latino, House, Techno, Trance, Disco / Funk, Soul / R&B,\n"
          "Hip-Hop / Rap, Rock, Électro / Dance, Slow / Ballade, Variété\n"
          "française, Pop, Jazz et quelques autres. Un genre inconnu reste\n"
          "affiché tel quel.\n\n"
          "Changer de mode remet à zéro le filtre de genre en cours." },

        { "Q50 : À quoi servent les cases à cocher de la Bibliothèque ?",
          "Cocher et sélectionner sont deux choses différentes. La sélection\n"
          "sert à l'affichage immédiat ; les cases servent à désigner un lot\n"
          "de travail qui survit au changement de filtre.\n\n"
          "1. Cliquez dans la case, tout à gauche de la ligne. Un clic\n"
          "   ailleurs sur la ligne ne fait que la sélectionner.\n"
          "2. Pour cocher une plage, cochez la première ligne puis maintenez\n"
          "   Maj et cliquez sur la dernière.\n"
          "3. La case placée dans l'en-tête coche ou décoche tout ce qui est\n"
          "   affiché.\n\n"
          "Le bouton de la barre d'actions indique « Tout cocher », puis\n"
          "« Décocher (N) » dès qu'un morceau est coché. Ctrl+A coche tout,\n"
          "Ctrl+D décoche tout.\n\n"
          "Les morceaux cochés sont ceux qu'utilisent l'éditeur de tags,\n"
          "l'analyse et les exports. Si aucune case n'est cochée, ces\n"
          "fonctions se rabattent sur la sélection en cours." },

        { "Q51 : Comment enregistrer une vue de ma bibliothèque ?",
          "Une vue mémorise votre façon de travailler pour la retrouver en\n"
          "un clic :\n\n"
          "1. Réglez la recherche, les facettes, le tri et les colonnes.\n"
          "2. Cliquez sur « Vues » dans la barre d'actions.\n"
          "3. Choisissez « Enregistrer la vue actuelle… ».\n"
          "4. Donnez un nom, puis validez par « Enregistrer ».\n\n"
          "Pour réappliquer une vue, ouvrez le même menu et cliquez sur son\n"
          "nom. Pour en supprimer une, passez par le sous-menu « Supprimer\n"
          "une vue ».\n\n"
          "Une vue conserve le texte recherché, les facettes actives, la\n"
          "colonne de tri et son sens, ainsi que la largeur et la visibilité\n"
          "de toutes les colonnes.\n\n"
          "Elle ne mémorise pas les curseurs BPM et énergie, ni les listes\n"
          "déroulantes de clé, genre et artiste, ni le mode Groupés / Détail.\n\n"
          "Enregistrer sous un nom existant remplace la vue précédente sans\n"
          "demander confirmation." },

        { "Q52 : Comment choisir et organiser les colonnes de la Bibliothèque ?",
          "La liste dispose de 25 colonnes : WAVE, ART, TITRE, ARTISTE,\n"
          "ALBUM, BPM, KEY, CAMELOT, ENERGY, DUREE, GENRE, NOTE, COL, CUES,\n"
          "STEMS, ANNEE, PLAYS, LUFS, MOOD, DANSE, LABEL, COMMENTAIRE, KBPS,\n"
          "JOUE LE et AJOUTE.\n\n"
          "- Pour afficher ou masquer une colonne, faites un clic droit sur\n"
          "  la ligne des en-têtes puis cochez ou décochez son nom. Le menu\n"
          "  reste ouvert pour en régler plusieurs à la suite. La colonne\n"
          "  TITRE ne peut pas être masquée.\n"
          "- Pour redimensionner, approchez le curseur du bord droit d'un\n"
          "  en-tête : il change de forme, glissez alors horizontalement.\n"
          "- Pour trier, cliquez sur l'en-tête. Un second clic sur la même\n"
          "  colonne inverse le sens ; une flèche indique le tri actif.\n"
          "- Si la somme des largeurs dépasse la fenêtre, une barre de\n"
          "  défilement horizontale apparaît et les en-têtes suivent.\n\n"
          "Largeurs et colonnes visibles sont conservées d'une session à\n"
          "l'autre, et peuvent être mémorisées dans une vue." },

        { "Q53 : Comment traiter les doublons et modifier plusieurs fiches à la fois ?",
          "Pour les doublons, faites un clic droit sur un morceau puis\n"
          "choisissez « Trouver les doublons ». La fenêtre propose trois\n"
          "méthodes :\n\n"
          "- « Par nom de fichier ».\n"
          "- « Par metadonnees (titre/artiste/duree) », méthode par défaut.\n"
          "- « Par empreinte audio », qui compare taille et durée.\n\n"
          "Cliquez sur « Analyser ». Chaque paire indique le fichier à\n"
          "GARDER en vert et celui à RETIRER en rouge, avec un pourcentage de\n"
          "confiance. BeatMate conserve le fichier réellement présent sur le\n"
          "disque, sinon le plus volumineux. Cliquez sur une ligne pour la\n"
          "cocher, puis sur « Fusionner la selection » : les points de repère,\n"
          "la note et les playlists sont reportés sur le morceau conservé\n"
          "avant que le doublon soit retiré de la bibliothèque. Le fichier\n"
          "audio n'est jamais effacé du disque.\n\n"
          "Pour modifier plusieurs fiches, sélectionnez les lignes puis\n"
          "appuyez sur Ctrl+E, ou faites un clic droit et choisissez\n"
          "« Modifier les metadonnees... ». Cochez les champs à écraser parmi\n"
          "Titre, Artiste, Album, Genre, BPM, Cle, Energie, Note, Couleur,\n"
          "Label et Commentaire, puis cliquez sur « Appliquer ». Seuls les\n"
          "champs cochés sont modifiés, sur les lignes sélectionnées." },

        { "Q54 : Comment comparer deux dossiers de musique ?",
          "La vue Comparaison sert à vérifier une clé USB, un disque de\n"
          "sauvegarde ou un second ordinateur :\n\n"
          "1. Ouvrez Comparaison dans le menu latéral.\n"
          "2. Cliquez sur « Dossier A… » puis « Dossier B… ».\n"
          "3. Réglez les options : « Sous-dossiers » et « Audio seulement »\n"
          "   sont actives par défaut. Choisissez le critère de comparaison :\n"
          "   « Nom », « Nom + taille » (par défaut) ou « Nom + taille +\n"
          "   date ».\n"
          "4. Cliquez sur « Comparer ». Le bouton devient « Stop » pendant\n"
          "   l'analyse.\n\n"
          "Chaque fichier est classé « A seulement », « B seulement »,\n"
          "« Différents » ou « Identiques ». Les quatre pastilles du haut\n"
          "affichent les compteurs et permettent de masquer une catégorie.\n\n"
          "« Copier A → B » copie vers B les fichiers absents ou différents ;\n"
          "« Copier B → A » fait l'inverse. Sans sélection, l'opération porte\n"
          "sur toute la liste affichée, et les fichiers différents sont\n"
          "remplacés ; une confirmation indique le nombre de fichiers.\n\n"
          "« Rapport CSV » enregistre le tableau complet. Les deux dossiers\n"
          "choisis sont mémorisés pour la prochaine ouverture." },

        { "Q55 : Pourquoi Mix Studio me demande-t-il une licence ?",
          "Mix Studio est réservé aux éditions Professional et Premium.\n\n"
          "Si vous cliquez sur « Mix » dans le menu latéral avec une licence\n"
          "d'un niveau inférieur, ou pendant la période d'essai, un message\n"
          "s'affiche : « Mix Studio necessite la version Professional ou\n"
          "Premium. » Aucune fenêtre ne s'ouvre.\n\n"
          "Pour vérifier votre édition, ouvrez Paramètres puis l'onglet\n"
          "Licence.\n\n"
          "Si votre licence est bien Professional ou Premium mais que le\n"
          "message « Module introuvable. Reinstallez la suite BeatMate. »\n"
          "apparaît, c'est que le module n'est pas présent sur le disque :\n"
          "réinstallez la suite complète en conservant la même licence.\n\n"
          "Mix Studio n'est pas un onglet de BeatMate : c'est une application\n"
          "distincte qui s'ouvre dans sa propre fenêtre. Vous pouvez donc\n"
          "continuer à travailler dans BeatMate pendant qu'elle est ouverte." },

        { "Q56 : Comment se servir de Mix Studio (projets, stems, transitions) ?",
          "Mix Studio s'ouvre sur une timeline. Le menu Fichier propose\n"
          "« Nouveau », « Ouvrir... », « Recents », « Enregistrer... »,\n"
          "« Exporter le mix... », « Importer des morceaux... »,\n"
          "« Enregistrer une voix off... » et « Quitter ».\n\n"
          "Un projet est enregistré au format .mixset : il mémorise votre\n"
          "montage, pas les fichiers audio eux-mêmes.\n\n"
          "Pour ajouter des morceaux, utilisez « Importer des morceaux... »,\n"
          "le bouton d'import de la barre de transport, ou glissez-déposez\n"
          "vos fichiers dans la fenêtre.\n\n"
          "Les stems séparent un morceau en pistes distinctes (voix, batterie,\n"
          "basse, reste) afin de mélanger la voix d'un titre avec\n"
          "l'instrumental d'un autre. La séparation se lance sur les clips\n"
          "sélectionnés et affiche une barre de progression ; un clip déjà\n"
          "traité est ignoré. Le panneau de stems occupe le bas de la\n"
          "fenêtre, à côté de l'éditeur de transition et du mixeur.\n\n"
          "Le bouton d'automix ordonne et enchaîne le set automatiquement,\n"
          "et l'éditeur de transition règle chaque enchaînement. Ctrl+Z et\n"
          "Ctrl+Y annulent et rétablissent, en indiquant l'action concernée." },

        { "Q57 : Comment exporter mon mix depuis Mix Studio ?",
          "Choisissez « Exporter le mix... » dans le menu Fichier. La\n"
          "fenêtre d'export propose :\n\n"
          "- Le format : WAV, MP3, FLAC ou OGG.\n"
          "- La fréquence : 44100, 48000 ou 96000 Hz.\n"
          "- La résolution : 16 ou 24 bits.\n"
          "- Le débit pour les formats compressés : 192, 256 ou 320 kbps.\n"
          "- L'étendue : « Tout le mix » ou « Boucle / selection ».\n"
          "- « Normaliser le volume (LUFS) », avec trois cibles :\n"
          "  -9 LUFS (club), -14 LUFS (streaming) ou -16 LUFS (podcast).\n"
          "- « Exporter une cue sheet (.cue) », en hot cues ou memory cues.\n"
          "- « Destination... » pour choisir le fichier de sortie.\n\n"
          "Cliquez ensuite sur « Exporter » ; une barre indique la\n"
          "progression et l'étape en cours.\n\n"
          "La même fenêtre exporte la liste des titres pour d'autres\n"
          "logiciels : « M3U8 », « Rekordbox XML », « Tracklist TXT/CSV »,\n"
          "« Serato CSV » et « Engine / Traktor / VirtualDJ ».\n\n"
          "Si le mix est vide, un message vous invite à ajouter d'abord des\n"
          "morceaux." },

        { "Q58 : À quoi servent les profils DJ de Live Suggest ?",
          "Un profil règle d'un coup la façon dont Live Suggest choisit les\n"
          "morceaux : plage de BPM, genre privilégié, évolution de l'énergie\n"
          "et type de lieu.\n\n"
          "La liste déroulante située en haut des suggestions contient\n"
          "« (aucun profil) » puis cinq profils fournis :\n\n"
          "- « Club Peak Hours » : 122 à 132 BPM, house, énergie croissante.\n"
          "- « Wedding Classics » : 95 à 130 BPM, tous genres.\n"
          "- « Lounge Bar » : 90 à 115 BPM, deep house, énergie stable.\n"
          "- « Festival Main Stage » : 124 à 150 BPM, énergie croissante.\n"
          "- « Warm-up Session » : 100 à 122 BPM, énergie stable.\n\n"
          "Sélectionner un profil l'applique immédiatement. Le bouton\n"
          "« Enregistrer comme... » crée une copie du profil actif sous un\n"
          "nouveau nom.\n\n"
          "Le profil actif n'est pas conservé au redémarrage : la liste\n"
          "revient sur « (aucun profil) », il faut le resélectionner en début\n"
          "de soirée.\n\n"
          "Live Suggest ne comporte pas de réglage manuel des pondérations :\n"
          "l'influence de chaque critère est gérée par le moteur lui-même." },

        { "Q59 : Comment envoyer une suggestion vers mon logiciel DJ ?",
          "Live Suggest ne pilote pas les platines de votre logiciel DJ. Il\n"
          "vous fait gagner la saisie du titre :\n\n"
          "1. Cliquez sur « Envoyer » sur la ligne de la suggestion.\n"
          "2. BeatMate copie « Artiste Titre » dans le presse-papiers et\n"
          "   rappelle la marche à suivre.\n"
          "3. Dans votre logiciel DJ, faites Ctrl+F pour ouvrir la recherche,\n"
          "   Ctrl+V pour coller, puis Entrée.\n\n"
          "Cette méthode fonctionne avec VirtualDJ, Rekordbox, Serato,\n"
          "Traktor et Engine.\n\n"
          "Deux autres possibilités :\n"
          "- Glissez la ligne de la suggestion directement dans la fenêtre de\n"
          "  votre logiciel DJ, si celui-ci accepte le glisser-déposer.\n"
          "- Double-cliquez sur la suggestion pour l'envoyer dans la vue\n"
          "  Preparation de BeatMate.\n\n"
          "Un simple clic sur la ligne lance la préécoute. Le clic droit sur\n"
          "un titre du classement HOT 100 propose « Préécouter (30 s) » et\n"
          "« Retrouver dans la bibliothèque »." },

        { "Q60 : Comment garder Live Suggest visible pendant que je mixe ?",
          "Deux réglages, tous deux accessibles depuis la fenêtre Live :\n\n"
          "- Le bouton « Mode Compact » réduit la fenêtre à l'essentiel :\n"
          "  le titre en cours, l'artiste, puis trois pastilles indiquant le\n"
          "  BPM, la clé et l'énergie. Si la hauteur le permet, les trois\n"
          "  premières suggestions restent affichées. Le bouton « Mode\n"
          "  Plein » rétablit l'affichage complet.\n"
          "- La case « Always on Top » maintient la fenêtre au-dessus des\n"
          "  autres applications, y compris votre logiciel DJ.\n\n"
          "Le mode compact réduit la fenêtre à environ 200 pixels de côté et\n"
          "vous pouvez ensuite la redimensionner et la placer où vous voulez.\n\n"
          "Le maintien au premier plan est conservé au redémarrage. Le mode\n"
          "compact, lui, doit être réactivé à chaque lancement.\n\n"
          "En haut de la fenêtre, la liste des sources permet de choisir le\n"
          "logiciel surveillé, et le bouton « Connecter » démarre la\n"
          "détection du morceau en cours." },

        { "Q61 : Que fait Live Suggest quand mon logiciel DJ passe au premier plan ?",
          "Un réglage évite que la fenêtre Live vous gêne pendant le mix.\n"
          "Ouvrez « Paramètres » dans la fenêtre Live et réglez la liste\n"
          "« Quand un logiciel DJ est actif: » :\n\n"
          "- « Ne rien faire » : la fenêtre reste telle quelle.\n"
          "- « Fondu (transparence 55%) » : la fenêtre devient translucide.\n"
          "  C'est le comportement par défaut.\n"
          "- « Reduire dans la barre des taches » : la fenêtre est réduite.\n"
          "- « Mode dock compact (haut-droite) » : la fenêtre se réduit à une\n"
          "  petite bande placée en haut à droite de l'écran, au-dessus des\n"
          "  autres applications.\n\n"
          "Dès qu'un de ces modes s'active, une icône marquée BM apparaît\n"
          "dans la zone de notification, à côté de l'horloge. Son infobulle\n"
          "indique « Live Suggest - Cliquer pour restaurer ».\n\n"
          "Cliquer sur cette icône, ou simplement survoler celle-ci, rétablit\n"
          "la fenêtre complète. Un clic droit ouvre un menu contenant\n"
          "« Afficher Live », « Plein ecran » et « Quitter Live »." },

        { "Q62 : Mes données sont-elles sauvegardées automatiquement ?",
          "Oui. BeatMate enregistre une sauvegarde à la fermeture de\n"
          "l'application, et à intervalle régulier si la sauvegarde\n"
          "automatique est activée.\n\n"
          "Les sauvegardes sont placées dans :\n"
          "%APPDATA%\\BeatMate\\backups\n\n"
          "Chaque sauvegarde regroupe la base de données de la bibliothèque\n"
          "et les fichiers de configuration qui l'accompagnent : agenda,\n"
          "réglages de l'application, préréglages de filtres, thème et\n"
          "raccourcis. Tous portent le même horodatage, ce qui garantit une\n"
          "restauration cohérente.\n\n"
          "Pour régler la fréquence et le nombre de sauvegardes conservées,\n"
          "ouvrez Paramètres puis l'onglet Sauvegarde : « Sauvegarde\n"
          "automatique » avec un intervalle (« Toutes les heures »,\n"
          "« Quotidien » ou « Hebdomadaire ») et « Nombre maximum de\n"
          "backups : », réglable de 1 à 50. Les sauvegardes les plus\n"
          "anciennes sont supprimées au-delà de cette limite.\n\n"
          "Les boutons « Creer un backup maintenant » et « Restaurer un\n"
          "backup » de cet onglet agissent sur le fichier de réglages : pour\n"
          "revenir à un état complet après un incident, utilisez la copie\n"
          "horodatée du dossier backups." },

        { "Q63 : Comment vérifier qu'une mise à jour est disponible ?",
          "Ouvrez Paramètres puis l'onglet Général :\n\n"
          "1. Cliquez sur « Verifier les mises a jour en ligne ».\n"
          "2. Le message indique « A jour » suivi de votre version, ou\n"
          "   « Mise a jour disponible » suivi du nouveau numéro.\n"
          "3. Si une version existe, une fenêtre propose de la télécharger et\n"
          "   de l'installer. Confirmez par « Mettre a jour ».\n"
          "4. BeatMate télécharge l'installeur en affichant la progression,\n"
          "   le lance, puis se ferme pour terminer l'installation.\n\n"
          "L'option « Mise a jour automatique au demarrage » lance cette\n"
          "vérification à chaque lancement de l'application.\n\n"
          "Le bouton « Installer depuis un .msi... » permet d'installer un\n"
          "fichier téléchargé à la main, utile sur un poste sans connexion.\n\n"
          "Vos morceaux, vos analyses et vos réglages sont conservés lors\n"
          "d'une mise à jour : ils vivent dans %APPDATA%\\BeatMate, en dehors\n"
          "du dossier de l'application. Une sauvegarde est de plus créée à la\n"
          "fermeture, juste avant l'installation." },

        { "Q64 : Comment changer la langue, le thème et le dossier de données ?",
          "Tout se passe dans Paramètres, onglet Général.\n\n"
          "- « Langue : » propose deux choix, « Francais » et « English ».\n"
          "  Le changement est appliqué immédiatement, sans redémarrage.\n"
          "- « Theme : » propose six présentations : « Sombre », « Clair »,\n"
          "  « Nord », « Dracula », « Haut contraste » et « Personnalise\n"
          "  (fichier) ». La sélection s'applique dès que vous la choisissez,\n"
          "  sans passer par « Appliquer ». Au prochain démarrage,\n"
          "  l'application revient au thème Sombre.\n"
          "- « Dossier de donnees : » affiche l'emplacement de vos réglages\n"
          "  et de vos sauvegardes, normalement %APPDATA%\\BeatMate. Le\n"
          "  bouton « Changer... » permet d'en désigner un autre.\n\n"
          "En bas de la fenêtre, « Par defaut » remet tous les réglages à\n"
          "leur valeur d'origine, « Annuler » abandonne les modifications en\n"
          "cours et « Appliquer » les valide.\n\n"
          "Le bouton rouge « Reinitialiser tous les paramètres » a le même\n"
          "effet que « Par defaut », après confirmation." },

        { "Q65 : Comment régler l'audio, la puissance de calcul et voir les raccourcis ?",
          "Dans Paramètres, onglet Audio :\n\n"
          "- « Peripherique de sortie : » choisit la carte son utilisée.\n"
          "- « Peripherique d'entree : » sert à l'enregistrement ; laissez\n"
          "  « Aucun (desactive) » si vous n'enregistrez pas.\n"
          "- « Taille du buffer : » de 64 à 2048 échantillons, 512 par\n"
          "  défaut. Une valeur basse réduit la latence mais expose aux\n"
          "  coupures ; augmentez-la si le son craque.\n"
          "- « Frequence d'echantillonnage : » 44100, 48000, 88200 ou\n"
          "  96000 Hz. 44100 Hz convient à la quasi-totalité des morceaux.\n"
          "- La ligne « Latence : » recalcule le délai en millisecondes à\n"
          "  chaque changement.\n\n"
          "Dans l'onglet Analyse, « Parallelisme : » choisit le nombre de\n"
          "traitements simultanés : 1, 2, 4 ou 8 threads.\n\n"
          "L'onglet Raccourcis affiche la liste des raccourcis clavier avec\n"
          "leur action, par exemple Ctrl+I pour importer, Ctrl+A pour\n"
          "analyser la sélection, Ctrl+F pour rechercher et F1 pour l'aide.\n"
          "Ce tableau est une référence à consulter : les raccourcis ne se\n"
          "modifient pas depuis cette fenêtre." },

    };

    for (auto& faq : faqs)
        m_faqContainer->addPanel(new FAQPanel(juce::String::fromUTF8(faq.q),
                                              juce::String::fromUTF8(faq.a)));

    return page;
}

void HelpView::buildDocumentationDB()
{
    m_documentationDB = {
        { "importer musique import fichiers dossiers drag drop",
          "Pour importer votre musique dans BeatMate V12 :\n\n"
          "1. Glisser-deposer : faites glisser vos fichiers ou dossiers depuis\n"
          "   l'Explorateur directement dans la fenetre BeatMate.\n\n"
          "2. Menu Import (Ctrl+I) : parcourez et selectionnez des dossiers.\n"
          "   BeatMate scanne recursivement tous les sous-dossiers.\n\n"
          "3. Dossiers surveillés : configurez dans Paramètres > Bibliothèque\n"
          "   pour un import automatique continu.\n\n"
          "Formats supportes : MP3, WAV, FLAC, AIFF, AAC, OGG, M4A, WMA.\n"
          "L'import est non-destructif : vos fichiers ne sont jamais modifies." },

        { "analyser analyse BPM key tonalite energy",
          "Pour analyser vos pistes :\n\n"
          "1. Selectionnez les pistes dans la bibliothèque.\n"
          "2. Ctrl+A pour analyser la selection.\n"
          "3. Ctrl+Shift+A pour analyser toute la bibliothèque.\n\n"
          "L'analyse detecte :\n"
          "- BPM : tempo du morceau (precision 0.1 BPM)\n"
          "- Key : tonalite en Camelot (1A-12B)\n"
          "- Energy : niveau d'energie (1-10)\n"
          "- Structure : intro, verse, drop, outro\n\n"
          "Qualité configurable dans Paramètres > Analyse :\n"
          "Rapide, Standard, Haute, Ultra." },

        { "set preparer preparation organiser DJ",
          "Comment preparer un set DJ :\n\n"
          "1. Creez un nouveau set (Ctrl+N > Set)\n"
          "2. Filtrez votre bibliothèque par genre/BPM/energy\n"
          "3. Glissez les pistes dans le set\n"
          "4. Verifiez les indicateurs de compatibilite :\n"
          "   - Vert = BPM et Key compatibles\n"
          "   - Jaune = partiellement compatible\n"
          "   - Rouge = clash harmonique\n"
          "5. Utilisez 'IA Auto-Arrange' pour optimiser l'ordre\n"
          "6. Verifiez la courbe d'energie\n"
          "7. Exportez vers USB ou logiciel DJ (Ctrl+E)\n\n"
          "Pour un set de 2h : prevoyez ~30 pistes + alternatives." },

        { "soiree nuit phases evening party",
          "Preparation de Soiree dans BeatMate :\n\n"
          "Phases typiques :\n"
          "- Accueil (22h-23h) : ambiance, Energy 2-4\n"
          "- Montee (23h-00h) : progression, Energy 4-6\n"
          "- Peak (00h-02h) : peak time, Energy 7-10\n"
          "- Descente (02h-03h) : retour calme, Energy 5-7\n"
          "- After (03h-05h) : ambient/deep, Energy 2-4\n\n"
          "Profils predefinis : Club, Festival, Lounge, After Party.\n"
          "'IA Auto-Remplir' selectionne les pistes pour chaque phase." },

        { "exporter export USB CDJ XDJ Rekordbox Serato Traktor",
          "Pour exporter vos pistes :\n\n"
          "1. Selectionnez les pistes ou playlists.\n"
          "2. Ctrl+E pour ouvrir le panneau d'export.\n"
          "3. Choisissez le format :\n"
          "   - Export USB : structure Pioneer pour CDJ/XDJ\n"
          "   - Rekordbox XML : playlists, hot cues, grilles\n"
          "   - Serato : crates, tags, BPM, key\n"
          "   - Traktor NML : playlists, cue points\n"
          "   - Engine DJ : Denon database\n"
          "   - M3U/CSV/PDF : formats universels\n\n"
          "Tous les metadata sont preserves dans l'export." },

        { "live Live Suggest suggestions temps réel",
          "Live Suggest — Assistant temps réel :\n\n"
          "1. Lancez avec Ctrl+L.\n"
          "2. Pendant votre mix, BeatMate affiche :\n"
          "   - La piste en cours (BPM, Key, Energy)\n"
          "   - 5 suggestions pour la prochaine piste\n"
          "   - Score de compatibilité pour chaque suggestion\n"
          "   - Courbe d'énergie en temps réel\n\n"
          "Modes de suggestion :\n"
          "- Mon Style : basé sur vos habitudes de mix\n"
          "- Smart AI : algorithme optimisé BeatMate" },

        { "normalisation volume LUFS loudness",
          "Normalisation audio dans BeatMate :\n\n"
          "LUFS = Loudness Units Full Scale (standard international).\n\n"
          "Presets disponibles :\n"
          "- Streaming (-14 LUFS) : Spotify, Apple Music\n"
          "- Club (-8 LUFS) : niveau club\n"
          "- Festival (-6 LUFS) : niveau maximum\n"
          "- Personnalise : votre valeur cible\n\n"
          "Traitement non-destructif : seul le gain de lecture est ajuste." },

        { "hot cues cue points repere waveform",
          "Hot Cues dans BeatMate :\n\n"
          "Placer : cliquez sur la waveform ou Ctrl+B (auto-generation).\n"
          "Nommer : double-cliquez sur un cue pour le renommer.\n"
          "Colorer : clic droit pour changer la couleur.\n\n"
          "Convention recommandee :\n"
          "- Vert = Intro / Mix IN\n"
          "- Rouge = Drop\n"
          "- Bleu = Breakdown\n"
          "- Jaune = Outro / Mix OUT\n\n"
          "Les hot cues sont exportes vers Rekordbox, Serato, Traktor, Engine DJ." },

        { "playlist smart intelligente regles dynamique",
          "Smart Playlists :\n\n"
          "Playlists dynamiques basees sur des regles.\n"
          "Se mettent a jour automatiquement.\n\n"
          "Exemple de regles :\n"
          "- Genre = Tech House\n"
          "- BPM entre 124 et 130\n"
          "- Energy >= 6\n"
          "- Rating >= 4 etoiles\n"
          "- Date ajout < 30 jours\n\n"
          "'IA Build' : decrivez en langage naturel et BeatMate genere la playlist." },

        { "licence activer cle activation achat acheter",
          "Licence BeatMate V12 :\n\n"
          "1. Paramètres > Licence\n"
          "2. Entrez votre cle XXXXX-XXXXX-XXXXX-XXXXX-XXXXX\n"
          "3. Cliquez 'Activer'\n\n"
          "Types : Trial (30j), Personal (1 machine), Professional (2),\n"
          "Family (5), Enterprise (illimite).\n\n"
          "Acheter : https://beatmate.fr/tarifs/\n"
          "Support : support@beatmate.fr" },

        { "streaming tendances charts decouvrir musique nouvelle",
          "Module Streaming de BeatMate :\n\n"
          "- Charts par genre et region\n"
          "- Tendances sur 30/60/90 jours\n"
          "- Recommandations basees sur votre bibliothèque\n"
          "- Nouvelles sorties dans vos genres favoris\n"
          "- Preview des pistes directement dans BeatMate\n"
          "- Integration Spotify, Beatport, SoundCloud" },

        { "sauvegarde backup restaurer donnees",
          "Sauvegarde dans BeatMate :\n\n"
          "Auto-backup : activee par defaut, intervalle configurable.\n"
          "Backup manuel : Paramètres > Sauvegarde > Creer un backup.\n"
          "Restauration : selectionnez un backup et cliquez 'Restaurer'.\n\n"
          "Les backups incluent : base de donnees, playlists, sets,\n"
          "paramètres, hot cues, ratings, tags." },

        { "paramètres configuration reglages settings",
          "Paramètres BeatMate V12 :\n\n"
          "- Général : langue, theme, auto-save, dossier donnees\n"
          "- Audio : peripherique, buffer, sample rate, latence\n"
          "- Analyse : qualité, BPM range, key notation, threads\n"
          "- Bibliothèque : dossiers surveillés, auto-import, doublons\n"
          "- DJ Software : detection, sync auto, chemins DB\n"
          "- Licence : cle, activation, statut, Machine ID\n"
          "- Sauvegarde : auto-backup, restauration\n"
          "- Raccourcis : table editable, reset par defaut\n"
          "- Apparence : couleur accent, police, densite, waveform" },

        { "camelot wheel harmonic mix harmonique tonalite compatibilite",
          "Camelot Wheel - Mix harmonique :\n\n"
          "Le systeme Camelot attribue un code de 1A a 12B.\n\n"
          "Regles de compatibilite :\n"
          "- Meme code = parfait (8A -> 8A)\n"
          "- +/- 1 = fluide (8A -> 7A ou 9A)\n"
          "- A <-> B meme numero = relatif (8A <-> 8B)\n"
          "- +7 semitones = energy boost (8A -> 3A)\n\n"
          "BeatMate calcule automatiquement la compatibilite\n"
          "et l'affiche dans les suggestions et le scoring de set." },

        { "raccourcis clavier keyboard shortcuts",
          "Raccourcis principaux de BeatMate V12 :\n\n"
          "Ctrl+I : Importer\n"
          "Ctrl+A : Analyser la selection\n"
          "Ctrl+Shift+A : Analyser toute la bibliothèque\n"
          "Ctrl+E : Exporter\n"
          "Ctrl+N : Nouveau set/playlist\n"
          "Ctrl+L : Live Suggest\n"
          "Ctrl+F : Recherche\n"
          "Ctrl+S : Sauvegarder\n"
          "Space : Play/Pause\n"
          "F1 : Aide\n"
          "Ctrl+, : Paramètres\n"
          "Ctrl+B : Hot cues auto\n"
          "F11 : Plein ecran" },
        { "agenda date prestation cachet rappel ics calendrier soiree planning mois semaine annee",
          juce::String::fromUTF8("L'Agenda de BeatMate (menu Agenda) :\n\n"
          "- Trois vues : Mois, Semaine et Annee. Les fleches et le bouton\n"
          "  Aujourd'hui deplacent la periode affichee.\n"
          "- Double-cliquez sur un jour pour creer une prestation, ou sur une\n"
          "  prestation existante pour la modifier.\n"
          "- Champs d'une prestation : titre, lieu, ville, date, heure de debut\n"
          "  et de fin, style, cachet et devise, statut et notes.\n"
          "- Quatre statuts : A confirmer, Confirme, Passe, Annule. La couleur\n"
          "  de la pastille est deduite automatiquement du style, du lieu ou du\n"
          "  titre ; les prestations annulees passent en gris.\n"
          "- Rappels : huit delais au choix (15 min, 30 min, 1 h, 2 h, 1 jour,\n"
          "  1 semaine, 1 mois, 2 mois), cumulables. Le bouton Rappels definit\n"
          "  le reglage global, et chaque prestation peut avoir le sien.\n"
          "  A l'echeance, BeatMate affiche une cloche dans la barre d'outils,\n"
          "  joue le son d'alerte du systeme et affiche un message dans l'appli.\n"
          "- Exports : .ics (iCalendar, a importer dans Google Agenda), HTML,\n"
          "  PDF, Word (.docx) et CSV. Les fichiers sont ecrits sur le Bureau.\n"
          "- Import : le bouton Importer .ics cree une prestation par evenement\n"
          "  du fichier. Il n'y a pas de detection de doublon a l'import.\n\n"
          "Les prestations sont enregistrees dans events.json, dans le dossier\n"
          "de donnees BeatMate.") },
        { "comparaison comparer dossiers dossier copier synchroniser difference identique csv",
          juce::String::fromUTF8("La Comparaison de dossiers (menu Comparaison) :\n\n"
          "Elle compare deux dossiers du disque, Dossier A et Dossier B.\n"
          "Ce n'est pas une comparaison de bibliotheques.\n\n"
          "Options de scan :\n"
          "- Sous-dossiers (recursif, actif par defaut).\n"
          "- Audio seulement (actif par defaut) : mp3, wav, flac, aac, m4a,\n"
          "  ogg, aiff, aif, wma.\n"
          "- Critere : Nom, Nom + taille (par defaut), ou Nom + taille + date.\n"
          "  Il n'y a pas de comparaison du contenu audio ni d'empreinte.\n\n"
          "Quatre etats, filtrables par pastilles qui affichent leur nombre :\n"
          "A seulement, B seulement, Differents, Identiques. Identiques est\n"
          "decoche par defaut.\n\n"
          "Colonnes : etat, chemin relatif, Taille A, Taille B, Date A, Date B.\n"
          "Double-cliquez sur une ligne pour ouvrir le fichier dans l'Explorateur.\n\n"
          "Actions :\n"
          "- Copier A vers B et Copier B vers A. La copie porte sur les lignes\n"
          "  selectionnees, ou sur toutes les lignes affichees si rien n'est\n"
          "  selectionne. Les sous-dossiers manquants sont crees et la\n"
          "  comparaison est relancee a la fin.\n"
          "- Rapport CSV : ecrit comparaison.csv sur le Bureau.") },
        { "editeur tags tag metadonnees titre artiste album pochette renommer casse deezer itunes",
          juce::String::fromUTF8("L'editeur de tags (bouton Editeur de tags dans la Bibliothèque) :\n\n"
          "Il charge les morceaux coches, sinon la selection, sinon la liste\n"
          "affichee.\n\n"
          "Douze champs modifiables : Titre, Artiste, Album, Genre, Annee, BPM,\n"
          "Cle, Energie, Label, Mood, Commentaire et Tags perso. En selection\n"
          "multiple, un champ affichant <multiple> n'est pas ecrit.\n\n"
          "Traitement par lot : chaque ligne a une case a cocher, avec Tout\n"
          "cocher / Tout decocher et selection d'une plage par Maj + clic.\n"
          "Toutes les actions ne portent que sur les lignes cochees.\n\n"
          "Actions disponibles :\n"
          "- Ecrire les tags dans les fichiers et dans la base.\n"
          "- Renommer les fichiers selon un masque, par defaut\n"
          "  %artist% - %title% (jetons : artist, title, album, genre, year,\n"
          "  bpm, key).\n"
          "- Tags depuis le nom : coupe le nom de fichier au premier tiret.\n"
          "- Casse : Title Case, MAJUSCULES, minuscules, Premiere lettre.\n"
          "- Nettoyer : supprime les underscores, les espaces doubles et les\n"
          "  suffixes promotionnels (Official Video, HD, Clip Officiel...).\n"
          "- Pochette : chercher sur Internet, importer une image, exporter\n"
          "  la pochette, retirer la pochette.\n"
          "- En ligne : completer les tags et le BPM, chercher les pochettes,\n"
          "  ou tout automatique.\n\n"
          "La recherche en ligne interroge Deezer (BPM et pochettes) et iTunes\n"
          "(titre, artiste, album, genre, annee). Aucune cle d'API n'est requise.\n"
          "Un score de similarite decide de l'application : rien n'est ecrit\n"
          "sous le seuil, et les versions karaoke, tribute ou instrumentales\n"
          "sont ecartees. Les resultats restent en attente : vous devez\n"
          "cliquer sur Ecrire les tags pour les enregistrer.\n\n"
          "Les Tags perso ne sont pas ecrits dans le fichier audio : ils sont\n"
          "stockes uniquement dans la base BeatMate.") },
        { "editeur audio couper rogner fondu gain silence normaliser inverser wavepad forme onde",
          juce::String::fromUTF8("L'editeur audio (Normalisation, bouton Editer le fichier) :\n\n"
          "Neuf operations :\n"
          "- Couper : supprime la selection.\n"
          "- Rogner : ne garde que la selection.\n"
          "- Silence : remplace la selection par du silence.\n"
          "- Gain : demande une valeur en dB et l'applique.\n"
          "- Fondu entrant et Fondu sortant, avec sept courbes : Lineaire,\n"
          "  Puissance constante, Exponentiel, Logarithmique, Courbe en S,\n"
          "  Rapide, Lent.\n"
          "- Normaliser : normalisation crete a -0,3 dBFS sur tout le fichier.\n"
          "- Inverser : lit la selection a l'envers.\n"
          "- Annuler et Retablir.\n\n"
          "Sans selection, l'operation porte sur tout le fichier.\n"
          "L'historique d'annulation retient quarante etapes ; il est remis a\n"
          "zero a chaque ouverture de fichier.\n\n"
          "Affichage : forme d'onde avec reglettes en dB des deux cotes, zone\n"
          "rouge au-dessus de -1 dBFS, zoom aux boutons + et -, a la molette,\n"
          "ou par Zoom tout. Le pied de page indique le debut, la fin et la\n"
          "duree de la selection, ainsi que sa crete et son niveau RMS.\n\n"
          "Ecoute : la barre d'espace joue la selection.\n\n"
          "Enregistrement : Enregistrer sous (wav, mp3, flac, ogg) ou Remplacer\n"
          "l'original. Dans ce dernier cas, l'original est d'abord copie dans\n"
          "le dossier audio_backups avec un horodatage.\n\n"
          "Il n'y a ni copier ni coller : Couper supprime sans rien memoriser.") },
        { "corrompu corrompus integrite reparer verifier fichier abime illisible ffmpeg suspect",
          juce::String::fromUTF8("Verification et reparation des fichiers (Bibliothèque, bouton\n"
          "Verifier / Reparer, ou module Analyse) :\n\n"
          "BeatMate decode chaque fichier de bout en bout et le classe :\n"
          "- Intact : aucune erreur.\n"
          "- Suspect : le decodage aboutit mais signale des avertissements.\n"
          "- Corrompu : le decodage echoue.\n"
          "- Illisible : le fichier est introuvable.\n\n"
          "Portees proposees : la selection, les morceaux affiches, ou toute\n"
          "la bibliothèque. Depuis le module Analyse, la verification tourne\n"
          "sur plusieurs threads et peut etre interrompue.\n\n"
          "La fenetre de rapport permet de filtrer par Corrompus, Suspects,\n"
          "Non verifies et Intacts, de cocher les fichiers, d'ouvrir leur\n"
          "dossier et d'exporter un rapport CSV.\n\n"
          "Reparation : BeatMate reconstruit le conteneur du fichier sans\n"
          "reencoder, donc sans perte de qualité. L'original est toujours\n"
          "copie dans le dossier integrity_backups avant remplacement, et le\n"
          "fichier repare est reverifie ensuite. Une reparation ne peut pas\n"
          "recreer de l'audio reellement manquant : elle corrige le conteneur.\n\n"
          "Les resultats sont mis en cache dans integrity_report.json et\n"
          "reevalues automatiquement si la taille ou la date du fichier change.\n"
          "La facette Etat > Corrompus reutilise ce cache sans relancer de scan.") },
        { "manquant manquants introuvable relier relink deplace renomme retrouver chemin casse",
          juce::String::fromUTF8("Fichiers manquants et relocalisation :\n\n"
          "Un fichier est manquant lorsque le chemin enregistre dans la\n"
          "bibliothèque ne pointe plus vers un fichier existant, par exemple\n"
          "apres un deplacement, un renommage de dossier ou un changement de\n"
          "lettre de lecteur.\n\n"
          "Au demarrage, BeatMate compte les fichiers introuvables et vous en\n"
          "avertit. Vous pouvez aussi filtrer la Bibliothèque avec la facette\n"
          "Etat > Fichiers manquants.\n\n"
          "Pour les relier : bouton Fichiers manquants de la Bibliothèque, ou\n"
          "clic droit > Relier les fichiers manquants.\n\n"
          "La recherche explore automatiquement les dossiers parents de votre\n"
          "bibliothèque ainsi que votre dossier Musique. Le bouton\n"
          "Chercher dans... ajoute un dossier et relance la recherche.\n\n"
          "Un candidat est retenu quand le nom de fichier correspond ; la\n"
          "confiance monte si la taille du fichier est identique. Chaque\n"
          "proposition s'affiche avec son pourcentage, l'ancien chemin en\n"
          "rouge et le nouveau en vert. Les correspondances les plus sures\n"
          "sont pre-cochees.\n\n"
          "Cliquez sur une ligne pour la cocher ou la decocher, puis sur\n"
          "Relier la selection : seul le chemin est mis a jour, vos analyses,\n"
          "cues et notes sont conserves.") },
        { "doublon doublons duplicate fusionner meme morceau deux fois empreinte",
          juce::String::fromUTF8("Gestion des doublons :\n\n"
          "Deux outils differents :\n\n"
          "1. La facette Etat > Doublons de la Bibliothèque. Elle regroupe\n"
          "   simplement les morceaux qui partagent le meme artiste et le meme\n"
          "   titre. C'est un filtre d'affichage, sans action associee.\n\n"
          "2. La fenetre Trouver les doublons (clic droit dans la\n"
          "   Bibliothèque). Elle propose trois methodes :\n"
          "   - Par nom de fichier : noms identiques une fois normalises.\n"
          "   - Par metadonnees (titre / artiste / duree), methode par defaut :\n"
          "     compare la similarite des titres et des artistes et verifie que\n"
          "     les durees sont proches.\n"
          "   - Par empreinte audio : regroupe les fichiers de taille\n"
          "     strictement identique dont la duree concorde.\n\n"
          "Chaque paire s'affiche avec une ligne GARDER en vert et une ligne\n"
          "RETIRER en rouge, chemins complets et pourcentage de confiance.\n"
          "BeatMate garde en priorite le fichier qui existe encore sur le\n"
          "disque, puis le plus volumineux.\n\n"
          "Fusionner la selection transfere les hot cues, la note et les\n"
          "appartenances aux playlists vers le morceau conserve, puis retire\n"
          "l'autre de la bibliothèque. Le fichier audio n'est jamais supprime\n"
          "du disque.\n\n"
          "A l'import, les doublons sont detectes et signales par un badge,\n"
          "puis ignores. Vous pouvez desactiver ce controle dans\n"
          "Reglages > Bibliothèque.") },
        { "facette facettes filtre filtrer genre famille groupes detail etat pastille chip",
          juce::String::fromUTF8("Les facettes de la Bibliothèque :\n\n"
          "La barre de facettes filtre la liste en un clic. Six dimensions :\n"
          "- Genre : un chip par genre, tries par nombre decroissant.\n"
          "- BPM : tranches (moins de 95, 95-105, 105-115, 115-122, 122-126,\n"
          "  126-130, 130-136, 136-142, 142 et plus), plus Sans BPM.\n"
          "- Energie : Douce 1-3, Moyenne 4-6, Forte 7-8, Max 9-10.\n"
          "- Annee : 70s et avant, 80s, 90s, 2000s, 2010s, 2020s.\n"
          "- Cle : les vingt-quatre codes Camelot, de 1A a 12B.\n"
          "- Etat : Fichiers manquants, Corrompus, Doublons, Sans BPM,\n"
          "  Sans cle, Sans genre, Sans annee, Non analyses, Jamais joues.\n\n"
          "Chaque pastille affiche le nombre de morceaux concernes, et les\n"
          "categories vides ne sont pas affichees.\n\n"
          "Vous pouvez cocher plusieurs pastilles. Dans une meme dimension,\n"
          "elles s'additionnent ; entre dimensions, elles se cumulent. Ainsi\n"
          "House + Techno avec 126-130 donne les morceaux House ou Techno\n"
          "dont le BPM est compris entre 126 et 130.\n"
          "Le bouton Effacer remet toutes les facettes a zero.\n\n"
          "Genres groupes ou detailles : sur la dimension Genre, le bouton\n"
          "Groupes / Detail bascule entre vos genres bruts et vingt-quatre\n"
          "familles (House, Techno, Trance, Disco / Funk, Soul / R&B,\n"
          "Hip-Hop / Rap, Reggae / Dancehall, Zouk, Kompa / Compas, Kizomba,\n"
          "Afro, Salsa / Latino, Rock, Metal, Electro / Dance, Pop, Jazz,\n"
          "Blues, Country, Classique, Gospel, Slow / Ballade,\n"
          "Variete francaise, Theme / B.O.).\n"
          "Regroupees, les variantes d'un meme genre se rassemblent sous une\n"
          "seule pastille.\n\n"
          "Les fleches a droite de la barre, ou la molette, font defiler les\n"
          "pastilles quand elles sont trop nombreuses.") },
        { "case cocher cochees selection tout cocher decocher lot maj clic",
          juce::String::fromUTF8("Cases a cocher et selection dans la Bibliothèque :\n\n"
          "Une colonne de cases a cocher est figee a gauche de la liste, avant\n"
          "toutes les colonnes.\n\n"
          "- Cliquez dans cette colonne pour cocher ou decocher une ligne.\n"
          "- Maj + clic coche ou decoche toute une plage.\n"
          "- Le bouton Tout cocher, ou la case de l'en-tete, coche ou decoche\n"
          "  tous les morceaux affiches. Le bouton devient Decocher (N).\n"
          "- Ctrl+A coche et selectionne tout ce qui est affiche.\n"
          "- Ctrl+D decoche tout et annule la selection.\n\n"
          "Regle importante : si au moins un morceau est coche, les actions\n"
          "portent sur les morceaux coches. Sinon, elles portent sur la\n"
          "selection de la liste. L'editeur de tags accepte une troisieme\n"
          "solution : si rien n'est coche ni selectionne, il charge la liste\n"
          "affichee.\n\n"
          "Les cases a cocher servent notamment a l'editeur de tags, a la\n"
          "verification d'integrite et aux traitements par lot. Elles\n"
          "survivent au defilement et au changement de tri, ce qui permet de\n"
          "constituer un lot en plusieurs passes.\n\n"
          "Il n'existe pas d'inversion de selection.") },
        { "vue vues sauvegardee enregistrer colonnes largeur tri disposition preset",
          juce::String::fromUTF8("Vues sauvegardees de la Bibliothèque (bouton Vues) :\n\n"
          "Une vue memorise l'etat de travail de la Bibliothèque :\n"
          "- le texte de la zone de recherche,\n"
          "- la colonne de tri et son sens,\n"
          "- la largeur de chacune des vingt-cinq colonnes,\n"
          "- les colonnes affichees ou masquees,\n"
          "- les facettes actives de chaque dimension.\n\n"
          "Ne sont pas memorises : les curseurs BPM et Energie, les listes\n"
          "deroulantes Cle, Genre, Artiste et Note, le mode Groupes / Detail,\n"
          "le noeud selectionne dans l'arborescence et les cases cochees.\n\n"
          "Le menu Vues liste vos vues, propose Enregistrer la vue actuelle\n"
          "et un sous-menu Supprimer une vue. Enregistrer sous un nom deja\n"
          "utilise remplace la vue existante.\n\n"
          "Colonnes : vingt-cinq colonnes disponibles, dont huit masquees par\n"
          "defaut (LUFS, MOOD, DANSE, LABEL, COMMENT, KBPS, JOUE LE, AJOUTE).\n"
          "Faites glisser le bord d'un en-tete pour redimensionner, ou faites\n"
          "un clic droit sur l'en-tete pour choisir les colonnes affichees.\n"
          "La colonne TITRE ne peut pas etre masquee et l'ordre des colonnes\n"
          "n'est pas modifiable. Quand la largeur totale depasse la fenetre,\n"
          "un defilement horizontal apparait.") },
        { "theme themes apparence couleur sombre clair nord dracula contraste personnalise",
          juce::String::fromUTF8("Themes et apparence :\n\n"
          "Le theme se choisit dans Reglages > Général, liste Theme. Il\n"
          "s'applique immediatement, sans redemarrage. Six themes :\n"
          "- Sombre\n"
          "- Clair\n"
          "- Nord\n"
          "- Dracula\n"
          "- Haut contraste\n"
          "- Personnalise (fichier)\n\n"
          "Le theme Personnalise lit un fichier theme.json place dans le\n"
          "dossier de donnees BeatMate. Il contient un objet colors dont\n"
          "chaque valeur est une couleur au format #RRGGBB. Il n'y a pas de\n"
          "selecteur de couleurs dans l'interface : ce fichier se modifie a\n"
          "la main.\n\n"
          "A noter : BeatMate demarre toujours sur le theme Sombre. Le theme\n"
          "choisi s'applique pour la session en cours.\n\n"
          "Ce qui se personnalise par ailleurs :\n"
          "- les colonnes affichees et leur largeur dans la Bibliothèque,\n"
          "- le regroupement des genres en familles,\n"
          "- les vues sauvegardees.") },
        { "mise a jour maj update version telecharger installer msi nouvelle",
          juce::String::fromUTF8("Mises a jour de BeatMate :\n\n"
          "Verification manuelle : Aide > A propos, bouton Verifier les mises\n"
          "a jour en ligne. Le meme bouton existe dans Reglages > Général.\n\n"
          "Verification automatique : BeatMate interroge le serveur peu apres\n"
          "le demarrage. Vous pouvez desactiver ce controle dans\n"
          "Reglages > Général.\n\n"
          "Si une version plus recente existe, BeatMate affiche la version, la\n"
          "date et les notes de version. En acceptant, le programme\n"
          "d'installation est telecharge avec une progression en pourcentage,\n"
          "puis lance ; BeatMate se ferme pour laisser l'installation se\n"
          "terminer.\n\n"
          "Installation depuis un fichier local : bouton Installer depuis un\n"
          ".msi, qui ouvre un selecteur de fichiers. Utile lorsque vous avez\n"
          "deja telecharge l'installeur.\n\n"
          "Les licences a vie incluent une fenetre de mise a jour. Passe cette\n"
          "echeance, BeatMate continue de fonctionner mais refuse les versions\n"
          "publiees apres la fin de votre periode de mise a jour, en vous\n"
          "l'indiquant.") },
        { "langue langue francais anglais language traduction interface",
          juce::String::fromUTF8("Langue de l'interface :\n\n"
          "BeatMate est disponible en deux langues : Francais et English.\n\n"
          "Le choix se fait dans Reglages > Général, liste Langue.\n"
          "Le changement est immediat : les menus, les boutons, les libelles\n"
          "de colonnes et le titre de la fenetre sont retraduits sans\n"
          "redemarrage.\n\n"
          "Le reglage est conserve d'une session a l'autre. Le francais est la\n"
          "langue par defaut.\n\n"
          "Les donnees de votre bibliothèque (titres, artistes, genres,\n"
          "commentaires) ne sont evidemment pas traduites.") },
        { "peripherique audio carte son sortie entree buffer latence frequence echantillonnage wasapi",
          juce::String::fromUTF8("Peripheriques audio :\n\n"
          "La configuration se trouve dans Reglages > Audio :\n"
          "- Peripherique de sortie.\n"
          "- Peripherique d'entree, regle sur Aucun par defaut.\n"
          "- Taille du buffer : 64, 128, 256, 512, 1024 ou 2048 echantillons.\n"
          "  512 par defaut.\n"
          "- Frequence d'echantillonnage : 44100, 48000, 88200 ou 96000 Hz.\n"
          "  44100 par defaut.\n"
          "- La latence correspondante est affichee en millisecondes et se met\n"
          "  a jour a chaque changement.\n\n"
          "Un buffer petit reduit la latence mais sollicite davantage le\n"
          "processeur ; en cas de craquements, augmentez-le.\n\n"
          "Changer le buffer ou la frequence redemarre le moteur audio : une\n"
          "breve coupure du son est normale.\n\n"
          "BeatMate utilise les pilotes audio standard de Windows. La\n"
          "preecoute rapide de la Bibliothèque et des suggestions passe par la\n"
          "sortie audio par defaut du systeme, independamment du choix fait\n"
          "ici.") },
        { "raccourci raccourcis clavier touche touches espace suppr echap entree ctrl",
          juce::String::fromUTF8("Raccourcis clavier reellement disponibles :\n\n"
          "Navigation, depuis n'importe quel module :\n"
          "- Ctrl+I : module Import\n"
          "- Ctrl+F : module Bibliothèque\n"
          "- Ctrl+E : module Export\n"
          "- Ctrl+L : module Live\n"
          "- F1 : cette aide\n"
          "- F5 : rafraichir l'affichage\n\n"
          "Bibliothèque :\n"
          "- Ctrl+F : place le curseur dans la zone de recherche\n"
          "- Ctrl+A : cocher et selectionner tout\n"
          "- Ctrl+D : tout decocher\n"
          "- Ctrl+E : ouvrir l'editeur de tags\n"
          "- Espace : preecouter le morceau selectionne\n"
          "- Suppr : retirer les morceaux selectionnes, avec confirmation\n\n"
          "Hot Cues :\n"
          "- 1 a 8 : poser le cue s'il est vide, sinon le jouer\n"
          "- Q : activer ou desactiver la quantification\n"
          "- Gauche / Droite : deplacer le cue selectionne de 10 ms\n"
          "- Maj + Gauche / Droite : le deplacer d'un temps\n"
          "- Suppr : effacer le cue selectionne\n"
          "- Espace : lecture / pause\n\n"
          "Editeur audio :\n"
          "- Espace : ecouter la selection\n"
          "- Suppr : couper la selection\n"
          "- Plus et Moins : zoom avant et arriere\n"
          "- Ctrl+Z : annuler, Ctrl+Y : retablir\n"
          "- Ctrl+A : tout selectionner\n\n"
          "Dans les fenetres de dialogue, Entree valide et Echap annule.\n\n"
          "L'onglet Raccourcis de cette aide en donne la liste complete.") },
        { "historique lecture joue joues recemment session statistiques ecoute play count",
          juce::String::fromUTF8("Historique de lecture :\n\n"
          "BeatMate enregistre chaque morceau charge dans la barre de lecture :\n"
          "la date et l'heure, le nombre de lectures et la derniere date de\n"
          "lecture du morceau.\n\n"
          "Ou le consulter :\n"
          "- Accueil : la carte Recemment joue affiche les derniers morceaux\n"
          "  avec leur anciennete.\n"
          "- Bibliothèque : les colonnes PLAYS et JOUE LE, et la facette\n"
          "  Etat > Jamais joues pour retrouver ce que vous n'avez pas encore\n"
          "  passe.\n"
          "- Live : l'historique de la session en cours peut etre exporte en\n"
          "  Rekordbox XML, Serato, Traktor NML ou CSV.\n\n"
          "L'historique alimente aussi le mode de suggestion Mon Style, qui\n"
          "apprend vos enchainements habituels.\n\n"
          "BeatMate peut par ailleurs importer l'historique de lecture de vos\n"
          "logiciels DJ, avec dedoublonnage des entrees deja connues.") },
        { "mix studio timeline transition automix harmonize megamix mashup medley remix voix off",
          juce::String::fromUTF8("BeatMate Mix Studio :\n\n"
          "Mix Studio est une application distincte, lancee depuis l'entree\n"
          "Mix du menu lateral. Elle necessite une licence Professional ou\n"
          "Premium. Les mix sont enregistres dans des fichiers .mixset.\n\n"
          "Montage :\n"
          "- Timeline multi-pistes ou vous deposez vos morceaux.\n"
          "- Par clip : deplacement, rognage, decoupe, gain, fondus, boucle,\n"
          "  transposition en demi-tons et BPM force.\n"
          "- Dix courbes d'automation par clip : Volume, Filtre, Bas, Mid,\n"
          "  Aigu, Bruit, et le volume de chaque stem.\n"
          "- Quatre pistes de samples, dont une reservee a la voix off, qui\n"
          "  s'enregistre directement dans l'application.\n"
          "- Vingt-sept effets repartis en six familles.\n\n"
          "Transitions : Coupe, Beatmix 8, 16 ou 32, Echange de basses, Echo,\n"
          "Filtre balaye, ou Personnalise. L'editeur de transition est une\n"
          "courbe editable point par point, avec aimantation sur les temps,\n"
          "presets, longueur en mesures, verrouillage et apercu A/B.\n\n"
          "Auto-mix : ordonne vos morceaux et choisit les transitions.\n"
          "Vous reglez l'ordre (harmonique, BPM croissant ou decroissant),\n"
          "le mode harmonique, la ponderation entre BPM et cle, le temps de\n"
          "calcul, la longueur et le style de transition. Chaque\n"
          "enchainement est note en vert, jaune ou rouge.\n\n"
          "IA : generer propose quatre modes :\n"
          "- Medley : le meilleur passage de chaque morceau.\n"
          "- Mashup : la voix d'un morceau sur l'instrumental d'un autre.\n"
          "  Necessite les stems des deux morceaux.\n"
          "- Remix : un morceau redecoupe par phrases et reorganise.\n"
          "- Megamix : le refrain de chaque morceau, tempo unifie.\n\n"
          "Grilles de beats : mode Fixe, IA, IA Flex ou Manuel. Le mode IA\n"
          "s'appuie sur un reseau de neurones qui detecte les temps et les\n"
          "premiers temps de mesure.\n\n"
          "Export audio : WAV, MP3, FLAC ou OGG, en 44100, 48000 ou 96000 Hz,\n"
          "16 ou 24 bits, sur tout le mix ou sur la selection, avec\n"
          "normalisation LUFS optionnelle et cue sheet .cue.\n"
          "Export de la tracklist : M3U8, Rekordbox XML, Serato CSV, TXT, CSV.") },
        { "import importer sources rekordbox serato traktor virtualdj enginedj djay dossier surveille",
          juce::String::fromUTF8("Le module Import comporte deux onglets.\n\n"
          "Onglet Fichiers :\n"
          "- Glissez vos fichiers ou dossiers sur la fenetre, ou utilisez\n"
          "  Parcourir fichiers et Parcourir dossier. Les dossiers sont\n"
          "  explores recursivement.\n"
          "- Formats acceptes : mp3, wav, flac, aac, ogg, m4a, aiff, aif,\n"
          "  wma, opus.\n"
          "- Les fichiers passent d'abord dans une liste d'attente ou vous\n"
          "  pouvez corriger titre, artiste, album et genre avant l'import.\n"
          "- Quatre options : Lire les tags, Detecter les doublons, Copier\n"
          "  dans la bibliothèque, Analyser automatiquement.\n"
          "- Un rapport final indique les morceaux importes, les doublons\n"
          "  ignores et les erreurs avec leur motif.\n\n"
          "Onglet Logiciel DJ :\n"
          "- Assistant en quatre etapes : choisir la source, choisir les\n"
          "  playlists, importer, terminer.\n"
          "- Six logiciels reconnus : Rekordbox, VirtualDJ, Serato DJ,\n"
          "  Traktor, Engine DJ et djay Pro. Les logiciels detectes sur votre\n"
          "  machine sont marques comme tels ; les autres restent grises.\n"
          "- Vous pouvez importer toute la collection ou seulement certaines\n"
          "  playlists.\n"
          "- BeatMate lit les bases de donnees reelles de ces logiciels, pas\n"
          "  seulement leurs exports XML, et convertit leurs conventions\n"
          "  (codes de tonalite Traktor, tempo VirtualDJ, chemins Rekordbox).\n\n"
          "Dossiers surveillés : si vous n'en configurez aucun, votre dossier\n"
          "Musique Windows est surveille par defaut. Les nouveaux fichiers\n"
          "sont importes automatiquement, les fichiers deja connus ignores.\n"
          "Le reglage se trouve dans Reglages > Bibliothèque.") },
        { "analyse detaillee structure lufs energie segments beatgrid reanalyser priorite source",
          juce::String::fromUTF8("Le module Analyse :\n\n"
          "Ce que BeatMate calcule, selon les cases activees :\n"
          "- BPM, avec un indice de confiance et la grille de beats.\n"
          "- Cle, en notation standard et en Camelot, avec confiance.\n"
          "- Energie de 1 a 10, decoupee en segments le long du morceau.\n"
          "- Loudness : LUFS integre, crete reelle en dBTP et plage\n"
          "  dynamique (LRA).\n"
          "- Structure : sections du morceau avec leur type et leur position.\n"
          "- Waveform : la forme d'onde, en une et plusieurs bandes.\n"
          "- Mood, deduit de la cle et de l'energie. Desactive par defaut.\n"
          "- Stems Ultra : pre-calcule les stems pendant l'analyse pour les\n"
          "  avoir immediatement disponibles ensuite. Desactive par defaut.\n\n"
          "L'analyse tourne sur un a quatre threads selon votre processeur,\n"
          "avec une barre de progression et un bouton d'annulation qui\n"
          "interrompt aussi le morceau en cours.\n\n"
          "Filtres de la liste : recherche, genre, statut, et bornes de BPM.\n"
          "Le panneau de detail affiche la courbe d'energie du morceau.\n\n"
          "Reanalyser : Analyser la selection ne traite que les morceaux\n"
          "choisis. Analyser tout reprend toute la bibliothèque, y compris\n"
          "les morceaux deja analyses. Les fichiers introuvables sont\n"
          "comptes comme ignores.\n\n"
          "Analyses importees de vos logiciels DJ : chaque morceau retient la\n"
          "provenance de son analyse. En cas de conflit, l'ordre de priorite\n"
          "est Rekordbox, puis Serato, Engine DJ, Traktor et VirtualDJ.\n"
          "Une analyse existante est aussi remplacee si elle est vide ou\n"
          "manifestement fausse. Une analyse faite par BeatMate n'est jamais\n"
          "ecrasee par une analyse importee de qualité inferieure.\n\n"
          "Le module Analyse comporte egalement la verification et la\n"
          "reparation d'integrite des fichiers.") },
        { "stems stem separation voix batterie basse instrumental acapella demucs gpu",
          juce::String::fromUTF8("Les stems dans BeatMate :\n\n"
          "Un stem est une des pistes separees d'un morceau : batterie,\n"
          "basse, voix et le reste.\n\n"
          "Dans l'application principale, la separation sert de preparation :\n"
          "- Module Analyse, case Stems Ultra (MDX-GPU) : les stems sont\n"
          "  calcules pendant l'analyse et mis en cache, pour etre\n"
          "  disponibles immediatement au montage. L'option est desactivee\n"
          "  par defaut et demande le moteur de separation installe avec la\n"
          "  suite. Un morceau deja separe n'est pas recalcule.\n"
          "- Bibliothèque, colonne STEMS : indique si des stems existent\n"
          "  deja a cote du fichier.\n\n"
          "Le mixage des stems se fait dans Mix Studio, qui propose quatre\n"
          "moteurs de separation, du plus rapide au plus fin :\n"
          "Rapide (temps reel), Ultra (MDX-GPU), Premium (GPU) et Studio.\n"
          "Vous y separez un clip entier ou seulement une region, avec des\n"
          "reglages immediats : Isoler voix, Acapella, Instrumental,\n"
          "Couper batterie, Echanger acapella et instrumental.\n\n"
          "La separation est un traitement lourd. Elle est nettement plus\n"
          "rapide avec une carte graphique compatible, et les resultats sont\n"
          "conserves en cache pour ne jamais etre recalcules deux fois.") },
    };
}

juce::String HelpView::searchDocumentation(const juce::String& query)
{
    if (query.trim().isEmpty())
        return "Posez une question sur BeatMate V12 et je vous guiderai.\n\n"
               "Exemples de questions :\n"
               "- Comment importer ma musique ?\n"
               "- Comment preparer un set de 2 heures ?\n"
               "- Qu'est-ce que le Camelot Wheel ?\n"
               "- Comment exporter vers USB pour CDJ ?\n"
               "- Comment utiliser Live Suggest ?\n"
               "- Comment activer ma licence ?\n"
               "- Comment fonctionne la normalisation LUFS ?\n"
               "- Comment creer une Smart Playlist ?\n"
               "- Comment optimiser les performances ?\n"
               "- Quels raccourcis clavier utiliser ?";

    // recherche insensible aux accents (les utilisateurs tapent sans accents)
    auto stripAccents = [](const juce::String& s) {
        juce::String out;
        out.preallocateBytes(s.getNumBytesAsUTF8());
        for (auto ch : s) {
            switch (ch) {
                case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x00E4: case 0x00E5: out += 'a'; break;
                case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB:                         out += 'e'; break;
                case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF:                         out += 'i'; break;
                case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x00F6:            out += 'o'; break;
                case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC:                         out += 'u'; break;
                case 0x00FD: case 0x00FF:                                                   out += 'y'; break;
                case 0x00E7:                                                                out += 'c'; break;
                case 0x00F1:                                                                out += 'n'; break;
                default:                                                                    out += ch; break;
            }
        }
        return out;
    };

    const auto queryLower = stripAccents(query.toLowerCase());
    auto words = juce::StringArray::fromTokens(queryLower, " ?!.,;:", "");
    words.removeEmptyStrings();

    // Synonym expansion — cheap way to improve recall on DJ jargon.
    static const std::pair<const char*, const char*> kSynonyms[] = {
        {"tempo", "bpm"},        {"bpm", "tempo"},
        {"tonalite", "key"},     {"key", "tonalite"},
        {"party", "soiree"},     {"soiree", "party"},
        {"set", "mix"},          {"mix", "set"},
        {"paramètres", "settings"}, {"settings", "paramètres"},
    };
    for (int si = 0; si < (int)std::size(kSynonyms); ++si) {
        if (words.contains(kSynonyms[si].first)
            && !words.contains(kSynonyms[si].second))
            words.add(kSynonyms[si].second);
    }

    struct ScoredResult {
        int score = 0;
        int index = -1;
    };
    std::vector<ScoredResult> results;

    for (int i = 0; i < (int)m_documentationDB.size(); ++i)
    {
        const auto titleLower   = stripAccents(m_documentationDB[(size_t)i].title.toLowerCase());
        const auto contentLower = stripAccents(m_documentationDB[(size_t)i].content.toLowerCase());
        int score = 0;
        for (const auto& word : words)
        {
            if (word.length() < 2) continue;
            if (titleLower.contains(word))    score += 10;
            if (contentLower.contains(word))  score += 3;
        }
        if (score > 0)
            results.push_back({ score, i });
    }

    std::sort(results.begin(), results.end(),
              [](const ScoredResult& a, const ScoredResult& b) { return a.score > b.score; });

    if (results.empty())
    {
        return "Je n'ai pas trouve de reponse precise a votre question.\n\n"
               "Suggestions :\n"
               "- Reformulez votre question avec d'autres mots-cles.\n"
               "- Consultez l'onglet 'Guide' pour le guide complet.\n"
               "- Consultez l'onglet 'FAQ' pour les questions frequentes.\n"
               "- Contactez support@beatmate.fr pour une aide personnalisee.\n\n"
               "Mots-cles reconnus : importer, analyser, BPM, key, set, soiree,\n"
               "exporter, USB, live, normalisation, hot cues, playlist, licence,\n"
               "streaming, sauvegarde, paramètres, camelot, raccourcis.";
    }

    juce::String result;
    result << "IA BeatMate - Reponse a votre question :\n";
    result << "============================================\n\n";

    result << m_documentationDB[(size_t)results[0].index].content;

    if (results.size() > 1 && results[1].score > results[0].score / 2)
    {
        result << "\n\n--------------------------------------------\n";
        result << "Voir aussi :\n\n";
        result << m_documentationDB[(size_t)results[1].index].content;
    }

    result << "\n\n--------------------------------------------\n";
    result << "BeatMate V12 par Sebastien Sainte-Foi\n";
    result << "Pour plus d'aide : support@beatmate.fr";

    return result;
}

juce::Component* HelpView::createIAAssistantTab()
{
    auto* page = new juce::Component();

    auto* titleLbl = new juce::Label("", "IA Assistant BeatMate");
    titleLbl->setFont(juce::Font(20.0f, juce::Font::bold));
    titleLbl->setColour(juce::Label::textColourId, Colors::textPrimary());
    titleLbl->setBounds(24, 16, 400, 28);
    page->addAndMakeVisible(titleLbl);

    auto* descLbl = new juce::Label("",
        "Posez une question en francais et l'IA BeatMate vous guidera. "
        "Base sur la documentation integree de BeatMate V12 par Sebastien Sainte-Foi.");
    descLbl->setFont(juce::Font(13.0f));
    descLbl->setColour(juce::Label::textColourId, Colors::textSecondary());
    descLbl->setBounds(24, 48, 580, 36);
    page->addAndMakeVisible(descLbl);

    auto* inputLbl = new juce::Label("", "Votre question :");
    inputLbl->setFont(juce::Font(14.0f, juce::Font::bold));
    inputLbl->setColour(juce::Label::textColourId, Colors::textPrimary());
    inputLbl->setBounds(24, 92, 200, 22);
    page->addAndMakeVisible(inputLbl);

    m_iaInput = std::make_unique<juce::TextEditor>();
    m_iaInput->setMultiLine(false);
    m_iaInput->setColour(juce::TextEditor::backgroundColourId, Colors::bgLighter());
    m_iaInput->setColour(juce::TextEditor::textColourId, Colors::textPrimary());
    m_iaInput->setColour(juce::TextEditor::outlineColourId, Colors::border());
    m_iaInput->setFont(juce::Font(14.0f));
    m_iaInput->setTextToShowWhenEmpty("Ex: Comment preparer un set de 2h ?", juce::Colours::grey);
    m_iaInput->setBounds(24, 118, 460, 30);
    m_iaInput->onReturnKey = [this] {
        if (m_iaOutput)
            m_iaOutput->setText(searchDocumentation(m_iaInput->getText()), false);
    };
    page->addAndMakeVisible(*m_iaInput);

    m_iaSearchBtn = std::make_unique<juce::TextButton>("Demander a l'IA");
    m_iaSearchBtn->setColour(juce::TextButton::buttonColourId, Colors::primary());
    m_iaSearchBtn->setColour(juce::TextButton::buttonOnColourId, Colors::primaryHover());
    m_iaSearchBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    m_iaSearchBtn->setColour(juce::TextButton::textColourOnId, Colors::textPrimary());
    m_iaSearchBtn->setBounds(494, 118, 130, 30);
    m_iaSearchBtn->onClick = [this] {
        spdlog::info("[HelpView] IA search clicked");
        if (m_iaOutput)
            m_iaOutput->setText(searchDocumentation(m_iaInput->getText()), false);
    };
    page->addAndMakeVisible(*m_iaSearchBtn);

    auto* outputLbl = new juce::Label("", "Reponse :");
    outputLbl->setFont(juce::Font(14.0f, juce::Font::bold));
    outputLbl->setColour(juce::Label::textColourId, Colors::textPrimary());
    outputLbl->setBounds(24, 158, 200, 22);
    page->addAndMakeVisible(outputLbl);

    m_iaOutput = std::make_unique<juce::TextEditor>();
    m_iaOutput->setMultiLine(true, true);
    m_iaOutput->setReadOnly(true);
    m_iaOutput->setScrollbarsShown(true);
    m_iaOutput->setCaretVisible(false);
    m_iaOutput->setColour(juce::TextEditor::backgroundColourId, Colors::bgDark());
    m_iaOutput->setColour(juce::TextEditor::textColourId, Colors::textPrimary());
    m_iaOutput->setColour(juce::TextEditor::outlineColourId, Colors::border());
    m_iaOutput->setColour(juce::ScrollBar::thumbColourId, Colors::bgLighter());
    m_iaOutput->setFont(juce::Font(13.0f));
    m_iaOutput->setBounds(24, 184, 600, 300);
    m_iaOutput->setText(searchDocumentation(""), false);
    page->addAndMakeVisible(*m_iaOutput);

    return page;
}

juce::Component* HelpView::createAboutTab()
{
    auto* page = new juce::Component();

    struct LogoComponent : public juce::Component {
        void paint(juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat().reduced(4.0f);
            float size = juce::jmin(bounds.getWidth(), bounds.getHeight());
            auto circle = juce::Rectangle<float>(
                bounds.getCentreX() - size / 2.0f,
                bounds.getCentreY() - size / 2.0f,
                size, size);

            g.setGradientFill(juce::ColourGradient(
                Colors::primary(), circle.getCentreX(), circle.getY(),
                Colors::primaryDark(), circle.getCentreX(), circle.getBottom(), false));
            g.fillEllipse(circle);

            g.setColour(Colors::primary().withAlpha(0.3f));
            g.drawEllipse(circle.expanded(2.0f), 2.0f);
            g.setColour(Colors::primary().withAlpha(0.15f));
            g.drawEllipse(circle.expanded(5.0f), 1.5f);

            g.setColour(Colors::textPrimary());
            g.setFont(juce::Font(size * 0.4f, juce::Font::bold));
            g.drawText("BM", circle, juce::Justification::centred);
        }
    };

    auto* logo = new LogoComponent();
    logo->setBounds(0, 20, 110, 110);
    page->addAndMakeVisible(logo);

    auto* titleLbl = new juce::Label("", "BeatMate V12 Professional");
    titleLbl->setFont(juce::Font(28.0f, juce::Font::bold));
    titleLbl->setColour(juce::Label::textColourId, Colors::textPrimary());
    titleLbl->setBounds(120, 20, 500, 35);
    page->addAndMakeVisible(titleLbl);

    auto* versionLbl = new juce::Label("", juce::String("Version ") + BEATMATE_VERSION);
    versionLbl->setFont(juce::Font(16.0f));
    versionLbl->setColour(juce::Label::textColourId, Colors::primary());
    versionLbl->setBounds(120, 55, 400, 24);
    page->addAndMakeVisible(versionLbl);

    auto* creatorLbl = new juce::Label("", "Developpe par Sebastien Sainte-Foi");
    creatorLbl->setFont(juce::Font(20.0f, juce::Font::bold));
    creatorLbl->setColour(juce::Label::textColourId, Colors::primary());
    creatorLbl->setBounds(120, 82, 500, 28);
    page->addAndMakeVisible(creatorLbl);

    auto* copyrightLbl = new juce::Label("", "(c) 2024-2026 BeatMate. Tous droits reserves.");
    copyrightLbl->setFont(juce::Font(13.0f));
    copyrightLbl->setColour(juce::Label::textColourId, Colors::textSecondary());
    copyrightLbl->setBounds(120, 110, 500, 20);
    page->addAndMakeVisible(copyrightLbl);

    auto* infoText = createStyledTextArea();
    juce::String info;

    info << "COPYRIGHT & CREATEUR\n";
    info << "  BeatMate V12 Professional\n";
    info << "  Developpe par Sebastien Sainte-Foi\n";
    info << "  Copyright (c) 2024-2026 BeatMate. Tous droits reserves.\n\n";

    info << "SITE WEB\n";
    info << "  https://beatmate.fr\n";
    info << "  support@beatmate.fr\n\n";

    info << "CREDITS - BIBLIOTHEQUES OPEN SOURCE\n";
    info << "  JUCE 8.0          - Framework C++ audio/UI (licence commerciale)\n";
    info << "  TagLib            - Lecture/ecriture des tags audio (ID3, Vorbis, etc.)\n";
    info << "  SoundTouch        - Time-stretching et pitch-shifting\n";
    info << "  KissFFT           - Transformee de Fourier rapide (FFT)\n";
    info << "  SQLite             - Base de donnees embarquee\n";
    info << "  spdlog            - Bibliothèque de logging haute performance\n";
    info << "  nlohmann/json     - Parsing JSON pour C++\n\n";

    info << "INFORMATIONS SYSTEME\n";
    info << "  OS           : " << juce::SystemStats::getOperatingSystemName() << "\n";
    info << "  CPU          : " << juce::SystemStats::getCpuModel() << "\n";
    info << "  Coeurs CPU   : " << juce::String(juce::SystemStats::getNumCpus()) << "\n";
    info << "  RAM          : " << juce::String(juce::SystemStats::getMemorySizeInMegabytes()) << " MB\n";
    info << "  Version JUCE : " << juce::String(JUCE_MAJOR_VERSION) << "."
         << juce::String(JUCE_MINOR_VERSION) << "."
         << juce::String(JUCE_BUILDNUMBER) << "\n\n";

    info << "REMERCIEMENTS\n";
    info << "  Merci a tous les beta-testeurs et a la communaute BeatMate\n";
    info << "  pour leurs retours precieux qui font de BeatMate le meilleur\n";
    info << "  outil de preparation musicale pour DJs.\n\n";

    info << "  Conception, developpement et design : Sebastien Sainte-Foi\n";

    infoText->setText(info, false);
    infoText->setBounds(20, 140, 600, 330);
    page->addAndMakeVisible(infoText);

    auto* webBtn = new juce::HyperlinkButton("beatmate.fr", juce::URL("https://beatmate.fr"));
    webBtn->setFont(juce::Font(14.0f, juce::Font::underlined), false);
    webBtn->setColour(juce::HyperlinkButton::textColourId, Colors::primary());
    webBtn->setBounds(20, 480, 200, 24);
    page->addAndMakeVisible(webBtn);

    m_checkUpdateBtn = std::make_unique<juce::TextButton>(
        juce::String::fromUTF8("Verifier les mises a jour en ligne"));
    m_checkUpdateBtn->setBounds(20, 512, 280, 30);
    m_checkUpdateBtn->onClick = [this] { runOnlineUpdateCheck(); };
    page->addAndMakeVisible(*m_checkUpdateBtn);

    m_installLocalMsiBtn = std::make_unique<juce::TextButton>(
        juce::String::fromUTF8("Installer depuis un .msi..."));
    m_installLocalMsiBtn->setBounds(310, 512, 220, 30);
    m_installLocalMsiBtn->onClick = [this] { runLocalMsiInstall(); };
    page->addAndMakeVisible(*m_installLocalMsiBtn);

    m_updateStatusLbl = std::make_unique<juce::Label>("", juce::String("BeatMate V") + BEATMATE_VERSION);
    m_updateStatusLbl->setFont(juce::Font(13.0f));
    m_updateStatusLbl->setColour(juce::Label::textColourId, Colors::textSecondary());
    m_updateStatusLbl->setBounds(20, 548, 600, 22);
    page->addAndMakeVisible(*m_updateStatusLbl);

    return page;
}

void HelpView::runOnlineUpdateCheck()
{
    if (m_updateStatusLbl)
        m_updateStatusLbl->setText(juce::String::fromUTF8("Recherche de mises a jour..."),
                                   juce::dontSendNotification);
    if (m_checkUpdateBtn) m_checkUpdateBtn->setEnabled(false);

    auto svc = std::make_shared<Services::Update::UpdateService>(std::string(BEATMATE_VERSION));
    svc->setBaseUrl("https://beatmate.fr/beatmate-mise-a-jour");

    juce::Component::SafePointer<HelpView> self (this);
    svc->checkRemoteAsync([self, svc](Services::Update::UpdateInfo info)
    {
        if (self == nullptr) return;
        if (self->m_checkUpdateBtn) self->m_checkUpdateBtn->setEnabled(true);

        if (! info.error.empty()) {
            if (self->m_updateStatusLbl)
                self->m_updateStatusLbl->setText(juce::String::fromUTF8(("Erreur : " + info.error).c_str()),
                                                 juce::dontSendNotification);
            return;
        }
        if (! info.available) {
            if (self->m_updateStatusLbl)
                self->m_updateStatusLbl->setText(juce::String("A jour (V") + BEATMATE_VERSION + ")",
                                                 juce::dontSendNotification);
            return;
        }

        if (self->m_updateStatusLbl)
            self->m_updateStatusLbl->setText(juce::String::fromUTF8(("Mise a jour disponible : V" + info.latestVersion).c_str()),
                                             juce::dontSendNotification);

        juce::String msg;
        msg << juce::String::fromUTF8("Une nouvelle version est disponible : V")
            << juce::String(info.latestVersion)
            << " (version actuelle V" << BEATMATE_VERSION << ").\n\n";
        if (! info.notes.empty()) msg << juce::String::fromUTF8(info.notes.c_str()) << "\n\n";
        msg << juce::String::fromUTF8("Telecharger et installer maintenant ? L'application se fermera pour terminer l'installation.");

        juce::AlertWindow::showOkCancelBox(
            juce::MessageBoxIconType::QuestionIcon,
            juce::String::fromUTF8("Mise a jour BeatMate"),
            msg,
            juce::String::fromUTF8("Mettre a jour"),
            juce::String::fromUTF8("Annuler"),
            nullptr,
            juce::ModalCallbackFunction::create([self, svc, info](int result)
            {
                if (result != 1 || self == nullptr) return;
                if (self->m_updateStatusLbl)
                    self->m_updateStatusLbl->setText(juce::String::fromUTF8("Telechargement..."),
                                                     juce::dontSendNotification);

                juce::Component::SafePointer<HelpView> inner (self);
                svc->downloadAndInstallAsync(info.downloadUrl,
                    [inner](bool ok, std::string m)
                    {
                        if (inner && inner->m_updateStatusLbl)
                            inner->m_updateStatusLbl->setText(juce::String::fromUTF8(m.c_str()),
                                                              juce::dontSendNotification);
                        if (ok)
                            if (auto* app = juce::JUCEApplicationBase::getInstance())
                                app->systemRequestedQuit();
                    },
                    [inner](double pct)
                    {
                        if (inner && inner->m_updateStatusLbl)
                            inner->m_updateStatusLbl->setText(
                                "Telechargement... " + juce::String(juce::roundToInt(pct * 100.0)) + "%",
                                juce::dontSendNotification);
                    });
            }));
    });
}

void HelpView::runLocalMsiInstall()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        juce::String::fromUTF8("Selectionner l'installeur MSI"),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.msi");

    juce::Component::SafePointer<HelpView> self (this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [self, chooser](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (! file.existsAsFile() || self == nullptr) return;

            Services::Update::UpdateService svc(std::string(BEATMATE_VERSION));
            std::string err;
            if (svc.installFromFile(file, err)) {
                if (self->m_updateStatusLbl)
                    self->m_updateStatusLbl->setText(juce::String::fromUTF8("Installeur lance, fermeture..."),
                                                     juce::dontSendNotification);
                if (auto* app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            } else if (self->m_updateStatusLbl) {
                self->m_updateStatusLbl->setText(juce::String::fromUTF8(("Echec : " + err).c_str()),
                                                 juce::dontSendNotification);
            }
        });
}

HelpView::HelpView()
{
    buildDocumentationDB();
    setupUI();
    retranslateUi();
}

void HelpView::setupUI()
{
    m_titleLabel = std::make_unique<juce::Label>("t", BM_TJ("help.title"));
    m_titleLabel->setFont(juce::Font(24.0f, juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId, Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    m_tabWidget = std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
    m_tabWidget->setColour(juce::TabbedComponent::backgroundColourId, Colors::bgDark());
    m_tabWidget->setTabBarDepth(32);

    addAndMakeVisible(*m_tabWidget);
}

void HelpView::visibilityChanged()
{
    // Toutes les vues sont brievement visibles pendant la construction de la
    // fenetre : on repasse par la boucle de messages pour ne construire que si
    if (! isVisible() || m_tabsBuilt) return;
    juce::Component::SafePointer<HelpView> safe(this);
    juce::MessageManager::callAsync([safe] {
        if (safe != nullptr && safe->isVisible() && ! safe->m_tabsBuilt) safe->buildTabs();
    });
}

void HelpView::buildTabs()
{
    if (m_tabsBuilt || ! m_tabWidget) return;
    m_tabsBuilt = true;

    const auto t0 = juce::Time::getMillisecondCounter();
    m_tabWidget->addTab("Guide", Colors::bgDark(),
        new LazyTab([this] { return createGuideTab(); }), true);
    m_tabWidget->addTab("Raccourcis", Colors::bgDark(),
        new LazyTab([this] { return createShortcutsTab(); }), true);
    m_tabWidget->addTab("FAQ", Colors::bgDark(),
        new LazyTab([this] { return createFAQTab(); }), true);
    m_tabWidget->addTab("IA Assistant", Colors::bgDark(),
        new LazyTab([this] { return createIAAssistantTab(); }), true);
    m_tabWidget->addTab("A propos", Colors::bgDark(),
        new LazyTab([this] { return createAboutTab(); }), true);
    resized();
    spdlog::info("[HelpView] onglets construits en {} ms",
                 juce::Time::getMillisecondCounter() - t0);
}

void HelpView::retranslateUi()
{
    if (m_titleLabel)
        m_titleLabel->setText(BM_TJ("help.title"), juce::dontSendNotification);
}

void HelpView::paint(juce::Graphics& g)
{
    ProDraw::viewBackground(g, getWidth(), getHeight());

    auto headerArea = getLocalBounds().removeFromTop(60).toFloat();
    g.setGradientFill(juce::ColourGradient(
        Colors::bgDark(), 0.0f, 0.0f,
        Colors::bgDarker(), 0.0f, headerArea.getBottom(), false));
    g.fillRect(headerArea);

    g.setColour(Colors::border());
    g.drawHorizontalLine(58, 24.0f, (float)(getWidth() - 24));
}

void HelpView::resized()
{
    m_titleLabel->setBounds(24, 18, 400, 30);
    m_tabWidget->setBounds(24, 62, getWidth() - 48, getHeight() - 72);

    if (m_shortcutsTable)
    {
        if (auto* parent = m_shortcutsTable->getParentComponent())
            m_shortcutsTable->setBounds(parent->getLocalBounds().reduced(12));
    }

    if (m_faqViewport)
    {
        if (auto* parent = m_faqViewport->getParentComponent())
        {
            m_faqViewport->setBounds(parent->getLocalBounds().reduced(8));
            if (m_faqContainer)
            {
                m_faqContainer->setSize(m_faqViewport->getWidth() - 16, m_faqContainer->getHeight());
                m_faqContainer->layoutPanels();
            }
        }
    }
}

}
