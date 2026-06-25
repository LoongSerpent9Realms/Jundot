/*  ai_chat_service.cpp                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/
/* Copyright (c) 2024-present JunDot contributors.                        */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/**************************************************************************/

#include "ai_chat_service.h"

#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "editor/ai/ai_http_response_text.h"
#include "editor/ai/ai_jundot_plugin_backend.h"
#include "editor/ai/ai_settings.h"
#include "editor/settings/editor_settings.h"
#include "scene/main/http_request.h"

static String _get_json_error(const String &p_text) {
	// Try to extract a human-readable error from a non-JSON response.
	if (p_text.is_empty()) {
		return "Empty response from server.";
	}
	// If it looks like HTML, extract a summary.
	if (p_text.begins_with("<")) {
		int title_start = p_text.find("<title>");
		if (title_start >= 0) {
			int title_end = p_text.find("</title>", title_start);
			if (title_end > title_start) {
				return "Server returned HTML: " + p_text.substr(title_start + 7, title_end - title_start - 7);
			}
		}
		return "Server returned an HTML page instead of JSON (response length: " + itos(p_text.length()) + " chars).";
	}
	// Truncate very long responses.
	String summary = p_text.substr(0, 300);
	if (p_text.length() > 300) {
		summary += "...";
	}
	return "Failed to parse JSON response. Body preview: " + summary;
}

static String _string_from_content_variant(const Variant &p_content) {
	switch (p_content.get_type()) {
		case Variant::STRING:
			return p_content;
		case Variant::ARRAY: {
			String text;
			Array content_parts = p_content;
			for (int i = 0; i < content_parts.size(); i++) {
				if (content_parts[i].get_type() == Variant::STRING) {
					text += String(content_parts[i]);
				} else if (content_parts[i].get_type() == Variant::DICTIONARY) {
					Dictionary part = content_parts[i];
					if (part.has("text")) {
						text += String(part["text"]);
					} else if (part.has("content")) {
						text += _string_from_content_variant(part["content"]);
					}
				}
			}
			return text;
		}
		default:
			return String();
	}
}

static String _variant_to_visible_text(const Variant &p_value) {
	switch (p_value.get_type()) {
		case Variant::NIL:
			return String();
		case Variant::STRING:
			return String(p_value).strip_edges();
		case Variant::ARRAY: {
			Array arr = p_value;
			String text;
			for (int i = 0; i < arr.size(); i++) {
				String item_text = _variant_to_visible_text(arr[i]);
				if (item_text.is_empty()) {
					continue;
				}
				if (!text.is_empty()) {
					text += "\n";
				}
				text += item_text;
			}
			return text;
		}
		case Variant::DICTIONARY: {
			Dictionary dict = p_value;
			if (dict.has("text")) {
				return _variant_to_visible_text(dict["text"]);
			}
			if (dict.has("content")) {
				return _variant_to_visible_text(dict["content"]);
			}
			if (dict.has("message")) {
				return _variant_to_visible_text(dict["message"]);
			}
			return JSON::stringify(dict, "\t");
		}
		default:
			return String(p_value).strip_edges();
	}
}

static void _append_visible_process_field(String &r_process, const Dictionary &p_dict, const String &p_key, const String &p_label) {
	if (!p_dict.has(p_key)) {
		return;
	}
	String value = _variant_to_visible_text(p_dict[p_key]);
	if (value.is_empty()) {
		return;
	}
	if (!r_process.is_empty()) {
		r_process += "\n\n";
	}
	r_process += "[" + p_label + "]\n" + value;
}

static String _extract_visible_process_from_message(const Dictionary &p_message) {
	String process;
	_append_visible_process_field(process, p_message, "reasoning", "Intermediate");
	_append_visible_process_field(process, p_message, "reasoning_content", "Intermediate");
	_append_visible_process_field(process, p_message, "thinking", "Intermediate");
	_append_visible_process_field(process, p_message, "think", "Intermediate");
	_append_visible_process_field(process, p_message, "intermediate", "Intermediate");
	_append_visible_process_field(process, p_message, "process", "Process");
	_append_visible_process_field(process, p_message, "build", "Build");
	_append_visible_process_field(process, p_message, "build_log", "Build");
	_append_visible_process_field(process, p_message, "tool_process", "Tool");
	_append_visible_process_field(process, p_message, "events", "Process");
	_append_visible_process_field(process, p_message, "steps", "Process");
	return process;
}

