# Tests

## Automatiques (sans le jeu)

```powershell
./scripts/build.ps1 -Target tests      # compile et exécute tests/gdds_tests.exe
```

`tests/test_analysis.cpp` (géométrie), `tests/test_basics.cpp` (JSON, format des objets, palettes,
couches, masques), `tests/test_generation.cpp` (pipeline complet sur un backend « chaîne de niveau »).
Les niveaux de test sont dans `core/src/TestLevels.cpp` (`gdds-cli testlevel A..L fichier.txt`).

| Scénario | Test automatique | Ce qui est vérifié |
|---|---|---|
| A – plateforme simple | `analysis A`, `generation A` | 30 blocs = 1 structure, 4 segments, 4 coins ; 5 objets de remplissage, contour continu de 12 morceaux à l'intérieur du bord |
| B – plateformes reliées | `analysis B` | fusion des blocs adjacents, séparation des contacts en diagonale |
| C – coins | `analysis C`, `generation C`, quadrants, anneau | 5 coins convexes + 1 concave ; aucun « cran » aux coins intérieurs ; cavité détectée |
| D – spikes | `analysis D/E`, `generation D` | clusters d'obstacles, support ; contour continu sous les spikes |
| E – passage wave | `analysis D/E`, `generation E` | section wave détectée « serrée » ; pas de premier plan, highlights ni particules dans le couloir |
| F – zone ouverte | rendus `tools/render` | composition artistique (voir les images) |
| G – couches et palettes | `generation: deterministic`, `gameplay untouched`, `every built-in preset` | déterminisme, gameplay identique, 7 presets distincts sans rejet |
| H – régénération dans un masque | `generation H` | seuls les objets de la couche choisie dans le masque changent ; tout le reste est identique à l'octet près |
| I – couche verrouillée | `generation I` | la couche verrouillée ne change pas (ni régénération, ni suppression) ; verrou hérité des groupes |
| J – sauvegarde / réouverture | `generation J` | réconciliation complète ; récupération des couches par les groupes-tags sans fichier projet |
| K – retouche manuelle | `generation K` | objet retouché détecté ; conservé (Keep), confirmation (Ask), remplacé (Replace) |
| L – suppression totale | `generation L` | le niveau redevient exactement le gameplay d'origine |
| Undo / redo / preview | `undo / redo ...` | états exacts restaurés ; une preview annulée ne laisse rien |
| Sécurité | `rolls the operation back` | un backend qui altère le gameplay provoque l'annulation complète |
| Couleurs | `changing intensity` | l'intensité d'une couche ne change que l'opacité de ses canaux |

Résultat actuel : **40 tests, 4 504 vérifications, 0 échec.**

Vérifications visuelles hors-jeu : `tools/render/render.py` (rendu avec les sprites du jeu).
Performances mesurées sur un de tes niveaux (« Backbeat Revenge 2 », 115 969 objets, export en
lecture seule) : analyse 0,22 s, génération Dark Tech 19 283 objets en 3 s, gameplay intact.

## Dans le jeu (manuel) – quand Geode supporte ta version

Préparation : sauvegarde `%LOCALAPPDATA%\GeometryDash\CCLocalLevels.dat`, crée un **nouveau niveau**
de test (n'utilise pas tes niveaux existants), compile avec `./scripts/build.ps1 -Target mod -Install`
(pour la bonne version de GD), lance le jeu.

1. **Bouton** : ouvre l'éditeur ; le bouton « Studio » apparaît en haut à gauche ; Alt+D ouvre/ferme.
2. **Niveau de test** : construis une plateforme de 30 blocs, un U, quelques spikes (ou importe
   `G.gmd` produit par `python tools/levels/gdsave.py gmd G.txt "Test G" G.gmd` avec GMD API).
3. **Preview** : onglet Generate → Preview. Attendu : objets proposés entourés de cyan, barre d'outils
   Apply / Cancel. Cancel → plus aucun objet ajouté. Preview puis Apply → la décoration reste.
4. **Couleurs** : vérifier que la décoration prend les couleurs du preset immédiatement. Si elle reste
   blanche jusqu'à la réouverture du niveau → noter (point « rafraîchissement des couleurs »).
5. **Glow** : onglet Layers → Structural Glow → Intensity à 0.3. Attendu : le glow s'atténue en direct.
6. **Régénérer une couche** : Background Structures → style « skyline » → Regenerate. Seule cette couche change.
7. **Masque** : onglet Masks → Rectangle → dessine autour d'une zone → Done. Sélectionne la couche
   Background Details, change sa densité, « Regenerate in mask ». Seule la zone change.
8. **Verrou** : verrouille Base Block Design, Regenerate dans Generate. Le remplissage ne bouge pas.
9. **Retouche** : déplace un objet de contour à la main, régénère Outlines → l'objet déplacé reste.
10. **Couche masquée** : décoche « show » sur une couche → ses objets disparaissent de l'éditeur ;
    sauvegarde → réouvre : ils sont bien dans le niveau.
11. **Sauvegarde / réouverture** : sauvegarde, quitte GD, relance, rouvre le niveau → onglet Stats :
    « linked » = nombre d'objets générés, 0 manquant.
12. **Suppression totale** : Generate → Remove all → le niveau ne contient plus que le gameplay.
13. **Gameplay** : joue le niveau avant/après (même chemin, mêmes collisions).

Pour chaque étape, noter ce qui diffère et joindre le fichier de log Geode
(`<GD>/geode/logs`, lignes `[GD Decoration Studio]`).
