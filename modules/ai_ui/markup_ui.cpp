/**************************************************************************/
/*  markup_ui.cpp                                                         */
/**************************************************************************/

#include "markup_ui.h"

#include "core/io/file_access.h"
#include "core/io/xml_parser.h"
#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/check_button.h"
#include "scene/gui/color_rect.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/panel.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/progress_bar.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/text_edit.h"
#include "scene/resources/style_box_flat.h"

Vector2 MarkupUI::_parse_vector2(const String &p_value, const Vector2 &p_default) {
	PackedStringArray parts = p_value.replace(",", " ").split(" ", false);
	if (parts.size() == 1) {
		double value = parts[0].to_float();
		return Vector2(value, value);
	}
	if (parts.size() >= 2) {
		return Vector2(parts[0].to_float(), parts[1].to_float());
	}
	return p_default;
}

Color MarkupUI::_parse_color(const String &p_value, const Color &p_default) {
	String value = p_value.strip_edges();
	if (value.is_empty()) {
		return p_default;
	}
	if (Color::html_is_valid(value)) {
		return Color::html(value);
	}
	return Color::from_string(value, p_default);
}

int MarkupUI::_parse_pixels(const String &p_value, int p_default) {
	String value = p_value.strip_edges().replace("px", "");
	if (value.is_valid_int()) {
		return value.to_int();
	}
	if (value.is_valid_float()) {
		return Math::round(value.to_float());
	}
	return p_default;
}

void MarkupUI::_clear_generated_children() {
	while (get_child_count() > 0) {
		Node *child = get_child(0);
		remove_child(child);
		memdelete(child);
	}
}

Control *MarkupUI::_create_control(const String &p_tag) const {
	String tag = p_tag.strip_edges();
	if (tag == "VBox" || tag == "VBoxContainer") {
		return memnew(VBoxContainer);
	}
	if (tag == "HBox" || tag == "HBoxContainer") {
		return memnew(HBoxContainer);
	}
	if (tag == "Grid" || tag == "GridContainer") {
		return memnew(GridContainer);
	}
	if (tag == "Panel") {
		return memnew(Panel);
	}
	if (tag == "PanelContainer") {
		return memnew(PanelContainer);
	}
	if (tag == "Margin" || tag == "MarginContainer") {
		return memnew(MarginContainer);
	}
	if (tag == "Scroll" || tag == "ScrollContainer") {
		return memnew(ScrollContainer);
	}
	if (tag == "Button") {
		return memnew(Button);
	}
	if (tag == "Label") {
		return memnew(Label);
	}
	if (tag == "RichTextLabel") {
		return memnew(RichTextLabel);
	}
	if (tag == "LineEdit" || tag == "Input") {
		return memnew(LineEdit);
	}
	if (tag == "TextEdit" || tag == "Textarea") {
		return memnew(TextEdit);
	}
	if (tag == "CheckBox") {
		return memnew(CheckBox);
	}
	if (tag == "CheckButton") {
		return memnew(CheckButton);
	}
	if (tag == "ColorRect") {
		return memnew(ColorRect);
	}
	if (tag == "ProgressBar") {
		return memnew(ProgressBar);
	}
	return memnew(Control);
}

Dictionary MarkupUI::_read_attributes(Ref<XMLParser> p_parser) const {
	Dictionary attributes;
	for (int i = 0; i < p_parser->get_attribute_count(); i++) {
		attributes[p_parser->get_attribute_name(i)] = p_parser->get_attribute_value(i);
	}
	return attributes;
}

