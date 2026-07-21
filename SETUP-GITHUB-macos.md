# Compiler BeatMate macOS gratuitement, sans Mac (GitHub Actions)

Pas de Mac, pas de VM ? GitHub compile ton app sur un **vrai Mac Apple Silicon**
et te rend le `.app` / `.dmg` à télécharger. Les erreurs de compilation
apparaissent dans les logs. C'est gratuit.

## Public ou privé ?

- **Dépôt public** → minutes macOS **illimitées et gratuites**. Le plus simple si
  le code source peut être visible.
- **Dépôt privé** → **~200 min de Mac/mois** incluses (offre gratuite). Le cache
  `ccache` (déjà configuré) rend les recompilations bien plus rapides, donc ça
  suffit largement pour itérer. Choisis privé si tu ne veux pas exposer le source.

## Étapes (une seule fois)

1. **Créer un compte** sur github.com (gratuit) si tu n'en as pas.
2. **Créer un dépôt** vide : bouton *New repository* → nom `BeatMateMac` →
   *Public* ou *Private* → *Create*.
3. **Envoyer le dossier** depuis Windows (PowerShell 7, dans `BeatMateMac/`) :
   ```powershell
   git init
   git add .
   git commit -m "BeatMate macOS"
   git branch -M main
   git remote add origin https://github.com/TON-PSEUDO/BeatMateMac.git
   git push -u origin main
   ```
   (GitHub te demandera de te connecter la première fois.)

> Les gros fichiers (modèles IA `models/`, `.dylib` ONNX) sont **exclus** par
> `.gitignore` — ils dépassent la limite de 100 Mo/fichier de GitHub. Ce n'est pas
> grave : la compilation n'en a **pas besoin** (ce sont des ressources d'exécution).
> ONNX Runtime est **retéléchargé automatiquement** par le workflow.

## Ce qui se passe ensuite (automatique)

Dès le `push`, GitHub lance le build (onglet **Actions** du dépôt). Tu verras :

- 🟡 en cours → 🟢 réussi, ou 🔴 échoué avec les **erreurs dans les logs**.
- Si 🟢 : en bas de la page du build, section **Artifacts** → **BeatMate-macOS** à
  télécharger (contient `BeatMate V11.app` et le `.dmg`).

## La boucle de correction

1. Tu m'envoies les erreurs rouges des logs (onglet Actions → clic sur le build).
2. Je corrige le fichier concerné **ici dans `BeatMateMac/`**.
3. Tu refais `git add . && git commit -m "fix" && git push`.
4. GitHub recompile tout seul. On répète jusqu'au 🟢.

## Relancer sans rien changer

Onglet **Actions** → *Build macOS* → bouton **Run workflow** (grâce à
`workflow_dispatch`).

## Pour tester l'app en vrai

La CI **compile** mais ne permet pas de cliquer dans l'app (pas d'écran). Pour
l'essayer : récupère le `.dmg` de l'artifact et ouvre-le sur **n'importe quel Mac**
(un pote, un Mac de prêt). Le binaire est un vrai `.app` signable et distribuable.

## Universal2 (Intel + Apple Silicon) — plus tard

Par défaut le build vise l'architecture native du runner (arm64). Quand la
compilation est verte, pour couvrir aussi les vieux Mac Intel, on passera le
workflow en universal2 (`ARCH="arm64;x86_64"`), au prix d'un build ~2× plus long.
