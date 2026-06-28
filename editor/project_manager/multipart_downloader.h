/**************************************************************************/
/*  multipart_downloader.h                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/

#pragma once

#include "scene/main/node.h"

class HTTPRequest;

class MultiPartDownloader : public Node {
	GDCLASS(MultiPartDownloader, Node);

	struct Chunk {
		uint64_t start = 0;
		uint64_t end = 0;
		uint64_t downloaded = 0;
		bool done = false;
		String part_path;
		HTTPRequest *http = nullptr;
	};

	enum class State {
		IDLE,
		PROBING,
		DOWNLOADING,
		MERGING,
		FINISHED,
		FAILED,
	};

	String url;
	String output_path;
	String fallback_tmp_path;
	int num_connections = 1;
	uint64_t total_size = 0;
	uint64_t merged_bytes = 0;
	State state = State::IDLE;

	HTTPRequest *probe_http = nullptr;
	HTTPRequest *fallback_http = nullptr;
	Vector<Chunk> chunks;

	void _clear_requests();
	void _start_probe();
	void _start_multipart_download(uint64_t p_total_size);
	void _start_fallback_download();
	void _on_probe_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);
	void _on_chunk_completed(int p_chunk_index, int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);
	void _on_fallback_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);
	bool _check_all_chunks_done() const;
	void _merge_chunks();
	void _fail(const String &p_reason);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	Error start(const String &p_url, const String &p_output_path, int p_num_connections);
	void cancel();
	uint64_t get_downloaded_bytes() const;
	uint64_t get_total_bytes() const { return total_size; }

	MultiPartDownloader();
	~MultiPartDownloader();
};
