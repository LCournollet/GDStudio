# Architecture

## Principle

The engine is a **pure** C++ library (`core/`, no Geode dependency) that works on GD's native
object format (`1,<id>,2,<x>,3,<y>,...`). The mod (`mod/`) is only an adapter: it reads the
editor's objects with `GameObject::getSaveString` and creates the generated objects with
`LevelEditorLayer::createObjectsFromString` (exactly what copy/paste does).
Consequences:

- the engine does not depend on the memory layout of objects (which changes between GD versions);
- the whole engine can be tested without the game (tests, CLI, offline renderer);
- the same code runs in the editor, in the tests and in the CLI (`LevelBackend` interface).

```
            ┌──────────────────────────── core (gdds_core) ────────────────────────────┐
 objects ─► │ Analyzer ─► LevelAnalysis ─► CompositionPlan ─► Engines ─► Harmonizer ─►  │
 (strings)  │                 (IR)                       (structural,     Validator     │
            │                                             lighting,          │         │
            │                                             artistic)          ▼         │
            │ Project (layers, masks, palette, registry) ◄── Studio (commit, rollback, │
            │                                                 history, preview)        │
            └──────────────────────────────────────▲───────────────────────────────────┘
                                                   │ LevelBackend
                       ┌───────────────────────────┴──────────────────────┐
                       │ EditorBackend (mod)   StringLevelBackend (tests, CLI)│
                       └──────────────────────────────────────────────────┘
```

## Responsibilities and where they live

| Responsibility | Files |
|---|---|
| Level Analyzer | `core/src/Analysis.cpp` (`Analyzer::analyze`, gameplay timeline) |
| Geometry Analyzer | `Analysis.cpp`: sparse grid (`SparseGrid.hpp`), connected components, contours, corners, depth, distance field, vertical clearance, staircases |
| Gameplay Safety Validator | `Generator.cpp` (`validatePlacements`), `Project.cpp` (`GameplaySnapshot`), `Studio.cpp` (`commit`: before/after comparison + rollback) |
| Object Catalog | `ObjectInfo.hpp` / `ObjectTable.cpp` (the game's 4,093 objects), `DecoCatalog.hpp/.cpp` (decorations: shape, orientation, visible size) |
| Color Palette Manager | `Palette.hpp/.cpp` (roles, adjustments, per-layer overrides, `ChannelAllocator`) |
| Structural Decoration Engine | `StructuralEngine.cpp` (fill, outlines, corners, internal textures, details, hazard accents) + `LightingEngine.cpp` |
| Artistic Decoration Engine | `ArtisticEngine.cpp` (background structures, details, atmosphere, foreground) |
| Layer Manager | `Layer.hpp/.cpp` (`LayerStack`: tree, order, lock, visibility, duplication) |
| Generation Mask Manager | `Mask.hpp/.cpp` (`Mask`, `MaskSet`: combination, edge policies) + `mod/src/MaskTool.cpp` |
| Procedural Composition Engine | `Composition.cpp` (rhythm, density budget, pattern variants) + `harmonize()` |
| Preview Renderer | `mod/src/EditorBackend.cpp` (`showPreview`: real objects + cyan outlines) and `tools/render/render.py` (offline) |
| Object Placement Manager | `Engine.cpp` (`EngineContext::place`: canonical orientation, scale, visual center), `EditorBackend::createObjects` |
| Preset Manager | `Presets.hpp/.cpp` |
| Project Persistence | `Project.hpp/.cpp` (registry, reconciliation, JSON), `mod/src/Session.cpp` (project files) |
| Editor UI | `mod/src/ui/*` (tabbed window, toolbar), `mod/src/main.cpp` (hooks) |

## Generation pipeline

1. **Analysis** – the collision footprints of gameplay objects are rasterized on a 15-unit grid
   (half a block). Adjacent blocks form a single *structure* (4-connected component); its contours
   are traced (outer loops and cavities), split into *segments* with a face (top/bottom/left/right),
   and *corners* are classified as convex/concave. Inner depth (Chebyshev distance to the edge)
   drives insets; a distance field gives the free space; a column sweep finds corridors. Portals
   give the timeline (mode, speed, gravity, size) and every section gets a *tightness* score.
   No-touch, hidden or trigger-moved objects do not form structures.
2. **Classification / planning** – `CompositionPlan`: density and detail budget per section
   (reduced in tight passages), rhythm derived from speed (~0.55 s between two motifs), pattern
   variant per game mode.
3. **Generation** – each layer is handed to its engine. Engines reason in *canonical shapes*
   (horizontal line, corner open to the north-east, glow fading downwards...); each sprite's base
   rotation, measured on the game's sprite sheets, is added at placement time.
4. **Harmonization** – readability (nothing covers a hazard), overlaps, density budget,
   repetitions, empty/overloaded areas, color contrast.
5. **Validation** – allowed IDs only, no unexpected property.
6. **Finalization** (main thread) – color channels, the layer's tag group, HSV variations.
7. **Apply** – removal of the zone's previous objects, creation of the new ones, channel writes,
   before/after gameplay comparison; any difference rolls the operation back.

Steps 1 to 5 are pure: in the editor they run on a cancellable worker thread
(`Studio::prepare/compute/finish`); the editor is only modified on the main thread.

## Determinism

No decision uses a shared sequential random stream: every decision derives its seed from a hash
(layer seed, global seed, position / signature of the element). Result: same level + same
configuration + same seed ⇒ same output, and regenerating a zone shifts nothing elsewhere.
Sequential layouts (pillars, skylines) always start from the level origin, never from the mask
edge.

## Masks and local operations

Mask membership is decided on each placement's **anchor point**, with the **same predicate** for
removal and generation: what is outside the mask is never touched, and what is regenerated inside
is exactly what was removed. Edge policies (per layer): `extend` (a structure touched by the mask
is processed entirely), `clip` (decorations are cut into pieces aligned on the world grid),
`inside-only`. Exclusion masks are always strict.

## Layers and Z

Studio layers are logical (≠ GD's Z layers and editor layers). Each layer's Z is chosen
automatically relative to the gameplay: block design just above the blocks, glow behind the blocks
(it covers neither blocks nor hazards), background on B4/B3, foreground on T3 (only far from the
gameplay). It can be set manually.
