/**************************************************************************/
/*  cpp_script.cpp                                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "cpp_script.h"

#include "core/io/file_access.h"
#include "core/object/class_db.h"

CppScriptLanguage *CppScriptLanguage::singleton = nullptr;

static String _cpp_pascal_case(const String &p_name) {
	const Vector<String> parts = p_name.replace("-", "_").split("_", false);
	String result;
	for (const String &part : parts) {
		if (part.is_empty()) {
			continue;
		}
		result += part.substr(0, 1).to_upper() + part.substr(1);
	}
	if (result.is_empty()) {
		result = "NativeScript";
	}
	if (result[0] >= '0' && result[0] <= '9') {
		result = "Native" + result;
	}
	return result;
}

static String _cpp_extract_after(const String &p_code, const String &p_prefix) {
	int idx = p_code.find(p_prefix);
	if (idx < 0) {
		return String();
	}
	idx += p_prefix.length();
	int end = p_code.find("\n", idx);
	if (end < 0) {
		end = p_code.length();
	}
	return p_code.substr(idx, end - idx).strip_edges();
}

static String _cpp_header_for_base(const String &p_base_class) {
	if (p_base_class == "Node") {
		return "scene/main/node.h";
	}
	if (p_base_class == "Node2D") {
		return "scene/2d/node_2d.h";
	}
	if (p_base_class == "CanvasItem") {
		return "scene/main/canvas_item.h";
	}
	if (p_base_class == "Control") {
		return "scene/gui/control.h";
	}
	if (p_base_class == "Node3D") {
		return "scene/3d/node_3d.h";
	}
	if (p_base_class == "CharacterBody2D") {
		return "scene/2d/physics/character_body_2d.h";
	}
	if (p_base_class == "CharacterBody3D") {
		return "scene/3d/physics/character_body_3d.h";
	}
	if (p_base_class == "RigidBody2D") {
		return "scene/2d/physics/rigid_body_2d.h";
	}
	if (p_base_class == "RigidBody3D") {
		return "scene/3d/physics/rigid_body_3d.h";
	}
	if (p_base_class == "Area2D") {
		return "scene/2d/physics/area_2d.h";
	}
	if (p_base_class == "Area3D") {
		return "scene/3d/physics/area_3d.h";
	}
	return "scene/main/node.h";
}

void CppScript::_parse_metadata() {
	String parsed_class = _cpp_extract_after(source_code, "// @class ");
	if (!parsed_class.is_empty()) {
		class_name = parsed_class;
	} else if (!get_path().is_empty()) {
		class_name = _cpp_pascal_case(get_path().get_file().get_basename());
	}

	String parsed_base = _cpp_extract_after(source_code, "// @extends ");
	if (!parsed_base.is_empty()) {
		base_type = parsed_base;
	}

	tool = source_code.contains("// @tool");
}

void CppScript::_bind_methods() {
}

void CppScript::_placeholder_erased(PlaceHolderScriptInstance *p_placeholder) {
	placeholders.erase(p_placeholder);
}

bool CppScript::can_instantiate() const {
	return false;
}

Ref<Script> CppScript::get_base_script() const {
	return Ref<Script>();
}

StringName CppScript::get_global_name() const {
	return class_name;
}

bool CppScript::inherits_script(const Ref<Script> &p_script) const {
	return p_script.ptr() == this;
}

StringName CppScript::get_instance_base_type() const {
	return base_type;
}

ScriptInstance *CppScript::instance_create(Object *p_this) {
	return nullptr;
}

PlaceHolderScriptInstance *CppScript::placeholder_instance_create(Object *p_this) {
	PlaceHolderScriptInstance *script_instance = memnew(PlaceHolderScriptInstance(CppScriptLanguage::get_singleton(), Ref<Script>(this), p_this));
	placeholders.insert(script_instance);
	return script_instance;
}

bool CppScript::has_source_code() const {
	return true;
}

String CppScript::get_source_code() const {
	return source_code;
}

void CppScript::set_source_code(const String &p_code) {
	source_code = p_code;
	_parse_metadata();
}

Error CppScript::reload(bool p_keep_state) {
	if (!get_path().is_empty() && FileAccess::exists(get_path())) {
		Error err = OK;
		const String code = FileAccess::get_file_as_string(get_path(), &err);
		if (err != OK) {
			return err;
		}
		set_source_code(code);
	}
	update_exports();
	return OK;
}

#ifdef TOOLS_ENABLED
StringName CppScript::get_doc_class_name() const {
	return class_name;
}

Vector<DocData::ClassDoc> CppScript::get_documentation() const {
	return Vector<DocData::ClassDoc>();
}

String CppScript::get_class_icon_path() const {
	return String();
}
#endif

bool CppScript::has_method(const StringName &p_method) const {
	return false;
}

MethodInfo CppScript::get_method_info(const StringName &p_method) const {
	return MethodInfo();
}

bool CppScript::is_tool() const {
	return tool;
}

bool CppScript::is_valid() const {
	return !source_code.is_empty();
}

bool CppScript::is_abstract() const {
	return false;
}

ScriptLanguage *CppScript::get_language() const {
	return CppScriptLanguage::get_singleton();
}

bool CppScript::has_script_signal(const StringName &p_signal) const {
	return false;
}

void CppScript::get_script_signal_list(List<MethodInfo> *r_signals) const {
}

bool CppScript::get_property_default_value(const StringName &p_property, Variant &r_value) const {
	return false;
}

void CppScript::update_exports() {
	List<PropertyInfo> properties;
	HashMap<StringName, Variant> values;
	for (PlaceHolderScriptInstance *placeholder : placeholders) {
		placeholder->update(properties, values);
	}
}

void CppScript::get_script_method_list(List<MethodInfo> *p_list) const {
}

void CppScript::get_script_property_list(List<PropertyInfo> *p_list) const {
}

const Variant CppScript::get_rpc_config() const {
	return Variant();
}

CppScriptLanguage *CppScriptLanguage::get_singleton() {
	return singleton;
}

String CppScriptLanguage::get_name() const {
	return "C++";
}

void CppScriptLanguage::init() {
}

String CppScriptLanguage::get_type() const {
	return "CppScript";
}

String CppScriptLanguage::get_extension() const {
	return "cpp";
}

void CppScriptLanguage::finish() {
}

Vector<String> CppScriptLanguage::get_reserved_words() const {
	static const char *const words[] = {
		"alignas", "alignof", "auto", "bool", "break", "case", "catch", "class", "const", "constexpr", "continue", "decltype", "default", "delete", "do", "double", "else", "enum", "explicit", "extern", "false", "float", "for", "if", "inline", "int", "namespace", "new", "nullptr", "private", "protected", "public", "return", "static", "struct", "switch", "template", "this", "true", "try", "using", "virtual", "void", "while"
	};

	Vector<String> result;
	for (const char *word : words) {
		result.push_back(word);
	}
	return result;
}

bool CppScriptLanguage::is_control_flow_keyword(const String &p_string) const {
	return p_string == "if" || p_string == "else" || p_string == "for" || p_string == "while" || p_string == "switch" || p_string == "case" || p_string == "return" || p_string == "break" || p_string == "continue";
}

Vector<String> CppScriptLanguage::get_comment_delimiters() const {
	Vector<String> delimiters;
	delimiters.push_back("//");
	delimiters.push_back("/* */");
	return delimiters;
}

