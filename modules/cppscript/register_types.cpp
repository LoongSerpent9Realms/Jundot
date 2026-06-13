/**************************************************************************/
/*  register_types.cpp                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "register_types.h"

#include "cpp_script.h"
#include "cpp_script_resource_format.h"

#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/class_db.h"

static CppScriptLanguage *script_language_cpp = nullptr;
static Ref<ResourceFormatLoaderCppScript> resource_loader_cpp;
static Ref<ResourceFormatSaverCppScript> resource_saver_cpp;

void initialize_cppscript_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(CppScript);

	script_language_cpp = memnew(CppScriptLanguage);
	ScriptServer::register_language(script_language_cpp);

	resource_loader_cpp.instantiate();
	ResourceLoader::add_resource_format_loader(resource_loader_cpp);

	resource_saver_cpp.instantiate();
	ResourceSaver::add_resource_format_saver(resource_saver_cpp);
}

void uninitialize_cppscript_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	if (script_language_cpp) {
		ScriptServer::unregister_language(script_language_cpp);
		memdelete(script_language_cpp);
		script_language_cpp = nullptr;
	}

	if (resource_loader_cpp.is_valid()) {
		ResourceLoader::remove_resource_format_loader(resource_loader_cpp);
		resource_loader_cpp.unref();
	}

	if (resource_saver_cpp.is_valid()) {
		ResourceSaver::remove_resource_format_saver(resource_saver_cpp);
		resource_saver_cpp.unref();
	}
}
