# Wwise module

This optional module integrates the Audiokinetic Wwise SDK as an engine singleton named `Wwise`.

Build example:

```powershell
python -m SCons platform=windows target=editor module_wwise_enabled=yes wwise_sdk_path="F:\Wwise_2025.1.8.9170\SDK" wwise_config=Profile
```

The module is disabled by default because the Wwise SDK is not redistributable with the engine source tree.

Downloadable/custom module packaging:

```powershell
python tools/package_wwise_module.py
```

The package is written to `artifacts/wwise-module/jundot-wwise-module.zip`. Extract it into a `custom_modules` directory, then build with:

```powershell
python -m SCons platform=windows target=editor custom_modules="C:\path\to\custom_modules" module_wwise_enabled=yes wwise_sdk_path="F:\Wwise_2025.1.8.9170\SDK" wwise_config=Profile
```

First runtime shape:

```gdscript
func _ready():
	Wwise.initialize()
	Wwise.register_game_object(1, "Player")
	Wwise.set_default_listener(1)
	Wwise.load_bank("res://Audio/GeneratedSoundBanks/Windows/Init.bnk")
	Wwise.load_bank("res://Audio/GeneratedSoundBanks/Windows/Main.bnk")
	Wwise.post_event("Play_Music", 1)

func _process(_delta):
	Wwise.render_audio()
```

Banks are loaded with `LoadBankMemoryView`, so this first integration does not require copying Wwise sample low-level I/O classes into the engine tree.

The first package links the Wwise foundation libraries (`AkSoundEngine`, `AkMemoryMgr`, and `AkStreamMgr`). Add-on libraries such as Spatial Audio can be layered on top once the base bridge is compiling in the target engine.
