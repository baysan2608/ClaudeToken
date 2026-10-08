# Fourfold (Unreal 5.8) — setup on your Mac, step by step

This is everything you do on the Mac (Apple silicon, Unreal Engine 5.8.3, Xcode). The code was written in a Linux
build container without Unreal, so **your Mac is where the Unreal C++ compiles and the editor scripts run for the first
time**. When something fails, section 9 says exactly what to send back — that is how the next round gets fixed fast.

Time needed the first time: ~15 min of clicks + 20–60 min of waiting (C++ build, asset imports, shader compilation,
lighting build).

**Fast path (does sections 2–4 in one go):** `bash unreal/Tools/mac/run.sh` from the repository root (pulls, builds
the editor into `logs/build_editor.log`, opens Unreal and runs the asset setup). `UE_ROOT="/path/to/UE_5.8"` overrides
the engine location. The step-by-step sections below are the same thing by hand, and what to do when a step fails.
The current state of the project, the exact order of steps and what to send back are in [`../HANDOFF.md`](../HANDOFF.md).

---

## 1. Install once
1. **Xcode 26** (26.1.1 recommended) from the App Store. Open it once, let it install its components, then in
   Terminal: `sudo xcode-select -s /Applications/Xcode.app` and `sudo xcodebuild -license accept`.
2. **Xcode ▸ Settings ▸ Accounts**: add your Apple ID (a free account can install on your own iPhone for 7 days; a paid
   Apple Developer account removes that limit and allows TestFlight).
3. **Epic Games Launcher ▸ Unreal Engine ▸ Library ▸ 5.8.3 ▸ Options**: tick **iOS** under *Target Platforms*
   (needed for iPhone / iPad builds). Default install path: `/Users/Shared/Epic Games/UE_5.8/`.
4. Optional (only to run the gameplay tests or regenerate assets): `brew install cmake ninja`, Python 3.11+,
   Blender 4.5 LTS, Godot 4.7.2.

## 2. Get the project
```bash
git clone <this repository> ClaudeToken
cd ClaudeToken/unreal
```
Everything Unreal-related lives in `unreal/` (`Fourfold.uproject`). The old Godot game stays in `game/` as the
reference.

Optional sanity check — the gameplay simulation and its tests build without Unreal:
```bash
cmake -S CoreTests -B ../build/core -G Ninja && cmake --build ../build/core && ../build/core/ff_tests
```

## 3. Build and open the editor
**Easiest:** double-click `unreal/Fourfold.uproject`. Unreal says the Fourfold modules are missing and asks to rebuild
them → **Yes**. The first build takes a few minutes.

If it says *"Fourfold could not be compiled"*, build from Terminal to see the real errors:
```bash
cd ClaudeToken/unreal
"/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh" FourfoldEditor Mac Development \
    -project="$PWD/Fourfold.uproject" -waitmutex 2>&1 | tee ../build_editor.log
```
Send `build_editor.log` back (section 9). Alternatively generate an Xcode workspace and build the *FourfoldEditor*
scheme there:
```bash
"/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/GenerateProjectFiles.sh" -project="$PWD/Fourfold.uproject" -game
open "Fourfold (Mac).xcworkspace"
```

On the first editor start a message about a missing map (`L_Lab`) is expected — step 4 creates it.

## 4. Build the game's assets (one script)
1. In the editor: **Tools ▸ Execute Python Script…** → choose `unreal/Content/Python/fourfold_setup.py`.
   (After the next editor restart the same thing is also in the menu bar: **Fourfold ▸ Build all**.)
2. Watch **Window ▸ Output Log** (Python lines start with `[Fourfold]`). The script imports the fighter, its animations,
   the VFX textures / meshes, the sounds, builds every material, creates the level `/Game/Fourfold/Maps/L_Lab`, builds
   its lighting and saves everything. The bottom-right corner shows shaders compiling — wait until it reaches zero
   (first time: 10–30 min on an M3 / M4).
3. At the end the log prints a summary and writes `unreal/Saved/Fourfold/setup_report.json` (created / skipped /
   failed). Re-running the script is safe: it skips what exists; to rebuild one part run, in the Output Log's Python
   prompt: `import fourfold_setup; fourfold_setup.main(force=True, only="fx")` (parts: `fx character animation world audio`).