static String _extract_visible_process_from_response(const Variant &p_data) {
	if (p_data.get_type() != Variant::DICTIONARY) {
		return String();
	}
	Dictionary root = p_data;
	String process = _extract_visible_process_from_message(root);

	if (root.has("choices") && root["choices"].get_type() == Variant::ARRAY) {
		Array choices = root["choices"];
		for (int i = 0; i < choices.size(); i++) {
			if (choices[i].get_type() != Variant::DICTIONARY) {
				continue;
			}
			Dictionary choice = choices[i];
			String choice_process = _extract_visible_process_from_message(choice);
			if (choice.has("message") && choice["message"].get_type() == Variant::DICTIONARY) {
				Dictionary message = choice["message"];
				String message_process = _extract_visible_process_from_message(message);
				if (!message_process.is_empty()) {
					if (!choice_process.is_empty()) {
						choice_process += "\n\n";
					}
					choice_process += message_process;
				}
			}
			if (choice_process.is_empty()) {
				continue;
			}
			if (!process.is_empty()) {
				process += "\n\n";
			}
			process += choice_process;
		}
	}

	return process;
}

static String _extract_stream_text_delta(const Dictionary &p_data) {
	if (p_data.has("choices") && p_data["choices"].get_type() == Variant::ARRAY) {
		Array choices = p_data["choices"];
		if (!choices.is_empty() && choices[0].get_type() == Variant::DICTIONARY) {
			Dictionary first_choice = choices[0];
			if (first_choice.has("delta") && first_choice["delta"].get_type() == Variant::DICTIONARY) {
				Dictionary delta = first_choice["delta"];
				String content_delta = _string_from_content_variant(delta.get("content", Variant()));
				if (!content_delta.is_empty()) {
					return content_delta;
				}
			}
			if (first_choice.has("message") && first_choice["message"].get_type() == Variant::DICTIONARY) {
				Dictionary message = first_choice["message"];
				String message_content = _string_from_content_variant(message.get("content", Variant()));
				if (!message_content.is_empty()) {
					return message_content;
				}
			}
			String process_delta = _extract_visible_process_from_message(first_choice);
			if (!process_delta.is_empty()) {
				return process_delta + "\n";
			}
			return _string_from_content_variant(first_choice.get("text", Variant()));
		}
	}

	String direct_delta = _string_from_content_variant(p_data.get("delta", Variant()));
	if (!direct_delta.is_empty()) {
		return direct_delta;
	}

	if (p_data.has("content")) {
		String content = _string_from_content_variant(p_data["content"]);
		if (!content.is_empty()) {
			return content;
		}
	}

	if (p_data.has("type") && String(p_data["type"]) == "content_block_delta" && p_data.has("delta") && p_data["delta"].get_type() == Variant::DICTIONARY) {
		Dictionary delta = p_data["delta"];
		String text = _string_from_content_variant(delta.get("text", Variant()));
		if (!text.is_empty()) {
			return text;
		}
		return _extract_visible_process_from_message(delta);
	}

	return String();
}

String AIChatService::_build_chat_url() const {
	String url = settings.base_url.strip_edges();
	while (url.ends_with("/")) {
		url = url.substr(0, url.length() - 1);
	}
	if (!url.ends_with("/chat/completions")) {
		url += "/chat/completions";
	}
	return url;
}

void AIChatService::_ensure_http_request() {
	if (http_request) {
		return;
	}

	http_request = memnew(HTTPRequest);
	http_request->set_name("AIChatHTTPRequest");
	http_request->set_use_threads(use_threads);
	http_request->set_timeout(timeout);

	// Delay EDITOR_GET until EditorSettings is ready
	if (EditorSettings::get_singleton()) {
		const String proxy_host = EDITOR_GET("network/http_proxy/host");
		const int proxy_port = EDITOR_GET("network/http_proxy/port");
		http_request->set_http_proxy(proxy_host, proxy_port);
		http_request->set_https_proxy(proxy_host, proxy_port);
	}

	http_request->connect(SNAME("request_completed"), callable_mp(this, &AIChatService::_request_completed));
	add_child(http_request, false, INTERNAL_MODE_BACK);
}

