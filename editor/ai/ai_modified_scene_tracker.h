/**************************************************************************/
/*                         ai_modified_scene_tracker.h                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/

#pragma once

#include "core/string/ustring.h"

class AIModifiedSceneTracker {
	static String _normalize_scene_path(const String &p_path);

public:
	static void mark_scene_written(const String &p_path);
	static bool consume_scene_write(const String &p_path);
};
