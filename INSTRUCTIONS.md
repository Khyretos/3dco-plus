# 3D Controller Overlay — Instructions

## Table of Contents

1. [What's New in 1.4.1](#whats-new-in-141)
2. [What's New in 1.4.0](#whats-new-in-140)
3. [What's New in 1.3.3](#whats-new-in-133)
4. [What's New in 1.3.2](#whats-new-in-132)
5. [What's New in 1.3.1](#whats-new-in-131)
6. [What's New in 1.3.0](#whats-new-in-130)
7. [What's New in 1.2.0](#whats-new-in-120)
8. [What's New in 1.1.1](#whats-new-in-111)
9. [What's New in 1.1.0](#whats-new-in-110)
10. [First Launch](#first-launch)
11. [Opening a Controller Window](#opening-a-controller-window)
12. [Mapping Inputs](#mapping-inputs)
13. [Additional Bindings](#additional-bindings)
14. [Gyro Support](#gyro-support)
15. [Touchpads](#touchpads)
16. [Highlighting & Press Feedback](#highlighting--press-feedback)
17. [Smooth Travel Animation](#smooth-travel-animation)
18. [Importing a Custom Model](#importing-a-custom-model)
19. [Textures & UV Mapping](#textures--uv-mapping)
20. [Materials](#materials)
21. [Lighting](#lighting)
22. [Window & Camera Settings](#window--camera-settings)
23. [Network Functionality](#network-functionality)
24. [Shader Effects](#shader-effects)
25. [Input History](#input-history)
26. [The Log Window](#the-log-window)
27. [Taskbar/Tray Icon & Debug Mode](#taskbartray-icon--debug-mode)
28. [Theme](#theme)
29. [Updates & Bundled Models](#updates--bundled-models)
30. [Data Directory & Backups](#data-directory--backups)
31. [Troubleshooting](#troubleshooting)

---

![Demo](images/demo.webp)

## What's New in 1.4.1

- Fixed: keyboard and mouse models showed nothing on a network receiver; only controller and joystick input came through. Keys, mouse movement, mouse buttons and the scroll wheel are now sent too. Update both the sender and the receiver.
- Fixed: the Linux AppImage showed no version in AppImage installers (AppImageLauncher, Gear Lever and similar).

---

## What's New in 1.4.0

- [Updates & Bundled Models](#updates--bundled-models) – a **What's New** window on the first launch after an update (release notes, plus any new bundled models to add in one click), a new **Bundled Models** section in Settings to add missing models or restore one to its original, and an optional check for newer releases.
- The running version now shows in the Settings window title, the tray tooltip, and the Windows `.exe` Properties / macOS Get Info, not only in Help.
- Lower CPU usage: the Frame Cap is respected on high-refresh monitors (it used to follow the monitor's refresh rate), Settings redraws slowly while in the background, and rendering large models is much cheaper.
- Smoother UI text (Noto Sans via FreeType), with dashes, quotes, arrows, check marks and color emoji rendering instead of `?`.

---

## What's New in 1.3.3

- [Additional Bindings](#additional-bindings) – a mesh can now respond to more than one input, each with its own Travel/Travel Rotation and Smooth Travel settings, all added together every frame. Built for joystick hats (one physical mesh, a different tilt per direction), but works for any "press"-style input.
- Two new bundled models, **Flightstick DAT L** and **Flightstick DAT R**, by [DAT](https://www.youtube.com/@gitardat) – their hat already uses Additional Bindings.
- Fixed: the opacity (alpha) of **Highlight Color (Global)** reset to fully opaque on every restart – only its RGB was being saved.
- Fixed: a mesh's highlight value wasn't saved to `info.json` and always reloaded as 0.
- The input picker in Additional Bindings and the **Pick meshes... / Copy Travel to Selected** row no longer overflow the width of the rest of the Movement & Animation panel.

---

## What's New in 1.3.2

- Fixed: a hard crash on Windows when closing a controller window. The underlying cause was Dear ImGui's OpenGL backend using its own, separate function loader instead of the one the rest of the app uses, which could end up bound to the wrong window's context after a close - it's now told to use the app's own loader everywhere, so this no longer happens.
- Fixed: the bundled example models (DAT Keyboard, 60% Keyboard) not showing their textures the first time you open them. Their texture paths now point somewhere that resolves correctly on any machine, instead of wherever they happened to be saved from originally.

---

## What's New in 1.3.1

Quick tour of what's new this release:

- [Textures & UV Mapping](#textures--uv-mapping) – Flip Y now starts off by default for a newly-added texture, not on. A texture you add is now copied into the model's own folder instead of depending on the original file's location forever, so moving or deleting that original doesn't break the model; a texture you remove has its own copy cleaned up too, as long as nothing else in the model still uses it. A model that ships with textures (a bundled example, or one copied between machines) now finds them correctly even if their saved path points somewhere that no longer exists.
- [Materials](#materials) – "Reset All Meshes to Global Textures" now shares a line with "New Global Texture", and there's a matching **Reset All Meshes to Global Material** for the same kind of bulk cleanup on the material side.
- [Smooth Travel Animation](#smooth-travel-animation) – a new **Remove Travel from All Buttons**, the opposite of Copy; and Copy now targets specific meshes through a checkbox picker instead of a name-substring filter, so you can select exactly which ones without relying on a shared naming convention.
- [Highlighting & Press Feedback](#highlighting--press-feedback) – **Add** is now the default Highlight Blend Mode (Replace was the default before), and it can now be set per-mesh under Highlight Override, not just globally for the whole model.
- [Window & Camera Settings](#window--camera-settings) – a clear ✓/✗ status line once Enable Shortcut Monitoring is on, so a shortcut that silently can't work (most commonly a Linux permissions issue) is visible and actionable instead of just never firing with no explanation.
- [Taskbar/Tray Icon & Debug Mode](#taskbartray-icon--debug-mode) – **(Linux only, for now)** Click-Through and Drag to Move can be toggled per controller window, and per its Input History window if one is open, straight from the tray icon's menu — a reachable fallback for enabling either one when the shortcut-based version can't be used because its underlying OS permission was never granted.
- Fixed: a model reloaded from its saved file could silently lose its per-mesh Highlight Override toggle — the color and blend mode underneath it loaded correctly, but the toggle controlling whether they were actually used didn't, and reset to off every time.
- Fixed: an Ignore Button rule set on a glyph style's Combine With target not taking effect while viewing the combining style.
- Noticeably lower baseline CPU usage, especially on models with many meshes or several texture maps per part — shader compilation, uniform lookups, and texture-uniform bookkeeping that used to be rebuilt from scratch on every mesh, every single frame, are now cached instead.

---

## What's New in 1.3.0

Quick tour of what's new this release:

- [Textures & UV Mapping](#textures--uv-mapping) – four new texture types: **Normal Map**, **Metallic Map**, **Roughness Map**, and **AO Map**, alongside the existing Diffuse/Specular/Emissive.
- [Materials](#materials) – set a texture or material once at the model level instead of on every part, with a per-part override for anything that needs to be different.
- [Window & Camera Settings](#window--camera-settings) – a new right-click menu on controller windows (Reset View / Click-Through / Drag to Move), and Click-Through/Drag-to-Move shortcuts can now be bound to any keyboard key instead of a fixed short list.
- A new **Description** field for a model, alongside the existing Source URL — see [Importing a Custom Model](#importing-a-custom-model).
- A confirmation prompt now appears before quitting with unsaved changes, whether from Escape, closing a controller window, or the tray icon's Quit.
- Fixed: switching to a model whose own file doesn't set a Source URL or Description used to leave the previous model's value displayed instead of going blank.
- Fixed crashes when adding/removing textures, and GPU memory leaks around texture handling on window close and model switch.

---

## What's New in 1.2.0

Quick tour of what's new this release:

- [Input History](#input-history) – a separate always-on-top window per controller/keyboard/mouse showing recent presses, fighting-game-style. Raw text, ms/frame-annotated notation, or a fully data-driven set of icon packs you can also build your own versions of (see **Custom Glyph Mappings**) — plus gyro flick detection, per-trigger analog depth, and a persistent log file so you can scroll back through everything you pressed.
- [Textures & UV Mapping](#textures--uv-mapping) – new section explaining how texture mapping actually works, plus: texture assignments now actually save with the model (previously lost on reload), mesh loading warns about missing/degenerate UV data, clearer guidance when a `.blend` import fails, and Flip X/Y controls alongside the existing Offset/Scale/Rotation for source images that read mirrored on a mesh.
- Fixed: camera pan (Pan X/Y) wasn't saved and reset to center on every reload.
- Fixed: capturing an input for a Dual Highlight axis always produced a one-directional binding, so travel/rotation could only ever swing through half its range (e.g. 0–90° instead of -90–90°) regardless of which way you moved the stick. See [Dual Highlighting](#highlighting--press-feedback).
- Real compound-motion detection – quarter-circles, dragon-punch motions, half-circles, and full 360s are now actually recognized as you input them (previously, motion glyphs could only ever be assigned manually, never triggered by play). See Input History's Motion Timeout setting.
- New **Theme** section in Settings (just before Help) for customizing the app's accent colors, with a one-click reset - applies instantly everywhere, including Log/Glyph Mapping Editor/Input History windows, and saves like any other setting.
- Fixed: the main Settings window's scrollbar didn't appear when a section's content was taller than the window - content was there, just unreachable.
- Fixed: D-Pad presses showed up twice in Input History (both a direction digit/glyph and a separate button entry) for the same press.
- The Glyph Mapping Editor is now its own resizable window instead of embedded in Settings, and Input History windows support the same drag-to-move/scroll-to-resize controls as controller windows.

---

## What's New in 1.1.1

Quick tour of what's new this release:

- [Smooth Travel Animation](#smooth-travel-animation) – buttons and paddles can now ease into a press instead of snapping instantly, with a per-mesh toggle and duration, plus one-click Copy/Unassign across every button on the controller.
- Popup (bumper/paddle) offsets and Travel now stack instead of one silently disabling the other — a mesh flagged as a bumper/paddle with Popup enabled now still presses and travels normally.
- Fixed the Touch Area's Yaw/Pitch/Roll sliders only rotating a tiny fraction of the angle shown — they now match the displayed degrees exactly.
- Settings window polish: collapsible section headers ("Position", "Rotation", "Shader Effect", etc.) now have their own darker background tint, so the header reads as a distinct title bar instead of blending into the section above it.

---

## What's New in 1.1.0

Quick tour of what's new this release — see the linked section for each for the full how-to:

- [Network Functionality](#network-functionality) – send a window's live state to another instance of the app over UDP/TCP.
- [Shader Effects](#shader-effects) – rewritten Pixel Art, Toon, Aurora, Infernal, and Rainbow looks, a new Galaxy shader, a fully-replaced Black Hole effect, plus ShaderToy shader import with automatic channel-texture handling and a new **Add Resource** picker.
- [The Log Window](#the-log-window) – now a real always-on-top window, with copyable log lines.
- [Taskbar/Tray Icon & Debug Mode](#taskbartray-icon--debug-mode) – the tray icon now shows the app's own icon, and verbose logging is now opt-in via a new checkbox.
- Per-mesh **Visible** checkboxes (Mesh List) now save and reload with the model instead of resetting every time.
- A new, much smaller **Steam Controller 2026** model, and fixes for transparent-background compositing on AMD/NVIDIA and overlay performance under click-through.

---

## First Launch

On first launch, the app extracts its built‑in model library to your data directory – this takes a few seconds and only happens once.

**macOS users:** grant Accessibility permissions (see README) for keyboard/mouse overlay.

---

## Opening a Controller Window

![Model picker GIF](images/model_picker.webp)

From the Settings window, pick a model from your library and a connected controller. Each model corresponds to a specific controller layout with meshes pre‑assigned.

You can open multiple windows for multiple controllers simultaneously.

---

## Mapping Inputs

![Mapping panel](images/mapping.webp)

Every visible part (button, stick, trigger, touchpad) has an **input binding**. Select a mesh and either:

- **Capture mode:** press the physical button/key/click – the app detects it automatically.
- **Manual selection:** choose from a dropdown of all known inputs.

**Binding types:**

| Type     | Description                                                   |
| -------- | ------------------------------------------------------------- |
| Gamepad  | SDL’s standardised buttons, sticks, triggers, D‑pad.          |
| Joystick | Raw, unmapped button/axis/hat indices.                        |
| Keyboard | Any key, captured globally (even when overlay isn’t focused). |
| Mouse    | Buttons, movement, and scroll.                                |

Use the **Invert** checkbox to flip axis direction.

---

## Additional Bindings

A mesh's input binding (above) and its **Travel**/**Travel Rotation** are its _primary_ binding. For a part that needs to react to more than one input in its own way – the classic case is a joystick hat: one physical mesh, but each direction should tilt it differently – add more under **Movement & Animation → Additional Bindings**:

1. Select the mesh and click **Add Binding**.
2. Pick its input with the same capture/dropdown picker used in the Mesh List.
3. Set that binding's own **Invert**, **Travel X/Y/Z**, **Rot X/Y/Z**, and optionally **Smooth Travel Animation** with its own duration.
4. Repeat for each extra input; each block has its own remove button.

Every active binding's Travel and Travel Rotation (primary included) are **added together** each frame. A diagonal hat press that triggers both "up" and "left" tilts both ways at once; two bindings moving +0.010 and -0.010 on the same axis, both active, cancel out to 0. The mesh highlights if any of its bindings is active.

**Supported inputs:** buttons, hat directions, axis-as-direction, keyboard keys, and mouse buttons. Sticks, raw axis passthrough, and touchpads aren't available here – they drive one continuous position rather than several contributions that can be summed.

Additional bindings save with the model (`extra_bindings` in `info.json`). Models without any behave exactly as before. The bundled **Flightstick DAT L/R** models use this for their hat, if you want a working example.

---

## Gyro Support

![Gyro settings](images/gyro.webp)

If your controller has a gyroscope, enable it per‑window:

- **Sensitivity** – rotation multiplier.
- **Correction** – drift correction strength.
- **Reset combo** – hold two buttons to snap gyro to neutral.
- **Debug logging** – logs raw Euler angles to the log window.

---

## Touchpads

![Touchpad config](images/touchpad.webp)

Controllers with capacitive touchpads (Steam Controller, DualSense/DualShock) support up to 2 fingers per pad (up to 4 pads per window).

- Set **touch width/height** to match the physical area.
- Adjust **offset/rotation** to align the touch indicator.
- Touchpoint meshes are automatically parented to the touchpad.

Touchpoints that go idle for 5 seconds auto‑hide.

---

## Highlighting & Press Feedback

![Highlight color picker](images/highlight.webp)

By default, pressing a button glows the mesh in a global highlight colour.  
**Per‑mesh override:** set a custom colour.

**Highlight Blend Mode** (next to the global highlight colour): **Add** (default) layers the highlight colour on top of the part's own texture, so the underlying texture stays visible and brightens/tints rather than disappearing - closer to how a real backlit key looks; **Replace** fully covers the texture with the highlight colour at full strength. It can also be set per mesh under **Highlight Override**, alongside that mesh's custom colour.

The global highlight colour's opacity (alpha) is saved along with its colour, so a semi-transparent highlight stays that way after a restart.

![Dual Highlight color picker](images/highlight_dual.webp)

**Dual highlighting** (for axes) – different colours for positive/negative directions, with adjustable deadzone.

---

## Smooth Travel Animation

![Smooth vs instant travel placeholder](images/key_smooth.webp)
_(GIF coming soon)_

By default, a button's Travel (its press offset/rotation, set under **Movement & Animation**) snaps instantly between pressed and released. Smooth Travel Animation eases it instead, so a press reads as a smooth motion rather than a single-frame jump — the GIF above shows the same button with it off vs. on, side by side.

Per mesh, under **Movement & Animation**:

- **Smooth Travel Animation** – on/off.
- **Duration (s)** – roughly how long the press/release takes to settle once enabled. Lower is snappier, higher is softer/slower.
- **Copy to All Buttons** / **Unassign from All Buttons** – apply (or clear) the current enabled state and duration across every other button-type mesh on the controller in one click, instead of setting each one individually.
- **Copy Travel to All Buttons** / **Remove Travel from All Buttons** – applies this mesh's own Travel and Travel Rotation values (the actual press-offset numbers, not the Smooth Travel settings above) to every other button-type mesh, or zeroes them back out across all of them.
- **Pick meshes... / Copy Travel to Selected** – the same copy, but only to the meshes you tick in the checkbox picker. Aimed at keyboards and other models with many mechanically-identical parts, without depending on a shared naming convention.

Each extra binding under [Additional Bindings](#additional-bindings) has its own Smooth Travel toggle and duration, independent of the mesh's primary one.

**Not available on sticks, triggers, or touchpads/touchpoints** – those track a live physical position every frame (how far a trigger is actually pulled, where a finger actually is on a touchpad right now), so easing them would make the rendered part visibly lag behind the real input instead of just looking like a nice animation. The control is hidden for those mesh types for exactly that reason; regular buttons, bumpers, and paddles are unaffected and can use it normally.

---

## Importing a Custom Model

![Import preview GIF](images/import.webp)

You can bring in your own 3D model (common formats like FBX, glTF, OBJ, etc.) instead of using a built‑in one:

1. **Settings → Import Model**, pick your file.
2. A preview window opens listing every mesh found in the file.
3. For each mesh, assign it to a controller part (or leave unassigned to hide it), and optionally set a parent part for correct pivoting (e.g. a touch finger indicator parented to its touchpad).
4. Save — the app converts the imported meshes into a usable model and writes it to your model library.

**Source URL and Description:** once a model is loaded, the Model tab has a **Source URL** field (where it came from) and a **Description** field — a multi-line box for crediting the model's creator/contributors, leaving setup notes, or anything else worth keeping with it. Both save and load with the model itself and have no effect on how it looks or behaves.

> **⚠️ Important – your model must be separated into parts.**  
> For the app to properly highlight, animate, and map inputs to individual buttons, triggers, sticks, etc., your 3D model file **must contain each interactive element as a separate mesh**.  
> For example:
>
> - `A_button`, `B_button`, `X_button`, `Y_button` as individual meshes
> - `left_stick`, `right_stick` as separate meshes
> - `left_trigger`, `right_trigger` as separate meshes
> - `dpad_up`, `dpad_down`, `dpad_left`, `dpad_right` as individual pieces
>
> If you export a single unified mesh (e.g., the entire controller as one object), you **will not** be able to assign different inputs or highlight colours to different buttons – the whole model will behave as a single part.  
> **Tip:** Name your meshes clearly (e.g., `touchpad`, `left_bumper`, `start_button`) – the app will show these names in the assignment list, making it easier to map correctly.

---

## Textures & UV Mapping

![Texture mapping placeholder](images/texture_demo.webp)
_(Video coming soon)_

**How texture mapping works:** 3dco+ always uses the mesh's own UV coordinates — the standard `vt` texture-coordinate data from an OBJ file, or the equivalent channel from whatever format you imported (FBX, glTF, etc.). It never uses object-space, triplanar, or normal-based mapping. If a mesh already has a proper UV unwrap from whatever 3D tool you made or exported it in, a texture applied here will follow that unwrap exactly.

Note: the video example shows a per mesh assignement of the texture and this is not necesary, you can assign textures & materials globally and override them per mesh if necesary... im just too lazy to make another video.

**Adding a texture to a mesh:**

![Texture material example](images/texture_material_example.webp)

Credits to [DAT](https://www.youtube.com/@gitardat) for the amazing 3D keyboard model and files to make this example possible!.

1. Select the mesh in the Mesh List.
2. Open its **Materials/Textures** section and click **Add Texture**.
3. Pick an image file (PNG/JPG). It's added with a **Type**, plus **Offset**, **Scale**, and **Rotation** controls for fine-tuning how it sits on the UV unwrap.
4. Texture assignments save with the model (`info.json`) and reload correctly.

**Texture types:**

| Type          | What it does                                                                                                                                                                                  |
| ------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Diffuse       | The base color image.                                                                                                                                                                         |
| Specular      | Controls highlight intensity/color.                                                                                                                                                           |
| Emissive      | Glows regardless of lighting.                                                                                                                                                                 |
| Normal Map    | Adds surface detail (bumps, grain, panel lines) without extra geometry. Use an image where flat areas are blue-purple (roughly RGB 128, 128, 255) — the standard format most 3D tools export. |
| Metallic Map  | Grayscale; brighter = more metallic.                                                                                                                                                          |
| Roughness Map | Grayscale; brighter = softer/rougher reflections, darker = sharper/glossier.                                                                                                                  |
| AO Map        | Grayscale; darker = more occluded (crevices, contact points), brighter = more exposed to ambient light.                                                                                       |

Any of these can also be set once for the whole model instead of per part — see [Materials](#materials).

**If a texture looks wrong (smeared, one flat color, or not lining up):** this is almost always a property of the _mesh's own UV data_, not a setting in this app. The most common cause is a missing or degenerate UV unwrap — some export/optimization workflows drop or never generate one unless you explicitly tell the tool to preserve/create it. 3dco+ detects this automatically: if a loaded mesh has no meaningful UV variation, a warning appears in the [log](#the-log-window) explaining exactly that, rather than leaving you guessing whether it's a texture-mapping bug. Re-export the mesh with a proper UV unwrap (most 3D tools have a "UV unwrap" or "smart UV project" operation) to fix it.

**A note on `.blend` files specifically:** importing a `.blend` file directly is unreliable for many Blender files — this is a long-standing limitation in the underlying Assimp library's own Blender parser (not something specific to your file), and it can fail outright rather than producing a usable model. If a `.blend` import fails, export from Blender as **OBJ, FBX, or glTF** instead (File → Export in Blender) and import that — those formats are handled reliably and are what this app's own built-in models use.

---

## Materials

Set a texture or material property once at the model level instead of assigning it to every part by hand.

**Global Textures:**

1. In the Model tab, find the **Global Textures** section (above the per-part texture list).
2. Add a texture there the same way you would for a single part — pick a file, then set its **Type** (any of the types listed in [Textures & UV Mapping](#textures--uv-mapping) above).
3. Each part has a **Use Custom Textures** checkbox: off (default) means the part uses the Global Textures list above, entirely; on means it uses its own texture list instead, entirely. This is an all-or-nothing switch per part, not a per-type merge — a part with only its own Diffuse texture set doesn't also pick up a global Normal Map alongside it, for example.
4. **Reset All Meshes to Global Textures** button (next to the Global Texture editor) turns Use Custom Textures back off on every mesh in the model at once — useful after adding or changing a global texture on a model where several meshes already had their own, without switching to each one individually. Safe and reversible: it never touches any mesh's own texture list, only the toggle, so switching a mesh back on afterward finds its textures exactly as they were.

**Global Material:**

1. Set the model's **Global Material** (ambient, diffuse, specular, shininess, color) once, in the Model tab.
2. For any part that needs different values, enable **Use Custom Material** on that part, then set its own ambient/diffuse/specular/shininess/color.
3. **Reset All Meshes to Global Material** turns Use Custom Material back off on every mesh at once – the material-side counterpart to Reset All Meshes to Global Textures, and just as reversible (each mesh's own values are kept).

The override is per part — some parts can use their own textures/material while others use the model's global ones — but for any one part, it's all-or-nothing (the whole texture list, or the whole material) rather than mixed field by field.

---

## Lighting

![Lighting panel](images/lighting_settings.webp)

Each window supports **directional**, **point**, and **spot** lights. Adjust ambient/diffuse/specular strength, colour, falloff (point/spot), and hide sources without deleting them.

---

## Window & Camera Settings

![Window settings](images/window_settings.webp)

- **Always on top**, **borderless**, **drag to move**, **scroll to resize** – useful for clean overlays.
- **On Windows especially**: these windows have no title bar to drag by design, so if you need to reposition one, turn on **Drag to Move** first - otherwise dragging on the model does whatever its normal input binding does instead.
- **Camera**: distance, yaw, pitch, roll, and a **freelook** mode (WASD + mouse‑look).
- **Swap interval** (V‑Sync: off/on/adaptive) and background colour/opacity.

**Right-click menu:** right-click anywhere on a controller window for a quick menu — **Reset View**, **Enable/Disable Click-Through**, **Enable/Disable Drag to Move** — without opening Settings. Note that once Click-Through is on, clicks (including right-clicks) pass straight through the window to whatever's behind it, so the menu itself becomes unreachable that way — use a keyboard shortcut (below) to turn Click-Through back off in that case.

**Keyboard shortcuts for Click-Through/Drag to Move:**

1. In Settings, enable **Enable Shortcut Monitoring** (off by default, applies to every window on every platform once on). A status line appears underneath once it's on: green **✓ Working** means the app can actually see keyboard/mouse input globally; red **✗ Not working** means it can't, with a specific reason and fix underneath it — most commonly on Linux, this account not being in the `input` group (fix: `sudo usermod -aG input $USER`, then log out and back in).
2. On any window's Click-Through and/or Drag to Move dropdown, pick any keyboard key — not limited to a fixed list.
3. With monitoring on and the status line showing green, holding that key toggles the setting from anywhere, without needing the window focused or hovered. If more than one window shares the same key, they react together when you press it.

---

## Network Functionality

![Network settings placeholder](images/network.webp)

Send a controller window's live state to another running instance of the app instead of only rendering it locally — useful for a two-PC setup where you want the overlay/capture happening on a machine other than the one generating input.

In a controller window's **Window** section:

1. Set **Mode** to `Sender` on the machine generating input, or `Receiver` on the machine that should display it.
2. Pick a **Protocol** — `UDP` for fast/connectionless (supports broadcast addresses), or `TCP` for a reliable one-to-one connection.
3. Enter a matching **IP Address** and **Port** on both ends (on the Sender, the IP is the Receiver's address).
4. Optionally lower **Send Rate** if you don't need the default rate.
5. Check **Enable Network** on both ends. A green status line confirms the connection.

A window in Receiver mode ignores local controller input entirely — everything it shows comes from the network.

---

## Shader Effects

![Shader effect picker placeholder](images/shader.webp)

Each mesh has a **Shader Effect** dropdown (and there's a global one per window) offering built-in looks: **Pixel Art**, **Cartoon** (cel-shaded/toon), **Aurora**, **Galaxy**, **Infernal**, **Rainbow**, and **Black Hole** (a swirling accretion disk, not a literal simulation) — plus support for pasting in your own ShaderToy-style shader.

**Bringing in your own ShaderToy shader:** most ShaderToy shaders (anything using `mainImage()`) work as-is — the app wraps them with the standard uniforms automatically. If a shader samples a channel texture (`iChannel0`-`iChannel3`):

- Click **Add Resource...** next to the shader dropdown to pick an image and assign it to the next free channel.
- Don't have a specific image in mind? Leave it — a channel with nothing assigned gets a generated noise texture instead of rendering black, which is enough for most shaders that just want "some noise" (very common for procedural effects).

Shader files live in your [data directory](#data-directory--backups), under `shaders/<name>/` — `fragment.glsl` plus any `channel0`–`channel3` image files, if you'd rather manage them by hand.

---

## Input History

![Input History demo placeholder](images/input_history_demo.webp)

A separate always-on-top window per controller/keyboard/mouse window, showing a scrolling list of recent presses — the input-display style fighting games like Street Fighter and Tekken use, equally handy for tutorials. Toggle it per-window from that window's **Input History** section in Settings.

**Display style** (one dropdown, three families):

1. **Raw** – plain text labels.
2. **Fighting-Game Notation** – numpad direction digits (5 = neutral, 1-9 8-way) plus button labels.
3. **Icon packs** – Xbox 360/One/Series, PlayStation 3/4/5, Switch, Steam Deck, Keyboard & Mouse (Dark) or (Light), FGC Motion combined with either PS5 or Xbox — or a mapping you built yourself (see **Custom Glyph Mappings** below). A button a pack has no art for falls back to its text label.

**Capture**, all independent per window:

- **Gamepad/Joystick, Keyboard, Mouse** – which device types this window records, independent of what's bound to the model's meshes.
- **Gyro (Flicks)** – logs a fast, deliberate rotation ("Left Flick", "Up Flick", etc.) rather than every small motion, which would flood the history. Two thresholds control this: **Motion Sensitivity** filters out slow drift/tremor entirely, and **Flick Threshold** is how much rotation has to accumulate (within the **Flick Window**) to count as one flick, with a **Flick Cooldown** so a single continued motion doesn't spam repeated entries. Requires Gyro enabled for this window (see [Gyro Support](#gyro-support)).
- **Merge Simultaneous Presses** – groups inputs landing within **Simultaneous Window (ms)** of each other (default 50ms, adjustable) into one entry (`A+B`). Matters for fighting games; leave off for a clean one-input-per-line list otherwise.
- **Show Return to Neutral** – off by default: letting go of the stick/D-pad doesn't log its own entry, only the direction that actually mattered does. Turn on to log every return to center too.
- **Motion Timeout** – how much time a compound motion (quarter-circle, dragon punch, 360, etc.) has to complete, calibrated to a 2-step quarter-circle (236/214) - longer motions automatically get proportionally more time. Default (220ms) sits close to Street Fighter 6's own quarter-circle window; the setting's own tooltip compares it against Tekken 8 and Guilty Gear Strive too. Motions are actually detected during play now, not just displayable via manual glyph mapping.

**Timing** – an independent **Show Input Timing** toggle (works with any display style, not just Notation) adds a Timing column showing milliseconds or frames (60fps reference) since the previous input. A gap longer than **Reset After** doesn't count as measured timing — it just means you paused — and shows as `--` instead of a misleading number. **Show Date/Time** adds a separate leftmost column with the real wall-clock time each entry was captured.

**Triggers** analog depth is shown as a percentage next to the icon (e.g. `RT 67%`); **Trigger Press Threshold** sets how far a trigger needs to be pulled to register at all.

**Appearance:** independent **Background Opacity** and **Content Opacity** (so a mostly-transparent background can still have fully-opaque icons, or vice versa), **Glyph Size** and **Font Size** (the latter now applies to every column - Time/Input/Timing - not just Raw/Notation text), **Alternating Row Colors** (on by default - turn off alongside 0% Background Opacity for a fully transparent window showing only glyphs/text), **Newest Entry On Top** (or bottom, scrolling like a terminal), **Click-Through**, and **Drag to Move**/**Scroll to Resize** (same controls as a controller window's own, only active while Click-Through is off).

**Log to File** writes every captured input to its own timestamped file under `input_history_logs/` in your [data directory](#data-directory--backups) — independent of the live window's history length, so you can review exactly what you pressed after the fact (checking a speedrun attempt frame by frame, for example).

### Custom Glyph Mappings

Build your own icon set instead of (or alongside) the bundled ones, in its own window — works the same way as mapping a custom controller model:

1. In the Display Style area, click **New Glyph Mapping...** and enter a name, or pick **Edit Existing Mapping** to modify one of the bundled styles (or one you made earlier) — this opens the Glyph Mapping Editor in its own window, separate from Settings.
2. An empty table appears. Click **Add Row** for each input you want to map: choose its type (Gamepad Button, D-Pad Direction, Gamepad Motion, Trigger, Keyboard, or Mouse), the specific input, then **Browse...** (the last column) to assign an image — either an existing glyph from the `glyphs/` folder or your own picture, which gets automatically converted and resized. **Gamepad Motion** is a fixed dropdown of the compound sequences the bundled FGC Motion art has icons for (`236`, `623`, `360`, and similar). These motions are detected during play (see **Motion Timeout** above), so this only chooses which glyph represents each one.
3. Optionally set **Combine With** to another style, so anything you don't define yourself falls back to that style's glyphs instead of a bare text label — a mapping can also exclude specific input types from that fallback (FGC Motion does this for D-Pad glyphs, since it already represents direction its own way). An **Ignore Button** rule (inputs this style should never show or track at all, set elsewhere in the mapping editor) follows this same Combine With chain — a rule set on either half of a combined pair applies while viewing the other.
4. **Save Mapping** — it's written to its own folder under `glyphs/` and immediately shows up as a Display Style choice, for any window. If a standard bundled style's folder ever goes missing (deleted by accident, an interrupted install), it's silently restored from the bundled pack the next time you launch.

And for controllers like the "Steam Controller 2026" you can explicitley add rows to ignore specific buttons. since some buttons are based on touch like button 22 and button 23 which are the surface of the thumbsticks on this controller or the grip sensors which are buttons 24 and 25 that will trigger only when you hold the controller. To tackle this situation you just add these buttons to be ignored so that the input history does not get filled with buttons that are being hold because of the nature of their functionality.

Note that these need to be specified per glyph mapping. So you need to pay attention on which button is being detected and map it according to your needs. In the mapping of your preference.

![Example ignore button](images/ignore_buttons.png)

---

## The Log Window

![Log window](images/logging.webp)

**Settings → Open Log Window** opens the log in its own always-on-top window (previously it lived inside the Settings window and could get sent behind it — that's fixed). The same log is also written to `logs/` in your data directory (rotated at 5 MB, 3 files kept).

Log lines are copyable: click-drag to select text like any other text field, use Ctrl+C, or just click **Copy All** to grab everything currently shown.

---

## Taskbar/Tray Icon & Debug Mode

Next to each other in a controller window's **Window** section:

- **Enable Taskbar Icon** – adds a system tray / menu bar icon (Windows, Linux, and macOS) showing the app's own icon. Click it to minimize/restore the main window; right-click for a menu with per-controller minimize/restore, network status, and Quit. On Windows and Linux each controller's submenu also has **Click-Through** and **Drag to Move** toggles (plus the same two for its Input History window, if open) – handy when a shortcut can't be used. Not yet in the macOS menu.
- **Enable Debug Mode** – turns on more verbose diagnostic logging (e.g. a line per mesh loaded). Off by default, since it adds a small delay when loading models with a lot of parts — turn it on before opening the Log Window if you're reporting a bug.

---

## Theme

![theming](images/theme_demo.webp)

A Settings section of its own (just before Help) for customizing the three accent colors used everywhere in the app - buttons, section headers, sliders, active tabs, and input field backgrounds, across Settings and every window it opens (Log, Glyph Mapping Editor, Input History):

- **Primary** – the main accent color: buttons, section headers, sliders, active tabs.
- **Primary (Light)** – hover/highlighted states.
- **Primary (Dark)** – pressed/active states and input field backgrounds.
- **Reset to Default** – one click back to the shipped purple scheme.

Changes apply immediately, everywhere, not just in Settings - open a controller window's Input History or the Glyph Mapping Editor while adjusting a color and it updates live. Saved the same way as every other setting, this is app-wide rather than per-window (there's only one theme, not one per controller tab).

---

## Updates & Bundled Models

**Which version am I running?** Shown in the Settings window title, the tray icon tooltip, and **Help**. Outside the app: Windows → right-click `3dco+.exe` → **Properties** → **Details**; macOS → **Get Info**.

**What's New:** the first launch after updating shows that version's release notes, once. If the update includes bundled models you don't have yet, they're listed below the notes, ticked by default:

1. Untick any you don't want.
2. **Add Selected Models** adds the ticked ones to your library; **Not Now** skips them (they stay available under Bundled Models).

**Update check:** under **Help**, **Check for updates on startup** (on by default) asks GitHub once per launch whether a newer release exists. If it does:

- A popup shows the new version's release notes.
- **Open Download Page** takes you to the release on GitHub. Nothing is downloaded or installed automatically.
- Tick **Don't remind me about vX.Y.Z again** to stop the popup for that version; a later release is still announced.

**Check Now** next to the toggle runs the check immediately (and shows the result even for a version you chose to skip). If GitHub can't be reached, the startup check simply stays quiet.

**Bundled Models** (Settings section, above Theme) lists every model that comes with the app, marked **Installed** or **Missing**:

- **Add** puts a missing model back; **Add All Missing** does all of them at once.
- **Restore...** resets a model to how it shipped, after a confirmation. Your current version (bindings, travel, textures and all) is moved to `model_backups/<model> <date>` in the data directory, not deleted. Any controller window showing that model reloads it right away.

---

## Data Directory & Backups

Everything you configure – bindings, imported models, tab layouts, controller mapping database – lives in your per‑OS data directory (see README for paths). Back it up to preserve your setup.

`model_backups/` in the same directory holds the previous version of any model you reset with **Restore...** under [Bundled Models](#updates--bundled-models). To go back to it, copy its folder into `models/` (renaming it to the model's name).

---

## Troubleshooting

- **Keyboard/mouse bindings don’t work (macOS):** grant Accessibility permissions (see README).
- **Controller shows up but buttons are unlabeled:** not in SDL’s gamepad database. Use manual mapping with raw Joystick bindings, or add an entry to `gamecontrollerdb.txt` in the data directory.
- **Gyro crashes or misbehaves on Windows:** open an issue with controller model and log contents.
- **First launch is slow:** expected – extracting the embedded model library. Subsequent launches are fast.
- **Grid not visible:** ensure “Show Grid” is checked and camera distance is not too far. The grid is now smaller and positioned below the model.
- **A "quit anyway / cancel" prompt appeared:** expected behavior, not a bug — you have unsaved changes, and this appears before Escape, closing a controller window, or the tray icon's Quit actually exits, so an accidental close doesn't silently discard them. Save first, or pick Quit Anyway to discard the changes on purpose.