void AIChatService::_ensure_jundot_plugin_backend() {
	if (jundot_plugin_backend) {
		return;
	}

	jundot_plugin_backend = memnew(AIJundotPluginBackend);
	jundot_plugin_backend->set_name("AIJundotPluginBackend");
	jundot_plugin_backend->connect(SNAME("chat_completed"), callable_mp(this, &AIChatService::_jundot_plugin_chat_completed));
	jundot_plugin_backend->connect(SNAME("chat_stream_data"), callable_mp(this, &AIChatService::_jundot_plugin_stream_data));
	add_child(jundot_plugin_backend, false, INTERNAL_MODE_BACK);
}

bool AIChatService::_should_use_jundot_plugin_backend() const {
	return settings.backend_type == AIBackendType::JUNDOT_PLUGIN;
}

String AIChatService::_extract_text_from_response(const Variant &p_data) const {
	if (p_data.get_type() != Variant::DICTIONARY) {
		return String();
	}

	Dictionary root = p_data;
	if (!root.has("choices")) {
		if (root.has("error") && root["error"].get_type() == Variant::DICTIONARY) {
			Dictionary error = root["error"];
			return error.get("message", String());
		}
		return String();
	}

	Array choices = root["choices"];
	if (choices.is_empty() || choices[0].get_type() != Variant::DICTIONARY) {
		return String();
	}

	Dictionary first_choice = choices[0];
	if (first_choice.has("message") && first_choice["message"].get_type() == Variant::DICTIONARY) {
		Dictionary message = first_choice["message"];
		return _string_from_content_variant(message.get("content", Variant()));
	}
	return _string_from_content_variant(first_choice.get("text", Variant()));
}

void AIChatService::_extract_usage_from_response(const Variant &p_data, int &r_prompt_tokens, int &r_completion_tokens) const {
	r_prompt_tokens = 0;
	r_completion_tokens = 0;
	if (p_data.get_type() != Variant::DICTIONARY) {
		return;
	}
	Dictionary root = p_data;
	if (root.has("usage") && root["usage"].get_type() == Variant::DICTIONARY) {
		Dictionary usage = root["usage"];
		r_prompt_tokens = usage.get("prompt_tokens", 0);
		r_completion_tokens = usage.get("completion_tokens", 0);
	}
}

void AIChatService::_extract_think_from_content(String &r_content, String &r_think) const {
	r_think = String();
	// Extract content from <think> ... </think> tags.
	// Some models (like DeepSeek R1) wrap their reasoning in these tags.
	const String think_start = "<think>";
	const String think_end = "</think>";
	int start = r_content.find(think_start);
	if (start >= 0) {
		int end = r_content.find(think_end, start + think_start.length());
		if (end > start) {
			r_think = r_content.substr(start + think_start.length(), end - start - think_start.length()).strip_edges();
			// Remove the think block from the main content.
			String before = r_content.substr(0, start);
			String after = r_content.substr(end + think_end.length());
			r_content = (before + after).strip_edges();
		}
	}
}

static String _http_result_to_string(int p_result) {
	switch (p_result) {
		case 0:
			return "Success";
		case 1:
			return "Chunked body size mismatch";
		case 2:
			return "Cannot connect to server";
		case 3:
			return "Cannot resolve hostname";
		case 4:
			return "Connection error";
		case 5:
			return "SSL handshake failed";
		case 6:
			return "No response from server";
		case 7:
			return "Body decompression failed";
		case 8:
			return "Request failed (possibly timed out)";
		case 9:
			return "Cannot open download file";
		case 10:
			return "Download file write error";
		case 11:
			return "Redirect limit reached";
		default:
			return "Unknown error (code: " + itos(p_result) + ")";
	}
}

