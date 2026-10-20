# Build expérimentale pour GD 2.2091 (avant la sortie officielle de Geode)

État au 2026-10-20 : **compilée et vérifiée hors du jeu, pas installée, pas testée en jeu.**

## Pourquoi pas avec Geode 5.11.2

- En *forward compatibility*, le loader 5.11.2 accepte un mod qui déclare cibler 2.2091
  (`ModMetadata::checkGameVersion`), **mais** en 2.2091 la classe de base `CCNode` a deux
  nouvelles fonctions virtuelles (`offsetPosition`, `applyScale`) : toutes les tables virtuelles
  des classes dérivées sont décalées. Un mod (ou Geode lui-même) compilé avec les en-têtes 5.11.2
  qui crée ses propres nœuds d'interface planterait. C'est pour cela que Geode 5.11.2 désactive son
  interface dans ce mode.
- Aujourd'hui, les 25 mods installés sont tous refusés par Geode 5.11.2 (24 « doesn't support this
  version », 1 échec de DLL) : remplacer le loader ne fait perdre aucun mod qui fonctionne.

## Méthode

1. **Loader** : branche `v6` de Geode (développement actif, cible native 2.2091, reconnaît
   l'exécutable via « add 2.2091 timestamp »), compilée avec clang-cl (LLVM 21, comme la CI de Geode).
   La libcurl précompilée par Geode demande `volatileaccessu.lib` (Windows SDK 10.0.26100) ; avec un
   SDK plus ancien, `tools/experimental/sdkshim/volatileaccess.c` fournit l'unique fonction utilisée
   (`RtlSetVolatileMemory`, un remplissage mémoire en écritures volatiles).
2. **Mod** : compilé contre ce SDK `v6` et **la même révision** des bindings 2.2091 que le loader.
   Geode v6 remplace `std::filesystem::path` par `asp::fs::Path` : les accès fichiers du mod passent
   par `mod/src/Files.hpp`, qui compile avec les deux versions (la build stable 2.2081 est inchangée).
3. Commande : `./tools/experimental/build_2091.ps1 -Part all` (prérequis en tête du script).

## Vérifications faites (sans lancer le jeu)

| Vérification | Résultat |
|---|---|
| Loader v6 compilé (`Geode.dll`, `Geode.lib`, `XInput1_4.dll`, `GeodeUpdater.exe`) | OK |
| Mod compilé pour `gd.win = 2.2091` | OK |
| 3 offsets de `GameObject` mesurés dans l'exe = ceux des bindings (`static_assert`, `mod/src/LayoutChecks.cpp`) | OK |
| 30 adresses de fonctions utilisées : débuts exacts de fonctions (`.pdata`), virtuelles présentes en vtable (`tools/experimental/verify_addresses.py`) | 30/30 |
| Bindings compilés = bindings vérifiés (diff des `.bro`) | identiques |
| Imports du mod depuis `Geode.dll` présents dans le loader compilé | 72/72 |
| Imports du mod depuis `libcocos2d.dll` / `libExtensions.dll` / glew32 / fmod du jeu installé | 1092 + 171 + 2 + 9, 0 manquant |
| Imports différés du loader depuis `libcocos2d.dll` / `libExtensions.dll` du jeu | 1174 + 172, 0 manquant |
| Installation puis retour arrière sur une **copie** du dossier du jeu | identique au départ (SHA-256, 0 différence) |
| Build stable 2.2081 + tests | 40/40 tests, mod empaqueté |

**Non vérifié** : le comportement réel en jeu (démarrage du loader, éditeur, génération). Seul un
lancement le prouvera.

## Installation (seulement avec ton accord)

`tools/experimental/install_2091.ps1` (GD fermé) :
- sauvegarde `Geode.dll`, `Geode.pdb`, `XInput1_4.dll`, `GeodeUpdater.exe`, les ressources du loader
  (`geode\resources\geode.loader`) et les fichiers de sauvegarde (`CCLocalLevels*.dat`,
  `CCGameManager*.dat`) dans `<GD>\gdds-backup-<date>\` ;
- installe le loader compilé **avec ses ressources** (le loader les contrôle par empreinte SHA-256 et
  téléchargerait sinon celles de la 5.11.2) et le mod ; les autres mods ne sont pas touchés.

Retour arrière : `tools/experimental/rollback_2091.ps1 -Backup "<dossier de sauvegarde>"` (les projets
du studio sont conservés).

## Quand Geode sortira officiellement pour 2.2091

Le loader expérimental demande au serveur « le dernier Geode pour gd=2.2091 » au démarrage (réglage
*auto-check-updates*). Tant qu'il n'y en a pas, rien ne se passe ; dès qu'une version officielle plus
récente existe, il la télécharge et la met en place au lancement suivant. Sinon : réinstaller Geode
avec l'installeur officiel. Le mod se recompile alors normalement (`docs/MISE_A_JOUR_GEODE.md`).

## Risques

- Loader de développement : peut planter, y compris hors de l'éditeur.
- Bindings 2.2091 en cours d'écriture : les fonctions utilisées sont vérifiées, la disposition
  mémoire des classes ne l'est que partiellement (3 champs de `GameObject`).
- Tester d'abord sur un **niveau neuf**, après sauvegarde.
