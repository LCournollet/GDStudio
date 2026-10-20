# Persistence, copying and sharing

## What is stored where

| Data | Location |
|---|---|
| Generated decoration | **In the level**, as native GD objects |
| Which layer an object belongs to | In the level: every generated object carries **one extra group**, its layer's *tag group* (a high ID, chosen among those no object or trigger of the level uses) |
| Decoration colors | In the level: color channels allocated by the studio (from 999 downwards, never a channel the level uses) |
| Layers, palette, masks, settings, seeds, object registry, operation log | JSON project file: `<geode save>/toyger.gd-decoration-studio/projects/<name>-<n>.json` |
| User presets | `<geode config>/toyger.gd-decoration-studio/presets/*.json` (export / import = these files) |
| Favorites, collections | the mod's saved data (global) |

The level **never** depends on the project file to display: without the mod it is a normal level.
The project is only used for advanced editing and regeneration.

## Reconciliation when the editor opens

Every registry record keeps the exact fingerprint (canonical form) of the object it created.
When the editor opens:

1. identical fingerprint → object **linked** (a record already marked as edited by hand stays so:
   the protection of hand edits survives any number of reopenings);
2. object carrying a tag group without an identical fingerprint → **edited by hand** (attached to
   the closest record of the same layer and the same ID); the *Keep* / *Ask* policies protect it;
3. record without an object → **deleted by hand** (or undone with GD's undo); the record is removed;
4. object carrying a tag group without a record → **recovered** into its layer. It is a studio
   object, so the next regeneration of its layer replaces it like any other (it is not treated as a
   hand edit, which would leave it under the new decoration).

The fingerprint ignores keys the game rewrites on its own (key `155` in GD 2.2091).

Case 4 keeps the system robust even if the project file is lost: layer membership is rebuilt from
the tag groups.

## Choosing the project file

Local levels have no stable identifier (`m_levelID` is 0 before upload) and several levels can
share the same name (a save file can easily contain two copies of the same level). The studio looks
at every project file with that name and picks the one whose objects are present in the level
(overlap score). Otherwise it creates a new file (`-2`, `-3`...).

## Copying, sharing, opening without the mod

| Situation | Consequence |
|---|---|
| Opened without the mod | The level displays normally; tag groups are inert (no trigger targets them) |
| Upload / sharing | Same; the project file stays local |
| Level copied in GD | The copy carries the same objects: it is recognized (same score); as long as the two levels do not diverge, they share a project. After they diverge, reconciliation attaches each level's objects to it; an object's detailed metadata (placement keys) may be lost for one of the two, but layer membership stays correct thanks to the tag groups |
| Copy/paste of generated objects | Copies carry the tag group: they are recovered into the original layer at the next reconciliation |
| The level starts using a tag group | The studio notices it (a non-generated object carrying that group) and gives the layer a new tag group |
| "Tag groups" option disabled | No group is added anymore; detecting manual edits is then impossible (only exact fingerprints are recognized) |

## History

The studio has its own history (Undo / Redo / Restore configuration). Objects are created without
an entry in GD's undo stack: Ctrl+Z in GD does not undo a generation (but if the user deletes
generated objects by hand, that is detected). A pending preview is cancelled automatically before
every level save, and hidden layers are shown again during the save: the level file never contains
preview objects.