void AIChatService::_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	// Calculate elapsed time.
	double elapsed = 0.0;
	if (request_start_usec > 0) {
		elapsed = (OS::get_singleton()->get_ticks_usec() - request_start_usec) / 1000000.0;
		request_start_usec = 0;
	}

	String body_text;
	if (!p_body.is_empty()) {
		body_text = ai_decode_http_response_text(p_body, p_headers);
	}

	Variant parsed;
	String parse_error;
	String content;

	if (p_result != 0) {
		String err = _http_result_to_string(p_result);
		parse_error = "Request failed: " + err;
		if (p_result == 8) {
			parse_error += vformat(" (timeout is %.0f seconds).", timeout);
		}
		ERR_PRINT("AIChatService: " + parse_error);
		content = "Error: " + parse_error;
	} else if (p_response_code >= 400) {
		parse_error = vformat("Server returned HTTP %d.", p_response_code);
		ERR_PRINT("AIChatService: " + parse_error);
		if (!body_text.is_empty()) {
			ERR_PRINT("AIChatService: Response body:\n" + body_text.substr(0, 2000));
		}
		content = "Error: " + parse_error;
	} else if (body_text.is_empty()) {
		parse_error = "Empty response body (HTTP " + itos(p_response_code) + ")";
		ERR_PRINT("AIChatService: " + parse_error);
		content = "Error: " + parse_error;
	} else {
		if (streaming) {
			// SSE format: data blocks separated by double newlines
			// Each block may contain multiple lines starting with "data:".
			String normalized_body = body_text.replace("\r\n", "\n");
			normalized_body = normalized_body.replace("\r", "\n");
			Vector<String> blocks = normalized_body.split("\n\n", false);
			print_line("AIChatService: Processing streaming response with " + itos(blocks.size()) + " blocks");

			for (int i = 0; i < blocks.size(); i++) {
				String block = blocks[i];
				if (block.is_empty()) {
					continue;
				}

				// If block doesn't start with "data:", try processing as raw JSON line
				if (!block.begins_with("data: ") && !block.begins_with("data:")) {
					block = block.strip_edges();
					if (!block.is_empty() && block.begins_with("{")) {
						// Raw JSON line, process directly
						_process_stream_chunk("data: " + block);
						continue;
					}
				}

				Vector<String> lines = block.split("\n", false);
				for (int j = 0; j < lines.size(); j++) {
					String line = lines[j].strip_edges();
					if (line.begins_with("data:")) {
						_process_stream_chunk(line);
					} else if (line.begins_with("{") && !line.begins_with("data:")) {
						// Raw JSON without "data:" prefix
						_process_stream_chunk("data: " + line);
					}
				}
			}

			content = stream_buffer;
			print_line("AIChatService: Stream buffer content length: " + itos(content.length()));
			if (!stream_tool_calls.is_empty()) {
				// The stream contained tool_calls. Construct a synthetic response
				// that exposes them so the chat panel can execute them.
				Dictionary synthetic;
				Array choices_arr;
				Dictionary choice;
				Dictionary msg;
				msg["role"] = "assistant";
				msg["content"] = content;
				msg["tool_calls"] = stream_tool_calls;
				choice["message"] = msg;
				choice["finish_reason"] = "tool_calls";
				choices_arr.push_back(choice);
				synthetic["choices"] = choices_arr;
				parsed = synthetic;
				print_line("AIChatService: Stream contained " + itos(stream_tool_calls.size()) + " tool_call(s), passing through for tool execution.");
			} else if (content.is_empty() && !body_text.strip_edges().begins_with("data:")) {
				// Fallback: try to parse as non-streaming response if stream parsing failed
				parsed = JSON::parse_string(body_text);
				if (parsed.get_type() == Variant::DICTIONARY) {
					content = _extract_text_from_response(parsed);
					print_line("AIChatService: Fallback non-streaming parse succeeded, content length: " + itos(content.length()));
				} else {
					print_line("AIChatService: Both stream and fallback parse failed");
				}
			} else if (content.is_empty()) {
				// Check if the response contains tool_calls instead of text content.
				// This is valid for function-calling models and should not be treated as an error.
				Variant parsed_check_var = JSON::parse_string(body_text);
				if (parsed_check_var.get_type() == Variant::DICTIONARY) {
					Dictionary parsed_check = parsed_check_var;
					Array choices_check = parsed_check.get("choices", Array());
					if (!choices_check.is_empty() && choices_check[0].get_type() == Variant::DICTIONARY) {
						Dictionary first = choices_check[0];
						Dictionary msg = first.get("message", Dictionary());
						bool has_tool_calls = msg.has("tool_calls") && msg["tool_calls"].get_type() == Variant::ARRAY &&
								!((Array)msg["tool_calls"]).is_empty();
						if (has_tool_calls) {
							// Construct a minimal valid response with tool_calls for the chat panel to process.
							parsed = parsed_check;
							print_line("AIChatService: Stream contained tool_calls without text content, passing through for tool execution.");
						} else {
							parse_error = "Streaming response did not contain assistant text.";
							content = "Error: " + parse_error;
							ERR_PRINT("AIChatService: " + parse_error);
						}
					} else {
						parse_error = "Streaming response did not contain assistant text.";
						content = "Error: " + parse_error;
						ERR_PRINT("AIChatService: " + parse_error);
					}
				} else {
					parse_error = "Streaming response did not contain assistant text.";
					content = "Error: " + parse_error;
					ERR_PRINT("AIChatService: " + parse_error);
				}
			} else {
				parsed = JSON::parse_string("{}");
			}
		} else {
			parsed = JSON::parse_string(body_text);
			if (parsed.get_type() == Variant::NIL) {
				parse_error = _get_json_error(body_text);
				ERR_PRINT("AIChatService: JSON parse failed. " + parse_error);
				ERR_PRINT("AIChatService: Raw response body:\n" + body_text.substr(0, 2000));
				content = "Error: " + parse_error;
			} else {
				content = _extract_text_from_response(parsed);
			}
		}
	}

	String think_content;
	String visible_process = _extract_visible_process_from_response(parsed);
	if (!visible_process.is_empty()) {
		if (!content.strip_edges().is_empty()) {
			content += "\n\n";
		}
		content += visible_process;
	}
	_extract_think_from_content(content, think_content);

	int prompt_tokens = stream_prompt_tokens;
	int completion_tokens = stream_completion_tokens;
	if (!streaming) {
		_extract_usage_from_response(parsed, prompt_tokens, completion_tokens);
	}

	Dictionary json;
	if (parsed.get_type() == Variant::DICTIONARY) {
		json = parsed;
	}

	if (streaming) {
		emit_signal(SNAME("chat_stream_complete"), p_result, p_response_code, content, json, body_text, elapsed, think_content, prompt_tokens, completion_tokens);
	}

	streaming = false;
	emit_signal(SNAME("chat_completed"), p_result, p_response_code, content, json, body_text, elapsed, think_content, prompt_tokens, completion_tokens);
}

