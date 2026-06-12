#include "multipart_downloader.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/callable_mp.h"
#include "core/string/translation_server.h"
#include "scene/main/http_request.h"

void MultiPartDownloader::_bind_methods() {
	ADD_SIGNAL(MethodInfo("progress",
			PropertyInfo(Variant::INT, "downloaded"),
			PropertyInfo(Variant::INT, "total")));
	ADD_SIGNAL(MethodInfo("finished", PropertyInfo(Variant::STRING, "output_path")));
	ADD_SIGNAL(MethodInfo("failed", PropertyInfo(Variant::STRING, "reason")));
}

void MultiPartDownloader::_notification(int p_what) {
	if (p_what == NOTIFICATION_PREDELETE) {
		_clear_requests();
	}
}

MultiPartDownloader::MultiPartDownloader() {
}

MultiPartDownloader::~MultiPartDownloader() {
	_clear_requests();
}

void MultiPartDownloader::_clear_requests() {
	if (probe_http) {
		probe_http->cancel_request();
		probe_http->queue_free();
		probe_http = nullptr;
	}

	for (int i = 0; i < chunks.size(); i++) {
		if (chunks[i].http) {
			chunks[i].http->cancel_request();
			chunks[i].http->queue_free();
			chunks.write[i].http = nullptr;
		}
		if (!chunks[i].part_path.is_empty()) {
			DirAccess::remove_absolute(chunks[i].part_path);
		}
	}
	chunks.clear();

	if (fallback_http) {
		fallback_http->cancel_request();
		fallback_http->queue_free();
		fallback_http = nullptr;
	}
	if (!fallback_tmp_path.is_empty()) {
		DirAccess::remove_absolute(fallback_tmp_path);
		fallback_tmp_path.clear();
	}
}

Error MultiPartDownloader::start(const String &p_url, const String &p_output_path, int p_num_connections) {
	if (state == State::PROBING || state == State::DOWNLOADING || state == State::MERGING) {
		return ERR_ALREADY_IN_USE;
	}

	url = p_url;
	output_path = p_output_path;
	num_connections = CLAMP(p_num_connections, 1, 16);
	total_size = 0;
	merged_bytes = 0;
	_clear_requests();

	const String base_dir = output_path.get_base_dir();
	if (!base_dir.is_empty()) {
		const Error err = DirAccess::make_dir_recursive_absolute(base_dir);
		if (err != OK) {
			_fail(vformat("Failed to create directory: %s (error %d).", base_dir, err));
			return err;
		}
	}

	state = State::PROBING;
	_start_probe();
	return OK;
}

void MultiPartDownloader::cancel() {
	if (state == State::IDLE || state == State::FINISHED || state == State::FAILED) {
		return;
	}

	_clear_requests();
	state = State::IDLE;
	emit_signal(SNAME("failed"), TTR("Download cancelled."));
}

uint64_t MultiPartDownloader::get_downloaded_bytes() const {
	uint64_t total = merged_bytes;
	for (int i = 0; i < chunks.size(); i++) {
		total += chunks[i].downloaded;
	}
	if (fallback_http) {
		total += (uint64_t)fallback_http->get_downloaded_bytes();
	}
	return total;
}

void MultiPartDownloader::_start_probe() {
	probe_http = memnew(HTTPRequest);
	probe_http->set_use_threads(true);
	probe_http->set_timeout(30);
	add_child(probe_http);

	Vector<String> headers;
	headers.push_back("User-Agent: Jundot/MultiPartDownloader");
	headers.push_back("Range: bytes=0-0");
	probe_http->connect("request_completed", callable_mp(this, &MultiPartDownloader::_on_probe_completed));
	probe_http->request(url, headers);
}

