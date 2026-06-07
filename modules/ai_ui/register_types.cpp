/**************************************************************************/
/*  register_types.cpp                                                    */
/**************************************************************************/

#include "register_types.h"

#include "ai_interface.h"
#include "ai_sandbox.h"
#include "markup_ui.h"

#include "core/object/class_db.h"

void initialize_ai_ui_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(AIInterface);
	GDREGISTER_CLASS(AISandbox);
	GDREGISTER_CLASS(MarkupUI);
}

void uninitialize_ai_ui_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}