Vector<String> CppScriptLanguage::get_doc_comment_delimiters() const {
	Vector<String> delimiters;
	delimiters.push_back("///");
	delimiters.push_back("/** */");
	return delimiters;
}

Vector<String> CppScriptLanguage::get_string_delimiters() const {
	Vector<String> delimiters;
	delimiters.push_back("\" \"");
	delimiters.push_back("' '");
	return delimiters;
}

Ref<Script> CppScriptLanguage::make_template(const String &p_template, const String &p_class_name, const String &p_base_class_name) const {
	Ref<CppScript> script;
	script.instantiate();

	const String class_name = _cpp_pascal_case(p_class_name);
	String base_class = p_base_class_name;
	if (base_class.is_empty() || base_class.is_quoted() || !ClassDB::class_exists(base_class)) {
		base_class = "Node";
	}

	String code = p_template;
	if (code.is_empty()) {
		code =
				"// @class %CLASS%\n"
				"// @extends %BASE%\n"
				"// Native C++ scripts are compiled code. Build this class as an engine module or GDExtension before using it at runtime.\n"
				"\n"
				"#include \"%BASE_HEADER%\"\n"
				"\n"
				"#include \"core/object/class_db.h\"\n"
				"\n"
				"class %CLASS% : public %BASE% {\n"
				"\tGDCLASS(%CLASS%, %BASE%);\n"
				"\n"
				"private:\n"
				"\tfloat move_speed = 300.0f;\n"
				"\n"
				"protected:\n"
				"\tstatic void _bind_methods() {\n"
				"\t\tClassDB::bind_method(D_METHOD(\"set_move_speed\", \"speed\"), &%CLASS%::set_move_speed);\n"
				"\t\tClassDB::bind_method(D_METHOD(\"get_move_speed\"), &%CLASS%::get_move_speed);\n"
				"\t\tADD_PROPERTY(PropertyInfo(Variant::FLOAT, \"move_speed\"), \"set_move_speed\", \"get_move_speed\");\n"
				"\t}\n"
				"\n"
				"public:\n"
				"\tvoid set_move_speed(float p_speed) { move_speed = MAX(p_speed, 0.0f); }\n"
				"\tfloat get_move_speed() const { return move_speed; }\n"
				"\n"
				"\tvoid _ready() override {\n"
				"\t\t// Called when the native class enters the scene tree.\n"
				"\t}\n"
				"};\n";
	}

	code = code.replace("%CLASS%", class_name);
	code = code.replace("%BASE%", base_class);
	code = code.replace("%BASE_CLASS%", base_class);
	code = code.replace("%BASE_HEADER%", _cpp_header_for_base(base_class));
	script->set_source_code(code);
	return script;
}

