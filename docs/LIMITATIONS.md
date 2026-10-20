# Limites connues et décisions

Ce document liste honnêtement ce qui n'est pas garanti ou pas encore vérifié.

## Version du jeu et de Geode (bloquant pour les tests en jeu)

- GD installé : **2.2091** (Steam, PE du 17/10/2026). Geode installé : **5.11.2**, qui cible
  **2.2081** et tourne en *forward compatibility mode* : dans ce mode, le loader **refuse** tout mod
  déclarant `gd: 2.2081` (c'est le cas de ce mod et aussi de `geode.node-ids`).
- Le mod est compilé contre le SDK 5.11.2 / GD 2.2081 : il ne peut donc pas être chargé dans ton
  jeu actuel. **Aucun comportement en jeu n'a été observé.** Dès que Geode publie la prise en charge
  de 2.2091 : mettre à jour le SDK et le loader, compiler avec `-GdVersion 2.2091`, tester.
- Les bindings 2.2091 publiés (`geode-sdk/bindings`, branche `main`, 19/10/2026) **ne compilent
  avec aucun SDK Geode publié** : ni la 5.11.2, ni la branche `main` du SDK (il manque les en-têtes
  cocos de 2.2091 `CCTouchClipper.h` / `CCKeyboardActionManager.h`, et des signatures changent).
  L'échec se produit dans la bibliothèque de bindings de Geode elle-même, avant notre code. Vérifié
  le 19/10/2026 sans rien installer dans le jeu. Conclusion : aucun mod ne peut encore cibler 2.2091 ;
  il faut attendre la publication officielle.
- Toutes les fonctions du jeu utilisées par le mod ont une adresse Windows dans les bindings
  2.2081 **et** 2.2091 (vérifié dans les fichiers broma). L'adaptateur évite volontairement
  `getLevelString`, `EditButtonBar::create`, `LevelSettingsObject::create`, `undoLastAction` de
  l'UI, et la dépendance à `node-ids`, absents ou cassés en 2.2091.

## Points non vérifiés dans le jeu (à tester en priorité)

- **Rafraîchissement des couleurs** après écriture des canaux (`ColorAction::setupFromString`,
  `transferDefaultColors`, `updateObjectColors`) : la séquence exacte qui rafraîchit l'éditeur n'est
  pas documentée ; si les couleurs n'apparaissent qu'après réouverture du niveau, c'est ce point.
- **Décalage vertical** entre coordonnées du niveau et nœuds de l'éditeur : mesuré automatiquement
  sur un objet (pas codé en dur) ; utilisé seulement pour dessiner les masques et l'overlay.
- **Priorités tactiles** de la barre d'outils et de l'outil de masques dans l'éditeur.
- **Visibilité des couches masquées** : réappliquée après chaque `updateVisibility` de l'éditeur.
- **Vérification du catalogue au démarrage** : crée chaque objet du catalogue et lit son type ; si
  plus d'un tiers des contrôles échouent (lecture de champ non fiable), la vérification est ignorée.

## Sécurité du gameplay : ce qui est garanti, ce qui ne l'est pas

Garanti (vérifié par les tests) :
- aucun objet non généré n'est modifié, déplacé ou supprimé : comparaison complète avant/après de
  chaque opération, annulation automatique en cas de différence ;
- seuls des objets du catalogue, vérifiés comme décoration (pas de hitbox, pas trigger/portail/orbe/pad),
  sont créés, sans propriété autre que position/rotation/échelle/couleur/Z/groupe-tag ;
- aucun objet dessiné au-dessus des obstacles ne recouvre un obstacle.

Non garanti :
- **les trajectoires ne sont pas simulées** : la lisibilité repose sur des règles prudentes (marges
  autour des obstacles, réduction dans les couloirs serrés, ordre d'affichage, pas de premier plan près
  du gameplay) ;
- la lisibilité dépend aussi des couleurs choisies : l'harmoniseur avertit si le remplissage et les
  contours, ou le remplissage et le fond, sont trop proches, mais ne corrige pas les couleurs.

## Analyse

- Grille d'analyse au demi-bloc (15 unités) : les objets plus fins sont approximés.
- Pentes : rastérisées en escalier pour l'analyse ; le contour suit l'hypoténuse, mais le remplissage
  ne couvre pas les cellules de pente (le sprite de la pente reste visible).
- Objets déplacés / activés par des triggers (groupes ciblés par un trigger), cachés ou *no-touch* :
  jamais décorés (une décoration statique resterait en place quand la plateforme bouge, ou révélerait
  un chemin caché). Une option « hériter des groupes de déplacement » reste à faire.
- Les valeurs par défaut de Z layer des objets ne sont pas dans les données extraites : dans le jeu
  elles sont lues sur les objets ; hors-jeu (CLI, rendu), on suppose B2 pour les blocs et obstacles.

## Interface

- Une fenêtre à onglets (modale) + une barre d'outils permanente : le dessin de masques et la
  prévisualisation se font fenêtre fermée. Le style d'interface utilise les sprites du jeu.
- Import de preset : copier le `.json` dans le dossier des presets (bouton « Open presets folder »).

## Rendu hors-jeu

`tools/render/render.py` approxime le rendu du jeu (pas de shaders, pulses, animations, sprites de
glow automatiques des blocs, effets de caméra). Il sert à juger la composition et l'alignement, pas
à reproduire le jeu au pixel près.
