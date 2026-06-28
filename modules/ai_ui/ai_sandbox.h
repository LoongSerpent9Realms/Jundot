/**************************************************************************/
/*  ai_sandbox.h                                                          */
/**************************************************************************/

#pragma once

#include "core/templates/hash_map.h"
#include "core/variant/callable.h"
#include "core/variant/dictionary.h"
#include "scene/main/node.h"

class AISandbox : public Node {
	GDCLASS(AISandbox, Node);

	struct Entry {
		Callable callback;
		Dictionary options;
	};

	bool enabled = true;
	bool read_only = false;
	bool allow_overrides = true;
	double default_timeout = 30.0;

	HashMap<StringName, Entry> capabilities;
	HashMap<StringName, Entry> overrides;

	Dictionary _make_result(bool p_ok, const StringName &p_name, const Variant &p_value, const String &p_error, bool p_handled = true) const;
	bool _entry_writes(const Entry &p_entry) const;
	Dictionary _call_entry(const StringName &p_name, const Entry &p_entry, const Variant **p_args, int p_argcount) const;
	PackedStringArray _get_entry_names(const HashMap<StringName, Entry> &p_entries) const;

protected:
	static void _bind_methods();

public:
	enum SandboxStatus {
		STATUS_OK = 0,
		STATUS_DISABLED,
		STATUS_NOT_FOUND,
		STATUS_DENIED,
		STATUS_CALL_ERROR,
	};

	void set_enabled(bool p_enabled);
	bool is_enabled() const;

	void set_read_only(bool p_read_only);
	bool is_read_only() const;

	void set_allow_overrides(bool p_allow_overrides);
	bool are_overrides_allowed() const;

	void set_default_timeout(double p_default_timeout);
	double get_default_timeout() const;

	void register_capability(const StringName &p_name, const Callable &p_callback, const Dictionary &p_options = Dictionary());
	void unregister_capability(const StringName &p_name);
	bool has_capability(const StringName &p_name) const;
	PackedStringArray get_capabilities() const;
	Dictionary get_capability_options(const StringName &p_name) const;
	void clear_capabilities();
	Dictionary execute_capability(const StringName &p_name, const Variant &p_payload = Variant());

	void register_override(const StringName &p_name, const Callable &p_callback, const Dictionary &p_options = Dictionary());
	void unregister_override(const StringName &p_name);
	bool has_override(const StringName &p_name) const;
	PackedStringArray get_overrides() const;
	Dictionary get_override_options(const StringName &p_name) const;
	void clear_overrides();
	Dictionary apply_override(const StringName &p_name, const Variant &p_original_value = Variant(), const Dictionary &p_context = Dictionary());
};

VARIANT_ENUM_CAST(AISandbox::SandboxStatus);
