/**************************************************************************/
/*  wwise.cpp                                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "wwise.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/string/ustring.h"

#include <AK/SoundEngine/Common/AkMemoryMgr.h>
#include <AK/SoundEngine/Common/AkMemoryMgrModule.h>
#include <AK/SoundEngine/Common/AkSoundEngine.h>
#include <AK/SoundEngine/Common/AkStreamMgrModule.h>

Wwise *Wwise::singleton = nullptr;

Wwise *Wwise::get_singleton() {
	return singleton;
}

Wwise::Wwise() {
	singleton = this;
}

Wwise::~Wwise() {
	shutdown();
	if (singleton == this) {
		singleton = nullptr;
	}
}

void Wwise::_bind_methods() {
	ClassDB::bind_method(D_METHOD("initialize"), &Wwise::initialize);
	ClassDB::bind_method(D_METHOD("shutdown"), &Wwise::shutdown);
	ClassDB::bind_method(D_METHOD("is_initialized"), &Wwise::is_initialized);

	ClassDB::bind_method(D_METHOD("register_game_object", "game_object_id", "name"), &Wwise::register_game_object, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("unregister_game_object", "game_object_id"), &Wwise::unregister_game_object);
	ClassDB::bind_method(D_METHOD("set_default_listener", "game_object_id"), &Wwise::set_default_listener);

	ClassDB::bind_method(D_METHOD("load_bank", "bank_path"), &Wwise::load_bank);
	ClassDB::bind_method(D_METHOD("unload_bank", "bank_id"), &Wwise::unload_bank);
	ClassDB::bind_method(D_METHOD("unload_bank_path", "bank_path"), &Wwise::unload_bank_path);

	ClassDB::bind_method(D_METHOD("post_event", "event_name", "game_object_id"), &Wwise::post_event);
	ClassDB::bind_method(D_METHOD("stop_all", "game_object_id"), &Wwise::stop_all, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("set_rtpc_value", "rtpc_name", "value", "game_object_id"), &Wwise::set_rtpc_value, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("render_audio"), &Wwise::render_audio);
}

Error Wwise::initialize() {
	if (initialized) {
		return OK;
	}

	AkMemSettings mem_settings;
	AK::MemoryMgr::GetDefaultSettings(mem_settings);
	if (AK::MemoryMgr::Init(&mem_settings) != AK_Success) {
		ERR_FAIL_V_MSG(ERR_CANT_CREATE, "Failed to initialize Wwise memory manager.");
	}

	AkStreamMgrSettings stream_settings;
	AK::StreamMgr::GetDefaultSettings(stream_settings);
	if (!AK::StreamMgr::Create(stream_settings)) {
		AK::MemoryMgr::Term();
		ERR_FAIL_V_MSG(ERR_CANT_CREATE, "Failed to create Wwise stream manager.");
	}

	AkInitSettings init_settings;
	AkPlatformInitSettings platform_init_settings;
	AK::SoundEngine::GetDefaultInitSettings(init_settings);
	AK::SoundEngine::GetDefaultPlatformInitSettings(platform_init_settings);
	if (AK::SoundEngine::Init(&init_settings, &platform_init_settings) != AK_Success) {
		AK::IAkStreamMgr::Get()->Destroy();
		AK::MemoryMgr::Term();
		ERR_FAIL_V_MSG(ERR_CANT_CREATE, "Failed to initialize Wwise sound engine.");
	}

	initialized = true;
	return OK;
}

void Wwise::shutdown() {
	if (!initialized) {
		return;
	}

	for (const KeyValue<AkBankID, PackedByteArray> &bank : loaded_bank_data) {
		AK::SoundEngine::UnloadBank(bank.key, nullptr);
	}
	loaded_bank_data.clear();
	loaded_banks_by_path.clear();

	AK::SoundEngine::Term();
	if (AK::IAkStreamMgr::Get()) {
		AK::IAkStreamMgr::Get()->Destroy();
	}
	AK::MemoryMgr::Term();
	initialized = false;
}

bool Wwise::is_initialized() const {
	return initialized;
}

Error Wwise::register_game_object(int64_t p_game_object_id, const String &p_name) {
	ERR_FAIL_COND_V_MSG(!initialized, ERR_UNCONFIGURED, "Wwise must be initialized before registering game objects.");

	CharString name_utf8 = p_name.utf8();
	AKRESULT result = p_name.is_empty() ? AK::SoundEngine::RegisterGameObj((AkGameObjectID)p_game_object_id) : AK::SoundEngine::RegisterGameObj((AkGameObjectID)p_game_object_id, name_utf8.get_data());
	ERR_FAIL_COND_V_MSG(result != AK_Success, FAILED, "Failed to register Wwise game object.");
	return OK;
}

void Wwise::unregister_game_object(int64_t p_game_object_id) {
	if (!initialized) {
		return;
	}
	AK::SoundEngine::UnregisterGameObj((AkGameObjectID)p_game_object_id);
}

void Wwise::set_default_listener(int64_t p_game_object_id) {
	if (!initialized) {
		return;
	}
	AkGameObjectID listener = (AkGameObjectID)p_game_object_id;
	AK::SoundEngine::SetDefaultListeners(&listener, 1);
}

int64_t Wwise::load_bank(const String &p_bank_path) {
	ERR_FAIL_COND_V_MSG(!initialized, 0, "Wwise must be initialized before loading banks.");

	String normalized_path = ProjectSettings::get_singleton()->globalize_path(p_bank_path);
	if (loaded_banks_by_path.has(normalized_path)) {
		return loaded_banks_by_path[normalized_path];
	}

	Error err;
	Vector<uint8_t> file_data = FileAccess::get_file_as_bytes(normalized_path, &err);
	ERR_FAIL_COND_V_MSG(err != OK, 0, vformat("Failed to read Wwise bank: %s", p_bank_path));
	ERR_FAIL_COND_V_MSG(file_data.is_empty(), 0, vformat("Wwise bank is empty: %s", p_bank_path));

	PackedByteArray bank_data;
	bank_data.resize(file_data.size());
	memcpy(bank_data.ptrw(), file_data.ptr(), file_data.size());

	AkBankID bank_id = AK_INVALID_BANK_ID;
	AKRESULT result = AK::SoundEngine::LoadBankMemoryView(bank_data.ptr(), bank_data.size(), bank_id);
	ERR_FAIL_COND_V_MSG(result != AK_Success, 0, vformat("Failed to load Wwise bank: %s", p_bank_path));

	loaded_bank_data.insert(bank_id, bank_data);
	loaded_banks_by_path.insert(normalized_path, bank_id);
	return (int64_t)bank_id;
}

Error Wwise::unload_bank(int64_t p_bank_id) {
	ERR_FAIL_COND_V_MSG(!initialized, ERR_UNCONFIGURED, "Wwise must be initialized before unloading banks.");

	AkBankID bank_id = (AkBankID)p_bank_id;
	ERR_FAIL_COND_V_MSG(!loaded_bank_data.has(bank_id), ERR_DOES_NOT_EXIST, "Wwise bank id is not loaded by this singleton.");

	AKRESULT result = AK::SoundEngine::UnloadBank(bank_id, nullptr);
	ERR_FAIL_COND_V_MSG(result != AK_Success, FAILED, "Failed to unload Wwise bank.");

	loaded_bank_data.erase(bank_id);
	String path_to_erase;
	for (const KeyValue<String, AkBankID> &bank_path : loaded_banks_by_path) {
		if (bank_path.value == bank_id) {
			path_to_erase = bank_path.key;
			break;
		}
	}
	if (!path_to_erase.is_empty()) {
		loaded_banks_by_path.erase(path_to_erase);
	}
	return OK;
}

Error Wwise::unload_bank_path(const String &p_bank_path) {
	String normalized_path = ProjectSettings::get_singleton()->globalize_path(p_bank_path);
	ERR_FAIL_COND_V_MSG(!loaded_banks_by_path.has(normalized_path), ERR_DOES_NOT_EXIST, "Wwise bank path is not loaded by this singleton.");
	return unload_bank(loaded_banks_by_path[normalized_path]);
}

int64_t Wwise::post_event(const String &p_event_name, int64_t p_game_object_id) {
	ERR_FAIL_COND_V_MSG(!initialized, 0, "Wwise must be initialized before posting events.");

	CharString event_name_utf8 = p_event_name.utf8();
	AkPlayingID playing_id = AK::SoundEngine::PostEvent(event_name_utf8.get_data(), (AkGameObjectID)p_game_object_id);
	ERR_FAIL_COND_V_MSG(playing_id == AK_INVALID_PLAYING_ID, 0, vformat("Failed to post Wwise event: %s", p_event_name));
	return (int64_t)playing_id;
}

void Wwise::stop_all(int64_t p_game_object_id) {
	if (!initialized) {
		return;
	}
	AK::SoundEngine::StopAll((AkGameObjectID)p_game_object_id);
}

Error Wwise::set_rtpc_value(const String &p_rtpc_name, double p_value, int64_t p_game_object_id) {
	ERR_FAIL_COND_V_MSG(!initialized, ERR_UNCONFIGURED, "Wwise must be initialized before setting RTPC values.");

	CharString rtpc_name_utf8 = p_rtpc_name.utf8();
	AKRESULT result = AK::SoundEngine::SetRTPCValue(rtpc_name_utf8.get_data(), (AkRtpcValue)p_value, (AkGameObjectID)p_game_object_id);
	ERR_FAIL_COND_V_MSG(result != AK_Success, FAILED, vformat("Failed to set Wwise RTPC value: %s", p_rtpc_name));
	return OK;
}

void Wwise::render_audio() {
	if (!initialized) {
		return;
	}
	AK::SoundEngine::RenderAudio();
}
