# Persistance, copie et partage

## Ce qui est stocké où

| Donnée | Emplacement |
|---|---|
| Décoration générée | **Dans le niveau**, sous forme d'objets natifs GD |
| Appartenance d'un objet à une couche | Dans le niveau : chaque objet généré porte **un seul groupe en plus**, le *groupe-tag* de sa couche (ID élevé, choisi parmi ceux qu'aucun objet ni trigger du niveau n'utilise) |
| Couleurs de la décoration | Dans le niveau : canaux de couleur alloués par le studio (de 999 vers le bas, jamais un canal utilisé par le niveau) |
| Couches, palette, masques, paramètres, seeds, registre des objets, journal des opérations | Fichier projet JSON : `<save geode>/toyger.gd-decoration-studio/projects/<nom>-<n>.json` |
| Presets utilisateur | `<config geode>/toyger.gd-decoration-studio/presets/*.json` (export / import = ces fichiers) |
| Favoris, collections | données sauvegardées du mod (globales) |

Le niveau ne dépend **jamais** du fichier projet pour s'afficher : sans le mod, c'est un niveau
normal. Le projet sert uniquement à l'édition avancée et à la régénération.

## Réconciliation à l'ouverture

Chaque enregistrement du registre garde l'empreinte exacte (forme canonique) de l'objet créé.
À l'ouverture de l'éditeur :

1. empreinte identique → objet **lié** ;
2. objet portant un groupe-tag sans empreinte identique → **modifié à la main** (rattaché à
   l'enregistrement le plus proche de la même couche et du même ID) ;
3. enregistrement sans objet → **supprimé à la main** (ou annulé avec l'undo de GD) ; il est retiré ;
4. objet portant un groupe-tag sans enregistrement → **récupéré** dans sa couche.

Le cas 4 rend le système robuste même si le fichier projet est perdu : l'appartenance aux couches
se reconstruit à partir des groupes-tags.

## Choix du fichier projet

Les niveaux locaux n'ont pas d'identifiant stable (`m_levelID` vaut 0 avant upload) et plusieurs
niveaux peuvent porter le même nom (c'est le cas dans ta sauvegarde : deux « Cataclysm 2 »).
Le studio examine tous les fichiers projet de ce nom et choisit celui dont les objets sont présents
dans le niveau (score de recouvrement). Sinon il crée un nouveau fichier (`-2`, `-3`...).

## Copie, partage, ouverture sans le mod

| Situation | Conséquence |
|---|---|
| Ouverture sans le mod | Le niveau s'affiche normalement ; les groupes-tags sont inertes (aucun trigger ne les cible) |
| Upload / partage | Idem ; le fichier projet reste local |
| Copie du niveau dans GD | La copie porte les mêmes objets : elle est reconnue (même score) ; tant que les deux niveaux ne divergent pas, ils partagent un projet. Après divergence, la réconciliation rattache à chacun ses objets ; les métadonnées détaillées (clés de placement) d'un objet peuvent être perdues pour l'un des deux, l'appartenance aux couches reste correcte grâce aux groupes-tags |
| Copier/coller d'objets générés | Les copies portent le groupe-tag : elles sont récupérées dans la couche d'origine à la prochaine réconciliation |
| Le niveau se met à utiliser un groupe-tag | Le studio s'en aperçoit (objet non généré portant ce groupe) et donne un nouveau groupe-tag à la couche |
| Option « groupes-tags » désactivée | Plus aucun groupe ajouté ; la détection des retouches manuelles n'est alors plus possible (seules les empreintes exactes sont reconnues) |

## Historique

Le studio a son propre historique (Undo / Redo / Restaurer la configuration). Les objets sont créés
sans entrée dans l'undo de GD : un Ctrl+Z dans GD n'annule donc pas une génération (mais si
l'utilisateur supprime des objets générés à la main, c'est détecté). Une prévisualisation en cours
est annulée automatiquement avant chaque sauvegarde du niveau, les couches masquées sont
réaffichées pendant la sauvegarde : le fichier du niveau ne contient jamais d'objets de prévisualisation.
