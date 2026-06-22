/*  ai_http_response_text.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/

#include "ai_http_response_text.h"

#include "core/os/os.h"

static bool _is_valid_utf8(const PackedByteArray &p_body) {
	const int size = p_body.size();
	if (size == 0) {
		return true;
	}

	const uint8_t *bytes = p_body.ptr();
	int i = 0;
	if (size >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf) {
		i = 3;
	}

	while (i < size) {
		const uint8_t c = bytes[i];
		if (c == 0) {
			return false;
		}
		if ((c & 0x80) == 0) {
			i++;
			continue;
		}

		int needed = 0;
		uint8_t min_second = 0x80;
		uint8_t max_second = 0xbf;

		if ((c & 0xe0) == 0xc0) {
			if (c < 0xc2) {
				return false;
			}
			needed = 1;
		} else if ((c & 0xf0) == 0xe0) {
			needed = 2;
			if (c == 0xe0) {
				min_second = 0xa0;
			} else if (c == 0xed) {
				max_second = 0x9f;
			}
		} else if ((c & 0xf8) == 0xf0) {
			if (c > 0xf4) {
				return false;
			}
			needed = 3;
			if (c == 0xf0) {
				min_second = 0x90;
			} else if (c == 0xf4) {
				max_second = 0x8f;
			}
		} else {
			return false;
		}

		if (i + needed >= size) {
			return false;
		}
		const uint8_t second = bytes[i + 1];
		if (second < min_second || second > max_second) {
			return false;
		}
		for (int j = 2; j <= needed; j++) {
			if ((bytes[i + j] & 0xc0) != 0x80) {
				return false;
			}
		}
		i += needed + 1;
	}

	return true;
}

static String _get_content_type_charset(const PackedStringArray &p_headers) {
	for (int i = 0; i < p_headers.size(); i++) {
		String header = p_headers[i].strip_edges();
		if (!header.to_lower().begins_with("content-type:")) {
			continue;
		}

		Vector<String> parts = header.split(";", false);
		for (int j = 1; j < parts.size(); j++) {
			String part = parts[j].strip_edges();
			int eq = part.find_char('=');
			if (eq <= 0) {
				continue;
			}
			if (part.substr(0, eq).strip_edges().to_lower() == "charset") {
				return part.substr(eq + 1).strip_edges().trim_prefix("\"").trim_suffix("\"");
			}
		}
	}

	return String();
}

static String _decode_multibyte(const PackedByteArray &p_body, const String &p_encoding) {
	if (p_encoding.is_empty() || p_encoding.to_lower() == "utf-8" || p_encoding.to_lower() == "utf8") {
		return String();
	}
	return OS::get_singleton()->multibyte_to_string(p_encoding, p_body);
}

String ai_decode_http_response_text(const PackedByteArray &p_body, const PackedStringArray &p_headers) {
	if (p_body.is_empty()) {
		return String();
	}

	if (_is_valid_utf8(p_body)) {
		return String::utf8((const char *)p_body.ptr(), p_body.size());
	}

	const String declared_charset = _get_content_type_charset(p_headers);
	String decoded = _decode_multibyte(p_body, declared_charset);
	if (!decoded.is_empty()) {
		return decoded;
	}

	decoded = _decode_multibyte(p_body, "GB18030");
	if (!decoded.is_empty()) {
		return decoded;
	}

	decoded = _decode_multibyte(p_body, "GBK");
	if (!decoded.is_empty()) {
		return decoded;
	}

	return String::latin1(Span((const char *)p_body.ptr(), p_body.size()));
}
