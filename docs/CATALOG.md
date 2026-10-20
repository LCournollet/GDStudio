# Catalogue d'objets : provenance des données

Les tables du moteur ne viennent pas de listes communautaires : elles ont été extraites du jeu
installé (GD 2.2091, `GeometryDash.exe` du 17/10/2026), **en lecture seule**, par analyse statique.

| Donnée | Méthode |
|---|---|
| ID → sprite (4 093 objets) | décodage des 4 094 insertions de `ObjectToolbox::init` |
| Classe C++ de chaque objet | émulation de `GameObject::createWithKey` + informations RTTI |
| Type (solide, obstacle, décoration, trigger, portail...) et hitbox | émulation du `customSetup` de chaque classe (constantes du binaire : spike 0,2×0,4 → 6×12, rayons des scies 32,3 / 21,6 / 11,925, orbes ×1,2) |
| Taille visible et décalage des sprites | planches `-uhd.plist` (4 px = 1 unité, vérifié : `square_01` = 120 px = 30 unités) |
| Orientation canonique des sprites (coins, glows, lignes) | mesure des pixels des planches du jeu (`tools/catalog/gen_catalog.py`) |

Fichiers générés : `core/src/generated/ObjectTableData.inc` (table de gameplay),
`core/src/generated/DecoCatalogData.inc` (61 objets choisis avec forme et rôle + 3 360 décorations
natives statiques pour la bibliothèque), `tools/data/*.json` (rendu hors-jeu).

Règles de sécurité : seuls les objets de type *Decoration* sans hitbox, de classe `GameObject`
(statique) ou `EnhancedGameObject` (marqués « animated », jamais utilisés par défaut) sont dans le
catalogue. Les objets de type portail/trigger/texte classés « décoration » par le jeu (sorties de
téléporteur, textes) sont exclus. Dans le jeu, chaque objet choisi est en plus recréé une fois au
démarrage et désactivé s'il ne se comporte pas comme une décoration.

Données extraites : `tools/catalog/data/` (`object_types.json`, `frames_uhd.json`,
`deco_candidates.json`). Scripts d'extraction (désassemblage / émulation, lecture seule de l'exe) :
`tools/catalog/extract/`.

Pour régénérer les tables C++ (par exemple après avoir réextrait les données pour une nouvelle
version de GD) :

```powershell
python tools/catalog/gen_catalog.py --objdata tools/catalog/data --resources "<GD>\Resources" --root .
```