void AIChatService::_jundot_plugin_chat_completed(int p_result, int p_response_code, const String &p_content, const Dictionary &p_json, const String &p_raw_body, double p_elapsed_seconds, const String &p_think_content, int p_prompt_tokens, int p_completion_tokens) {
	String content = p_content;
	String visible_process = _extract_visible_process_from_response(p_json);
	if (!visible_process.is_empty()) {
		if (!content.strip_edges().is_empty()) {
			content += "\n\n";
		}
		content += visible_process;
	}
	emit_signal(SNAME("chat_completed"), p_result, p_response_code, content, p_json, p_raw_body, p_elapsed_seconds, p_think_content, p_prompt_tokens, p_completion_tokens);
}

void AIChatService::_jundot_plugin_stream_data(const String &p_delta, const String &p_full_content, int p_completion_tokens) {
	emit_signal(SNAME("chat_stream_data"), p_delta, p_full_content, p_completion_tokens);
}

void AIChatService::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		_ensure_http_request();
		_ensure_jundot_plugin_backend();
	}
}

void AIChatService::_bind_methods() {
	ClassDB::bind_method(D_METHOD("send_chat", "message"), &AIChatService::send_chat);
	ClassDB::bind_method(D_METHOD("cancel_request"), &AIChatService::cancel_request);
	ClassDB::bind_method(D_METHOD("is_requesting"), &AIChatService::is_requesting);
	ClassDB::bind_method(D_METHOD("is_streaming"), &AIChatService::is_streaming);

	ADD_SIGNAL(MethodInfo("chat_completed",
			PropertyInfo(Variant::INT, "result"),
			PropertyInfo(Variant::INT, "response_code"),
			PropertyInfo(Variant::STRING, "content"),
			PropertyInfo(Variant::DICTIONARY, "json"),
			PropertyInfo(Variant::STRING, "raw_body"),
			PropertyInfo(Variant::FLOAT, "elapsed_seconds"),
			PropertyInfo(Variant::STRING, "think_content"),
			PropertyInfo(Variant::INT, "prompt_tokens"),
			PropertyInfo(Variant::INT, "completion_tokens")));

	ADD_SIGNAL(MethodInfo("chat_stream_data",
			PropertyInfo(Variant::STRING, "delta"),
			PropertyInfo(Variant::STRING, "full_content"),
			PropertyInfo(Variant::INT, "completion_tokens")));

	ADD_SIGNAL(MethodInfo("chat_stream_complete",
			PropertyInfo(Variant::INT, "result"),
			PropertyInfo(Variant::INT, "response_code"),
			PropertyInfo(Variant::STRING, "content"),
			PropertyInfo(Variant::DICTIONARY, "json"),
			PropertyInfo(Variant::STRING, "raw_body"),
			PropertyInfo(Variant::FLOAT, "elapsed_seconds"),
			PropertyInfo(Variant::STRING, "think_content"),
			PropertyInfo(Variant::INT, "prompt_tokens"),
			PropertyInfo(Variant::INT, "completion_tokens")));
}

