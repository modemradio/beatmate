#include "AIHelpPanel.h"
#include "../../styles/ColorPalette.h"
#include "../../../services/config/I18n.h"
#include <algorithm>

namespace BeatMate::UI {

void AIHelpPanel::TopicListModel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (!topics || row < 0 || row >= static_cast<int>(topics->size())) return;
    auto& topic = (*topics)[static_cast<size_t>(row)];

    if (selected) {
        g.setColour(Colors::primary().withAlpha(0.2f));
        g.fillRect(0, 0, w, h);
    }

    g.setFont(juce::Font("Segoe UI", 12.0f, selected ? juce::Font::bold : juce::Font::plain));
    g.setColour(selected ? Colors::textPrimary() : Colors::textSecondary());
    g.drawText(juce::String(topic.title), 8, 0, w - 16, h, juce::Justification::centredLeft, true);
}

void AIHelpPanel::TopicListModel::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    if (onSelect) onSelect(row);
}

AIHelpPanel::AIHelpPanel()
{
    m_titleLabel = std::make_unique<juce::Label>("t", BM_TJ("widget.AIHelpPanel.title"));
    m_titleLabel->setFont(juce::Font("Segoe UI", 18.0f, juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId, Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    m_searchEditor = std::make_unique<juce::TextEditor>("search");
    m_searchEditor->setTextToShowWhenEmpty(BM_TJ("widget.AIHelpPanel.searchPlaceholder"), Colors::textMuted());
    m_searchEditor->setFont(juce::Font(13.0f));
    m_searchEditor->setColour(juce::TextEditor::backgroundColourId, Colors::bgLighter());
    m_searchEditor->setColour(juce::TextEditor::textColourId, Colors::textPrimary());
    m_searchEditor->onTextChange = [this] { updateResults(); };
    addAndMakeVisible(*m_searchEditor);

    m_listModel = std::make_unique<TopicListModel>();
    m_listModel->onSelect = [this](int idx) { showTopicContent(idx); };

    m_topicList = std::make_unique<juce::ListBox>("topics", m_listModel.get());
    m_topicList->setRowHeight(28);
    m_topicList->setColour(juce::ListBox::backgroundColourId, Colors::bgDarker());
    addAndMakeVisible(*m_topicList);

    m_contentDisplay = std::make_unique<juce::TextEditor>("content");
    m_contentDisplay->setMultiLine(true, true);
    m_contentDisplay->setReadOnly(true);
    m_contentDisplay->setFont(juce::Font("Segoe UI", 13.0f, juce::Font::plain));
    m_contentDisplay->setColour(juce::TextEditor::backgroundColourId, Colors::bgSurface());
    m_contentDisplay->setColour(juce::TextEditor::textColourId, Colors::textPrimary());
    addAndMakeVisible(*m_contentDisplay);

    m_shortcutsBtn = std::make_unique<juce::TextButton>(BM_TJ("widget.AIHelpPanel.shortcutsBtn"));
    m_shortcutsBtn->setColour(juce::TextButton::buttonColourId, Colors::secondary().withAlpha(0.3f));
    m_shortcutsBtn->onClick = [this] { showShortcuts(); };
    addAndMakeVisible(*m_shortcutsBtn);

    m_closeBtn = std::make_unique<juce::TextButton>("X");
    m_closeBtn->setColour(juce::TextButton::buttonColourId, Colors::error().withAlpha(0.3f));
    m_closeBtn->onClick = [this] { setVisible(false); };
    addAndMakeVisible(*m_closeBtn);

    initHelpDatabase();
    initShortcuts();
    updateResults();
}

void AIHelpPanel::initHelpDatabase()
{

    m_topics.push_back({"Bienvenue dans BeatMate V12", "BeatMate V12 est un logiciel professionnel de preparation DJ,\n"
        "de mixage et de creation audio.\n\n"
        "Modules principaux :\n"
        "- Bibliotheque : gerez votre collection musicale\n"
        "- Import : importez depuis vos logiciels DJ et vos fichiers\n"
        "- Analyse : BPM, Key, Energy, Waveform, Structure\n"
        "- Hot Cues : placez et gerez vos reperes\n"
        "- Normalisation : egalisez les volumes (LUFS)\n"
        "- Preparation Set : organisez vos DJ sets\n"
        "- Preparation Soiree : planifiez vos evenements\n"
        "- Playlists : creez des playlists intelligentes\n"
        "- DJ et Studio : mixage live et montage audio\n"
        "- Export : exportez vers tous les logiciels DJ\n"
        "- Streaming : connectez vos plateformes\n"
        "- Parametres : configurez l'application\n\n"
        "Utilisez F1 pour la Aide ou cliquez sur '?' en bas a gauche.", "Accueil", {"accueil","bienvenue","demarrer","start","decouvrir"}});

    m_topics.push_back({"Tableau de bord", "Le tableau de bord affiche les statistiques de votre collection :\n\n"
        "- Nombre total de morceaux\n- Morceaux analyses\n- Morceaux avec cue points\n"
        "- Derniers morceaux ajoutes\n- Morceaux les plus joues\n"
        "- Repartition par genre, BPM range, tonalite\n"
        "- Espace disque utilise par le cache\n\n"
        "Utilisez les boutons d'action rapide pour importer, analyser ou exporter.", "Accueil", {"accueil","dashboard","stats","statistiques"}});

    m_topics.push_back({"Gestion de la bibliotheque", "La bibliotheque contient tous vos morceaux importes.\n\n"
        "Fonctionnalites :\n"
        "- Recherche par titre, artiste, album\n"
        "- Filtres : BPM, Key, Genre, Energy, Rating\n"
        "- Tri par colonnes (cliquez sur l'en-tete)\n"
        "- Rating : cliquez sur les etoiles\n"
        "- Color coding : clic droit > couleur\n"
        "- Tags personnalises dans le champ Commentaire\n\n"
        "Conseil pro : utilisez des tags comme [BANGER], [OPENER], [CLOSER] dans les commentaires.", "Bibliotheque", {"library","recherche","tri","filtre"}});

    m_topics.push_back({"Smart Playlists", "Les smart playlists se remplissent automatiquement selon des regles.\n\n"
        "Exemples :\n"
        "- BPM entre 120 et 130 AND Key = 8A\n"
        "- Genre contient 'House' AND Energy >= 7\n"
        "- Ajoute dans les 30 derniers jours AND Rating >= 4\n\n"
        "Operateurs : Equals, Contains, Between, GreaterThan, LessThan, InLast...\n"
        "Logic : AND (toutes les conditions) ou OR (au moins une).", "Playlists", {"smart","playlist","auto","regles"}});

    m_topics.push_back({"Importer des morceaux", "Methodes d'import :\n\n"
        "1. Drag & drop de fichiers/dossiers\n"
        "2. Bouton Parcourir\n"
        "3. Import depuis autre logiciel DJ\n\n"
        "Formats supportes : MP3, WAV, FLAC, AIFF, AAC, OGG, M4A, WMA\n\n"
        "Options :\n"
        "- Auto-analyser a l'import (BPM, Key, Waveform)\n"
        "- Detecter les doublons\n"
        "- Lire les tags ID3", "Import", {"import","drag","drop","fichier"}});

    m_topics.push_back({"Import depuis Rekordbox", "BeatMate peut importer depuis Rekordbox :\n\n"
        "1. Dans Rekordbox : File > Export collection as XML\n"
        "2. Dans BeatMate : Import > Rekordbox XML\n"
        "3. Selectionnez le fichier XML\n\n"
        "Donnees importees : morceaux, playlists, cue points, beat grids, couleurs.", "Import", {"rekordbox","xml","pioneer","cdj"}});

    m_topics.push_back({"Import depuis Serato/Traktor/VirtualDJ", "BeatMate detecte automatiquement les installations de :\n"
        "- Serato DJ Pro\n- Traktor Pro\n- VirtualDJ\n- Engine DJ\n- djay Pro\n\n"
        "Allez dans Import > [Logiciel] pour importer la collection.", "Import", {"serato","traktor","virtualdj","engine"}});

    m_topics.push_back({"Analyse audio", "L'analyse detecte automatiquement :\n\n"
        "- BPM (tempo) avec precision haute\n"
        "- Key (tonalite) en notation Camelot, Open Key, ou classique\n"
        "- Energy (1-10, comme Mixed In Key)\n"
        "- Waveform (forme d'onde coloree)\n"
        "- Beat grid (grille de battements)\n"
        "- Structure (intro, couplet, refrain, drop, outro)\n"
        "- Detection vocale\n\n"
        "Lancez l'analyse : selectionnez les morceaux > Analyser", "Analyse", {"bpm","key","energy","waveform","beatgrid"}});

    m_topics.push_back({"Camelot Wheel (roue harmonique)", "Le systeme Camelot simplifie le mixage harmonique :\n\n"
        "Chaque tonalite = nombre (1-12) + lettre (A=mineur, B=majeur)\n\n"
        "Transitions compatibles :\n"
        "- Meme cle (8A > 8A) : parfait\n"
        "- +1 demiton (8A > 9A) : boost d'energie\n"
        "- -1 demiton (8A > 7A) : smooth\n"
        "- A/B switch (8A > 8B) : changement mood majeur/mineur\n\n"
        "Conseil : restez sur meme cle ou +/-1 pour 80% des transitions.", "Analyse", {"camelot","harmonique","key","tonalite","mixing"}});

    m_topics.push_back({"Placer des Hot Cues", "Workflow professionnel :\n\n"
        "1. Selectionnez un morceau dans la bibliotheque\n"
        "2. La waveform s'affiche\n"
        "3. Cliquez sur la waveform pour naviguer (SEEK)\n"
        "4. Appuyez PLAY pour ecouter\n"
        "5. Quand vous trouvez un bon point, cliquez sur un pad vide (A-H)\n"
        "6. Le cue est place a la position du playhead\n"
        "7. Renommez et changez la couleur si necessaire\n\n"
        "Systeme recommande :\n"
        "A (rouge) = Mix-in point\n"
        "B (orange) = Verse/Vocal\n"
        "C (jaune) = Buildup\n"
        "D (vert) = Drop 1\n"
        "E (bleu) = Breakdown\n"
        "F (violet) = Drop 2\n"
        "G (rose) = Pre-outro\n"
        "H (blanc) = Mix-out point", "Hot Cues", {"cue","hotcue","pad","placer","marker"}});

    m_topics.push_back({"Deplacer un Hot Cue", "Pour deplacer un cue point sur la waveform :\n\n"
        "1. Cliquez sur le marqueur du cue sur la waveform\n"
        "2. Le curseur change en fleche gauche-droite\n"
        "3. Glissez vers la nouvelle position\n"
        "4. Relachez : le cue est deplace et le son reprend a cette position\n\n"
        "Le cue se snappe automatiquement au beat le plus proche.", "Hot Cues", {"deplacer","drag","move","glisser"}});

    m_topics.push_back({"Generation automatique IA", "L'IA place automatiquement 8 cue points :\n\n"
        "Algorithme :\n"
        "- Analyse spectral flux (changements de timbre)\n"
        "- Detection des transitions d'energie\n"
        "- Alignement sur les phrases de 16 mesures\n"
        "- Detection : Drop, Chorus, Breakdown, Buildup, Mix-in, Mix-out\n\n"
        "Cliquez 'Auto-generer (IA)' pour lancer.\n"
        "Vous pouvez ensuite ajuster manuellement chaque cue.", "Hot Cues", {"ia","auto","generer","automatique","intelligent"}});

    m_topics.push_back({"Exporter vers d'autres logiciels DJ", "BeatMate exporte vers 5 logiciels :\n\n"
        "- Rekordbox : XML (DJ_PLAYLISTS) avec cues, couleurs, playlists\n"
        "- Traktor : NML avec CUE_V2, MUSICAL_KEY, TEMPO\n"
        "- VirtualDJ : database.xml avec POI\n"
        "- Serato : fichier sidecar Markers2 avec cues colores\n"
        "- Engine DJ : via format Rekordbox XML\n\n"
        "Dans le module Hot Cues :\n"
        "1. Selectionnez le format cible dans le menu deroulant\n"
        "2. Cliquez 'Exporter vers DJ Software'\n"
        "3. Choisissez l'emplacement de sauvegarde", "Export", {"export","rekordbox","serato","traktor","virtualdj","engine"}});

    m_topics.push_back({"Normalisation audio", "La normalisation egalise le volume de tous vos morceaux :\n\n"
        "- Auto-Gain non-destructif (metadata LUFS)\n"
        "- Target configurable : -14 LUFS (streaming) a -8 LUFS (club)\n"
        "- Peak limiting pour eviter le clipping\n"
        "- Batch : traiter toute la collection\n\n"
        "Recommande : -12 LUFS pour la preparation DJ.", "Normalisation", {"normaliser","volume","gain","lufs","loudness"}});

    m_topics.push_back({"Preparer un DJ set", "La preparation de set utilise :\n\n"
        "Score de compatibilite (0-100) entre morceaux adjacents :\n"
        "- Key Camelot : 0-30 points\n"
        "- BPM proximite : 0-25 points\n"
        "- Flow d'energie : 0-20 points\n"
        "- Genre compatible : 0-15 points\n"
        "- Variete artiste : 0-10 points\n\n"
        "Conseil : visez un score > 70 entre chaque morceau.\n"
        "L'auto-ordering arrange les morceaux pour le meilleur flow.", "Preparation Set", {"set","preparation","compatibilite","score","order"}});

    m_topics.push_back({"Preparation de soiree", "Pour un evenement (mariage, corporate, club) :\n\n"
        "1. Definissez les phases : Cocktail, Diner, Danse, Peak, Fin\n"
        "2. Assignez genre et BPM range par phase\n"
        "3. Ajoutez les moments speciaux (premiere danse, etc.)\n"
        "4. Glissez les morceaux dans chaque phase\n"
        "5. Exportez le plan en PDF", "Preparation Soiree", {"soiree","mariage","evenement","wedding","timeline"}});

    m_topics.push_back({"Trial et Licence", "BeatMate Pro offre :\n\n"
        "- Essai gratuit : 7 jours, lecture limitee a 5 minutes par morceau\n"
        "- Quand la limite est atteinte, un message s'affiche\n"
        "- Pour activer : Parametres > Licence > Entrer la cle\n\n"
        "Types de licence : Personal, Professional, Family, Enterprise", "General", {"trial","licence","activer","cle","essai"}});

    m_topics.push_back({"Performances et vitesse", "Pour accelerer BeatMate :\n\n"
        "- La waveform est cachee apres le 1er chargement (instantane ensuite)\n"
        "- L'analyse tourne en arriere-plan sans bloquer l'interface\n"
        "- L'auto-generation IA tourne dans un thread separe\n"
        "- Le playhead se met a jour a 20fps\n\n"
        "Si l'application est lente :\n"
        "- Verifiez l'espace disque\n"
        "- Fermez les autres applications gourmandes", "General", {"lent","performance","vitesse","rapide","cache"}});

    m_topics.push_back({"Mode DJ Performance", "Le mode DJ Performance permet le mix en temps reel :\n\n"
        "Fonctionnalites :\n"
        "- 2/4 decks avec waveforms synchronisees\n"
        "- Beat Sync automatique (quantize beat-sur-beat)\n"
        "- Crossfader avec courbes configurables\n"
        "- EQ 3 bandes par deck (Low/Mid/High)\n"
        "- Effets en temps reel (Reverb, Delay, Flanger, Phaser, etc.)\n"
        "- BPM detection et beat grid auto\n"
        "- Key Lock (pitch sans changer le tempo)\n"
        "- Auto-Gain pour egaliser les volumes\n\n"
        "Conseil : activez Quantize pour des transitions parfaites.", "DJ et Studio", {"dj","performance","mix","deck","crossfader","beat sync"}});

    m_topics.push_back({"Separation de Stems", "La separation de stems utilise l'IA (Demucs) :\n\n"
        "4 stems disponibles :\n"
        "- Vocals (voix)\n"
        "- Drums (batterie/percussions)\n"
        "- Bass (basse)\n"
        "- Other (melodie, synthes, etc.)\n\n"
        "Utilisation :\n"
        "1. Chargez un morceau dans un deck\n"
        "2. Cliquez 'Stems' pour lancer la separation\n"
        "3. Utilisez les boutons mute/solo pour isoler chaque stem\n"
        "4. Mix creatif : retirez les vocals d'un track pendant la transition\n\n"
        "La premiere separation prend ~30s, les suivantes sont cachees.", "DJ et Studio", {"stem","stems","vocal","drums","bass","demucs","separation","isoler"}});

    m_topics.push_back({"Mode Studio - Montage Audio", "Le mode Studio est un DAW professionnel pour creer :\n\n"
        "- Remix : rearrangez les sections d'un morceau\n"
        "- Mashup : superposez des elements de plusieurs morceaux\n"
        "- Medley : enchainez plusieurs morceaux avec transitions\n"
        "- Megamix : creez un mix continu de 30+ morceaux\n\n"
        "Interface :\n"
        "- Timeline multi-pistes avec clips audio\n"
        "- Editeur de clips (couper, copier, coller, fade)\n"
        "- Automation des parametres (volume, EQ, effets)\n"
        "- Beat grid synchronise sur toutes les pistes\n"
        "- Export en WAV, MP3, FLAC, AIFF", "DJ et Studio", {"studio","montage","remix","mashup","medley","megamix","daw","arrangement"}});

    m_topics.push_back({"Transitions et Automix", "Le systeme de transitions IA :\n\n"
        "Types de transitions :\n"
        "- Transition simple (1-32 beats)\n"
        "- EQ fade (basses d'abord, puis aigus)\n"
        "- Echo out / Reverb tail\n"
        "- Cut (coupe nette sur le beat)\n"
        "- Backspin / Brake\n"
        "- Filter sweep\n\n"
        "Automix :\n"
        "- L'IA analyse la structure des morceaux\n"
        "- Detecte les meilleurs points d'entree/sortie\n"
        "- Place les transitions automatiquement\n"
        "- Respecte les compatibilites harmoniques (Camelot)", "DJ et Studio", {"transition","automix","crossfade","echo","filter","backspin","ia"}});

    m_topics.push_back({"Effets Audio DSP", "Effets disponibles dans DJ & Studio :\n\n"
        "Delays et Reverbs :\n"
        "- Reverb (convolution avec IR presets)\n"
        "- Delay (sync BPM, ping-pong)\n"
        "- Echo freeze\n\n"
        "Modulation :\n"
        "- Flanger, Phaser, Chorus\n"
        "- Tremolo, Vibrato\n\n"
        "Filtres et Distortion :\n"
        "- Ladder Filter (Moog-style)\n"
        "- Bitcrusher, Vinyl Effect\n"
        "- Gate, Compressor\n\n"
        "Controle XY Pad : assignez 2 parametres aux axes X/Y.", "DJ et Studio", {"effet","fx","reverb","delay","flanger","phaser","filter","dsp","bitcrusher","vinyl"}});

    m_topics.push_back({"Compatibilite Harmonique", "Le systeme Camelot pour mixer harmoniquement :\n\n"
        "Regles Camelot :\n"
        "- Meme numero = parfait (8A -> 8A)\n"
        "- +/- 1 = compatible (8A -> 7A ou 9A)\n"
        "- A <-> B = relatif (8A -> 8B)\n\n"
        "Le panneau de compatibilite affiche :\n"
        "- Roue Camelot interactive\n"
        "- Score de compatibilite entre decks\n"
        "- Suggestions de morceaux harmoniquement compatibles\n"
        "- Key Lock pour ajuster la tonalite", "DJ et Studio", {"camelot","key","tonalite","harmonique","compatible","roue"}});

    m_topics.push_back({"Export depuis DJ et Studio", "Formats d'export disponibles :\n\n"
        "Audio :\n"
        "- WAV 16/24/32 bits (qualite studio)\n"
        "- FLAC (sans perte, compresse)\n"
        "- MP3 128-320 kbps\n"
        "- AIFF\n"
        "- OGG Vorbis\n\n"
        "Projets :\n"
        "- Export Ableton Live (.als)\n"
        "- Export cue sheet (.cue)\n"
        "- Export Rekordbox XML\n\n"
        "Video :\n"
        "- MP4 avec visualisation waveform\n"
        "- Export YouTube, TikTok, Instagram", "DJ et Studio", {"export","wav","mp3","flac","ableton","video","youtube"}});

    m_topics.push_back({"Services de streaming", "BeatMate peut se connecter aux plateformes de streaming :\n\n"
        "Plateformes supportees :\n"
        "- Spotify (recherche, preview 30s, metadata)\n"
        "- Apple Music\n"
        "- SoundCloud (telecharger les morceaux libres)\n"
        "- Tidal (qualite HiFi/Master)\n"
        "- YouTube Music\n"
        "- Amazon Music\n"
        "- Beatport (recherche par label, genre, charts)\n"
        "- Billboard (charts, classements)\n\n"
        "Configuration :\n"
        "Allez dans Parametres > Streaming pour connecter vos comptes.\n"
        "Les morceaux streaming apparaissent dans la bibliotheque avec un badge.", "Streaming", {"spotify","apple","soundcloud","tidal","youtube","beatport","streaming","amazon","billboard"}});

    m_topics.push_back({"Mode Live (detection temps reel)", "Le mode Live detecte automatiquement ce qui joue :\n\n"
        "Logiciels supportes :\n"
        "- Rekordbox : polling de la base de donnees toutes les 2s\n"
        "- VirtualDJ : polling de l'API HTTP\n"
        "- Serato DJ Pro\n"
        "- Traktor Pro\n\n"
        "Quand un morceau change dans le logiciel DJ, BeatMate :\n"
        "1. Detecte le nouveau morceau\n"
        "2. Affiche ses informations (BPM, Key, Energy)\n"
        "3. Suggere le prochain morceau compatible\n"
        "4. Met a jour l'historique de lecture\n\n"
        "Activez le mode Live dans Parametres > DJ Software.", "Live", {"live","temps reel","realtime","detection","monitor","rekordbox","virtualdj"}});

    m_topics.push_back({"Parametres - General", "Onglet General des parametres :\n\n"
        "- Langue : Francais / English\n"
        "- Demarrage : Plein ecran / Derniere taille / Taille par defaut\n"
        "- Sauvegarde automatique : intervalle configurable (1-30 min)\n"
        "- Mise a jour automatique au demarrage\n"
        "- Dossier de donnees : emplacement des fichiers BeatMate\n"
        "- Reinitialiser tous les parametres\n\n"
        "Les parametres sont sauvegardes dans %APPDATA%/BeatMate/appsettings.json", "Parametres", {"parametres","settings","langue","language","demarrage","general"}});

    m_topics.push_back({"Parametres - Audio", "Configuration audio :\n\n"
        "- Peripherique de sortie : selectionnez votre interface audio\n"
        "- Peripherique d'entree : pour l'enregistrement\n"
        "- Taille du buffer : 64 a 4096 samples\n"
        "  (petit = faible latence, grand = stable)\n"
        "- Frequence d'echantillonnage : 44100 / 48000 / 96000 Hz\n"
        "- Latence affichee en millisecondes\n\n"
        "Conseil : 256 samples a 44100 Hz = ~5.8ms de latence.\n"
        "Pour le DJ live, visez < 10ms.", "Parametres", {"audio","buffer","latence","latency","sample rate","peripherique","device"}});

    m_topics.push_back({"Parametres - Bibliotheque", "Configuration de la bibliotheque :\n\n"
        "- Dossiers surveilles : ajoutez des dossiers pour l'import automatique\n"
        "- Import automatique : detecte les nouveaux fichiers\n"
        "- Analyser a l'import : lance l'analyse BPM/Key automatiquement\n"
        "- Detection des doublons : evite les fichiers en double\n\n"
        "Les dossiers surveilles sont scannes periodiquement.", "Parametres", {"bibliotheque","dossier","watch","folder","import auto","doublons"}});

    m_topics.push_back({"Parametres - Analyse", "Configuration de l'analyse audio :\n\n"
        "- Qualite : Rapide / Normale / Haute / Ultra\n"
        "  (Ultra = plus precis mais plus lent)\n"
        "- BPM range : min et max (ex: 70-180 BPM)\n"
        "- Mode BPM : Normal / Double / Half\n"
        "- Methode de detection de cle : EDM / Classique\n"
        "- Auto-analyser les nouveaux morceaux\n"
        "- Nombre de threads (1-16)\n"
        "- Activer la separation de stems\n"
        "- Generer les waveforms colorees\n\n"
        "Plus de threads = analyse plus rapide si votre CPU le supporte.", "Parametres", {"analyse","qualite","bpm range","thread","stems","waveform"}});

    m_topics.push_back({"Parametres - DJ Software", "Synchronisation avec les logiciels DJ :\n\n"
        "- Synchronisation automatique : active le polling periodique\n"
        "- Intervalle de sync : 1 min / 5 min / 15 min / 1h\n"
        "- Logiciels detectes automatiquement :\n"
        "  Rekordbox, VirtualDJ, Serato, Traktor, Engine DJ, djay Pro\n\n"
        "La synchronisation importe les morceaux, cue points et playlists\n"
        "depuis le logiciel DJ selectionne.", "Parametres", {"dj software","sync","rekordbox","serato","traktor","virtualdj","engine","interval"}});

    m_topics.push_back({"Parametres - Apparence", "Personnalisation de l'interface :\n\n"
        "- Couleur d'accent : Bleu / Violet / Vert / Rouge / Orange / Rose / Cyan / Blanc\n"
        "  Change la couleur principale des boutons et sliders\n"
        "- Taille de police : 10-20pt (ajuste tout le texte)\n"
        "- Densite : Compact / Normal / Spacieux\n"
        "- Style waveform : Classic / Multi-band / Spectre / Minimal\n\n"
        "Cliquez 'Appliquer' pour voir les changements immediatement.\n"
        "Le theme est Dark uniquement (optimise pour les conditions DJ).", "Parametres", {"apparence","theme","couleur","accent","police","font","densite","waveform style"}});

    m_topics.push_back({"Parametres - Licence", "Gestion de votre licence BeatMate :\n\n"
        "Mode essai (Trial) :\n"
        "- 7 jours gratuits\n"
        "- Lecture limitee a 5 minutes par morceau\n"
        "- Toutes les fonctionnalites accessibles\n\n"
        "Types de licence :\n"
        "- Personal : 1 machine, usage personnel\n"
        "- Professional : 3 machines, usage commercial\n"
        "- Family : 5 machines, usage familial\n"
        "- Enterprise : machines illimitees, support prioritaire\n\n"
        "Activation : entrez votre cle dans le champ et cliquez 'Activer'.\n"
        "La licence est verifiee en ligne puis stockee localement.", "Parametres", {"licence","license","trial","essai","activer","cle","key","personal","professional"}});

    m_topics.push_back({"Parametres - Sauvegarde", "Systeme de sauvegarde automatique :\n\n"
        "- Sauvegarde automatique : oui/non\n"
        "- Intervalle : quotidien / hebdomadaire / mensuel\n"
        "- Nombre max de sauvegardes conservees\n"
        "- Sauvegarder maintenant : cree une sauvegarde immediate\n"
        "- Restaurer : revenir a une sauvegarde precedente\n\n"
        "Les sauvegardes incluent : base de donnees, parametres, playlists,\n"
        "cue points, presets. Les fichiers audio ne sont PAS sauvegardes.", "Parametres", {"sauvegarde","backup","restaurer","restore","automatique"}});

    m_topics.push_back({"Parametres - Raccourcis clavier", "Personnalisation des raccourcis :\n\n"
        "Tous les raccourcis sont modifiables.\n"
        "Double-cliquez sur un raccourci dans la liste pour le modifier.\n"
        "Appuyez sur la nouvelle combinaison de touches.\n\n"
        "Raccourcis par defaut :\n"
        "- Espace : Play/Pause\n"
        "- 1-8 : Cue points\n"
        "- Ctrl+I : Import\n"
        "- Ctrl+E : Export\n"
        "- F1-F9 : Navigation entre modules\n"
        "- +/- : Zoom waveform\n"
        "- Ctrl+Z : Annuler", "Parametres", {"raccourcis","shortcut","clavier","keyboard","touche","keybinding"}});

    m_topics.push_back({"Intelligence Artificielle dans BeatMate", "BeatMate utilise l'IA pour :\n\n"
        "1. Detection de BPM : analyse de correlation et autocorrelation FFT\n"
        "2. Detection de cle : analyse chromatique + classification ML\n"
        "3. Detection de structure : intro, verse, chorus, drop, outro\n"
        "4. Placement automatique de cue points : analyse d'energie et spectrale\n"
        "5. Separation de stems : reseau neuronal Demucs (vocals, drums, bass, other)\n"
        "6. Suggestions de mix : compatibilite harmonique + energy flow\n"
        "7. Automix IA : detection des meilleurs points de transition\n"
        "8. Classification de genre : reseau ONNX pre-entraine\n"
        "9. Detection de mood : analyse spectrale et rythmique\n"
        "10. Style DJ learning : apprend vos preferences de mix\n\n"
        "Les modeles IA sont executes localement (pas de cloud).", "IA", {"ia","ai","intelligence","artificielle","machine learning","ml","onnx","demucs","neural"}});

    m_topics.push_back({"Suggestions intelligentes", "Le moteur de suggestions propose :\n\n"
        "- Prochain morceau compatible (key + BPM + energy)\n"
        "- Morceaux similaires (genre, mood, energy profile)\n"
        "- Transitions harmoniques optimales\n"
        "- Mix path : sequence optimale pour un set entier\n\n"
        "Score de compatibilite :\n"
        "- Key Camelot : 0-30 pts (meme cle = 30, +/-1 = 20)\n"
        "- BPM : 0-25 pts (meme BPM = 25, +/-3% = 15)\n"
        "- Energy flow : 0-20 pts (progression logique)\n"
        "- Genre : 0-15 pts (meme genre = 15)\n"
        "- Variete : 0-10 pts (pas le meme artiste)\n\n"
        "Score > 70 = excellente transition, > 50 = correcte, < 30 = risquee.", "IA", {"suggestion","compatibilite","score","prochain","recommandation","next track"}});

    m_topics.push_back({"Gestion des playlists", "Types de playlists :\n\n"
        "1. Playlist manuelle : ajoutez/retirez des morceaux manuellement\n"
        "2. Smart Playlist : se remplit automatiquement selon des regles\n"
        "3. Dossiers : organisez vos playlists en hierarchie\n\n"
        "Operations :\n"
        "- Drag & drop pour reordonner\n"
        "- Clic droit > Couleur pour personnaliser\n"
        "- Export en M3U, M3U8, PLS\n"
        "- Import depuis fichiers M3U\n"
        "- Dupliquer, renommer, supprimer", "Playlists", {"playlist","dossier","folder","m3u","organiser","creer"}});

    m_topics.push_back({"Controleurs MIDI", "BeatMate supporte les controleurs MIDI :\n\n"
        "Configuration :\n"
        "1. Branchez votre controleur MIDI\n"
        "2. Allez dans Parametres > MIDI\n"
        "3. Selectionnez votre controleur\n"
        "4. Utilisez le mode Learn pour assigner les controles\n\n"
        "Controleurs testes :\n"
        "- Pioneer DDJ-400, DDJ-800, DDJ-1000\n"
        "- Native Instruments Traktor S2/S4\n"
        "- Numark Mixtrack\n"
        "- Denon DJ MC6000\n"
        "- Allen & Heath Xone\n\n"
        "Tout controleur MIDI standard est compatible.", "MIDI", {"midi","controleur","controller","ddj","pioneer","numark","learn","mapping"}});

    m_topics.push_back({"Enregistrement de mix", "Enregistrez vos sessions DJ :\n\n"
        "1. Cliquez le bouton 'REC' dans la barre de controle\n"
        "2. Le mix est enregistre en temps reel\n"
        "3. Cliquez 'STOP' pour arreter\n"
        "4. Choisissez le format d'export (WAV, MP3, FLAC)\n\n"
        "Options :\n"
        "- Qualite : 16/24/32 bits\n"
        "- Split automatique par morceau\n"
        "- Metadata : titre, artiste, tracklist\n"
        "- Cue sheet automatique pour le mix", "Enregistrement", {"enregistrer","record","rec","mix","session","capturer"}});

    m_topics.push_back({"Depannage - Pas de son", "Si vous n'avez pas de son :\n\n"
        "1. Verifiez Parametres > Audio > Peripherique de sortie\n"
        "2. Assurez-vous que le volume systeme n'est pas coupe\n"
        "3. Testez avec un autre peripherique audio\n"
        "4. Verifiez que le fichier audio n'est pas corrompu\n"
        "5. Augmentez la taille du buffer si le son crepite\n"
        "6. Redemarrez l'application\n\n"
        "Si le probleme persiste, verifiez les drivers audio de votre systeme.", "Depannage", {"son","audio","probleme","pas de son","silence","crepitement","glitch"}});

    m_topics.push_back({"Depannage - Application lente", "Pour ameliorer les performances :\n\n"
        "1. Fermez les autres applications gourmandes\n"
        "2. Reduisez le nombre de threads d'analyse\n"
        "3. Utilisez un SSD pour la bibliotheque\n"
        "4. Videz le cache waveform si > 1 Go\n"
        "5. Desactivez les waveforms haute resolution\n"
        "6. Augmentez la taille du buffer audio\n\n"
        "Configuration minimale : 8 Go RAM, SSD, CPU 4 coeurs\n"
        "Configuration recommandee : 16 Go RAM, NVMe, CPU 8 coeurs", "Depannage", {"lent","performance","vitesse","rapide","cache","ram","cpu","ssd"}});

    m_topics.push_back({"Depannage - Base de donnees", "Problemes de base de donnees :\n\n"
        "- 'no such column' : La migration automatique corrige ce probleme.\n"
        "  Redemarrez l'application.\n"
        "- Base corrompue : Restaurez depuis une sauvegarde\n"
        "  (Parametres > Sauvegarde > Restaurer)\n"
        "- Base verrouillee : Fermez toutes les instances de BeatMate\n\n"
        "Emplacement de la base :\n"
        "%APPDATA%/BeatMate/beatmate.db\n\n"
        "Pour reinitialiser : supprimez beatmate.db et relancez.\n"
        "ATTENTION : cela efface toute votre bibliotheque.", "Depannage", {"database","base","donnees","corrompu","erreur","migration","sqlite"}});

    m_topics.push_back({"FAQ - Import et Fichiers", "Q: Quels formats audio sont supportes ?\n"
        "R: WAV, MP3, FLAC, AIFF, AAC, OGG, M4A, WMA, ALAC.\n\n"
        "Q: Puis-je importer depuis une cle USB Rekordbox ?\n"
        "R: Oui, BeatMate lit les bases Rekordbox sur USB.\n\n"
        "Q: L'import est tres lent, pourquoi ?\n"
        "R: L'analyse BPM/Key prend du temps. Desactivez 'Analyser a l'import'\n"
        "   et analysez plus tard en batch.\n\n"
        "Q: Mes morceaux ont disparu de la bibliotheque ?\n"
        "R: Les fichiers ont peut-etre ete deplaces. Utilisez 'Relocaliser'\n"
        "   pour les retrouver, ou re-importez le dossier.\n\n"
        "Q: Comment importer depuis iTunes/Apple Music ?\n"
        "R: Importez les fichiers directement depuis le dossier Media\n"
        "   (generalement ~/Music/iTunes/iTunes Media/).", "FAQ", {"faq","import","format","fichier","usb","itunes","disparu"}});

    m_topics.push_back({"FAQ - Analyse et Detection", "Q: Le BPM detecte est faux (double ou moitie) ?\n"
        "R: Changez le mode BPM dans Parametres > Analyse.\n"
        "   Utilisez 'Double' pour les morceaux rapides ou 'Half' pour les lents.\n\n"
        "Q: La cle detectee est differente de Mixed In Key ?\n"
        "R: Changez la methode de detection (EDM vs Classique).\n"
        "   Les resultats varient selon l'algorithme.\n\n"
        "Q: L'analyse prend trop de temps ?\n"
        "R: Augmentez le nombre de threads dans Parametres > Analyse.\n"
        "   Passez en qualite 'Rapide' pour un premier scan.\n\n"
        "Q: L'energy est differente de Mixed In Key ?\n"
        "R: L'echelle est 1-10 comme Mixed In Key mais l'algorithme differe.\n"
        "   L'energy BeatMate prend en compte le loudness, le rythme et le spectre.", "FAQ", {"faq","bpm","key","analyse","detection","mixedinkey","energy"}});

    m_topics.push_back({"FAQ - DJ et Studio / Mix", "Q: Comment synchroniser deux morceaux ?\n"
        "R: Chargez les morceaux dans les decks A et B.\n"
        "   Cliquez 'Sync' ou appuyez 'S' pour aligner BPM et phase.\n\n"
        "Q: Le beat sync est decale ?\n"
        "R: La beatgrid est peut-etre mal callee. Editez le premier beat\n"
        "   dans l'editeur de beatgrid.\n\n"
        "Q: Les stems sont de mauvaise qualite ?\n"
        "R: Utilisez des fichiers WAV ou FLAC. Le MP3 128kbps degrade\n"
        "   la separation. La qualite augmente avec la qualite source.\n\n"
        "Q: Comment creer un mashup ?\n"
        "R: En mode Studio, chargez plusieurs morceaux sur des pistes\n"
        "   separees, alignez les beatgrids, et mixez.\n\n"
        "Q: L'automix fait des transitions bizarres ?\n"
        "R: Verifiez que les morceaux ont ete analyses (BPM + structure).\n"
        "   L'automix fonctionne mieux avec des analyses completes.", "FAQ", {"faq","dj","studio","sync","stems","mashup","automix","transition"}});

    m_topics.push_back({"FAQ - Licence et Technique", "Q: L'application plante au demarrage ?\n"
        "R: Supprimez %APPDATA%/BeatMate/appsettings.json et relancez.\n\n"
        "Q: Le mode Live ne detecte pas mes morceaux ?\n"
        "R: Assurez-vous que Rekordbox ou VirtualDJ est lance.\n"
        "   Pour Rekordbox 6+, la base est chiffree - exportez en XML.\n\n"
        "Q: Ma licence n'est pas reconnue ?\n"
        "R: Verifiez votre connexion internet lors de l'activation.\n"
        "   La cle est validee en ligne puis stockee localement.\n\n"
        "Q: Combien de machines par licence ?\n"
        "R: Personal=1, Professional=3, Family=5, Enterprise=illimite.\n\n"
        "Q: Puis-je utiliser BeatMate en concert ?\n"
        "R: Oui, avec une licence Professional ou Enterprise.\n"
        "   Le mode performance est optimise pour le live.", "FAQ", {"faq","licence","crash","plante","live","machine","concert"}});

    m_topics.push_back({"A propos de BeatMate V12", "BeatMate V12 Professional\n"
        "Version 12.0.0\n\n"
        "Developpeur : Sebastien Sainte-Foi\n\n"
        "Technologies :\n"
        "- JUCE 8.0.4 (framework audio/UI)\n"
        "- SQLite3 (base de donnees locale)\n"
        "- Demucs (separation de stems IA)\n"
        "- ONNX Runtime (inference ML)\n"
        "- RubberBand (time-stretching)\n"
        "- SoundTouch (pitch-shifting)\n"
        "- Signalsmith Stretch\n"
        "- r8brain (resampling haute qualite)\n"
        "- Aubio, Essentia, QM-DSP (analyse audio)\n\n"
        "Licence : Proprietary. Tous droits reserves.\n"
        "Contact : support@beatmate.fr", "A propos", {"about","version","credit","licence","juce","contact","developpeur"}});
}

void AIHelpPanel::initShortcuts()
{
    m_shortcuts = {
        {"F1", "Aide", "Navigation"},
        {"F2", "Bibliotheque", "Navigation"},
        {"F3", "Import", "Navigation"},
        {"F4", "Analyse", "Navigation"},
        {"F5", "Hot Cues", "Navigation"},
        {"F6", "Normalisation", "Navigation"},
        {"F7", "Preparation Set", "Navigation"},
        {"F8", "Preparation Soiree", "Navigation"},
        {"F9", "Playlists", "Navigation"},
        {"F10", "DJ et Studio", "Navigation"},
        {"F11", "Export", "Navigation"},
        {"F12", "Parametres", "Navigation"},

        {"Ctrl+I", "Importer des fichiers", "Global"},
        {"Ctrl+O", "Ouvrir un fichier audio", "Global"},
        {"Ctrl+S", "Sauvegarder le projet", "Global"},
        {"Ctrl+Shift+S", "Sauvegarder sous...", "Global"},
        {"Ctrl+E", "Exporter", "Global"},
        {"Ctrl+F", "Rechercher dans la bibliotheque", "Global"},
        {"Ctrl+N", "Nouveau projet/playlist", "Global"},
        {"Ctrl+Z", "Annuler", "Global"},
        {"Ctrl+Shift+Z", "Retablir", "Global"},
        {"Ctrl+Q", "Quitter l'application", "Global"},
        {"Ctrl+,", "Ouvrir les parametres", "Global"},
        {"Echap", "Fermer le panneau/dialogue actif", "Global"},

        {"Espace", "Play / Pause", "Lecture"},
        {"Entree", "Stop et retour au debut", "Lecture"},
        {"Fleche Droite", "Avancer de 5 secondes", "Lecture"},
        {"Fleche Gauche", "Reculer de 5 secondes", "Lecture"},
        {"Shift+Droite", "Avancer de 30 secondes", "Lecture"},
        {"Shift+Gauche", "Reculer de 30 secondes", "Lecture"},
        {"Ctrl+Droite", "Morceau suivant", "Lecture"},
        {"Ctrl+Gauche", "Morceau precedent", "Lecture"},
        {"[/]", "Ajuster le tempo -/+ 0.5 BPM", "Lecture"},

        {"1-8", "Placer/sauter au cue point 1-8", "Hot Cues"},
        {"Shift+1-8", "Supprimer le cue point 1-8", "Hot Cues"},
        {"Suppr", "Supprimer le dernier cue", "Hot Cues"},
        {"Clic droit", "Menu contextuel du cue (couleur, renommer)", "Hot Cues"},
        {"A", "Auto-generer les cue points (IA)", "Hot Cues"},
        {"+/-", "Zoom waveform avant/arriere", "Hot Cues"},
        {"Molette", "Zoom waveform", "Hot Cues"},
        {"Shift+Clic", "Selection de region sur waveform", "Hot Cues"},
        {"Ctrl+Clic", "Placer un marqueur temporaire", "Hot Cues"},

        {"Ctrl+A", "Selectionner tout", "Bibliotheque"},
        {"Suppr", "Supprimer la selection", "Bibliotheque"},
        {"Entree", "Charger le morceau selectionne", "Bibliotheque"},
        {"Ctrl+R", "Evaluer (rating) le morceau", "Bibliotheque"},
        {"Ctrl+D", "Detecter les doublons", "Bibliotheque"},

        {"Q", "Charger sur Deck A", "DJ et Studio"},
        {"W", "Charger sur Deck B", "DJ et Studio"},
        {"Tab", "Basculer le deck actif", "DJ et Studio"},
        {"S", "Sync (BPM + phase)", "DJ et Studio"},
        {"D", "Sync phase seulement", "DJ et Studio"},
        {"X", "Crossfader gauche/droite", "DJ et Studio"},
        {"Z", "EQ Kill Low", "DJ et Studio"},
        {"C", "EQ Kill High", "DJ et Studio"},
        {"V", "EQ Kill Mid", "DJ et Studio"},
        {"L", "Loop on/off", "DJ et Studio"},
        {"K", "Loop x2 (doubler)", "DJ et Studio"},
        {"J", "Loop /2 (diviser)", "DJ et Studio"},
        {"R", "Enregistrer le mix", "DJ et Studio"},
        {"Ctrl+1-4", "Activer l'effet 1-4", "DJ et Studio"},
        {"Shift+Q/W", "Ecouter en preview (PFL) Deck A/B", "DJ et Studio"},

        {"Ctrl+M", "Mode Studio (montage/arrangement)", "DJ et Studio"},
        {"Ctrl+C", "Copier le clip selectionne", "DJ et Studio"},
        {"Ctrl+V", "Coller le clip", "DJ et Studio"},
        {"Ctrl+X", "Couper le clip", "DJ et Studio"},
        {"B", "Diviser le clip a la position du curseur", "DJ et Studio"},
        {"G", "Snap to grid on/off", "DJ et Studio"},
        {"T", "Ajouter une piste", "DJ et Studio"},
        {"M", "Muter la piste selectionnee", "DJ et Studio"},
        {"Ctrl+Shift+E", "Exporter le montage (bounce)", "DJ et Studio"},
    };
}

void AIHelpPanel::setContext(int moduleIndex)
{
    m_currentModule = moduleIndex;
    m_searchEditor->clear();
    updateResults();
}

void AIHelpPanel::updateResults()
{
    juce::String query = m_searchEditor->getText().toLowerCase();
    m_filteredTopics.clear();

    for (auto& t : m_topics) {
        if (query.isEmpty()) {
            m_filteredTopics.push_back(t);
        } else {
            bool match = juce::String(t.title).toLowerCase().contains(query)
                      || juce::String(t.content).toLowerCase().contains(query);
            if (!match) {
                for (auto& kw : t.keywords)
                    if (juce::String(kw).toLowerCase().contains(query)) { match = true; break; }
            }
            if (match) m_filteredTopics.push_back(t);
        }
    }

    m_listModel->topics = &m_filteredTopics;
    m_topicList->updateContent();

    if (!m_filteredTopics.empty())
        showTopicContent(0);
}

void AIHelpPanel::showTopicContent(int topicIndex)
{
    if (topicIndex < 0 || topicIndex >= static_cast<int>(m_filteredTopics.size())) return;
    auto& topic = m_filteredTopics[static_cast<size_t>(topicIndex)];
    m_contentDisplay->setText(juce::String(topic.content));
    m_showingShortcuts = false;
}

void AIHelpPanel::showShortcuts()
{
    m_showingShortcuts = true;
    juce::String text = "=== RACCOURCIS CLAVIER ===\n\n";

    std::string lastModule;
    for (auto& s : m_shortcuts) {
        if (s.module != lastModule) {
            text += "\n--- " + juce::String(s.module) + " ---\n";
            lastModule = s.module;
        }
        text += "  " + juce::String(s.key) + "  :  " + juce::String(s.action) + "\n";
    }

    m_contentDisplay->setText(text);
}

void AIHelpPanel::toggleVisible()
{
    setVisible(!isVisible());
}

void AIHelpPanel::paint(juce::Graphics& g)
{
    g.fillAll(Colors::bgSurface());
    g.setColour(Colors::border());
    g.drawRect(getLocalBounds(), 1);
}

void AIHelpPanel::resized()
{
    int w = getWidth(), h = getHeight();
    int M = 10;

    m_titleLabel->setBounds(M, M, w - 80, 24);
    m_closeBtn->setBounds(w - 36, M, 26, 24);
    m_searchEditor->setBounds(M, 40, w - M * 2, 28);
    m_shortcutsBtn->setBounds(M, 72, w - M * 2, 26);

    int listH = (h - 110) / 3;
    m_topicList->setBounds(M, 104, w - M * 2, listH);
    m_contentDisplay->setBounds(M, 110 + listH, w - M * 2, h - 120 - listH);
}

} // namespace BeatMate::UI
