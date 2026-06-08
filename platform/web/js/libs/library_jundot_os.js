/**************************************************************************/
/*  library_jundot_os.js                                                   */
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

const IDHandler = {
	$IDHandler: {
		_last_id: 0,
		_references: {},

		get: function (p_id) {
			return IDHandler._references[p_id];
		},

		add: function (p_data) {
			const id = ++IDHandler._last_id;
			IDHandler._references[id] = p_data;
			return id;
		},

		remove: function (p_id) {
			delete IDHandler._references[p_id];
		},
	},
};

autoAddDeps(IDHandler, '$IDHandler');
mergeInto(LibraryManager.library, IDHandler);

const JundotConfig = {
	$JundotConfig__postset: 'Module["initConfig"] = JundotConfig.init_config;',
	$JundotConfig__deps: ['$JundotRuntime'],
	$JundotConfig: {
		canvas: null,
		locale: 'en',
		canvas_resize_policy: 2, // Adaptive
		virtual_keyboard: false,
		persistent_drops: false,
		jundot_pool_size: 4,
		on_execute: null,
		on_exit: null,

		init_config: function (p_opts) {
			JundotConfig.canvas_resize_policy = p_opts['canvasResizePolicy'];
			JundotConfig.canvas = p_opts['canvas'];
			JundotConfig.locale = p_opts['locale'] || JundotConfig.locale;
			JundotConfig.virtual_keyboard = p_opts['virtualKeyboard'];
			JundotConfig.persistent_drops = !!p_opts['persistentDrops'];
			JundotConfig.jundot_pool_size = p_opts['jundotPoolSize'];
			JundotConfig.on_execute = p_opts['onExecute'];
			JundotConfig.on_exit = p_opts['onExit'];
			if (p_opts['focusCanvas']) {
				JundotConfig.canvas.focus();
			}
		},

		locate_file: function (file) {
			return Module['locateFile'](file);
		},
		clear: function () {
			JundotConfig.canvas = null;
			JundotConfig.locale = 'en';
			JundotConfig.canvas_resize_policy = 2;
			JundotConfig.virtual_keyboard = false;
			JundotConfig.persistent_drops = false;
			JundotConfig.on_execute = null;
			JundotConfig.on_exit = null;
		},
	},

	jundot_js_config_canvas_id_get__proxy: 'sync',
	jundot_js_config_canvas_id_get__sig: 'vii',
	jundot_js_config_canvas_id_get: function (p_ptr, p_ptr_max) {
		JundotRuntime.stringToHeap(`#${JundotConfig.canvas.id}`, p_ptr, p_ptr_max);
	},

	jundot_js_config_locale_get__proxy: 'sync',
	jundot_js_config_locale_get__sig: 'vii',
	jundot_js_config_locale_get: function (p_ptr, p_ptr_max) {
		JundotRuntime.stringToHeap(JundotConfig.locale, p_ptr, p_ptr_max);
	},
};

autoAddDeps(JundotConfig, '$JundotConfig');
mergeInto(LibraryManager.library, JundotConfig);