void MarkupUI::_apply_attributes(Control *p_control, const Dictionary &p_attributes) {
	Array keys = p_attributes.keys();
	for (int i = 0; i < keys.size(); i++) {
		String key = keys[i];
		String value = p_attributes[key];

		if (key == "id") {
			p_control->set_meta(SNAME("_markup_id"), value);
			p_control->set_name(value);
		} else if (key == "class") {
			p_control->set_meta(SNAME("_markup_class"), value);
		} else if (key == "name") {
			p_control->set_name(value);
		} else if (key == "text") {
			p_control->set(SNAME("text"), value);
		} else if (key == "tooltip") {
			p_control->set_tooltip_text(value);
		} else if (key == "min_size" || key == "custom_minimum_size") {
			p_control->set_custom_minimum_size(_parse_vector2(value));
		} else if (key == "width") {
			Size2 min_size = p_control->get_custom_minimum_size();
			min_size.x = _parse_pixels(value, min_size.x);
			p_control->set_custom_minimum_size(min_size);
		} else if (key == "height") {
			Size2 min_size = p_control->get_custom_minimum_size();
			min_size.y = _parse_pixels(value, min_size.y);
			p_control->set_custom_minimum_size(min_size);
		} else if (key == "position") {
			p_control->set_position(_parse_vector2(value));
		} else if (key == "size") {
			p_control->set_size(_parse_vector2(value));
		} else if (key == "anchors_preset") {
			String preset = value.to_lower();
			if (preset == "full_rect") {
				p_control->set_anchors_preset(Control::PRESET_FULL_RECT);
			} else if (preset == "center") {
				p_control->set_anchors_preset(Control::PRESET_CENTER);
			} else if (preset == "top_wide") {
				p_control->set_anchors_preset(Control::PRESET_TOP_WIDE);
			}
		} else if (key == "h_size_flags") {
			p_control->set_h_size_flags(value.to_lower() == "expand_fill" ? Control::SIZE_EXPAND_FILL : Control::SIZE_FILL);
		} else if (key == "v_size_flags") {
			p_control->set_v_size_flags(value.to_lower() == "expand_fill" ? Control::SIZE_EXPAND_FILL : Control::SIZE_FILL);
		} else if (key == "columns") {
			p_control->set(SNAME("columns"), value.to_int());
		} else if (key == "value") {
			p_control->set(SNAME("value"), value.to_float());
		}
	}
}

bool MarkupUI::_selector_matches(Control *p_control, const String &p_tag, const String &p_selector) const {
	String selector = p_selector.strip_edges();
	if (selector.is_empty()) {
		return false;
	}
	if (selector.begins_with("#")) {
		return String(p_control->get_meta(SNAME("_markup_id"), String())) == selector.substr(1);
	}
	if (selector.begins_with(".")) {
		PackedStringArray classes = String(p_control->get_meta(SNAME("_markup_class"), String())).split(" ", false);
		String wanted_class = selector.substr(1);
		for (int i = 0; i < classes.size(); i++) {
			if (classes[i] == wanted_class) {
				return true;
			}
		}
		return false;
	}
	return selector == p_tag || selector == p_control->get_class();
}

void MarkupUI::_apply_style_property(Control *p_control, const String &p_name, const String &p_value) {
	String name = p_name.strip_edges().to_lower();
	String value = p_value.strip_edges();

	if (name == "color" || name == "font-color") {
		p_control->add_theme_color_override(SNAME("font_color"), _parse_color(value));
	} else if (name == "background-color" || name == "background") {
		Ref<StyleBoxFlat> style;
		style.instantiate();
		style->set_bg_color(_parse_color(value));
		p_control->add_theme_style_override(SNAME("panel"), style);
		p_control->add_theme_style_override(SNAME("normal"), style);
	} else if (name == "border-color") {
		Ref<StyleBoxFlat> style;
		style.instantiate();
		style->set_bg_color(Color(0, 0, 0, 0));
		style->set_border_color(_parse_color(value));
		style->set_border_width_all(1);
		p_control->add_theme_style_override(SNAME("panel"), style);
		p_control->add_theme_style_override(SNAME("normal"), style);
	} else if (name == "border-width") {
		Ref<StyleBoxFlat> style;
		style.instantiate();
		style->set_bg_color(Color(0, 0, 0, 0));
		style->set_border_width_all(_parse_pixels(value));
		p_control->add_theme_style_override(SNAME("panel"), style);
		p_control->add_theme_style_override(SNAME("normal"), style);
	} else if (name == "border-radius") {
		Ref<StyleBoxFlat> style;
		style.instantiate();
		style->set_bg_color(Color(0, 0, 0, 0));
		style->set_corner_radius_all(_parse_pixels(value));
		p_control->add_theme_style_override(SNAME("panel"), style);
		p_control->add_theme_style_override(SNAME("normal"), style);
	} else if (name == "padding" || name == "margin") {
		int pixels = _parse_pixels(value);
		p_control->add_theme_constant_override(SNAME("margin_left"), pixels);
		p_control->add_theme_constant_override(SNAME("margin_top"), pixels);
		p_control->add_theme_constant_override(SNAME("margin_right"), pixels);
		p_control->add_theme_constant_override(SNAME("margin_bottom"), pixels);
	} else if (name == "gap" || name == "separation") {
		p_control->add_theme_constant_override(SNAME("separation"), _parse_pixels(value));
		p_control->add_theme_constant_override(SNAME("h_separation"), _parse_pixels(value));
		p_control->add_theme_constant_override(SNAME("v_separation"), _parse_pixels(value));
	} else if (name == "min-size") {
		p_control->set_custom_minimum_size(_parse_vector2(value));
	} else if (name == "width") {
		Size2 min_size = p_control->get_custom_minimum_size();
		min_size.x = _parse_pixels(value, min_size.x);
		p_control->set_custom_minimum_size(min_size);
	} else if (name == "height") {
		Size2 min_size = p_control->get_custom_minimum_size();
		min_size.y = _parse_pixels(value, min_size.y);
		p_control->set_custom_minimum_size(min_size);
	} else if (name == "modulate") {
		p_control->set_modulate(_parse_color(value));
	} else if (name == "self-modulate") {
		p_control->set_self_modulate(_parse_color(value));
	}
}

