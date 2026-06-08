/**************************************************************************/
/*  update_manifest.cpp                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/**************************************************************************/

#include "update_manifest.h"

#include "core/crypto/crypto_core.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/string/ustring.h"

// ═══════════════════════════════════════════════════════════════
//  UpdateManifest::parse
// ═══════════════════════════════════════════════════════════════

bool UpdateManifest::parse(const String &p_json) {
	// Parse JSON string
	Variant result = JSON::parse_string(p_json);
	if (result == Variant() || result.get_type() != Variant::DICTIONARY) {
		return false;
	}

	Dictionary root = result;

	// ── Mandatory fields ────────────────────────────────────
	manifest_version = root.get("manifest_version", String());
	if (manifest_version.is_empty()) {
		return false; // Missing required field
	}

	package_name = root.get("package_name", String());

	// Parse version sub-object
	Dictionary ver_dict = root.get("version", Dictionary());
	version.major = int(ver_dict.get("major", 0));
	version.minor = int(ver_dict.get("minor", 0));
	version.patch = int(ver_dict.get("patch", 0));
	version.status = ver_dict.get("status", "stable");
	version.full = ver_dict.get("full", String());
	version.commit = ver_dict.get("commit", String());

	platform = root.get("platform", String());
	target = root.get("target", String());
	arch = root.get("arch", String());
	mono = bool(root.get("mono", false));

	download_url = root.get("download_url", String());
	package_size = int64_t(root.get("package_size", 0));
	sha256 = root.get("sha256", String());
	sha1 = root.get("sha1", String());

	release_date = root.get("release_date", String());
	channel = root.get("channel", "stable");
	changelog = root.get("changelog", String());

	// ── Optional fields ─────────────────────────────────────
	mandatory = bool(root.get("mandatory", false));
	min_version = root.get("min_version", String());
	rollback_to = root.get("rollback_to", String());

	// Parse grayscale sub-object
	Dictionary gs_dict = root.get("grayscale", Dictionary());
	grayscale.enabled = bool(gs_dict.get("enabled", false));
	grayscale.percentage = int(gs_dict.get("percentage", 100));
	grayscale.machine_id_hash_seed = gs_dict.get("machine_id_hash_seed", "jundot-grayscale-v1");

	Array whitelist_arr = gs_dict.get("whitelist", Array());
	grayscale.whitelist.clear();
	for (int i = 0; i < whitelist_arr.size(); i++) {
		grayscale.whitelist.push_back(String(whitelist_arr[i]));
	}

	// Parse files array
	Array files_arr = root.get("files", Array());
	files.clear();
	for (int i = 0; i < files_arr.size(); i++) {
		Dictionary file_dict = files_arr[i];
		FileEntry entry;
		entry.path = file_dict.get("path", String());
		entry.sha256 = file_dict.get("sha256", String());
		entry.size = int64_t(file_dict.get("size", 0));
		entry.required = bool(file_dict.get("required", true));
		files.push_back(entry);
	}

	return true;
}

void UpdateManifest::clear() {
	manifest_version = String();
	package_name = String();
	version = Version();
	platform = String();
	target = String();
	arch = String();
	mono = false;
	download_url = String();
	package_size = 0;
	sha256 = String();
	sha1 = String();
	release_date = String();
	channel = "stable";
	changelog = String();
	mandatory = false;
	min_version = String();
	rollback_to = String();
	grayscale = Grayscale();
	files.clear();
}

String UpdateManifest::get_version_string() const {
	return version.full.is_empty()
		? vformat("%d.%d.%d-%s", version.major, version.minor, version.patch, version.status)
		: version.full;
}

String UpdateManifest::format_size(int64_t p_bytes) {
	if (p_bytes >= 1024 * 1024 * 1024) {
		return vformat("%.1f GB", double(p_bytes) / (1024.0 * 1024.0 * 1024.0));
	}
	if (p_bytes >= 1024 * 1024) {
		return vformat("%.1f MB", double(p_bytes) / (1024.0 * 1024.0));
	}
	if (p_bytes >= 1024) {
		return vformat("%.1f KB", double(p_bytes) / 1024.0);
	}
	return vformat("%d B", p_bytes);
}

// ═══════════════════════════════════════════════════════════════
//  GrayscaleEvaluator
// ═══════════════════════════════════════════════════════════════

String GrayscaleEvaluator::generate_machine_id() {
	// Use machine name + Windows SID (or OS name on non-Windows)
	String input = OS::get_singleton()->get_environment("COMPUTERNAME");
	if (input.is_empty()) {
		input = OS::get_singleton()->get_environment("HOSTNAME");
	}
	if (input.is_empty()) {
		input = "jundot-machine";
	}

	// Add a salt to prevent trivial replay
	input += "|jundot-launcher-v1";

	// SHA256 hash
	CryptoCore::SHA256Context ctx;
	ctx.start();
	ctx.update(reinterpret_cast<const uint8_t *>(input.utf8().get_data()), input.utf8().length());
	unsigned char hash[32];
	ctx.finish(hash);

	// Convert first 8 bytes to hex for a 16-char ID
	String hex;
	for (int i = 0; i < 8; i++) {
		hex += String::num_int64(hash[i], 16, true).lpad(2, '0');
	}

	return hex;
}

