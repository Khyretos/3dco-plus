# 3D Controller Overlay — Instructions

## Table of Contents

1. [What's New in 1.1.1](#whats-new-in-111)
2. [What's New in 1.1.0](#whats-new-in-110)
3. [First Launch](#first-launch)
4. [Opening a Controller Window](#opening-a-controller-window)
5. [Mapping Inputs](#mapping-inputs)
6. [Gyro Support](#gyro-support)
7. [Touchpads](#touchpads)
8. [Highlighting & Press Feedback](#highlighting--press-feedback)
9. [Smooth Travel Animation](#smooth-travel-animation)
10. [Importing a Custom Model](#importing-a-custom-model)
11. [Lighting](#lighting)
12. [Window & Camera Settings](#window--camera-settings)
13. [Network Functionality](#network-functionality)
14. [Shader Effects](#shader-effects)
15. [The Log Window](#the-log-window)
16. [Taskbar/Tray Icon & Debug Mode](#taskbartray-icon--debug-mode)
17. [Data Directory & Backups](#data-directory--backups)
18. [Troubleshooting](#troubleshooting)

---

![Demo](images/demo.webp)

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

![Dual Highlight color picker](images/highlight_dual.webp)

**Dual highlighting** (for axes) – different colours for positive/negative directions, with adjustable deadzone.

---

## Smooth Travel Animation

![Smooth vs instant travel placeholder](images/key_smooth.webp)

By default, a button's Travel (its press offset/rotation, set under **Movement & Animation**) snaps instantly between pressed and released. Smooth Travel Animation eases it instead, so a press reads as a smooth motion rather than a single-frame jump — the GIF above shows the same button with it off vs. on, side by side.

Per mesh, under **Movement & Animation**:

- **Smooth Travel Animation** – on/off.
- **Duration (s)** – roughly how long the press/release takes to settle once enabled. Lower is snappier, higher is softer/slower.
- **Copy to All Buttons** / **Unassign from All Buttons** – apply (or clear) the current enabled state and duration across every other button-type mesh on the controller in one click, instead of setting each one individually.

**Not available on sticks, triggers, or touchpads/touchpoints** – those track a live physical position every frame (how far a trigger is actually pulled, where a finger actually is on a touchpad right now), so easing them would make the rendered part visibly lag behind the real input instead of just looking like a nice animation. The control is hidden for those mesh types for exactly that reason; regular buttons, bumpers, and paddles are unaffected and can use it normally.

---

## Importing a Custom Model

![Import preview GIF](images/import.webp)

You can bring in your own 3D model (common formats like FBX, glTF, OBJ, etc.) instead of using a built‑in one:

1. **Settings → Import Model**, pick your file.
2. A preview window opens listing every mesh found in the file.
3. For each mesh, assign it to a controller part (or leave unassigned to hide it), and optionally set a parent part for correct pivoting (e.g. a touch finger indicator parented to its touchpad).
4. Save — the app converts the imported meshes into a usable model and writes it to your model library.

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

## The Log Window

![Log window](images/logging.webp)

**Settings → Open Log Window** opens the log in its own always-on-top window (previously it lived inside the Settings window and could get sent behind it — that's fixed). The same log is also written to `logs/` in your data directory (rotated at 5 MB, 3 files kept).

Log lines are copyable: click-drag to select text like any other text field, use Ctrl+C, or just click **Copy All** to grab everything currently shown.

---

## Taskbar/Tray Icon & Debug Mode

Next to each other in a controller window's **Window** section:

- **Enable Taskbar Icon** – adds a system tray icon (Windows and Linux) showing the app's own icon. Click it to minimize/restore the main window; right-click for a menu with per-controller minimize/restore, network status, and Quit. Not yet available on macOS.
- **Enable Debug Mode** – turns on more verbose diagnostic logging (e.g. a line per mesh loaded). Off by default, since it adds a small delay when loading models with a lot of parts — turn it on before opening the Log Window if you're reporting a bug.

---

## Data Directory & Backups

Everything you configure – bindings, imported models, tab layouts, controller mapping database – lives in your per‑OS data directory (see README for paths). Back it up to preserve your setup.

---

## Troubleshooting

- **Keyboard/mouse bindings don’t work (macOS):** grant Accessibility permissions (see README).
- **Controller shows up but buttons are unlabeled:** not in SDL’s gamepad database. Use manual mapping with raw Joystick bindings, or add an entry to `gamecontrollerdb.txt` in the data directory.
- **Gyro crashes or misbehaves on Windows:** open an issue with controller model and log contents.
- **First launch is slow:** expected – extracting the embedded model library. Subsequent launches are fast.
- **Grid not visible:** ensure “Show Grid” is checked and camera distance is not too far. The grid is now smaller and positioned below the model.