void MarkupUI::_apply_styles(Control *p_control, const String &p_tag) {
	for (int i = 0; i < style_rules.size(); i++) {
		if (!_selector_matches(p_control, p_tag, style_rules[i].selector)) {
			continue;
		}
		Array keys = style_rules[i].properties.keys();
		for (int j = 0; j < keys.size(); j++) {
			_apply_style_property(p_control, keys[j], style_rules[i].properties[keys[j]]);
		}
	}
}

void MarkupUI::_parse_styles(const String &p_styles) {
	style_rules.clear();

	int cursor = 0;
	while (cursor < p_styles.length()) {
		int open = p_styles.find_char('{', cursor);
		if (open < 0) {
			break;
		}
		int close = p_styles.find_char('}', open + 1);
		if (close < 0) {
			break;
		}

		String selector = p_styles.substr(cursor, open - cursor).strip_edges();
		String body = p_styles.substr(open + 1, close - open - 1);
		cursor = close + 1;

		if (selector.is_empty()) {
			continue;
		}

		StyleRule rule;
		rule.selector = selector;
		PackedStringArray declarations = body.split(";", false);
		for (int i = 0; i < declarations.size(); i++) {
			int colon = declarations[i].find_char(':');
			if (colon <= 0) {
				continue;
			}
			String key = declarations[i].substr(0, colon).strip_edges();
			String value = declarations[i].substr(colon + 1).strip_edges();
			if (!key.is_empty() && !value.is_empty()) {
				rule.properties[key] = value;
			}
		}
		style_rules.push_back(rule);
	}
}

Error MarkupUI::_build_from_xml(const String &p_xml) {
	Ref<XMLParser> parser;
	parser.instantiate();

	CharString xml_chars = p_xml.utf8();
	Vector<uint8_t> buffer;
	buffer.resize(xml_chars.length());
	if (xml_chars.length() > 0) {
		memcpy(buffer.ptrw(), xml_chars.ptr(), xml_chars.length());
	}

	Error err = parser->open_buffer(buffer);
	ERR_FAIL_COND_V(err != OK, err);

	Vector<Control *> stack;
	while (parser->read() == OK) {
		if (parser->get_node_type() == XMLParser::NODE_ELEMENT) {
			String tag = parser->get_node_name();
			Control *control = _create_control(tag);
			_apply_attributes(control, _read_attributes(parser));
			_apply_styles(control, tag);

			if (stack.is_empty()) {
				add_child(control);
			} else {
				stack[stack.size() - 1]->add_child(control);
			}
			if (!parser->is_empty()) {
				stack.push_back(control);
			}
		} else if (parser->get_node_type() == XMLParser::NODE_ELEMENT_END) {
			if (!stack.is_empty()) {
				stack.resize(stack.size() - 1);
			}
		} else if (parser->get_node_type() == XMLParser::NODE_TEXT && !stack.is_empty()) {
			String text = parser->get_node_data().strip_edges();
			if (!text.is_empty()) {
				stack[stack.size() - 1]->set(SNAME("text"), text);
			}
		}
	}

	return OK;
}

