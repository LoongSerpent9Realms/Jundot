/**************************************************************************/
/*  markup_ui.h                                                           */
/**************************************************************************/

#pragma once

#include "core/variant/dictionary.h"
#include "scene/gui/control.h"

class XMLParser;

class MarkupUI : public Control {
	GDCLASS(MarkupUI, Control);

	struct StyleRule {
		String selector;
		Dictionary properties;
	};

	String xml_source;
	String scss_source;
	String xml_file;
	String scss_file;
	bool auto_rebuild = true;
	Vector<StyleRule> style_rules;

	void _clear_generated_children();
	void _parse_styles(const String &p_styles);
	Control *_create_control(const String &p_tag) const;
	void _apply_attributes(Control *p_control, const Dictionary &p_attributes);
	void _apply_styles(Control *p_control, const String &p_tag);
	bool _selector_matches(Control *p_control, const String &p_tag, const String &p_selector) const;
	void _apply_style_property(Control *p_control, const String &p_name, const String &p_value);
	Dictionary _read_attributes(Ref<XMLParser> p_parser) const;
	Error _build_from_xml(const String &p_xml);

	static Vector2 _parse_vector2(const String &p_value, const Vector2 &p_default = Vector2());
	static Color _parse_color(const String &p_value, const Color &p_default = Color(1, 1, 1, 1));
	static int _parse_pixels(const String &p_value, int p_default = 0);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_xml_source(const String &p_source);
	String get_xml_source() const;

	void set_scss_source(const String &p_source);
	String get_scss_source() const;

	void set_xml_file(const String &p_file);
	String get_xml_file() const;

	void set_scss_file(const String &p_file);
	String get_scss_file() const;

	void set_auto_rebuild(bool p_auto_rebuild);
	bool is_auto_rebuild_enabled() const;

	Error rebuild();
	Error load_from_files(const String &p_xml_file, const String &p_scss_file = String());
};
