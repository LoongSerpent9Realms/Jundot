/**************************************************************************/
/*                       ai_code_security_checker.cpp                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                               JunDot                                   */
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
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE        */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "ai_code_security_checker.h"

struct DangerPattern {
	const char *keyword;
	int risk_level; // 1 = low, 2 = medium, 3 = high
	const char *description;
};

// Simple keyword-based pattern list.
// Note: To minimize false positives, only patterns that are highly indicative
// of suspicious behavior are included. Comments are stripped before matching.
static const DangerPattern DANGER_PATTERNS[] = {
	// High risk: system command execution
	{ "system(", 3, "system() call - arbitrary system command execution" },
	{ "exec(", 3, "exec() call - arbitrary process execution" },
	{ "popen(", 3, "popen() call - pipeline command execution" },
	{ "ShellExecute", 3, "ShellExecute - Windows process launch" },
	{ "CreateProcess", 3, "CreateProcess - Windows process creation" },
	{ "ws2_32", 3, "ws2_32 - Windows socket library usage" },
	{ "OS::execute", 3, "OS::execute - arbitrary process execution" },
	{ "OS::create_process", 3, "OS::create_process - process creation" },

	// Medium risk: suspicious network operations
	{ "http://", 2, "HTTP URL detected" },
	{ "https://", 2, "HTTPS URL detected" },
	{ "curl ", 2, "curl command usage" },
	{ "wget ", 2, "wget command usage" },
	{ "HTTPClient", 2, "HTTPClient - network HTTP client" },
	{ "HTTPRequest", 2, "HTTPRequest - HTTP requests" },
	{ "WebSocket", 2, "WebSocket - persistent network connection" },
	{ "TCPServer", 2, "TCPServer - network server" },
	{ "TCPClient", 2, "TCPClient - network client" },
	{ "UDPServer", 2, "UDPServer - network server" },
	{ "PacketPeerUDP", 2, "PacketPeerUDP - UDP networking" },
	{ "PacketPeerTCP", 2, "PacketPeerTCP - TCP networking" },

	// Medium risk: file system operations
	{ "FileAccess::remove", 2, "FileAccess::remove - file deletion" },
	{ "DirAccess::remove", 2, "DirAccess::remove - directory removal" },
	{ "DirAccess::remove_contents", 2, "DirAccess::remove_contents - recursive deletion" },

	// Low risk: information disclosure / obfuscation
	{ "base64_decode", 1, "base64_decode - obfuscation pattern" },
	{ "Base64", 1, "Base64 - obfuscation pattern" },
	{ "eval(", 1, "eval() - dynamic code execution" },
	{ "password", 1, "hardcoded password reference" },
	{ "secret_key", 1, "secret_key reference" },
	{ "api_key", 1, "api_key reference" },
	{ "access_token", 1, "access_token reference" },
	{ "private_key", 1, "private_key reference" },
};

static const int DANGER_PATTERN_COUNT = sizeof(DANGER_PATTERNS) / sizeof(DANGER_PATTERNS[0]);

static String _strip_comments_and_strings(const String &p_code) {
	String result = p_code;

	// Step 1: Strip C++ single-line comments "// ..."
	while (true) {
		int pos = result.find("//");
		if (pos < 0) break;
		int end_pos = result.find("\n", pos);
		if (end_pos < 0) {
			result = result.substr(0, pos);
			break;
		}
		result = result.substr(0, pos) + " " + result.substr(end_pos);
	}

	// Step 2: Strip C++ block comments "/* ... */"
	while (true) {
		int block_start = result.find("/*");
		if (block_start < 0) break;
		int block_end = result.find("*/", block_start + 2);
		if (block_end < 0) {
			result = result.substr(0, block_start);
			break;
		}
		result = result.substr(0, block_start) + " " + result.substr(block_end + 2);
	}

	// Step 3: Strip string literals "..."
	while (true) {
		int str_start = result.find("\"");
		if (str_start < 0) break;
		int str_end = result.find("\"", str_start + 1);
		if (str_end < 0) {
			result = result.substr(0, str_start);
			break;
		}
		result = result.substr(0, str_start) + " \"\" " + result.substr(str_end + 1);
	}

	// Step 4: Strip char literals '...'
	while (true) {
		int char_start = result.find("'");
		if (char_start < 0) break;
		int char_end = result.find("'", char_start + 1);
		if (char_end < 0) {
			result = result.substr(0, char_start);
			break;
		}
		result = result.substr(0, char_start) + " " + result.substr(char_end + 1);
	}

	return result;
}

// Case-insensitive substring search.
static bool _ci_find(const String &p_haystack, const String &p_needle) {
	String haystack = p_haystack.to_lower();
	String needle = p_needle.to_lower();
	return haystack.find(needle) >= 0;
}

CodeSecurityReport AICodeSecurityChecker::check(const String &p_code) {
	CodeSecurityReport report;

	// Strip comments and string literals to reduce false positives.
	String stripped = _strip_comments_and_strings(p_code);

	int total_matches = 0;

	// Check each danger pattern.
	for (int i = 0; i < DANGER_PATTERN_COUNT; i++) {
		const DangerPattern &dp = DANGER_PATTERNS[i];

		if (_ci_find(stripped, String(dp.keyword))) {
			report.is_safe = false;
			report.risk_level = MAX(report.risk_level, dp.risk_level);
			report.warnings.push_back(String("[RISK ") + String::num(dp.risk_level) + "] " + String(dp.description));
			total_matches++;
		}
	}

	if (!report.is_safe) {
		String summary = "Security: " + String::num(total_matches) + " issue(s) detected, max risk level " + String::num(report.risk_level) + "/3";
		report.warnings.insert(0, summary);
	}

	return report;
}
