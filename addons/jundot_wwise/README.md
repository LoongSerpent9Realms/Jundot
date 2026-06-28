# Jundot Wwise Addon

This is the project-side helper for Jundot builds that include the Wwise engine module.

After importing:

1. Enable `Jundot Wwise` in Project Settings > Plugins.
2. Put generated Wwise banks in `res://Audio/GeneratedSoundBanks/Windows/`.
3. Call the autoload singleton:

```gdscript
func _ready():
	JundotWwise.initialize()
	JundotWwise.register_game_object(1, "Player")
	JundotWwise.set_default_listener(1)
	JundotWwise.load_bank("res://Audio/GeneratedSoundBanks/Windows/Init.bnk")
	JundotWwise.load_bank("res://Audio/GeneratedSoundBanks/Windows/Main.bnk")
	JundotWwise.post_event("Play_Music", 1)
```

If the editor was not built with the Wwise module, this addon will import successfully but will warn that the `Wwise` singleton is unavailable.
