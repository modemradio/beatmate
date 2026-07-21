# Dépendances de BeatMate — version macOS

Recensées depuis `cmake/Dependencies.cmake` et le `CMakeLists.txt`. Rien de tout
ceci ne modifie la version Windows : cette liste concerne la copie Mac
(`BeatMateMac/`).

## A. À installer sur le Mac (une fois)

| Outil | Installation | Rôle |
|---|---|---|
| **Xcode + Command Line Tools** | App Store, puis `xcode-select --install` | Compilateur clang + SDK macOS |
| **CMake** | `brew install cmake` | Génère le projet Xcode et pilote le build |
| **rsync** | déjà présent sur macOS | (utilisé par les scripts) |
| **ffmpeg** *(runtime)* | `brew install ffmpeg` | Export MP3 / MP4 (sinon ces exports sont désactivés) |

C'est tout ce qui s'installe à la main. Le reste est récupéré automatiquement.

## B. Bibliothèques C++ — téléchargées AUTOMATIQUEMENT par CMake

Exactement comme sur Windows : `cmake` les récupère depuis GitHub au moment de la
configuration (FetchContent). **Rien à installer, rien à copier.**

| Bibliothèque | Source | Rôle |
|---|---|---|
| JUCE | github.com/juce-framework/JUCE | Framework UI + audio (cross-platform) |
| SoundTouch | codeberg.org/soundtouch | Time-stretch / pitch |
| kissfft | github.com/mborgerding/kissfft | FFT (analyse) |
| TagLib | github.com/taglib/taglib | Lecture/écriture des tags audio |
| nlohmann/json | github.com/nlohmann/json | JSON |
| spdlog | github.com/gabime/spdlog | Journalisation |
| signalsmith-stretch | github.com/Signalsmith-Audio | Keylock / pitch-shift |
| SQLite3MultipleCiphers | github.com/utelle/SQLite3MultipleCiphers | Base de données chiffrée |
| fmt | github.com/fmtlib/fmt | Formatage de chaînes |
| **ONNX Runtime** | releases GitHub microsoft/onnxruntime | Inférence IA (beatgrid, CLAP). **Binaire pré-compilé** : version macOS `universal2` (arm64+Intel). URL adaptée dans `BeatMateMac/cmake/Dependencies.cmake`. |

> ✅ ONNX Runtime macOS est **déjà placé dans le dossier** : `third_party/onnxruntime/`
> (include + `libonnxruntime.1.16.3.dylib`, universal2 officiel Microsoft **1.16.3**).
> Le CMake (`cmake/Dependencies.cmake`) pointe dessus en local — pas de
> téléchargement au build.
>
> ⚠️ Version **1.16.3** choisie volontairement : ONNX Runtime **1.17+ exige macOS
> 13.3+** (passage au C++20). La 1.16.3 supporte **macOS 10.15**, ce qui permet de
> viser les Mac de 2019. Mêmes modèles, résultats IA quasi-identiques.

## Compatibilité macOS

- **Plancher : macOS 10.15 (Catalina, 2019)** — fixé par `std::filesystem` (10.15+)
  et compatible avec ONNX RT 1.16.3. Couvre ~7 ans de Mac jusqu'à la dernière version.
- Réglé dans `build_mac.sh` (`CMAKE_OSX_DEPLOYMENT_TARGET=10.15`).
- Fonction « écoute du son système » = macOS 14.4+ uniquement (secondaire, stubbée).
- Matériel recommandé : bon CPU + 16 Go de RAM pour l'analyse et les stems (comme Windows).

## C. Frameworks système macOS — liés AUTOMATIQUEMENT par JUCE

`juce_add_gui_app` lie tout seul ce dont l'app a besoin. **Rien à faire.**
Concrètement : Cocoa/AppKit, CoreAudio, CoreMIDI, AudioToolbox, Accelerate,
QuartzCore, CoreGraphics, WebKit (aperçu web), IOKit, Security, Metal…

## D. Outils / assets runtime — versions Mac à fournir (les « chantiers »)

Ceux-là ne sont pas nécessaires pour **compiler** ; ils manquent seulement à
l'**exécution** et l'app dégrade proprement sans eux (message, fonction désactivée) :

| Élément | Windows aujourd'hui | Équivalent Mac |
|---|---|---|
| **ffmpeg** | `tools/external/ffmpeg.exe` | `brew install ffmpeg` (ou binaire bundlé arm64) — export MP3/MP4 |
| **Séparation de stems** | `stemsep.exe` / `demucs.exe` (ONNX-DirectML) | binaire macOS/arm64 (ONNX Runtime CoreML EP) — chantier lourd |
| **Modèles CLAP** | `models/clap`, `models/clap_tags` (~531 Mo, ONNX) | **identiques** (cross-platform) — à copier à côté de l'app pour la similarité/tags IA |
| **ONNX Runtime .dylib** | fourni par (B) | fourni par (B), à placer dans le bundle |

## Résumé

- **Pour COMPILER** : Xcode + CMake, et tout le reste se télécharge tout seul
  (FetchContent + ONNX macOS). La copie `BeatMateMac/` est suffisante.
- **Pour un app 100 % fonctionnelle au RUNTIME** : ajouter ffmpeg (brew), les
  modèles CLAP (copie), et à terme un moteur de stems macOS.
- **Rien** de tout ceci ne touche ni ne dépend de la version Windows.