void MultiPartDownloader::_on_probe_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	if (probe_http) {
		probe_http->cancel_request();
		probe_http->queue_free();
		probe_http = nullptr;
	}

	if (p_result != HTTPRequest::RESULT_SUCCESS || !(p_response_code == 206 || p_response_code == 200)) {
		_start_fallback_download();
		return;
	}

	bool accept_ranges = false;
	uint64_t content_size = 0;
	bool got_size = false;

	for (int i = 0; i < p_headers.size(); i++) {
		const String header = p_headers[i];
		const int sep = header.find(":");
		if (sep < 0) {
			continue;
		}

		const String key = header.substr(0, sep).strip_edges().to_lower();
		const String value = header.substr(sep + 1).strip_edges();
		if (key == "accept-ranges") {
			accept_ranges = value.find("bytes") >= 0;
		} else if (key == "content-range") {
			const int slash = value.find("/");
			if (slash >= 0) {
				const String total_str = value.substr(slash + 1).strip_edges();
				if (total_str.is_valid_int()) {
					content_size = total_str.to_int();
					got_size = true;
				}
			}
		} else if (key == "content-length" && !got_size && p_response_code == 200 && value.is_valid_int()) {
			content_size = value.to_int();
			got_size = true;
		}
	}

	if (!accept_ranges || !got_size || content_size == 0) {
		_start_fallback_download();
		return;
	}

	total_size = content_size;
	const uint64_t min_chunk = 128 * 1024;
	int effective_connections = num_connections;
	if (total_size / (uint64_t)effective_connections < min_chunk) {
		effective_connections = MAX(1, (int)(total_size / min_chunk));
	}
	if (effective_connections <= 1) {
		_start_fallback_download();
		return;
	}

	num_connections = effective_connections;
	_start_multipart_download(total_size);
}

void MultiPartDownloader::_start_multipart_download(uint64_t p_total_size) {
	state = State::DOWNLOADING;
	total_size = p_total_size;

	const uint64_t chunk_size = p_total_size / (uint64_t)num_connections;
	chunks.resize(num_connections);

	for (int i = 0; i < num_connections; i++) {
		Chunk &chunk = chunks.write[i];
		chunk.start = (uint64_t)i * chunk_size;
		chunk.end = (i == num_connections - 1) ? (p_total_size - 1) : ((uint64_t)(i + 1) * chunk_size - 1);
		chunk.downloaded = 0;
		chunk.done = false;
		chunk.part_path = output_path + vformat(".part%d", i);

		chunk.http = memnew(HTTPRequest);
		chunk.http->set_use_threads(true);
		chunk.http->set_download_file(chunk.part_path);
		chunk.http->set_timeout(0);
		add_child(chunk.http);

		Vector<String> headers;
		headers.push_back("User-Agent: Jundot/MultiPartDownloader");
		headers.push_back(vformat("Range: bytes=%llu-%llu", (long long unsigned)chunk.start, (long long unsigned)chunk.end));
		chunk.http->connect("request_completed", callable_mp(this, &MultiPartDownloader::_on_chunk_completed).bind(i));
		const Error err = chunk.http->request(url, headers);
		if (err != OK) {
			_fail(vformat("Failed to start chunk %d request (error %d).", i, err));
			return;
		}
	}

	emit_signal(SNAME("progress"), 0, (int64_t)p_total_size);
}

void MultiPartDownloader::_on_chunk_completed(int p_chunk_index, int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	if (state != State::DOWNLOADING) {
		return;
	}
	ERR_FAIL_INDEX(p_chunk_index, chunks.size());

	Chunk &chunk = chunks.write[p_chunk_index];
	if (p_result == HTTPRequest::RESULT_SUCCESS && (p_response_code == 206 || p_response_code == 200)) {
		chunk.done = true;
		chunk.downloaded = chunk.end - chunk.start + 1;
	} else {
		_fail(vformat("Chunk %d download failed: HTTP %d (result %d).", p_chunk_index, p_response_code, p_result));
		return;
	}

	emit_signal(SNAME("progress"), (int64_t)get_downloaded_bytes(), (int64_t)total_size);
	if (_check_all_chunks_done()) {
		_merge_chunks();
	}
}

