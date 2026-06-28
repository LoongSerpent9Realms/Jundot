/**************************************************************************/
/*  wwise.h                                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/object/object.h"
#include "core/templates/hash_map.h"
#include "core/variant/typed_array.h"

#include <AK/SoundEngine/Common/AkTypes.h>

class Wwise : public Object {
	GDCLASS(Wwise, Object);

	static Wwise *singleton;

	bool initialized = false;
	HashMap<AkBankID, PackedByteArray> loaded_bank_data;
	HashMap<String, AkBankID> loaded_banks_by_path;

	static void _bind_methods();

public:
	static Wwise *get_singleton();

	Wwise();
	~Wwise();

	Error initialize();
	void shutdown();
	bool is_initialized() const;

	Error register_game_object(int64_t p_game_object_id, const String &p_name = String());
	void unregister_game_object(int64_t p_game_object_id);
	void set_default_listener(int64_t p_game_object_id);

	int64_t load_bank(const String &p_bank_path);
	Error unload_bank(int64_t p_bank_id);
	Error unload_bank_path(const String &p_bank_path);

	int64_t post_event(const String &p_event_name, int64_t p_game_object_id);
	void stop_all(int64_t p_game_object_id = 0);
	Error set_rtpc_value(const String &p_rtpc_name, double p_value, int64_t p_game_object_id = 0);
	void render_audio();
};