void MarkupUI::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY && auto_rebuild && get_child_count() == 0) {
		rebuild();
	}
}

void MarkupUI::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_xml_source", "source"), &MarkupUI::set_xml_source);
	ClassDB::bind_method(D_METHOD("get_xml_source"), &MarkupUI::get_xml_source);
	ClassDB::bind_method(D_METHOD("set_scss_source", "source"), &MarkupUI::set_scss_source);
	ClassDB::bind_method(D_METHOD("get_scss_source"), &MarkupUI::get_scss_source);
	ClassDB::bind_method(D_METHOD("set_xml_file", "file"), &MarkupUI::set_xml_file);
	ClassDB::bind_method(D_METHOD("get_xml_file"), &MarkupUI::get_xml_file);
	ClassDB::bind_method(D_METHOD("set_scss_file", "file"), &MarkupUI::set_scss_file);
	ClassDB::bind_method(D_METHOD("get_scss_file"), &MarkupUI::get_scss_file);
	ClassDB::bind_method(D_METHOD("set_auto_rebuild", "enable"), &MarkupUI::set_auto_rebuild);
	ClassDB::bind_method(D_METHOD("is_auto_rebuild_enabled"), &MarkupUI::is_auto_rebuild_enabled);
	ClassDB::bind_method(D_METHOD("rebuild"), &MarkupUI::rebuild);
	ClassDB::bind_method(D_METHOD("load_from_files", "xml_file", "scss_file"), &MarkupUI::load_from_files, DEFVAL(String()));

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "xml_source", PROPERTY_HINT_MULTILINE_TEXT), "set_xml_source", "get_xml_source");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "scss_source", PROPERTY_HINT_MULTILINE_TEXT), "set_scss_source", "get_scss_source");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "xml_file", PROPERTY_HINT_FILE, "*.xml"), "set_xml_file", "get_xml_file");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "scss_file", PROPERTY_HINT_FILE, "*.scss,*.css"), "set_scss_file", "get_scss_file");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_rebuild"), "set_auto_rebuild", "is_auto_rebuild_enabled");
}

void MarkupUI::set_xml_source(const String &p_source) {
	xml_source = p_source;
	if (auto_rebuild && is_inside_tree()) {
		rebuild();
	}
}

String MarkupUI::get_xml_source() const {
	return xml_source;
}

void MarkupUI::set_scss_source(const String &p_source) {
	scss_source = p_source;
	if (auto_rebuild && is_inside_tree()) {
		rebuild();
	}
}

String MarkupUI::get_scss_source() const {
	return scss_source;
}

void MarkupUI::set_xml_file(const String &p_file) {
	xml_file = p_file;
}

String MarkupUI::get_xml_file() const {
	return xml_file;
}

void MarkupUI::set_scss_file(const String &p_file) {
	scss_file = p_file;
}

String MarkupUI::get_scss_file() const {
	return scss_file;
}

void MarkupUI::set_auto_rebuild(bool p_auto_rebuild) {
	auto_rebuild = p_auto_rebuild;
}

bool MarkupUI::is_auto_rebuild_enabled() const {
	return auto_rebuild;
}

Error MarkupUI::rebuild() {
	String xml = xml_source;
	String styles = scss_source;

	if (!xml_file.is_empty()) {
		Error err;
		xml = FileAccess::get_file_as_string(xml_file, &err);
		ERR_FAIL_COND_V(err != OK, err);
	}
	if (!scss_file.is_empty()) {
		Error err;
		styles = FileAccess::get_file_as_string(scss_file, &err);
		ERR_FAIL_COND_V(err != OK, err);
	}

	_clear_generated_children();
	_parse_styles(styles);
	if (xml.strip_edges().is_empty()) {
		return OK;
	}
	return _build_from_xml(xml);
}

Error MarkupUI::load_from_files(const String &p_xml_file, const String &p_scss_file) {
	xml_file = p_xml_file;
	scss_file = p_scss_file;
	return rebuild();
}
