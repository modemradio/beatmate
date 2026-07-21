#!/usr/bin/env bash
#
# package_dmg.sh — Fabrique un .dmg d'installation à partir de BeatMate V11.app.
# À LANCER SUR UN MAC. L'équivalent Mac du MSI/zip Windows.
#
#   Usage :
#     ./package_dmg.sh "/chemin/vers/BeatMate V11.app"
#     ./package_dmg.sh "/chemin/vers/BeatMate V11.app" BeatMate-12.0.24.dmg
#
# Le .dmg contient l'app + un raccourci vers /Applications : l'utilisateur
# glisse l'app dans Applications, exactement comme une install Mac classique.
#
set -euo pipefail

APP="${1:?Usage: ./package_dmg.sh \"<BeatMate V11.app>\" [sortie.dmg]}"
OUT="${2:-BeatMate-macOS.dmg}"

if [[ ! -d "$APP" ]]; then
  echo "App introuvable : $APP"
  exit 1
fi

STAGE="$(mktemp -d)"
VOLNAME="BeatMate"

echo "== Préparation du contenu du .dmg =="
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"   # cible du glisser-déposer

echo "== Création du .dmg ($OUT) =="
rm -f "$OUT"
hdiutil create \
  -volname "$VOLNAME" \
  -srcfolder "$STAGE" \
  -ov -format UDZO \
  "$OUT"

rm -rf "$STAGE"
echo ""
echo "OK -> $OUT"
echo ""
echo "NOTE : l'app n'est PAS signée/notariée. Au premier lancement, macOS affichera"
echo "un avertissement Gatekeeper (clic droit > Ouvrir pour passer outre, ou"
echo "réglages Confidentialité > Ouvrir quand même). Pour une distribution sans"
echo "avertissement, il faudra un compte développeur Apple (99 \$/an) + notarisation."