Vector<ScriptLanguage::ScriptTemplate> CppScriptLanguage::get_built_in_templates(const StringName &p_object) {
	Vector<ScriptTemplate> templates;
	ScriptTemplate native_class;
	native_class.inherit = p_object;
	native_class.name = "Native Class";
	native_class.description = "UE-like native C++ class skeleton.";
	native_class.content = "";
	templates.push_back(native_class);
	return templates;
}

bool CppScriptLanguage::is_using_templates() {
	return true;
}

bool CppScriptLanguage::validate(const String &p_script, const String &p_path, List<String> *r_functions, List<ScriptError> *r_errors, List<Warning> *r_warnings, HashSet<int> *r_safe_lines) const {
	return true;
}

String CppScriptLanguage::validate_path(const String &p_path) const {
	return String();
}

bool CppScriptLanguage::supports_builtin_mode() const {
	return false;
}

int CppScriptLanguage::find_function(const String &p_function, const String &p_code) const {
	const int idx = p_code.find(p_function + "(");
	if (idx < 0) {
		return -1;
	}
	return p_code.substr(0, idx).count("\n");
}

String CppScriptLanguage::make_function(const String &p_class, const String &p_name, const PackedStringArray &p_args) const {
	return "void " + p_class + "::" + p_name + "() {\n\t\n}\n";
}

bool CppScriptLanguage::can_make_function() const {
	return false;
}

ScriptLanguage::ScriptNameCasing CppScriptLanguage::preferred_file_name_casing() const {
	return SCRIPT_NAME_CASING_SNAKE_CASE;
}