const JundotFS = {
	$JundotFS__deps: ['$FS', '$IDBFS', '$JundotRuntime'],
	$JundotFS__postset: [
		'Module["initFS"] = JundotFS.init;',
		'Module["copyToFS"] = JundotFS.copy_to_fs;',
	].join(''),
	$JundotFS: {
		// ERRNO_CODES works every odd version of emscripten, but this will break too eventually.
		ENOENT: 44,
		_idbfs: false,
		_syncing: false,
		_mount_points: [],

		is_persistent: function () {
			return JundotFS._idbfs ? 1 : 0;
		},

		// Initialize jundot file system, setting up persistent paths.
		// Returns a promise that resolves when the FS is ready.
		// We keep track of mount_points, so that we can properly close the IDBFS
		// since emscripten is not doing it by itself. (emscripten GH#12516).
		init: function (persistentPaths) {
			JundotFS._idbfs = false;
			if (!Array.isArray(persistentPaths)) {
				return Promise.reject(new Error('Persistent paths must be an array'));
			}
			if (!persistentPaths.length) {
				return Promise.resolve();
			}
			JundotFS._mount_points = persistentPaths.slice();

			function createRecursive(dir) {
				try {
					FS.stat(dir);
				} catch (e) {
					if (e.errno !== JundotFS.ENOENT) {
						// Let mkdirTree throw in case, we cannot trust the above check.
						JundotRuntime.error(e);
					}
					FS.mkdirTree(dir);
				}
			}

			JundotFS._mount_points.forEach(function (path) {
				createRecursive(path);
				FS.mount(IDBFS, {}, path);
			});
			return new Promise(function (resolve, reject) {
				FS.syncfs(true, function (err) {
					if (err) {
						JundotFS._mount_points = [];
						JundotFS._idbfs = false;
						JundotRuntime.print(`IndexedDB not available: ${err.message}`);
					} else {
						JundotFS._idbfs = true;
					}
					resolve(err);
				});
			});
		},

		// Deinit jundot file system, making sure to unmount file systems, and close IDBFS(s).
		deinit: function () {
			JundotFS._mount_points.forEach(function (path) {
				try {
					FS.unmount(path);
				} catch (e) {
					JundotRuntime.print('Already unmounted', e);
				}
				if (JundotFS._idbfs && IDBFS.dbs[path]) {
					IDBFS.dbs[path].close();
					delete IDBFS.dbs[path];
				}
			});
			JundotFS._mount_points = [];
			JundotFS._idbfs = false;
			JundotFS._syncing = false;
		},

		sync: function () {
			if (JundotFS._syncing) {
				JundotRuntime.error('Already syncing!');
				return Promise.resolve();
			}
			JundotFS._syncing = true;
			return new Promise(function (resolve, reject) {
				FS.syncfs(false, function (error) {
					if (error) {
						JundotRuntime.error(`Failed to save IDB file system: ${error.message}`);
					}
					JundotFS._syncing = false;
					resolve(error);
				});
			});
		},

		// Copies a buffer to the internal file system. Creating directories recursively.
		copy_to_fs: function (path, buffer) {
			const idx = path.lastIndexOf('/');
			let dir = '/';
			if (idx > 0) {
				dir = path.slice(0, idx);
			}
			try {
				FS.stat(dir);
			} catch (e) {
				if (e.errno !== JundotFS.ENOENT) {
					// Let mkdirTree throw in case, we cannot trust the above check.
					JundotRuntime.error(e);
				}
				FS.mkdirTree(dir);
			}
			FS.writeFile(path, new Uint8Array(buffer));
		},
	},
};
mergeInto(LibraryManager.library, JundotFS);

