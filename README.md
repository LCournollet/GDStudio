# GD Decoration Studio

Mod Geode (C++23) qui génère automatiquement la décoration d'un niveau Geometry Dash dont le
gameplay existe déjà : analyse de la géométrie, couches indépendantes, masques, palettes,
prévisualisation, édition non destructive. Tout ce qui est généré est fait d'**objets natifs**
du jeu, éditables dans l'éditeur classique, et le niveau s'affiche normalement sans le mod.

## État actuel (à lire)

| Élément | État |
|---|---|
| Moteur (analyse, moteurs structurel / éclairage / artistique, couches, masques, palettes, presets, persistance, historique, validation) | Implémenté, **40 tests automatisés** (4 500 vérifications) qui passent |
| Rendu hors-jeu avec les vrais sprites du jeu | Implémenté (`tools/render`), utilisé pour juger la qualité visuelle |
| Mod Geode (adaptateur éditeur + interface) | **Compile et se package** (`.geode`) pour Geode 5.11.2 / GD 2.2081 |
| Exécution dans le jeu | **Non testée** : ta version de GD (2.2091, build Steam du 17/10/2026) n'est pas encore supportée par Geode (le loader 5.11.2 refuse les mods 2.2081 en mode « forward compatibility »). Voir `docs/LIMITATIONS.md` et `docs/TESTING.md`. |

## Structure

```
core/            bibliothèque C++ pure (aucune dépendance à Geode) : tout le moteur
  include/gdds/  API publique (Analysis, Engine, Generator, Studio, Layer, Mask, Palette, Presets, Project...)
  src/           implémentation ; src/generated/ = tables générées depuis les données du jeu
mod/             mod Geode : adaptateur éditeur (EditorBackend), session, outil de masques, interface
tests/           tests unitaires et d'intégration (scénarios A à L)
tools/cli/       gdds-cli : analyser / décorer des chaînes de niveau sans le jeu
tools/render/    rendu PNG d'un niveau avec les sprites du jeu (lecture seule de Resources/)
tools/catalog/   génération des tables d'objets à partir des données extraites du jeu installé
tools/levels/    lecture seule de tes niveaux locaux (export) et création de .gmd
docs/            architecture, persistance, limites, tests, catalogue
scripts/build.ps1  compilation (tests ou mod)
```

## Compiler

Prérequis installés sur ce PC : Visual Studio Build Tools 2022 (MSVC 14.44), CMake + Ninja
(pip `--user`), Geode CLI 3.9.0 (`%LOCALAPPDATA%\Programs\geode-cli`), Geode SDK 5.11.2
(`%USERPROFILE%\geode-sdk`, variable `GEODE_SDK`).

```powershell
./scripts/build.ps1 -Target tests          # cœur + tests + CLI, exécute les tests
./scripts/build.ps1 -Target mod            # mod Geode -> %LOCALAPPDATA%\gdds-build\mod\mod\toyger.gd-decoration-studio.geode
./scripts/build.ps1 -Target mod -Install   # idem + copie dans le dossier mods de GD (à faire seulement sur une version de GD supportée)
```

Les dossiers de compilation sont dans `%LOCALAPPDATA%\gdds-build` (chemins courts : MSVC limite
les chemins d'objets à 250 caractères).

## Essayer sans le jeu

```powershell
$cli = "$env:LOCALAPPDATA\gdds-build\tests\tools\cli\gdds-cli.exe"
& $cli testlevel G G.txt                                   # niveau de test (A..L)
& $cli decorate G.txt G_deco.txt --preset "Neon" --level-colors --save-project G.json
python tools/render/render.py G_deco.txt G_deco.png --x0 0 --x1 2500
# Un de tes niveaux (lecture seule de la sauvegarde) :
python tools/levels/gdsave.py list
python tools/levels/gdsave.py export "Nom du niveau" mon_niveau.txt
& $cli decorate mon_niveau.txt mon_niveau_deco.txt --preset "Dark Tech"
# Régénérer une seule couche dans une zone :
& $cli decorate G_deco.txt G_deco2.txt --project G.json --mode regenerate --layers background-details --mask 5000,-1000,8000,3000
```

## Utilisation dans l'éditeur (quand la version de GD est supportée)

1. Ouvre un niveau dans l'éditeur : un bouton **Studio** apparaît en haut à gauche (ou **Alt+D**).
2. **Presets** : choisis un style (Modern Glow, Minimal, Dark Tech, Neon, Industrial, Cyberpunk, Abstract).
3. **Objects / Colors** : choisis les objets et les couleurs, globalement ou par couche.
4. **Layers** : règle chaque couche (densité, complexité, intensité, échelle, opacité, seed, style, Z, politique de bord de masque), verrouille / masque / renomme / duplique / crée des sous-couches.
5. **Masks** : dessine rectangles, polygones, pinceau, sections X, ou crée un masque à partir des structures sélectionnées ; inclusion ou exclusion.
6. **Generate** : *Preview* (objets proposés surlignés en cyan, Apply / Cancel dans la barre d'outils), puis *Generate* / *Regenerate* / *Remove*.
7. Ajuste une couche (ex. intensité du glow : appliqué en direct, sans régénération), régénère une seule couche ou une seule zone.
8. Retouche à la main : les objets modifiés sont détectés et conservés (politique *Keep* / *Replace* / *Ask*).
9. Sauvegarde le niveau : le projet du studio est sauvegardé avec, et retrouvé à la réouverture.

Documentation : [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [docs/PERSISTENCE.md](docs/PERSISTENCE.md),
[docs/LIMITATIONS.md](docs/LIMITATIONS.md), [docs/TESTING.md](docs/TESTING.md), [docs/CATALOG.md](docs/CATALOG.md).
