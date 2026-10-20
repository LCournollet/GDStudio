# objectDefinitions.plist summary

Source: `Resources/objectDefinitions.plist` from the installed GD (26,835 bytes, 1,261 lines, file date 2023-12-20). It is an XML plist (Apple PLIST 1.0 DTD). Every leaf value is stored as a string.

## What it is (and is not)

It holds **sprite-part animation definitions for 13 animated characters**. It does **not** contain object IDs, object types, hitboxes or frame names for regular objects. Those are hard-coded in `GeometryDash.exe`: `ObjectToolbox::init`, `GameObject::createWithKey` and `customSetup` (see `object_frames.json` and `object_types.json`).

## Structure

```
<root dict>
  <EntryName> : dict
     animDesc          string  e.g. "GJBeast01_AnimDesc.plist" (part layout/keyframes, separate file in Resources)
     spriteSheet       string  sheet the parts live in (GJ_GameSheetIcons, FireSheet_01, GJ_GameSheet, GJ_ShopSheet01/02/03)
     defaultAnimation  string  name of an entry in `animations`
     useFBF            "0"     (frame-by-frame mode off for every entry)
     usePA             "1"     (part-animation mode on for every entry)
     animations        dict  name -> either
         { delay (s/frame), frames (count), looped "0"/"1", other "0", prio, usesParts "1" }
       or{ singleFrame "<frame>.png", prio, usesParts }
```

## Entries and the level objects they drive

Entries are matched to object IDs by frame-name prefix in the exe's ObjectToolbox table. All 6 level objects below are `AnimatedGameObject` in `GameObject::createWithKey`.

| Entry | Sheet | Default anim | Animations (frames@delay) | Object ID(s) |
|---|---|---|---|---|
| Robot | GJ_GameSheetIcons | run | run/run2/run3 16f@0.0275 loop, idle01 17f, idle02 33f, jump_start 5f, jump_loop 5f, fall_start 7f, fall_loop 5f, idle (single) | none (player robot icon) |
| Spider | GJ_GameSheetIcons | run | run/run2 7f@0.04 loop, walk 7f, idle01 21f, idle02 14f, fall_loop 5f, idle/jump (single) | none (player spider icon) |
| GJBeast01 | FireSheet_01 | bite | bite 16f@0.025 loop, attack01 3f, attack01_loop 7f, attack01_end 6f, idle01 8f | 918 (`GJBeast01_01_001.png`), AnimatedHazard |
| GJBeast02 | FireSheet_01 | idle01 | idle01 9f@0.06, idle02 9f | 1327, AnimatedHazard |
| GJBeast03 | FireSheet_01 | idle01 | idle01 9f@0.06, idle02 9f | 1328, AnimatedHazard |
| GJBeast04 | FireSheet_01 | idle01 | idle01-03 14f@0.06, sleep/sleep_end 9f, sleep_loop 13f, attack01 9f, attack02 3f, attack02_loop 6f, attack02_end 5f | 1584, AnimatedHazard |
| GJBeast05 | FireSheet_01 | idle01 | idle01 13f@0.06, idle02 12f, idle03 9f, attack01 13f, attack02 13f, attack03 4f, to/fromAttack 8-9f | 2012, AnimatedHazard |
| BlackSludge | GJ_GameSheet | loop | loop 11f@0.06 loop | 919 (`dA_blackSludge_01_001.png`), Hazard |
| GJShopKeeper, GJShopKeeper2..5 | GJ_ShopSheet01/01/01/02/03 | idle00 | idle00-02 17f@0.08, idle03 30f@0.10, react01 13f (first keeper only), static (single) | none (shop NPCs in menus) |

Notes:
- Each `animDesc` file exists in `Resources/`, and there is an extra `GJBeast01_AnimDesc2.plist`. These files hold the per-part sprite layout. A renderer that wants the true look of objects 918, 919, 1327, 1328, 1584 and 2012 must compose the parts from them. Otherwise it can fall back to the static frame in `object_frames.json` (e.g. `GJBeast01_01_001.png`).
- The `frames` count is the number of keyframes that `animDesc` defines for that animation, and `delay` is seconds per frame.