const JundotOS = {
	$JundotOS__deps: ['$JundotRuntime', '$JundotConfig', '$JundotFS'],
	$JundotOS__postset: [
		'Module["request_quit"] = function() { JundotOS.request_quit() };',
		'Module["onExit"] = JundotOS.cleanup;',
		'JundotOS._fs_sync_promise = Promise.resolve();',
	].join(''),
	$JundotOS: {
		request_quit: function () {},
		_async_cbs: [],
		_fs_sync_promise: null,

		atexit: function (p_promise_cb) {
			JundotOS._async_cbs.push(p_promise_cb);
		},

		cleanup: function (exit_code) {
			const cb = JundotConfig.on_exit;
			JundotFS.deinit();
			JundotConfig.clear();
			if (cb) {
				cb(exit_code);
			}
		},

		finish_async: function (callback) {
			JundotOS._fs_sync_promise.then(function (err) {
				const promises = [];
				JundotOS._async_cbs.forEach(function (cb) {
					promises.push(new Promise(cb));
				});
				return Promise.all(promises);
			}).then(function () {
				return JundotFS.sync(); // Final FS sync.
			}).then(function (err) {
				// Always deferred.
				setTimeout(function () {
					callback();
				}, 0);
			});
		},
	},

	jundot_js_os_finish_async__proxy: 'sync',
	jundot_js_os_finish_async__sig: 'vi',
	jundot_js_os_finish_async: function (p_callback) {
		const func = JundotRuntime.get_func(p_callback);
		JundotOS.finish_async(func);
	},

	jundot_js_os_request_quit_cb__proxy: 'sync',
	jundot_js_os_request_quit_cb__sig: 'vi',
	jundot_js_os_request_quit_cb: function (p_callback) {
		JundotOS.request_quit = JundotRuntime.get_func(p_callback);
	},

	jundot_js_os_fs_is_persistent__proxy: 'sync',
	jundot_js_os_fs_is_persistent__sig: 'i',
	jundot_js_os_fs_is_persistent: function () {
		return JundotFS.is_persistent();
	},

	jundot_js_os_fs_sync__proxy: 'sync',
	jundot_js_os_fs_sync__sig: 'vi',
	jundot_js_os_fs_sync: function (callback) {
		const func = JundotRuntime.get_func(callback);
		JundotOS._fs_sync_promise = JundotFS.sync();
		JundotOS._fs_sync_promise.then(function (err) {
			func();
		});
	},

	jundot_js_os_has_feature__proxy: 'sync',
	jundot_js_os_has_feature__sig: 'ii',
	jundot_js_os_has_feature: function (p_ftr) {
		const ftr = JundotRuntime.parseString(p_ftr);
		const ua = navigator.userAgent;
		if (ftr === 'web_macos') {
			return (ua.indexOf('Mac') !== -1) ? 1 : 0;
		}
		if (ftr === 'web_windows') {
			return (ua.indexOf('Windows') !== -1) ? 1 : 0;
		}
		if (ftr === 'web_android') {
			return (ua.indexOf('Android') !== -1) ? 1 : 0;
		}
		if (ftr === 'web_ios') {
			return ((ua.indexOf('iPhone') !== -1) || (ua.indexOf('iPad') !== -1) || (ua.indexOf('iPod') !== -1)) ? 1 : 0;
		}
		if (ftr === 'web_linuxbsd') {
			return ((ua.indexOf('CrOS') !== -1) || (ua.indexOf('BSD') !== -1) || (ua.indexOf('Linux') !== -1) || (ua.indexOf('X11') !== -1)) ? 1 : 0;
		}
		return 0;
	},

	jundot_js_os_execute__proxy: 'sync',
	jundot_js_os_execute__sig: 'ii',
	jundot_js_os_execute: function (p_json) {
		const json_args = JundotRuntime.parseString(p_json);
		const args = JSON.parse(json_args);
		if (JundotConfig.on_execute) {
			JundotConfig.on_execute(args);
			return 0;
		}
		return 1;
	},

	jundot_js_os_shell_open__proxy: 'sync',
	jundot_js_os_shell_open__sig: 'vi',
	jundot_js_os_shell_open: function (p_uri) {
		window.open(JundotRuntime.parseString(p_uri), '_blank');
	},

	jundot_js_os_hw_concurrency_get__proxy: 'sync',
	jundot_js_os_hw_concurrency_get__sig: 'i',
	jundot_js_os_hw_concurrency_get: function () {
		// TODO Jundot core needs fixing to avoid spawning too many threads (> 24).
		const concurrency = navigator.hardwareConcurrency || 1;
		return concurrency < 2 ? concurrency : 2;
	},

	jundot_js_os_thread_pool_size_get__proxy: 'sync',
	jundot_js_os_thread_pool_size_get__sig: 'i',
	jundot_js_os_thread_pool_size_get: function () {
		if (typeof PThread === 'undefined') {
			// Threads aren't supported, so default to `1`.
			return 1;
		}

		return JundotConfig.jundot_pool_size;
	},

	jundot_js_os_download_buffer__proxy: 'sync',
	jundot_js_os_download_buffer__sig: 'viiii',
	jundot_js_os_download_buffer: function (p_ptr, p_size, p_name, p_mime) {
		const buf = JundotRuntime.heapSlice(HEAP8, p_ptr, p_size);
		const name = JundotRuntime.parseString(p_name);
		const mime = JundotRuntime.parseString(p_mime);
		const blob = new Blob([buf], { type: mime });
		const url = window.URL.createObjectURL(blob);
		const a = document.createElement('a');
		a.href = url;
		a.download = name;
		a.style.display = 'none';
		document.body.appendChild(a);
		a.click();
		a.remove();
		window.URL.revokeObjectURL(url);
	},
};

