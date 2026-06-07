/**************************************************************************/
/*  ai_sandbox.cpp                                                        */
/**************************************************************************/

#include "ai_sandbox.h"

#include "core/object/class_db.h"

Dictionary AISandbox::_make_result(bool p_ok, const StringName &p_name, const Variant &p_value, const String &p_error, bool p_handled) const {
	Dictionary result;
	result["ok"] = p_ok;
	result["status"] = p_ok ? STATUS_OK : STATUS_CALL_ERROR;
	result["name"] = p_name;
	result["handled"] = p_handled;
	result["value"] = p_value;
	result["error"] = p_error;
	return result;
}

bool AISandbox::_entry_writes(const Entry &p_entry) const {
	return p_entry.options.get("writes", false);
}

Dictionary AISandbox::_call_entry(const StringName &p_name, const Entry &p_entry, const Variant **p_args, int p_argcount) const {
	Variant value;
	Callable::CallError call_error;
	p_entry.callback.callp(p_args, p_argcount, value, call_error);

	if (call_error.error != Callable::CallError::CALL_OK) {
		String error_text = Variant::get_callable_error_text(p_entry.callback, p_args, p_argcount, call_error);
		return _make_result(false, p_name, Variant(), error_text);
	}

	return _make_result(true, p_name, value, String());
}

PackedStringArray AISandbox::_get_entry_names(const HashMap<StringName, Entry> &p_entries) const {
	PackedStringArray names;
	for (const KeyValue<StringName, Entry> &E : p_entries) {
		names.push_back(String(E.key));
	}
	return names;
}

void AISandbox::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &AISandbox::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &AISandbox::is_enabled);
	ClassDB::bind_method(D_METHOD("set_read_only", "read_only"), &AISandbox::set_read_only);
	ClassDB::bind_method(D_METHOD("is_read_only"), &AISandbox::is_read_only);
	ClassDB::bind_method(D_METHOD("set_allow_overrides", "allow"), &AISandbox::set_allow_overrides);
	ClassDB::bind_method(D_METHOD("are_overrides_allowed"), &AISandbox::are_overrides_allowed);
	ClassDB::bind_method(D_METHOD("set_default_timeout", "seconds"), &AISandbox::set_default_timeout);
	ClassDB::bind_method(D_METHOD("get_default_timeout"), &AISandbox::get_default_timeout);

	ClassDB::bind_method(D_METHOD("register_capability", "name", "callback", "options"), &AISandbox::register_capability, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("unregister_capability", "name"), &AISandbox::unregister_capability);
	ClassDB::bind_method(D_METHOD("has_capability", "name"), &AISandbox::has_capability);
	ClassDB::bind_method(D_METHOD("get_capabilities"), &AISandbox::get_capabilities);
	ClassDB::bind_method(D_METHOD("get_capability_options", "name"), &AISandbox::get_capability_options);
	ClassDB::bind_method(D_METHOD("clear_capabilities"), &AISandbox::clear_capabilities);
	ClassDB::bind_method(D_METHOD("execute_capability", "name", "payload"), &AISandbox::execute_capability, DEFVAL(Variant()));

	ClassDB::bind_method(D_METHOD("register_override", "name", "callback", "options"), &AISandbox::register_override, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("unregister_override", "name"), &AISandbox::unregister_override);
	ClassDB::bind_method(D_METHOD("has_override", "name"), &AISandbox::has_override);
	ClassDB::bind_method(D_METHOD("get_overrides"), &AISandbox::get_overrides);
	ClassDB::bind_method(D_METHOD("get_override_options", "name"), &AISandbox::get_override_options);
	ClassDB::bind_method(D_METHOD("clear_overrides"), &AISandbox::clear_overrides);
	ClassDB::bind_method(D_METHOD("apply_override", "name", "original_value", "context"), &AISandbox::apply_override, DEFVAL(Variant()), DEFVAL(Dictionary()));

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "read_only"), "set_read_only", "is_read_only");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "allow_overrides"), "set_allow_overrides", "are_overrides_allowed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "default_timeout", PROPERTY_HINT_RANGE, "0,3600,0.1,suffix:s"), "set_default_timeout", "get_default_timeout");

	ADD_SIGNAL(MethodInfo("capability_registered", PropertyInfo(Variant::STRING_NAME, "name")));
	ADD_SIGNAL(MethodInfo("capability_executed",
			PropertyInfo(Variant::STRING_NAME, "name"),
			PropertyInfo(Variant::BOOL, "ok"),
			PropertyInfo(Variant::DICTIONARY, "result")));
	ADD_SIGNAL(MethodInfo("override_registered", PropertyInfo(Variant::STRING_NAME, "name")));
	ADD_SIGNAL(MethodInfo("override_applied",
			PropertyInfo(Variant::STRING_NAME, "name"),
			PropertyInfo(Variant::BOOL, "ok"),
			PropertyInfo(Variant::DICTIONARY, "result")));

	BIND_ENUM_CONSTANT(STATUS_OK);
	BIND_ENUM_CONSTANT(STATUS_DISABLED);
	BIND_ENUM_CONSTANT(STATUS_NOT_FOUND);
	BIND_ENUM_CONSTANT(STATUS_DENIED);
	BIND_ENUM_CONSTANT(STATUS_CALL_ERROR);
}

void AISandbox::set_enabled(bool p_enabled) {
	enabled = p_enabled;
}

bool AISandbox::is_enabled() const {
	return enabled;
}

