# Quand Geode supportera GD 2.2091

À la date du 20/10/2026, Geode 5.11.2 fait tourner GD 2.2091 en *forward compatibility mode* et
refuse tous les mods ciblant 2.2081 (dont celui-ci). Procédure à suivre quand la mise à jour sort.

## 1. Vérifier que la mise à jour est vraiment là

- Les notes de version de Geode (https://github.com/geode-sdk/geode/releases) annoncent le support
  de **2.2091** (pas seulement des « fixes » en forward compat).
- Après la mise à jour, le dernier log (`<GD>\geode\logs`) ne contient plus
  `(forward compatibility mode)` dans la ligne `Running Geode vX in Geometry Dash ...`.

## 2. Mettre à jour le jeu

1. Sauvegarder `%LOCALAPPDATA%\GeometryDash\CCLocalLevels.dat` et `CCGameManager.dat`.
2. Lancer GD : le loader se met à jour (ou utiliser l'installeur officiel de Geode).
3. Mettre à jour les autres mods depuis la liste des mods de Geode.

## 3. Recompiler le mod

```powershell
geode sdk update                      # met à jour %USERPROFILE%\geode-sdk vers la nouvelle version
geode sdk install-binaries --platform win
./scripts/build.ps1 -Target mod -GdVersion 2.2091 -Clean
```

Le SDK actuel est un clone superficiel figé sur le tag v5.11.2 : si `geode sdk update` échoue,
supprimer `%USERPROFILE%\geode-sdk` puis `geode sdk install "%USERPROFILE%\geode-sdk"` (clone neuf).

Si la compilation échoue (nouvelle version majeure de Geode = changements d'API), c'est le moment
de me renvoyer les erreurs : l'adaptateur (`mod/src`) est volontairement fin, le moteur (`core/`)
ne dépend pas de Geode.

Quand ça compile : `./scripts/build.ps1 -Target mod -GdVersion 2.2091 -Install` copie le `.geode`
dans le dossier des mods du jeu (profil CLI `steam`).

## 4. Premier test en jeu

Sur un **nouveau niveau de test**, jamais sur un niveau existant : suivre la liste de
`docs/TESTING.md` (section « Dans le jeu »). Pour chaque problème, noter l'étape et joindre le log
Geode (`<GD>\geode\logs`, lignes `[GD Decoration Studio]`).

Points à surveiller en priorité : rafraîchissement des couleurs, bouton/raccourci Alt+D, dessin des
masques, prévisualisation (contours cyan), sauvegarde/réouverture.
