@tool
extends EditorPlugin

const AUTOLOAD_NAME := "JundotWwise"
const AUTOLOAD_PATH := "res://addons/jundot_wwise/wwise_runtime.gd"

func _enter_tree() -> void:
	if not Engine.has_singleton("Wwise"):
		push_warning("Jundot Wwise addon imported, but this editor was not built with the Wwise engine module. Use a Wwise-enabled Jundot build.")
	if not ProjectSettings.has_setting("autoload/%s" % AUTOLOAD_NAME):
		add_autoload_singleton(AUTOLOAD_NAME, AUTOLOAD_PATH)

func _exit_tree() -> void:
	if ProjectSettings.has_setting("autoload/%s" % AUTOLOAD_NAME):
		remove_autoload_singleton(AUTOLOAD_NAME)