## 5. Play on the Mac
1. Content Browser ▸ `Fourfold/Maps/L_Lab` (double-click), then **Play** (or Alt+P).
2. The title screen runs an AI-vs-AI duel behind the menu: **Lab** (every element and sub-element, dummies, dev panel
   on **F2**), **Free Spar** (pick the rival's difficulty and kit), **Practice**, **Watch**, **Settings**.
3. Keyboard: **WASD** move · arrows camera · **J** strike (hold = charge tiers) · **U / N / H** thrust / ground / sweep ·
   **K** guard (**K+J** push, **K+N** sink) · **L** technique (hold, aim, release; **J** while held = shape) · **Space**
   evade (hold = movement mode) · **1–4** element (same key again = next sub-element) · **Q / E** sub-element · **Tab**
   target · **Esc** pause. A gamepad works too (`docs/CONTROLS.md` in the repo root has every binding).
4. Try the touch controls with the mouse: **Settings ▸ Touch controls ▸ Always**.
5. See the iPhone look on the Mac: main toolbar **Settings ▸ Preview Platform ▸ iOS** (mobile renderer preview).
   The Mac itself renders with the desktop renderer (Lumen), which looks richer than the phone.

## 6. Play on an iPhone / iPad
1. Connect the device by cable, unlock it, tap *Trust*. The Developer Mode switch stays hidden until a Mac with Xcode
   has seen the device: open Xcode ▸ **Window ▸ Devices and Simulators** and select it (or start a deploy once).
   Then on the device: **Settings ▸ Privacy & Security ▸ Developer Mode** (bottom of the list) **▸ On**, restart, confirm.
2. Signing (UE 5.8 only has the modern Xcode workflow; the generated Xcode project takes its bundle id and team from
   **Xcode Projects**, not from the iOS page). In the editor: **Edit ▸ Project Settings ▸ Platforms ▸ Xcode Projects**:
   keep **Use Automatic Code Signing** on, set **Apple Dev Account Team ID** (developer.apple.com ▸ Account ▸ Membership
   details, 10 characters) and **Bundle ID** to something unique you own (e.g. `com.<yourname>.fourfold`; `com.example.*`
   cannot be registered). Then in **Platforms ▸ iOS** enter the same **Bundle Identifier** and **IOS Team ID** (only the
   Turnkey / packaging path reads those, but keep them equal). Or edit the four `EDIT-ME` lines in
   `Config/DefaultEngine.ini` (two values) before opening the editor. If you change them after generating the Xcode
   project, regenerate it (step 3).
3. Toolbar **Platforms ▸ iOS ▸ <your device> ▸ Launch** (cooks and installs; the first cook takes a while).
   Or from a terminal: `bash unreal/Tools/mac/ios.sh` (build + cook + install + start; `--fast` reinstalls the last
   cook, `--check` only checks the device). The install step needs the USB cable: over Wi-Fi it fails with
   "No device found with udid".
   Or generate the Xcode workspace (step 3), open `Fourfold (IOS).xcworkspace`, pick the *Fourfold* scheme and your
   device, **Run**.
4. On the device, play the Lab and Free Spar (Hard) for 10 minutes each. Open the console with a **four-finger tap**
   and type `stat unit` and `stat fps` to see the frame times.

## 7. What "good" looks like (quick checklist)
* 60 fps in the Lab with a tornado + fire field + fog at once; no hitch when moves start.
* Fighters plant their feet (no sliding), strikes land exactly when the effect fires, holds tremble at higher tiers.
* Touch: a flick from ATTACK gives the thrust / ground / sweep move; a technique aimed and then dragged into the red
  zone is cancelled, never fired; nothing sticks after you leave the app and come back.
* Sounds play for every move and impact; haptics on hits (if enabled in Settings).

## 8. Optional but very useful: give the build streams the exact Python API
The project enables Python *developer mode*, so after the first editor start Unreal writes
`unreal/Intermediate/PythonStub/unreal.py` (the exact Python API of your engine). Copy it to
`unreal/Tools/py_stub/unreal.py` and commit it — later rounds then check every editor script against your engine.

## 9. When something fails — send this back
| Problem | Send |
|---|---|
| C++ does not compile | `build_editor.log` from step 3 (or the first ~50 error lines from Xcode's Issue navigator) |
| Setup script errors | `unreal/Saved/Fourfold/setup_report.json` and `unreal/Saved/Logs/Fourfold.log` |
| Crash / wrong behaviour in Play | `unreal/Saved/Logs/Fourfold.log`, what you did, a screenshot or screen recording |
| iPhone problems | device model + iOS version, the Xcode console output, `stat unit` numbers |
| iOS signing fails | a screenshot of Xcode ▸ target *Fourfold* ▸ **Signing & Capabilities** and of Project Settings ▸ Platforms ▸ **Xcode Projects** (hide nothing; the Team ID is not secret) |
| Anything visual / feel | a short screen recording and one sentence of what feels wrong |

## 10. Regenerating assets (optional, for later)
Everything binary is generated by scripts and committed, so you never need these tools to play:
* Gameplay data from the Godot reference: `godot --headless --path game -s "$PWD/unreal/Tools/godot_export/export_core_data.gd" -- --out="$PWD/unreal/Source/FourfoldCore/Data"`.
* Fighter and animations: Blender 4.5 LTS scripts in `unreal/Tools/blender/` (see `unreal/docs/character/` and
  `unreal/docs/animation/`).
* Effects textures / flipbooks: `unreal/Tools/vfx/`; environment: `unreal/Tools/world/`; sounds: `unreal/Tools/audio/`.
If you commit the generated `Content/**/*.uasset` files, use Git LFS (`git lfs track "*.uasset" "*.umap"`).