void AIChatService::configure(const AISettingsData &p_settings) {
	settings = p_settings;
	if (jundot_plugin_backend) {
		jundot_plugin_backend->configure(settings);
	}
}

Error AIChatService::send_chat(const String &p_message) {
	Array messages;
	String effective_prompt = AISettings::get_effective_system_prompt(settings);
	if (!effective_prompt.is_empty()) {
		Dictionary system_message;
		system_message["role"] = "system";
		system_message["content"] = effective_prompt;
		messages.push_back(system_message);
	}

	Dictionary user_message;
	user_message["role"] = "user";
	user_message["content"] = p_message;
	messages.push_back(user_message);

	return send_messages(messages);
}

Error AIChatService::send_messages(const Array &p_messages) {
	return send_messages(p_messages, Array());
}

Error AIChatService::send_messages(const Array &p_messages, const Array &p_tools) {
	ERR_FAIL_COND_V_MSG(!is_inside_tree(), ERR_UNCONFIGURED, "AIChatService must be inside the scene tree before sending a request.");
	ERR_FAIL_COND_V_MSG(!AISettings::is_usage_agreement_current(AISettings::load()), ERR_UNAUTHORIZED, "AI usage agreement must be accepted before sending requests.");

	if (_should_use_jundot_plugin_backend()) {
		_ensure_jundot_plugin_backend();
		jundot_plugin_backend->configure(settings);
		return jundot_plugin_backend->send_messages(p_messages, p_tools);
	}

	ERR_FAIL_COND_V_MSG(settings.base_url.strip_edges().is_empty(), ERR_UNCONFIGURED, "AI base URL is empty.");
	ERR_FAIL_COND_V_MSG(settings.model.strip_edges().is_empty(), ERR_UNCONFIGURED, "AI model is empty.");
	ERR_FAIL_COND_V_MSG(settings.api_key.is_empty(), ERR_UNCONFIGURED, "AI API key is empty.");

	_ensure_http_request();

	request_start_usec = OS::get_singleton()->get_ticks_usec();

	const bool request_stream = p_tools.is_empty();
	streaming = request_stream;
	stream_buffer = String();
	stream_prompt_tokens = 0;
	stream_completion_tokens = 0;
	stream_tool_calls = Array();

	Dictionary payload;
	payload["model"] = settings.model;
	payload["messages"] = p_messages;
	payload["temperature"] = settings.temperature;
	payload["max_tokens"] = settings.max_tokens;
	payload["stream"] = request_stream;

	if (!p_tools.is_empty()) {
		payload["tools"] = p_tools;
		payload["tool_choice"] = "auto";
	}

	Vector<String> headers;
	headers.push_back("Content-Type: application/json; charset=utf-8");
	headers.push_back("Authorization: Bearer " + settings.api_key);
	headers.push_back("Accept-Charset: utf-8");
	if (request_stream) {
		headers.push_back("Accept: text/event-stream");
	}

	return http_request->request(_build_chat_url(), headers, HTTPClient::METHOD_POST, JSON::stringify(payload));
}

