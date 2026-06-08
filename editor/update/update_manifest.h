/**************************************************************************/
/*  update_dialog.cpp                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             JunDot ENGINE                               */
/**************************************************************************/
/* Copyright (c) 2026-present JunDot Engine contributors . */
/**************************************************************************/

#pragma once

#include "core/crypto/crypto_core.h"
#include "core/string/ustring.h"
#include "core/typedefs.h"
#include "core/variant/dictionary.h"

/// C++ mirror of the update-manifest.json schema (v1).
/// Used by UpdateManager to parse and evaluate remote manifests.
struct UpdateManifest {
	// ── Mandatory fields ────────────────────────────────────
	String manifest_version;
	String package_name;

	struct Version {
		int major = 0;
		int minor = 0;
		int patch = 0;
		String status = "stable";
		String full;
		String commit;
	} version;

	String platform;
	String target;
	String arch;
	bool mono = false;

	String download_url;
	int64_t package_size = 0;
	String sha256;
	String sha1;

	String release_date;
	String channel = "stable";
	String changelog;

	// ── Optional fields ─────────────────────────────────────
	bool mandatory = false;
	String min_version;
	String rollback_to;

	struct Grayscale {
		bool enabled = false;
		int percentage = 100;
		Vector<String> whitelist;
		String machine_id_hash_seed = "jundot-grayscale-v1";
	} grayscale;

	struct FileEntry {
		String path;
		String sha256;
		int64_t size = 0;
		bool required = true;
	};
	Vector<FileEntry> files;

	// ── Parsing ─────────────────────────────────────────────
	/// Parse from a JSON string. Returns true on success.
	bool parse(const String &p_json);

	/// Clear all fields.
	void clear();

	/// Get a human-readable version string for display.
	String get_version_string() const;

	/// Format file size for display (e.g. "512 MB").
	static String format_size(int64_t p_bytes);
};

/// Evaluate whether the current machine should receive a grayscale update.
/// Supports percentage-based hashing and whitelist.
namespace GrayscaleEvaluator {
/// Compute a stable machine identifier hash from machine name + SID.
String generate_machine_id();

/// Evaluate: is this machine eligible for the given grayscale config?
bool is_eligible(const String &p_machine_id, const UpdateManifest::Grayscale &p_config, String *r_reason = nullptr);
} // namespace GrayscaleEvaluator

/// Compare two Jundot version strings.
/// Returns positive if a > b, negative if a < b, 0 if equal.
int compare_versions(const String &p_a, const String &p_b);

/// Check if a manifest channel is eligible for the user's preferred channel.
bool channel_matches(const String &p_manifest_channel, const String &p_preferred_channel);

/// Check if version meets a minimum version requirement.
bool meets_min_version(const String &p_current, const String &p_minimum);
