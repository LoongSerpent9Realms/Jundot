/*  ai_importer.h                                                          */
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

#pragma once

#include "editor/ai/ai_chat_parser.h"

#include "core/error/error_list.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"

class AIImporter {
	static constexpr int MAX_IMPORT_ENTRIES = 50;
	static constexpr int MAX_FILE_SIZE = 64 * 1024;

	enum FileFormat {
		FORMAT_UNKNOWN,
		FORMAT_SKILL_MD,
		FORMAT_MCP_JSON,
		FORMAT_TEXT_MEMORY,
	};

	static FileFormat _detect_format(const String &p_path);
	static Error _parse_skill_md(const String &p_content, AISkillEntry &r_skill);
	static Error _parse_mcp_json(const String &p_content, Vector<AIMCPServerEntry> &r_servers);
	static Error _parse_text_memory(const String &p_path, const String &p_content, AIMemoryEntry &r_memory);

public:
	static Error preview_from_file(const String &p_path, Vector<AISuggestion> &r_suggestions);
	static Error preview_from_directory(const String &p_dir, Vector<AISuggestion> &r_suggestions);
	static Error import_suggestions(const Vector<AISuggestion> &p_suggestions);
};
