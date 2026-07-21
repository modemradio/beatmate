# Chantiers avancés macOS — à construire ET tester sur le Mac

Ces 2 fonctions **compilent déjà** (stubs `#else`) : l'app tourne, elles sont
seulement inactives sur Mac. Leur implémentation touche des **permissions système
+ de l'audio temps réel + de l'automation UI** qui doivent être testées sur la
machine — les écrire à l'aveugle donnerait du code non fiable.

---

## 1. Écoute du son système — `AudioListenerService`

Rôle : capter la sortie audio du Mac, l'empreinter, reconnaître quel morceau de la
bibliothèque joue (dans rekordbox, Serato…). Sur Windows = WASAPI loopback.

### API macOS (recherchée)
Deux voies, choisir **CoreAudio Process Taps** (audio-only, plus propre) :

| Voie | macOS | Permission | Remarque |
|---|---|---|---|
| **CoreAudio Process Taps** (`CATapDescription`, `AudioHardwareCreateProcessTap`) | **14.4+** | `NSAudioCaptureUsageDescription` (TCC dédiée) | audio-only, propre ; **nécessite une identité de signature stable** |
| ScreenCaptureKit (audio) | 13+ | Screen Recording | indicateur menu-bar, pensé écran+audio |

### À faire côté build
1. **Info.plist** : ajouter la clé `NSAudioCaptureUsageDescription`
   (« BeatMate écoute la sortie audio pour reconnaître le morceau en cours »).
   En JUCE : passer par un Info.plist custom ou `juce_add_gui_app(... )` +
   `set_target_properties(... XCODE_ATTRIBUTE_INFOPLIST_KEY_...)`.
2. **Signature** : la permission tap est liée à l'identité → app signée (dev id).
3. **Implémentation** (`#elif defined(__APPLE__)` dans `runCaptureLoop`) :
   - créer un `CATapDescription` (tap global sur la sortie),
   - `AudioHardwareCreateProcessTap` → `AudioHardwareCreateAggregateDevice`,
   - installer un `AudioDeviceIOProc` qui pousse les frames dans le même pipeline
     d'empreinte que la branche Windows (ring buffer → fingerprint existant).
   - Le format (sample rate/canaux) se lit sur l'aggregate device.

### Référence
- AudioCap (code exemple macOS 14.4+) : https://github.com/insidegui/AudioCap
- ScreenCaptureKit audio : https://github.com/connerkward/macos-screen-recorder-system-audio

---

## 2. Envoi vers les platines — `SendToDJRouter`

Rôle : envoyer un morceau vers rekordbox/Serato/Traktor/VirtualDJ (glisser-déposer
synthétique, focus fenêtre). Sur Windows = `EnumWindows` + `SendInput` + OLE
`DoDragDrop` + presse-papiers `CF_HDROP`.

### Équivalent macOS
- **Presse-papiers** : `NSPasteboard` (type `NSPasteboardTypeFileURL`).
- **Focus / fenêtres** : Accessibility API (`AXUIElement`) → **permission
  Accessibilité** (TCC) obligatoire, l'utilisateur doit l'accorder.
- **Glisser-déposer synthétique** : `CGEvent` (mouse down/move/up) — fragile,
  dépend de l'app cible ; alternative plus fiable = **AppleScript**
  (`osascript`) quand l'app cible l'expose, ou déposer le fichier dans le
  dossier « inbox » du logiciel (ce que fait déjà `sendToRekordbox` via XML).

### Recommandation
Commencer par la voie **fichier/inbox** (déjà utilisée pour rekordbox) plutôt que
l'automation clavier/souris : plus robuste et sans permission Accessibilité. Le
drag&drop synthétique n'est à faire que si nécessaire, et se teste sur le Mac.

---

## En résumé
Le **cœur** (analyse, bibliothèque, Mix Studio, stems, IA, licence) est prêt et
identique. Ces 2 fonctions sont des **sous-systèmes à finir sur le Mac** (permissions
+ audio/automation à tester). Elles n'empêchent ni la compilation ni le lancement.