bool GrayscaleEvaluator::is_eligible(const String &p_machine_id, const UpdateManifest::Grayscale &p_config, String *r_reason) {
	// Not enabled → everyone eligible
	if (!p_config.enabled) {
		if (r_reason) {
			*r_reason = "Grayscale not enabled.";
		}
		return true;
	}

	// Whitelist check (highest priority)
	for (const String &id : p_config.whitelist) {
		if (id.strip_edges().casecmp_to(p_machine_id) == 0) {
			if (r_reason) {
				*r_reason = "Machine is on the whitelist.";
			}
			return true;
		}
	}

	// Percentage=0 → nobody eligible (unless whitelisted, already checked)
	if (p_config.percentage <= 0) {
		if (r_reason) {
			*r_reason = "Grayscale percentage is 0%.";
		}
		return false;
	}

	// Percentage=100 → everyone eligible
	if (p_config.percentage >= 100) {
		if (r_reason) {
			*r_reason = "Grayscale percentage is 100%.";
		}
		return true;
	}

	// Percentage hash evaluation
	String seed = p_config.machine_id_hash_seed.is_empty()
		? "jundot-grayscale-v1"
		: p_config.machine_id_hash_seed;

	String hash_input = p_machine_id + "|" + seed + "|v1";

	CryptoCore::SHA256Context ctx;
	ctx.start();
	ctx.update(reinterpret_cast<const uint8_t *>(hash_input.utf8().get_data()), hash_input.utf8().length());
	unsigned char hash[32];
	ctx.finish(hash);

	// Take first 4 bytes as uint32, mod 100
	uint32_t value = (uint32_t(hash[0]) << 24) | (uint32_t(hash[1]) << 16) | (uint32_t(hash[2]) << 8) | uint32_t(hash[3]);
	int bucket = int(value % 100);

	bool eligible = bucket < p_config.percentage;
	if (r_reason) {
		*r_reason = eligible
			? vformat("Grayscale %d%% matched (bucket=%d).", p_config.percentage, bucket)
			: vformat("Grayscale %d%% not matched (bucket=%d).", p_config.percentage, bucket);
	}
	return eligible;
}

// ═══════════════════════════════════════════════════════════════
//  Version comparison utilities
// ═══════════════════════════════════════════════════════════════

// Status priority: stable > rc > beta > alpha > dev
static int _status_priority(const String &p_status) {
	if (p_status == "stable") {
		return 50;
	} else if (p_status == "rc") {
		return 40;
	} else if (p_status == "beta") {
		return 30;
	} else if (p_status == "alpha") {
		return 20;
	} else if (p_status == "dev") {
		return 10;
	}
	return 0;
}

/// Split "1.7.2-beta" into base="1.7.2" and status="beta"
struct ParsedVersion {
	int major = 0;
	int minor = 0;
	int patch = 0;
	String status = "stable";
};

static ParsedVersion _parse_version(const String &p_version) {
	ParsedVersion result;
	String v = p_version.strip_edges();

	// Split on first hyphen: numeric + status
	int hyphen = v.find_char('-');
	String numeric = (hyphen >= 0) ? v.substr(0, hyphen) : v;
	String status_str = (hyphen >= 0) ? v.substr(hyphen + 1) : "stable";

	// Remove leading 'v' if present
	numeric = numeric.trim_prefix("v");

	// Split numeric part
	Vector<String> parts = numeric.split(".");
	if (parts.size() > 0) {
		result.major = parts[0].to_int();
	}
	if (parts.size() > 1) {
		result.minor = parts[1].to_int();
	}
	if (parts.size() > 2) {
		result.patch = parts[2].to_int();
	}

	result.status = status_str;

	return result;
}

int compare_versions(const String &p_a, const String &p_b) {
	ParsedVersion a = _parse_version(p_a);
	ParsedVersion b = _parse_version(p_b);

	if (a.major != b.major) {
		return a.major - b.major;
	}
	if (a.minor != b.minor) {
		return a.minor - b.minor;
	}
	if (a.patch != b.patch) {
		return a.patch - b.patch;
	}

	int a_pri = _status_priority(a.status);
	int b_pri = _status_priority(b.status);
	return a_pri - b_pri;
}

bool channel_matches(const String &p_manifest_channel, const String &p_preferred_channel) {
	if (p_preferred_channel == "dev") {
		return true; // Dev gets everything
	}
	if (p_preferred_channel == "beta") {
		return p_manifest_channel == "stable" || p_manifest_channel == "beta";
	}
	// Default: stable only
	return p_manifest_channel == "stable";
}

bool meets_min_version(const String &p_current, const String &p_minimum) {
	if (p_minimum.is_empty()) {
		return true;
	}
	return compare_versions(p_current, p_minimum) >= 0;
}
