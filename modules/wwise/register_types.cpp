/**************************************************************************/
/*  register_types.cpp                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "register_types.h"

#include "wwise.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

static Wwise *wwise_singleton = nullptr;

void initialize_wwise_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SERVERS) {
		return;
	}

	GDREGISTER_CLASS(Wwise);

	wwise_singleton = memnew(Wwise);
	Engine::get_singleton()->add_singleton(Engine::Singleton("Wwise", Wwise::get_singleton()));
}

void uninitialize_wwise_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SERVERS) {
		return;
	}

	if (wwise_singleton) {
		wwise_singleton->shutdown();
		memdelete(wwise_singleton);
		wwise_singleton = nullptr;
	}
}