void AIChatService::_process_stream_chunk(const String &p_chunk) {
	String chunk = p_chunk.strip_edges();
	if (chunk.is_empty()) {
		return;
	}

	if (!chunk.begins_with("data:")) {
		return;
	}

	String json_str = chunk.substr(5).strip_edges();
	if (json_str == "[DONE]") {
		return;
	}

	Variant parsed = JSON::parse_string(json_str);
	if (parsed.get_type() != Variant::DICTIONARY) {
		ERR_PRINT("AIChatService: Failed to parse stream chunk JSON: " + json_str.substr(0, 200));
		return;
	}

	Dictionary chunk_data = parsed;
	String content_delta = _extract_stream_text_delta(chunk_data);

	if (!content_delta.is_empty()) {
		stream_buffer += content_delta;
		stream_completion_tokens += content_delta.length() / 4;
		emit_signal(SNAME("chat_stream_data"), content_delta, stream_buffer, stream_completion_tokens);
	}

	// Accumulate tool_calls from stream deltas. In OpenAI-compatible streaming,
	// tool_calls come incrementally, so we merge them by index.
	if (chunk_data.has("choices") && chunk_data["choices"].get_type() == Variant::ARRAY) {
		Array choices = chunk_data["choices"];
		if (!choices.is_empty() && choices[0].get_type() == Variant::DICTIONARY) {
			Dictionary first_choice = choices[0];
			if (first_choice.has("delta") && first_choice["delta"].get_type() == Variant::DICTIONARY) {
				Dictionary delta = first_choice["delta"];
				if (delta.has("tool_calls") && delta["tool_calls"].get_type() == Variant::ARRAY) {
					Array tool_call_deltas = delta["tool_calls"];
					for (int i = 0; i < tool_call_deltas.size(); i++) {
						Dictionary tc_delta = tool_call_deltas[i];
						int index = tc_delta.get("index", -1);
						if (index < 0) {
							continue;
						}
						// Ensure the accumulated array is large enough.
						while (stream_tool_calls.size() <= index) {
							Dictionary new_tc;
							new_tc["id"] = String();
							new_tc["type"] = "function";
							Dictionary func;
							func["name"] = String();
							func["arguments"] = String();
							new_tc["function"] = func;
							stream_tool_calls.push_back(new_tc);
						}
						Dictionary accumulated = stream_tool_calls[index];
						if (tc_delta.has("id") && !String(tc_delta["id"]).is_empty()) {
							accumulated["id"] = String(tc_delta["id"]);
						}
						if (tc_delta.has("type") && !String(tc_delta["type"]).is_empty()) {
							accumulated["type"] = String(tc_delta["type"]);
						}
						if (tc_delta.has("function") && tc_delta["function"].get_type() == Variant::DICTIONARY) {
							Dictionary fn_delta = tc_delta["function"];
							Dictionary fn_acc = accumulated.get("function", Dictionary());
							if (fn_delta.has("name") && !String(fn_delta["name"]).is_empty()) {
								fn_acc["name"] = String(fn_delta["name"]);
							}
							if (fn_delta.has("arguments")) {
								String existing_args = fn_acc.get("arguments", String());
								existing_args += String(fn_delta["arguments"]);
								fn_acc["arguments"] = existing_args;
							}
							accumulated["function"] = fn_acc;
						}
						stream_tool_calls[index] = accumulated;
					}
				}
			}
		}
	}

	if (chunk_data.has("usage")) {
		Dictionary usage = chunk_data["usage"];
		stream_prompt_tokens = usage.get("prompt_tokens", stream_prompt_tokens);
		stream_completion_tokens = usage.get("completion_tokens", stream_completion_tokens);
	}
}

void AIChatService::cancel_request() {
	streaming = false;
	if (_should_use_jundot_plugin_backend() && jundot_plugin_backend) {
		jundot_plugin_backend->cancel_request();
		return;
	}
	if (http_request) {
		http_request->cancel_request();
	}
}

bool AIChatService::is_requesting() const {
	if (_should_use_jundot_plugin_backend()) {
		return jundot_plugin_backend && jundot_plugin_backend->is_requesting();
	}
	return http_request && http_request->get_http_client_status() != HTTPClient::STATUS_DISCONNECTED;
}

bool AIChatService::is_streaming() const {
	return streaming;
}