autoAddDeps(JundotOS, '$JundotOS');
mergeInto(LibraryManager.library, JundotOS);

/*
 * Jundot event listeners.
 * Keeps track of registered event listeners so it can remove them on shutdown.
 */
const JundotEventListeners = {
	$JundotEventListeners__deps: ['$JundotOS'],
	$JundotEventListeners__postset: 'JundotOS.atexit(function(resolve, reject) { JundotEventListeners.clear(); resolve(); });',
	$JundotEventListeners: {
		handlers: [],

		has: function (target, event, method, capture) {
			return JundotEventListeners.handlers.findIndex(function (e) {
				return e.target === target && e.event === event && e.method === method && e.capture === capture;
			}) !== -1;
		},

		add: function (target, event, method, capture) {
			if (JundotEventListeners.has(target, event, method, capture)) {
				return;
			}
			function Handler(p_target, p_event, p_method, p_capture) {
				this.target = p_target;
				this.event = p_event;
				this.method = p_method;
				this.capture = p_capture;
			}
			JundotEventListeners.handlers.push(new Handler(target, event, method, capture));
			target.addEventListener(event, method, capture);
		},

		clear: function () {
			JundotEventListeners.handlers.forEach(function (h) {
				h.target.removeEventListener(h.event, h.method, h.capture);
			});
			JundotEventListeners.handlers.length = 0;
		},
	},
};
mergeInto(LibraryManager.library, JundotEventListeners);

const JundotPWA = {

	$JundotPWA__deps: ['$JundotRuntime', '$JundotEventListeners'],
	$JundotPWA: {
		hasUpdate: false,

		updateState: function (cb, reg) {
			if (!reg) {
				return;
			}
			if (!reg.active) {
				return;
			}
			if (reg.waiting) {
				JundotPWA.hasUpdate = true;
				cb();
			}
			JundotEventListeners.add(reg, 'updatefound', function () {
				const installing = reg.installing;
				JundotEventListeners.add(installing, 'statechange', function () {
					if (installing.state === 'installed') {
						JundotPWA.hasUpdate = true;
						cb();
					}
				});
			});
		},
	},

	jundot_js_pwa_cb__proxy: 'sync',
	jundot_js_pwa_cb__sig: 'vi',
	jundot_js_pwa_cb: function (p_update_cb) {
		if ('serviceWorker' in navigator) {
			try {
				const cb = JundotRuntime.get_func(p_update_cb);
				navigator.serviceWorker.getRegistration().then(JundotPWA.updateState.bind(null, cb));
			} catch (e) {
				JundotRuntime.error('Failed to assign PWA callback', e);
			}
		}
	},

	jundot_js_pwa_update__proxy: 'sync',
	jundot_js_pwa_update__sig: 'i',
	jundot_js_pwa_update: function () {
		if ('serviceWorker' in navigator && JundotPWA.hasUpdate) {
			try {
				navigator.serviceWorker.getRegistration().then(function (reg) {
					if (!reg || !reg.waiting) {
						return;
					}
					reg.waiting.postMessage('update');
				});
			} catch (e) {
				JundotRuntime.error(e);
				return 1;
			}
			return 0;
		}
		return 1;
	},
};

autoAddDeps(JundotPWA, '$JundotPWA');
mergeInto(LibraryManager.library, JundotPWA);
