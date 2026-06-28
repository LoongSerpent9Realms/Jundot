/**************************************************************************/
/*  cpp_script_resource_format.cpp                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "cpp_script_resource_format.h"

#include "cpp_script.h"

#include "core/io/file_access.h"

Ref<Resource> ResourceFormatLoaderCppScript::load(const String &p_path, const String &p_original_path, Error *r_error, bool p_use_sub_threads, float *r_progress, CacheMode p_cache_mode) {
	Error err = OK;
	const String source = FileAccess::get_file_as_string(p_path, &err);
	if (r_error) {
		*r_error = err;
	}
	if (err != OK) {
		return Ref<Resource>();
	}

	Ref<CppScript> script;
	script.instantiate();
	script->set_source_code(source);
	script->set_path(p_original_path.is_empty() ? p_path : p_original_path);
	return script;
}

void ResourceFormatLoaderCppScript::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("cpp");
}

bool ResourceFormatLoaderCppScript::handles_type(const String &p_type) const {
	return p_type == "Script" || p_type == "CppScript";
}

String ResourceFormatLoaderCppScript::get_resource_type(const String &p_path) const {
	return p_path.get_extension().nocasecmp_to("cpp") == 0 ? "CppScript" : String();
}

Error ResourceFormatSaverCppScript::save(const Ref<Resource> &p_resource, const String &p_path, uint32_t p_flags) {
	Ref<CppScript> script = p_resource;
	ERR_FAIL_COND_V(script.is_null(), ERR_INVALID_PARAMETER);

	Error err = OK;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &err);
	ERR_FAIL_COND_V(err != OK || file.is_null(), err != OK ? err : ERR_CANT_OPEN);
	file->store_string(script->get_source_code());
	return OK;
}

void ResourceFormatSaverCppScript::get_recognized_extensions(const Ref<Resource> &p_resource, List<String> *p_extensions) const {
	if (Object::cast_to<CppScript>(*p_resource)) {
		p_extensions->push_back("cpp");
	}
}

bool ResourceFormatSaverCppScript::recognize(const Ref<Resource> &p_resource) const {
	return Object::cast_to<CppScript>(*p_resource) != nullptr;
}
