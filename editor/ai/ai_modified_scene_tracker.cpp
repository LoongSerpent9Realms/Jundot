/**************************************************************************/
/*                         ai_modified_scene_tracker.cpp                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/

#include "ai_modified_scene_tracker.h"

#include "core/config/project_settings.h"
#include "core/os/mutex.h"
#include "core/templates/hash_set.h"

static Mutex ai_scene_tracker_mutex;
static HashSet<String> ai_written_scenes;

String AIModifiedSceneTracker::_normalize_scene_path(const String &p_path) {
	String path = p_path.strip_edges().replace("\\", "/");
	if (path.is_empty()) {
		return String();
	}

	path = path.trim_prefix("res://").simplify_path();

	if (path.is_absolute_path() && ProjectSettings::get_singleton()) {
		const String project_root = ProjectSettings::get_singleton()->get_resource_path().replace("\\", "/").simplify_path().trim_suffix("/");
		const String path_lower = path.to_lower();
		const String root_lower = project_root.to_lower();
		if (path_lower == root_lower) {
			return String();
		}
		if (path_lower.begins_with(root_lower + "/")) {
			path = path.substr(project_root.length() + 1).simplify_path();
		}
	}

	return path;
}

void AIModifiedSceneTracker::mark_scene_written(const String &p_path) {
	const String path = _normalize_scene_path(p_path);
	const String lower = path.to_lower();
	if (!lower.ends_with(".tscn") && !lower.ends_with(".scn")) {
		return;
	}

	MutexLock lock(ai_scene_tracker_mutex);
	ai_written_scenes.insert(path);
}

bool AIModifiedSceneTracker::consume_scene_write(const String &p_path) {
	const String path = _normalize_scene_path(p_path);
	if (path.is_empty()) {
		return false;
	}

	MutexLock lock(ai_scene_tracker_mutex);
	if (!ai_written_scenes.has(path)) {
		return false;
	}
	ai_written_scenes.erase(path);
	return true;
}
