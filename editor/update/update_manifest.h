/**************************************************************************/
/*  update_manifest.h                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             JUNDOT ENGINE                               */
/*                        https://jundotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
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
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "core/crypto/crypto_core.h"
#include "core/string/ustring.h"
#include "core/typedefs.h"
#include "core/variant/dictionary.h"

/// <summary>
/// Per-platform download entry inside the unified update-manifest.json.
/// When the engine UpdateManager sees platform_downloads[], it resolves to
/// the entry matching the runtime OS + CPU architecture instead of using the
/// top-level download_url.
/// </summary>
struct PlatformDownload {
	String package_name;
	String platform;
	String arch;
	String key; // "windows-x86_64", "linux-arm64", "macos-arm64", ...
	String download_url;
	String manifest_url;
	int64_t package_size = 0;
	String sha256;
};

/// C++ mirror of the update-manifest.json schema (v1 / v1.1).
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

	// ── v1.1: multi-platform publishing ─────────────────────
	Vector<PlatformDownload> platform_downloads;

	// ── Parsing ─────────────────────────────────────────────
	/// Parse from a JSON string. Returns true on success.
	bool parse(const String &p_json);

	/// Clear all fields.
	void clear();

	/// Get a human-readable version string for display.
	String get_version_string() const;

	/// Format file size for display (e.g. "512 MB").
	static String format_size(int64_t p_bytes);

	/// Find the PlatformDownload entry that best matches the runtime OS/CPU.
	/// Returns true and fills p_out on success.
	/// The "key" field in each entry is compared as "{platform}-{arch}" (e.g. "windows-x86_64").
	/// Falls back to exact key match, then platform-only match, then false.
	bool resolve_platform_download(const String &p_runtime_platform, const String &p_runtime_arch, PlatformDownload &p_out) const;
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
