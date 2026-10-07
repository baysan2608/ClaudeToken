# Local environment (owner's Mac)

Verified 2026-10-07. Use these paths in scripts and docs.

| Tool | Path | Version |
|---|---|---|
| Mac | Apple M4 Pro, 24 GB | macOS 27.0.1 |
| Unreal Engine | `/Users/Shared/Epic Games/UE_5.8` | 5.8.3 (CL 58210709); iOS + VisionOS platforms installed |
| Editor | `/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app` | |
| Build script | `/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh` | |
| Xcode | `~/Downloads/Xcode-beta.app` (selected by `xcode-select`) | Xcode 27.0 beta (27A5218g), Mac SDK 27.0, Apple clang 21.0.0. UE 5.8 `Apple_SDK.json` accepts Xcode 15.2 – 27.9, so this is within range. |
| Blender | `/Applications/Blender.app` | 5.0.1 (scripts target 4.5 LTS) |
| Godot | `/Applications/Godot.app` | 4.7.2 |
| Python | `/usr/bin/python3` | 3.9.6 |
| Not installed | Homebrew, cmake, ninja, git-lfs | `CoreTests/run_all.sh` must work without cmake |
