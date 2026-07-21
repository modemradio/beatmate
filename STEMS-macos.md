# Moteur de séparation de stems — version macOS

Le moteur Windows (`stemsep.exe`) est **`audio-separator`** (paquet Python, ~UVR)
figé avec PyInstaller, exposant un CLI maison : `--input --out --model --device`.

`audio-separator` a une **version macOS Apple Silicon officielle** (accélération
CoreML), avec les **mêmes modèles**. On reconstruit donc le même binaire pour Mac,
avec le **même CLI** → remplacement direct. Le C++ (`StemSepSotaService`) cherche
déjà le binaire `stemsep` (sans `.exe`) sur Mac.

## Modèles attendus par le C++

| `--model` envoyé par BeatMate | Modèle réel |
|---|---|
| `htdemucs_ft` | Demucs 4-stems (voix/batterie/basse/autre) |
| `vocft` | UVR-MDX-NET-Voc_FT (voix / instrumental) |

Les `.onnx` (UVR-MDX-NET-Voc_FT, Kim_Vocal_2…) sont **cross-platform** : réutiliser
ceux du dépôt Windows (`tools/external/stemsep/models/`).

## Construction (sur le Mac / la VM)

Pour un résultat **IDENTIQUE** à Windows : même version du paquet + mêmes modèles.

```sh
brew install ffmpeg python@3.12
python3.12 -m venv stemsep-venv && source stemsep-venv/bin/activate
pip install "audio-separator[silicon]==0.44.2" pyinstaller   # MÊME version que Windows
```

Wrapper `stemsep_cli.py` (reproduit le CLI attendu par BeatMate) :

```python
import argparse, os
from audio_separator.separator import Separator

MODEL_MAP = {
    "htdemucs_ft": "htdemucs_ft.yaml",
    "vocft":       "UVR-MDX-NET-Voc_FT.onnx",
}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--model", required=True)
    ap.add_argument("--device", default="auto")   # audio-separator[silicon] -> CoreML auto
    a = ap.parse_args()

    model_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "models")
    sep = Separator(output_dir=a.out, model_file_dir=model_dir)
    sep.load_model(model_filename=MODEL_MAP.get(a.model, a.model))
    sep.separate(a.input)

if __name__ == "__main__":
    main()
```

Figer en binaire `stemsep` :

```sh
pyinstaller --onedir --name stemsep stemsep_cli.py
# -> dist/stemsep/stemsep  (+ dossier _internal)
```

## Mise en place

Copier à côté de `BeatMate V11.app` (le C++ le cherche là) :

```
BeatMate V11.app/Contents/MacOS/
  tools/external/stemsep/
    stemsep                    <- le binaire figé
    _internal/…                <- runtime PyInstaller
    models/                    <- les .onnx (réutilisés du dépôt Windows)
```

Vérifier au premier lancement le log : *« ONNXruntime has CoreMLExecutionProvider
available, enabling acceleration »* = GPU Apple Silicon actif.

## Alternative plus rapide (optionnel)

`demucs-mlx` / `mlx-audio-separator` (framework MLX Apple) : ~73× temps réel,
2,6× plus rapide que PyTorch MPS. Nécessiterait d'adapter le mapping de modèles,
mais reste un CLI Python figeable de la même façon.

## Sources
- audio-separator : https://github.com/nomadkaraoke/python-audio-separator (macOS : `pip install "audio-separator[silicon]"`)
- MLX Apple Silicon : https://github.com/ssmall256/mlx-audio-separator · https://github.com/ssmall256/demucs-mlx
