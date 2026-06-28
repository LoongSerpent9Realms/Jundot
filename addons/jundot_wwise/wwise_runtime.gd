extends Node

var initialized := false

func is_available() -> bool:
	return Engine.has_singleton("Wwise")

func initialize() -> int:
	if not is_available():
		push_error("Wwise singleton is not available. Use a Wwise-enabled Jundot build.")
		return ERR_UNCONFIGURED

	var err: int = Wwise.initialize()
	initialized = err == OK
	return err

func shutdown() -> void:
	if is_available() and initialized:
		Wwise.shutdown()
	initialized = false

func register_game_object(game_object_id: int, name := "") -> int:
	if not is_available():
		return ERR_UNCONFIGURED
	return Wwise.register_game_object(game_object_id, name)

func set_default_listener(game_object_id: int) -> void:
	if is_available():
		Wwise.set_default_listener(game_object_id)

func load_bank(bank_path: String) -> int:
	if not is_available():
		push_error("Cannot load Wwise bank because the Wwise singleton is not available.")
		return 0
	return Wwise.load_bank(bank_path)

func post_event(event_name: String, game_object_id: int) -> int:
	if not is_available():
		return 0
	return Wwise.post_event(event_name, game_object_id)

func set_rtpc_value(rtpc_name: String, value: float, game_object_id := 0) -> int:
	if not is_available():
		return ERR_UNCONFIGURED
	return Wwise.set_rtpc_value(rtpc_name, value, game_object_id)

func stop_all(game_object_id := 0) -> void:
	if is_available():
		Wwise.stop_all(game_object_id)

func _process(_delta: float) -> void:
	if is_available() and initialized:
		Wwise.render_audio()
