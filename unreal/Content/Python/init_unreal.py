"""Fourfold editor start-up (stream `world_audio`): adds a "Fourfold" entry to the main menu bar - Build all, Rebuild all (force),
Build one part, Open report.  Light and failure-safe: any error is logged as a warning and the editor starts normally."""
import unreal

OWNER = "Fourfold"
SECTION = "FourfoldBuild"
PARTS = ["fx", "character", "animation", "world", "audio"]


def _cmd(code):
    return code


def _entry(name, label, tip, command):
    e = unreal.ToolMenuEntry(name=name, type=unreal.MultiBlockType.MENU_ENTRY)
    e.set_label(label)
    try:
        e.set_tool_tip(tip)
    except Exception:  # noqa: BLE001
        pass
    e.set_string_command(unreal.ToolMenuStringCommandType.PYTHON, "", command)
    return e


def register():
    menus = unreal.ToolMenus.get()
    main = menus.find_menu("LevelEditor.MainMenu")
    if main is None:
        unreal.log_warning("[Fourfold] main menu not found: the Fourfold menu was not added (use Tools > Execute Python Script instead)")
        return False
    menu = main.add_sub_menu(OWNER, "", "FourfoldMenu", "Fourfold", "Fourfold build tools")
    menu.add_section(SECTION, "Build")
    menu.add_menu_entry(SECTION, _entry("FourfoldBuildAll", "Build all", "Imports and builds everything that is missing (fx, character, animation, world, audio).",
                                       "import fourfold_setup; fourfold_setup.main()"))
    menu.add_menu_entry(SECTION, _entry("FourfoldRebuildAll", "Rebuild all (force)", "Re-imports and rebuilds everything, replacing existing assets.",
                                       "import fourfold_setup; fourfold_setup.main(force=True)"))
    part_menu = menu.add_sub_menu(OWNER, SECTION, "FourfoldPartMenu", "Build one part", "Build a single part")
    part_menu.add_section("FourfoldParts", "Parts")
    for p in PARTS:
        part_menu.add_menu_entry("FourfoldParts", _entry(f"FourfoldPart_{p}", p, f"Build only '{p}'.",
                                                         f"import fourfold_setup; fourfold_setup.main(only='{p}')"))
        part_menu.add_menu_entry("FourfoldParts", _entry(f"FourfoldPartForce_{p}", f"{p} (force)", f"Rebuild only '{p}', replacing its assets.",
                                                         f"import fourfold_setup; fourfold_setup.main(force=True, only='{p}')"))
    menu.add_section("FourfoldInfo", "Info")
    menu.add_menu_entry("FourfoldInfo", _entry("FourfoldOpenReport", "Open report", "Opens Saved/Fourfold/setup_report.json.",
                                              "import init_unreal; init_unreal.open_report()"))
    menus.refresh_all_widgets()
    return True


def open_report():
    import fourfold_setup
    path = fourfold_setup.report_path()
    try:
        unreal.SystemLibrary.launch_url("file://" + path)
    except Exception as e:  # noqa: BLE001
        unreal.log_warning(f"[Fourfold] could not open the report ({e}); it is at {path}")
    unreal.log(f"[Fourfold] report: {path}")


try:
    register()
except Exception as e:  # noqa: BLE001
    unreal.log_warning(f"[Fourfold] menu registration failed: {e}")