void AISandbox::set_read_only(bool p_read_only) {
	read_only = p_read_only;
}

bool AISandbox::is_read_only() const {
	return read_only;
}

void AISandbox::set_allow_overrides(bool p_allow_overrides) {
	allow_overrides = p_allow_overrides;
}

bool AISandbox::are_overrides_allowed() const {
	return allow_overrides;
}

void AISandbox::set_default_timeout(double p_default_timeout) {
	default_timeout = MAX(0.0, p_default_timeout);
}

double AISandbox::get_default_timeout() const {
	return default_timeout;
}

void AISandbox::register_capability(const StringName &p_name, const Callable &p_callback, const Dictionary &p_options) {
	ERR_FAIL_COND_MSG(String(p_name).is_empty(), "AISandbox capability name cannot be empty.");
	ERR_FAIL_COND_MSG(!p_callback.is_valid(), "AISandbox capability callback must be valid.");

	Entry entry;
	entry.callback = p_callback;
	entry.options = p_options;
	entry.options["timeout"] = entry.options.get("timeout", default_timeout);
	capabilities[p_name] = entry;
	emit_signal(SNAME("capability_registered"), p_name);
}

void AISandbox::unregister_capability(const StringName &p_name) {
	capabilities.erase(p_name);
}

bool AISandbox::has_capability(const StringName &p_name) const {
	return capabilities.has(p_name);
}

PackedStringArray AISandbox::get_capabilities() const {
	return _get_entry_names(capabilities);
}

Dictionary AISandbox::get_capability_options(const StringName &p_name) const {
	const Entry *entry = capabilities.getptr(p_name);
	return entry ? entry->options : Dictionary();
}

void AISandbox::clear_capabilities() {
	capabilities.clear();
}

Dictionary AISandbox::execute_capability(const StringName &p_name, const Variant &p_payload) {
	Dictionary result;
	if (!enabled) {
		result = _make_result(false, p_name, Variant(), "AISandbox is disabled.");
		result["status"] = STATUS_DISABLED;
		emit_signal(SNAME("capability_executed"), p_name, false, result);
		return result;
	}

	Entry *entry = capabilities.getptr(p_name);
	if (!entry) {
		result = _make_result(false, p_name, Variant(), "Capability is not registered.");
		result["status"] = STATUS_NOT_FOUND;
		emit_signal(SNAME("capability_executed"), p_name, false, result);
		return result;
	}

	if (read_only && _entry_writes(*entry)) {
		result = _make_result(false, p_name, Variant(), "Capability is denied while AISandbox is read-only.");
		result["status"] = STATUS_DENIED;
		emit_signal(SNAME("capability_executed"), p_name, false, result);
		return result;
	}

	const Variant *arg = &p_payload;
	result = _call_entry(p_name, *entry, &arg, 1);
	emit_signal(SNAME("capability_executed"), p_name, result["ok"], result);
	return result;
}

void AISandbox::register_override(const StringName &p_name, const Callable &p_callback, const Dictionary &p_options) {
	ERR_FAIL_COND_MSG(String(p_name).is_empty(), "AISandbox override name cannot be empty.");
	ERR_FAIL_COND_MSG(!p_callback.is_valid(), "AISandbox override callback must be valid.");

	Entry entry;
	entry.callback = p_callback;
	entry.options = p_options;
	entry.options["timeout"] = entry.options.get("timeout", default_timeout);
	overrides[p_name] = entry;
	emit_signal(SNAME("override_registered"), p_name);
}

void AISandbox::unregister_override(const StringName &p_name) {
	overrides.erase(p_name);
}

bool AISandbox::has_override(const StringName &p_name) const {
	return overrides.has(p_name);
}

PackedStringArray AISandbox::get_overrides() const {
	return _get_entry_names(overrides);
}

Dictionary AISandbox::get_override_options(const StringName &p_name) const {
	const Entry *entry = overrides.getptr(p_name);
	return entry ? entry->options : Dictionary();
}

void AISandbox::clear_overrides() {
	overrides.clear();
}

Dictionary AISandbox::apply_override(const StringName &p_name, const Variant &p_original_value, const Dictionary &p_context) {
	Dictionary result;
	if (!enabled) {
		result = _make_result(false, p_name, p_original_value, "AISandbox is disabled.", false);
		result["status"] = STATUS_DISABLED;
		emit_signal(SNAME("override_applied"), p_name, false, result);
		return result;
	}

	if (!allow_overrides) {
		result = _make_result(false, p_name, p_original_value, "AISandbox overrides are disabled.", false);
		result["status"] = STATUS_DENIED;
		emit_signal(SNAME("override_applied"), p_name, false, result);
		return result;
	}

	Entry *entry = overrides.getptr(p_name);
	if (!entry) {
		result = _make_result(true, p_name, p_original_value, String(), false);
		emit_signal(SNAME("override_applied"), p_name, true, result);
		return result;
	}

	if (read_only && _entry_writes(*entry)) {
		result = _make_result(false, p_name, p_original_value, "Override is denied while AISandbox is read-only.", false);
		result["status"] = STATUS_DENIED;
		emit_signal(SNAME("override_applied"), p_name, false, result);
		return result;
	}

	Variant context = p_context;
	const Variant *args[2] = { &p_original_value, &context };
	result = _call_entry(p_name, *entry, args, 2);
	emit_signal(SNAME("override_applied"), p_name, result["ok"], result);
	return result;
}
