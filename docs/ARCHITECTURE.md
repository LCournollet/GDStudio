# Architecture

## Principe

Le moteur est une bibliothèque C++ **pure** (`core/`, aucune dépendance à Geode) qui travaille
sur le format natif des objets GD (`1,<id>,2,<x>,3,<y>,...`). Le mod (`mod/`) n'est qu'un
adaptateur : il lit les objets de l'éditeur avec `GameObject::getSaveString` et crée les objets
générés avec `LevelEditorLayer::createObjectsFromString` (exactement ce que fait le copier/coller).
Conséquences :

- le moteur ne dépend pas de la disposition mémoire des objets (qui change entre versions de GD) ;
- tout le moteur est testable sans le jeu (tests, CLI, rendu hors-jeu) ;
- le même code tourne dans l'éditeur, dans les tests et dans le CLI (interface `LevelBackend`).

```
            ┌──────────────────────────── core (gdds_core) ────────────────────────────┐
 objets ──► │ Analyzer ─► LevelAnalysis ─► CompositionPlan ─► Engines ─► Harmonizer ─►  │
 (strings)  │                 (IR)                       (structural,     Validator     │
            │                                             lighting,          │         │
            │                                             artistic)          ▼         │
            │ Project (couches, masques, palette, registre) ◄── Studio (commit, rollback,│
            │                                                    historique, preview) │
            └──────────────────────────────────────▲───────────────────────────────────┘
                                                   │ LevelBackend
                       ┌───────────────────────────┴──────────────────────┐
                       │ EditorBackend (mod)   StringLevelBackend (tests, CLI)│
                       └──────────────────────────────────────────────────┘
```

## Correspondance avec les responsabilités demandées

| Responsabilité | Fichiers |
|---|---|
| Level Analyzer | `core/src/Analysis.cpp` (`Analyzer::analyze`, timeline de gameplay) |
| Geometry Analyzer | `Analysis.cpp` : grille creuse (`SparseGrid.hpp`), composantes connexes, contours, coins, profondeur, champ de distance, dégagement vertical, escaliers |
| Gameplay Safety Validator | `Generator.cpp` (`validatePlacements`), `Project.cpp` (`GameplaySnapshot`), `Studio.cpp` (`commit` : comparaison avant/après + rollback) |
| Object Catalog | `ObjectInfo.hpp` / `ObjectTable.cpp` (4 093 objets du jeu), `DecoCatalog.hpp/.cpp` (décorations : forme, orientation, taille visible) |
| Color Palette Manager | `Palette.hpp/.cpp` (rôles, ajustements, surcharges par couche, `ChannelAllocator`) |
| Structural Decoration Engine | `StructuralEngine.cpp` (remplissage, contours, coins, textures internes, détails, accents d'obstacles) + `LightingEngine.cpp` |
| Artistic Decoration Engine | `ArtisticEngine.cpp` (structures d'arrière-plan, détails, atmosphère, premier plan) |
| Layer Manager | `Layer.hpp/.cpp` (`LayerStack` : arbre, ordre, verrou, visibilité, duplication) |
| Generation Mask Manager | `Mask.hpp/.cpp` (`Mask`, `MaskSet` : combinaison, politiques de bord) + `mod/src/MaskTool.cpp` |
| Procedural Composition Engine | `Composition.cpp` (rythme, budget de densité, variantes de motifs) + `harmonize()` |
| Preview Renderer | `mod/src/EditorBackend.cpp` (`showPreview` : objets réels + contours cyan) et `tools/render/render.py` (hors-jeu) |
| Object Placement Manager | `Engine.cpp` (`EngineContext::place` : orientation canonique, échelle, centre visuel), `EditorBackend::createObjects` |
| Preset Manager | `Presets.hpp/.cpp` |
| Project Persistence | `Project.hpp/.cpp` (registre, réconciliation, JSON), `mod/src/Session.cpp` (fichiers projet) |
| Editor UI | `mod/src/ui/*` (fenêtre à onglets, barre d'outils), `mod/src/main.cpp` (hooks) |

## Pipeline de génération

1. **Analyse** – les empreintes de collision des objets de gameplay sont rastérisées sur une grille
   de 15 unités (demi-bloc). Les blocs adjacents forment une seule *structure* (composante 4-connexe) ;
   ses contours sont tracés (boucles extérieures et cavités), découpés en *segments* avec une face
   (haut/bas/gauche/droite), et les *coins* sont classés convexes/concaves. La profondeur intérieure
   (distance de Chebyshev au bord) sert aux insets ; un champ de distance donne l'espace libre ; un
   balayage par colonne donne les couloirs. Les portails donnent la chronologie (mode, vitesse,
   gravité, taille) et chaque section reçoit un indice de *resserrement*.
   Les objets *no-touch*, cachés, ou déplacés par des triggers ne forment pas de structure.
2. **Classification / planification** – `CompositionPlan` : densité et budget de détails par
   section (réduits dans les passages serrés), rythme dérivé de la vitesse (~0,55 s entre deux
   motifs), variante de motif par mode de jeu.
3. **Génération** – chaque couche est confiée à son moteur. Les moteurs raisonnent en *formes
   canoniques* (ligne horizontale, coin ouvert au nord-est, glow clair vers le bas...) ; la rotation
   de base de chaque sprite, mesurée sur les planches du jeu, est ajoutée au placement.
4. **Harmonisation** – lisibilité (rien ne couvre un obstacle), chevauchements, budget de densité,
   répétitions, zones vides/surchargées, contraste des couleurs.
5. **Validation** – identifiants autorisés uniquement, aucune propriété inattendue.
6. **Finalisation** (thread principal) – canaux de couleur, groupe-tag de la couche, variations HSV.
7. **Application** – suppression des anciens objets de la zone, création des nouveaux, écriture des
   canaux, comparaison du gameplay avant/après ; toute différence annule l'opération.

Les étapes 1 à 5 sont pures : dans l'éditeur elles tournent dans un thread de travail annulable
(`Studio::prepare/compute/finish`), l'éditeur n'est modifié que sur le thread principal.

## Déterminisme

Aucune décision n'utilise un flux aléatoire séquentiel partagé : chaque décision dérive sa graine
d'un hachage (seed de la couche, seed globale, position / signature de l'élément). Résultat :
même niveau + même configuration + même seed ⇒ même résultat, et régénérer une zone ne décale rien
ailleurs. Les mises en page séquentielles (piliers, skylines) partent toujours de l'origine du
niveau, jamais du bord du masque.

## Masques et opérations locales

L'appartenance à un masque est décidée sur le **point d'ancrage** de chaque placement, avec le
**même prédicat** pour la suppression et la génération : ce qui est hors du masque n'est jamais
touché, et ce qui est régénéré à l'intérieur est exactement ce qui a été retiré. Politiques de bord
(par couche) : `extend` (une structure touchée par le masque est traitée entièrement), `clip`
(les décorations sont découpées en morceaux alignés sur la grille du monde), `inside-only`.
Les masques d'exclusion sont toujours stricts.

## Couches et Z

Les couches du studio sont logiques (≠ Z layers et editor layers de GD). Le Z de chaque couche est
choisi automatiquement par rapport au gameplay : le block design juste au-dessus des blocs, le glow
derrière les blocs (il ne recouvre ni blocs ni obstacles), l'arrière-plan en B4/B3, le premier plan
en T3 (seulement loin du gameplay). Il peut être fixé manuellement.
