# GD Decoration Studio

Procedural decoration for levels whose **gameplay already exists**.

1. Build your gameplay.
2. Open the studio in the editor (button in the editor menu, or **Alt+D**).
3. Pick a preset or your own objects and colors, configure the layers.
4. Preview, then apply.
5. Fine-tune: regenerate one layer, or only one zone with a mask, change a layer's colors or glow intensity without regenerating anything.

Everything generated is made of **native Geometry Dash objects**: the level displays normally without the mod, and every object stays editable in the normal editor.

## Gameplay safety

- Gameplay objects are never moved, edited or removed. Every operation compares all non-generated objects before and after, and rolls back on any difference.
- Only objects verified as pure decorations (no hitbox, not a trigger, portal, orb or pad) are ever created.
- Readability is protected by conservative rules (hazard margins, corridor tightness, draw order). The player's trajectory is **not** simulated.

## Layers

The studio's layers (Base Block Design, Outlines, Corners, Internal Textures, Details, Glow, Background, Atmosphere, Foreground...) are independent: lock, hide, regenerate or delete one without touching the others. They are separate from GD's Z layers and editor layers.