bool MultiPartDownloader::_check_all_chunks_done() const {
	for (int i = 0; i < chunks.size(); i++) {
		if (!chunks[i].done) {
			return false;
		}
	}
	return true;
}

void MultiPartDownloader::_merge_chunks() {
	state = State::MERGING;
	Error err = OK;
	Ref<FileAccess> out = FileAccess::open(output_path, FileAccess::WRITE, &err);
	if (out.is_null() || err != OK) {
		_fail(vformat("Failed to open output file: %s (error %d).", output_path, err));
		return;
	}

	Vector<uint8_t> buffer;
	buffer.resize(1024 * 1024);
	for (int i = 0; i < chunks.size(); i++) {
		Ref<FileAccess> part = FileAccess::open(chunks[i].part_path, FileAccess::READ, &err);
		if (part.is_null() || err != OK) {
			_fail(vformat("Failed to open part file: %s.", chunks[i].part_path));
			return;
		}

		while (!part->eof_reached()) {
			const int read = part->get_buffer(buffer.ptrw(), buffer.size());
			if (read > 0) {
				out->store_buffer(buffer.ptr(), read);
				merged_bytes += read;
				emit_signal(SNAME("progress"), (int64_t)get_downloaded_bytes(), (int64_t)total_size);
			}
		}
		part.unref();
		DirAccess::remove_absolute(chunks[i].part_path);
	}

	out->flush();
	state = State::FINISHED;
	emit_signal(SNAME("finished"), output_path);
}

void MultiPartDownloader::_start_fallback_download() {
	state = State::DOWNLOADING;
	fallback_tmp_path = output_path + ".downloading";

	fallback_http = memnew(HTTPRequest);
	fallback_http->set_use_threads(true);
	fallback_http->set_download_file(fallback_tmp_path);
	fallback_http->set_timeout(0);
	add_child(fallback_http);

	Vector<String> headers;
	headers.push_back("User-Agent: Jundot/MultiPartDownloader");
	fallback_http->connect("request_completed", callable_mp(this, &MultiPartDownloader::_on_fallback_completed));
	fallback_http->request(url, headers);
}

void MultiPartDownloader::_on_fallback_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	if (state != State::DOWNLOADING) {
		return;
	}

	if (p_result != HTTPRequest::RESULT_SUCCESS || p_response_code < 200 || p_response_code >= 300) {
		_fail(vformat("Download failed: HTTP %d (result %d).", p_response_code, p_result));
		return;
	}

	if (total_size == 0 && fallback_http) {
		const int64_t body_size = fallback_http->get_body_size();
		total_size = body_size > 0 ? (uint64_t)body_size : (uint64_t)fallback_http->get_downloaded_bytes();
	}

	DirAccess::remove_absolute(output_path);
	Error err = DirAccess::rename_absolute(fallback_tmp_path, output_path);
	if (err != OK) {
		Ref<FileAccess> src = FileAccess::open(fallback_tmp_path, FileAccess::READ);
		Ref<FileAccess> dst = FileAccess::open(output_path, FileAccess::WRITE);
		if (src.is_null() || dst.is_null()) {
			_fail(vformat("Failed to move download result (error %d).", err));
			return;
		}

		Vector<uint8_t> buffer;
		buffer.resize(1024 * 1024);
		while (!src->eof_reached()) {
			const int read = src->get_buffer(buffer.ptrw(), buffer.size());
			if (read > 0) {
				dst->store_buffer(buffer.ptr(), read);
			}
		}
		dst->flush();
		DirAccess::remove_absolute(fallback_tmp_path);
	}

	fallback_tmp_path.clear();
	state = State::FINISHED;
	emit_signal(SNAME("progress"), (int64_t)total_size, (int64_t)total_size);
	emit_signal(SNAME("finished"), output_path);
}

void MultiPartDownloader::_fail(const String &p_reason) {
	_clear_requests();
	state = State::FAILED;
	emit_signal(SNAME("failed"), p_reason);
}