void CppScriptLanguage::auto_indent_code(String &p_code, int p_from_line, int p_to_line) const {
}

void CppScriptLanguage::add_global_constant(const StringName &p_variable, const Variant &p_value) {
}

String CppScriptLanguage::debug_get_error() const {
	return String();
}

int CppScriptLanguage::debug_get_stack_level_count() const {
	return 0;
}

int CppScriptLanguage::debug_get_stack_level_line(int p_level) const {
	return -1;
}

String CppScriptLanguage::debug_get_stack_level_function(int p_level) const {
	return String();
}

String CppScriptLanguage::debug_get_stack_level_source(int p_level) const {
	return String();
}

void CppScriptLanguage::debug_get_stack_level_locals(int p_level, List<String> *p_locals, List<Variant> *p_values, int p_max_subitems, int p_max_depth) {
}

void CppScriptLanguage::debug_get_stack_level_members(int p_level, List<String> *p_members, List<Variant> *p_values, int p_max_subitems, int p_max_depth) {
}

void CppScriptLanguage::debug_get_globals(List<String> *p_globals, List<Variant> *p_values, int p_max_subitems, int p_max_depth) {
}

String CppScriptLanguage::debug_parse_stack_level_expression(int p_level, const String &p_expression, int p_max_subitems, int p_max_depth) {
	return String();
}

void CppScriptLanguage::reload_all_scripts() {
}

void CppScriptLanguage::reload_scripts(const Array &p_scripts, bool p_soft_reload) {
	for (int i = 0; i < p_scripts.size(); i++) {
		Ref<CppScript> script = p_scripts[i];
		if (script.is_valid()) {
			script->reload(p_soft_reload);
		}
	}
}

void CppScriptLanguage::reload_tool_script(const Ref<Script> &p_script, bool p_soft_reload) {
	if (p_script.is_valid()) {
		p_script->reload(p_soft_reload);
	}
}

void CppScriptLanguage::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("cpp");
}

void CppScriptLanguage::get_public_functions(List<MethodInfo> *p_functions) const {
}

void CppScriptLanguage::get_public_constants(List<Pair<String, Variant>> *p_constants) const {
}

void CppScriptLanguage::get_public_annotations(List<MethodInfo> *p_annotations) const {
}

void CppScriptLanguage::profiling_start() {
}

void CppScriptLanguage::profiling_stop() {
}

void CppScriptLanguage::profiling_set_save_native_calls(bool p_enable) {
}

int CppScriptLanguage::profiling_get_accumulated_data(ProfilingInfo *p_info_arr, int p_info_max) {
	return 0;
}

int CppScriptLanguage::profiling_get_frame_data(ProfilingInfo *p_info_arr, int p_info_max) {
	return 0;
}

bool CppScriptLanguage::handles_global_class_type(const String &p_type) const {
	return p_type == "CppScript";
}

String CppScriptLanguage::get_global_class_name(const String &p_path, String *r_base_type, String *r_icon_path, bool *r_is_abstract, bool *r_is_tool) const {
	Error err = OK;
	const String code = FileAccess::get_file_as_string(p_path, &err);
	if (err != OK) {
		return String();
	}

	String name = _cpp_extract_after(code, "// @class ");
	if (name.is_empty()) {
		name = _cpp_pascal_case(p_path.get_file().get_basename());
	}
	if (r_base_type) {
		const String base = _cpp_extract_after(code, "// @extends ");
		*r_base_type = base.is_empty() ? "Object" : base;
	}
	if (r_icon_path) {
		*r_icon_path = String();
	}
	if (r_is_abstract) {
		*r_is_abstract = false;
	}
	if (r_is_tool) {
		*r_is_tool = code.contains("// @tool");
	}
	return name;
}

CppScriptLanguage::CppScriptLanguage() {
	singleton = this;
}

CppScriptLanguage::~CppScriptLanguage() {
	if (singleton == this) {
		singleton = nullptr;
	}
}
