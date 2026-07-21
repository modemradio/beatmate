# BeatMate — version macOS (passation)

Ce dossier est une **copie complète autonome** du code de BeatMate, adaptée pour
macOS. Il est **séparé du projet Windows** (`BeatMateV11/`) : rien ici ne modifie
la version Windows, qui reste intacte.

## Pour Claude Code sur le Mac — lis d'abord ceci

- Ce dossier compile pour macOS. Le code Windows d'origine n'est **pas** ici.
- Les parties spécifiques Windows sont entourées de `#ifdef _WIN32` ; les branches
  Mac ont été ajoutées (`#elif defined(__APPLE__)`), **recherche à l'appui**.
- **Méthode de travail** : lancer `./build_mac.sh`, lire les erreurs de compilation,
  corriger le fichier concerné **ici même** (c'est la copie Mac), relancer. On
  avance piloté par les erreurs réelles.
- Ne PAS chercher à revenir vers le dossier Windows : cette copie est autonome.

## Build

```sh
xcode-select --install          # une fois
brew install cmake ffmpeg       # une fois
cd BeatMateMac
./build_mac.sh                  # arch native ; ARCH=arm64 pour forcer Apple Silicon
```

Sortie attendue : `build-mac/BeatMateV11_artefacts/Release/BeatMate V11.app`
Puis : `./package_dmg.sh "<...>/BeatMate V11.app"` → le `.dmg`.

## Ce qui est déjà adapté (dans cette copie)

- **CMake** : ONNX Runtime macOS (local, `third_party/onnxruntime/`), flags SIMD
  x86 désactivés sur ARM, `@rpath` pour le `.dylib`, PerfDJ désactivé.
- **Licence** : chemins `~/Library/Application Support`, **HardwareId via IOKit**
  (`IOPlatformSerialNumber` + `IOPlatformUUID` + MAC `en0`).
- **Détection des logiciels DJ** (13 fichiers) : chemins Mac vérifiés — rekordbox
  `~/Library/Pioneer`, Serato `~/Music/_Serato_`, Traktor `~/Documents/Native
  Instruments`, Engine DJ `~/Music/Engine Library`, VirtualDJ `~/Library/Application
  Support/VirtualDJ`, djay Pro (conteneur sandbox).
- **Stems** : `StemSepSotaService` / `DemucsStemService` cherchent le binaire Mac
  (`stemsep` / `demucs`, sans `.exe`). Voir `STEMS-macos.md` pour construire le moteur.
- **Dépendances placées** : ONNX macOS + modèles IA CLAP (`models/`).

## Dépendances

Voir **`DEPENDANCES-macos.md`**. Les libs C++ (JUCE, TagLib, ONNX, SoundTouch…) se
téléchargent automatiquement au `cmake` — rien à installer à part Xcode, CMake, ffmpeg.

## Restant (documenté, à finir/tester sur le Mac)

- **`STEMS-macos.md`** — construire le moteur de stems (audio-separator 0.44.2 +
  mêmes modèles → sortie **identique** à Windows).
- **`CHANTIERS-AVANCES-macos.md`** — écoute du son système (CoreAudio Taps) et envoi
  vers platines (Accessibility / inbox). Ces 2 compilent en stub (app inactive sur
  ces points), à implémenter + tester sur le Mac.

## Ce qui fonctionne à l'identique dès la compilation

Analyse (BPM, clé, énergie, structure, LUFS), bibliothèque, tags, playlists, Mix
Studio, transitions, comparaison, agenda, suggestions IA / similarité (CLAP).
