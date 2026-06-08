/**************************************************************************/
/*  library_jundot_webxr.js                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             JUNDOT ENGINE                               */
/*                        https://jundotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Jundot Engine contributors (see AUTHORS.md). */
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

const JundotWebXR = {
	$JundotWebXR__deps: ['$MainLoop', '$GL', '$JundotRuntime', '$runtimeKeepalivePush', '$runtimeKeepalivePop'],
	$JundotWebXR: {
		gl: null,

		session: null,
		gl_binding: null,
		layer: null,
		space: null,
		frame: null,
		pose: null,
		view_count: 1,
		input_sources: new Array(16),
		touches: new Array(5),
		onsimpleevent: null,

		// Monkey-patch the requestAnimationFrame() used by Emscripten for the main
		// loop, so that we can swap it out for XRSession.requestAnimationFrame()
		// when an XR session is started.
		orig_requestAnimationFrame: null,
		requestAnimationFrame: (callback) => {
			if (JundotWebXR.session && JundotWebXR.space) {
				const onFrame = function (time, frame) {
					JundotWebXR.frame = frame;
					JundotWebXR.pose = frame.getViewerPose(JundotWebXR.space);
					callback(time);
					JundotWebXR.frame = null;
					JundotWebXR.pose = null;
				};
				JundotWebXR.session.requestAnimationFrame(onFrame);
			} else {
				JundotWebXR.orig_requestAnimationFrame(callback);
			}
		},
		monkeyPatchRequestAnimationFrame: (enable) => {
			if (JundotWebXR.orig_requestAnimationFrame === null) {
				JundotWebXR.orig_requestAnimationFrame = MainLoop.requestAnimationFrame;
			}
			MainLoop.requestAnimationFrame = enable
				? JundotWebXR.requestAnimationFrame
				: JundotWebXR.orig_requestAnimationFrame;
		},
		pauseResumeMainLoop: () => {
			// Once both JundotWebXR.session and JundotWebXR.space are set or
			// unset, our monkey-patched requestAnimationFrame() should be
			// enabled or disabled. When using the WebXR API Emulator, this
			// gets picked up automatically, however, in the Oculus Browser
			// on the Quest, we need to pause and resume the main loop.
			MainLoop.pause();
			runtimeKeepalivePush();
			window.setTimeout(function () {
				runtimeKeepalivePop();
				MainLoop.resume();
			}, 0);
		},

		getLayer: () => {
			const new_view_count = (JundotWebXR.pose) ? JundotWebXR.pose.views.length : 1;
			let layer = JundotWebXR.layer;

			// If the view count hasn't changed since creating this layer, then
			// we can simply return it.
			if (layer && JundotWebXR.view_count === new_view_count) {
				return layer;
			}

			if (!JundotWebXR.session || !JundotWebXR.gl_binding || !JundotWebXR.gl_binding.createProjectionLayer) {
				return null;
			}

			const gl = JundotWebXR.gl;

			layer = JundotWebXR.gl_binding.createProjectionLayer({
				textureType: new_view_count > 1 ? 'texture-array' : 'texture',
				colorFormat: gl.RGBA8,
				depthFormat: gl.DEPTH_COMPONENT24,
			});
			JundotWebXR.session.updateRenderState({ layers: [layer] });

			JundotWebXR.layer = layer;
			JundotWebXR.view_count = new_view_count;
			return layer;
		},

		getSubImage: () => {
			if (!JundotWebXR.pose) {
				return null;
			}
			const layer = JundotWebXR.getLayer();
			if (layer === null) {
				return null;
			}

			// Because we always use "texture-array" for multiview and "texture"
			// when there is only 1 view, it should be safe to only grab the
			// subimage for the first view.
			return JundotWebXR.gl_binding.getViewSubImage(layer, JundotWebXR.pose.views[0]);
		},

		getTextureId: (texture) => {
			if (texture.name !== undefined) {
				return texture.name;
			}

			const id = GL.getNewId(GL.textures);
			texture.name = id;
			GL.textures[id] = texture;

			return id;
		},

		addInputSource: (input_source) => {
			let name = -1;
			if (input_source.targetRayMode === 'tracked-pointer' && input_source.handedness === 'left') {
				name = 0;
			} else if (input_source.targetRayMode === 'tracked-pointer' && input_source.handedness === 'right') {
				name = 1;
			} else {
				for (let i = 2; i < 16; i++) {
					if (!JundotWebXR.input_sources[i]) {
						name = i;
						break;
					}
				}
			}
			if (name >= 0) {
				JundotWebXR.input_sources[name] = input_source;
				input_source.name = name;

				// Find a free touch index for screen sources.
				if (input_source.targetRayMode === 'screen') {
					let touch_index = -1;
					for (let i = 0; i < 5; i++) {
						if (!JundotWebXR.touches[i]) {
							touch_index = i;
							break;
						}
					}
					if (touch_index >= 0) {
						JundotWebXR.touches[touch_index] = input_source;
						input_source.touch_index = touch_index;
					}
				}
			}
			return name;
		},

		removeInputSource: (input_source) => {
			if (input_source.name !== undefined) {
				const name = input_source.name;
				if (name >= 0 && name < 16) {
					JundotWebXR.input_sources[name] = null;
				}

				if (input_source.touch_index !== undefined) {
					const touch_index = input_source.touch_index;
					if (touch_index >= 0 && touch_index < 5) {
						JundotWebXR.touches[touch_index] = null;
					}
				}
				return name;
			}
			return -1;
		},

		getInputSourceId: (input_source) => {
			if (input_source !== undefined) {
				return input_source.name;
			}
			return -1;
		},

		getTouchIndex: (input_source) => {
			if (input_source.touch_index !== undefined) {
				return input_source.touch_index;
			}
			return -1;
		},
	},

	jundot_webxr_is_supported__proxy: 'sync',
	jundot_webxr_is_supported__sig: 'i',
	jundot_webxr_is_supported: function () {
		return !!navigator.xr;
	},

	jundot_webxr_is_session_supported__proxy: 'sync',
	jundot_webxr_is_session_supported__sig: 'vii',
	jundot_webxr_is_session_supported: function (p_session_mode, p_callback) {
		const session_mode = JundotRuntime.parseString(p_session_mode);
		const cb = JundotRuntime.get_func(p_callback);
		if (navigator.xr) {
			navigator.xr.isSessionSupported(session_mode).then(function (supported) {
				const c_str = JundotRuntime.allocString(session_mode);
				cb(c_str, supported ? 1 : 0);
				JundotRuntime.free(c_str);
			});
		} else {
			const c_str = JundotRuntime.allocString(session_mode);
			cb(c_str, 0);
			JundotRuntime.free(c_str);
		}
	},

	jundot_webxr_initialize__deps: ['emscripten_webgl_get_current_context'],
	jundot_webxr_initialize__proxy: 'sync',
	jundot_webxr_initialize__sig: 'viiiiiiiii',
	jundot_webxr_initialize: function (p_session_mode, p_required_features, p_optional_features, p_requested_reference_spaces, p_on_session_started, p_on_session_ended, p_on_session_failed, p_on_input_event, p_on_simple_event) {
		JundotWebXR.monkeyPatchRequestAnimationFrame(true);

		const session_mode = JundotRuntime.parseString(p_session_mode);
		const required_features = JundotRuntime.parseString(p_required_features).split(',').map((s) => s.trim()).filter((s) => s !== '');
		const optional_features = JundotRuntime.parseString(p_optional_features).split(',').map((s) => s.trim()).filter((s) => s !== '');
		const requested_reference_space_types = JundotRuntime.parseString(p_requested_reference_spaces).split(',').map((s) => s.trim());
		const onstarted = JundotRuntime.get_func(p_on_session_started);
		const onended = JundotRuntime.get_func(p_on_session_ended);
		const onfailed = JundotRuntime.get_func(p_on_session_failed);
		const oninputevent = JundotRuntime.get_func(p_on_input_event);
		const onsimpleevent = JundotRuntime.get_func(p_on_simple_event);

		const session_init = {};
		if (required_features.length > 0) {
			session_init['requiredFeatures'] = required_features;
		}
		if (optional_features.length > 0) {
			session_init['optionalFeatures'] = optional_features;
		}

		navigator.xr.requestSession(session_mode, session_init).then(function (session) {
			JundotWebXR.session = session;

			session.addEventListener('end', function (evt) {
				onended();
			});

			session.addEventListener('inputsourceschange', function (evt) {
				evt.added.forEach(JundotWebXR.addInputSource);
				evt.removed.forEach(JundotWebXR.removeInputSource);
			});

			['selectstart', 'selectend', 'squeezestart', 'squeezeend'].forEach((input_event, index) => {
				session.addEventListener(input_event, function (evt) {
					// Since this happens in-between normal frames, we need to
					// grab the frame from the event in order to get poses for
					// the input sources.
					JundotWebXR.frame = evt.frame;
					oninputevent(index, JundotWebXR.getInputSourceId(evt.inputSource));
					JundotWebXR.frame = null;
				});
			});

			session.addEventListener('visibilitychange', function (evt) {
				const c_str = JundotRuntime.allocString('visibility_state_changed');
				onsimpleevent(c_str);
				JundotRuntime.free(c_str);
			});

			// Store onsimpleevent so we can use it later.
			JundotWebXR.onsimpleevent = onsimpleevent;

			const gl_context_handle = _emscripten_webgl_get_current_context();
			const gl = GL.getContext(gl_context_handle).GLctx;
			JundotWebXR.gl = gl;

			gl.makeXRCompatible().then(function () {
				const throwNoWebXRLayersError = () => {
					throw new Error('This browser doesn\'t support WebXR Layers (which Jundot requires) nor is the polyfill in use. If you are the developer of this application, please consider including the polyfill.');
				};

				try {
					JundotWebXR.gl_binding = new XRWebGLBinding(session, gl);
				} catch (error) {
					// We'll end up here for browsers that don't have XRWebGLBinding at all, or if the browser does support WebXR Layers,
					// but is using the WebXR polyfill, so calling native XRWebGLBinding with the polyfilled XRSession won't work.
					throwNoWebXRLayersError();
				}

				if (!JundotWebXR.gl_binding.createProjectionLayer) {
					// On other browsers, XRWebGLBinding exists and works, but it doesn't support creating projection layers (which is
					// contrary to the spec, which says this MUST be supported) and so the polyfill is required.
					throwNoWebXRLayersError();
				}

				// This will trigger the layer to get created.
				const layer = JundotWebXR.getLayer();
				if (!layer) {
					throw new Error('Unable to create WebXR Layer.');
				}

				function onReferenceSpaceSuccess(reference_space, reference_space_type) {
					JundotWebXR.space = reference_space;

					// Using reference_space.addEventListener() crashes when
					// using the polyfill with the WebXR Emulator extension,
					// so we set the event property instead.
					reference_space.onreset = function (evt) {
						const c_str = JundotRuntime.allocString('reference_space_reset');
						onsimpleevent(c_str);
						JundotRuntime.free(c_str);
					};

					// Now that both JundotWebXR.session and JundotWebXR.space are
					// set, we need to pause and resume the main loop for the XR
					// main loop to kick in.
					JundotWebXR.pauseResumeMainLoop();

					// Call in setTimeout() so that errors in the onstarted()
					// callback don't bubble up here and cause Jundot to try the
					// next reference space.
					window.setTimeout(function () {
						const reference_space_c_str = JundotRuntime.allocString(reference_space_type);
						const enabled_features = 'enabledFeatures' in session ? Array.from(session.enabledFeatures) : [];
						const enabled_features_c_str = JundotRuntime.allocString(enabled_features.join(','));
						const environment_blend_mode = 'environmentBlendMode' in session ? session.environmentBlendMode : '';
						const environment_blend_mode_c_str = JundotRuntime.allocString(environment_blend_mode);
						onstarted(reference_space_c_str, enabled_features_c_str, environment_blend_mode_c_str);
						JundotRuntime.free(reference_space_c_str);
						JundotRuntime.free(enabled_features_c_str);
						JundotRuntime.free(environment_blend_mode_c_str);
					}, 0);
				}

				function requestReferenceSpace() {
					const reference_space_type = requested_reference_space_types.shift();
					session.requestReferenceSpace(reference_space_type)
						.then((refSpace) => {
							onReferenceSpaceSuccess(refSpace, reference_space_type);
						})
						.catch(() => {
							if (requested_reference_space_types.length === 0) {
								const c_str = JundotRuntime.allocString('Unable to get any of the requested reference space types');
								onfailed(c_str);
								JundotRuntime.free(c_str);
							} else {
								requestReferenceSpace();
							}
						});
				}

				requestReferenceSpace();
			}).catch(function (error) {
				const c_str = JundotRuntime.allocString(`Unable to make WebGL context compatible with WebXR: ${error}`);
				onfailed(c_str);
				JundotRuntime.free(c_str);
			});
		}).catch(function (error) {
			const c_str = JundotRuntime.allocString(`Unable to start session: ${error}`);
			onfailed(c_str);
			JundotRuntime.free(c_str);
		});
	},

	jundot_webxr_uninitialize__proxy: 'sync',
	jundot_webxr_uninitialize__sig: 'v',
	jundot_webxr_uninitialize: function () {
		if (JundotWebXR.session) {
			JundotWebXR.session.end()
				// Prevent exception when session has already ended.
				.catch((e) => { });
		}

		JundotWebXR.session = null;
		JundotWebXR.gl_binding = null;
		JundotWebXR.layer = null;
		JundotWebXR.space = null;
		JundotWebXR.frame = null;
		JundotWebXR.pose = null;
		JundotWebXR.view_count = 1;
		JundotWebXR.input_sources = new Array(16);
		JundotWebXR.touches = new Array(5);
		JundotWebXR.onsimpleevent = null;

		// Disable the monkey-patched window.requestAnimationFrame() and
		// pause/restart the main loop to activate it on all platforms.
		JundotWebXR.monkeyPatchRequestAnimationFrame(false);
		JundotWebXR.pauseResumeMainLoop();
	},

	jundot_webxr_get_view_count__proxy: 'sync',
	jundot_webxr_get_view_count__sig: 'i',
	jundot_webxr_get_view_count: function () {
		if (!JundotWebXR.session || !JundotWebXR.pose) {
			return 1;
		}
		const view_count = JundotWebXR.pose.views.length;
		return view_count > 0 ? view_count : 1;
	},

	jundot_webxr_get_render_target_size__proxy: 'sync',
	jundot_webxr_get_render_target_size__sig: 'ii',
	jundot_webxr_get_render_target_size: function (r_size) {
		const subimage = JundotWebXR.getSubImage();
		if (subimage === null) {
			return false;
		}

		JundotRuntime.setHeapValue(r_size + 0, subimage.viewport.width, 'i32');
		JundotRuntime.setHeapValue(r_size + 4, subimage.viewport.height, 'i32');

		return true;
	},

	jundot_webxr_get_transform_for_view__proxy: 'sync',
	jundot_webxr_get_transform_for_view__sig: 'iii',
	jundot_webxr_get_transform_for_view: function (p_view, r_transform) {
		if (!JundotWebXR.session || !JundotWebXR.pose) {
			return false;
		}

		const views = JundotWebXR.pose.views;
		let matrix;
		if (p_view >= 0) {
			matrix = views[p_view].transform.matrix;
		} else {
			// For -1 (or any other negative value) return the HMD transform.
			matrix = JundotWebXR.pose.transform.matrix;
		}

		for (let i = 0; i < 16; i++) {
			JundotRuntime.setHeapValue(r_transform + (i * 4), matrix[i], 'float');
		}

		return true;
	},

	jundot_webxr_get_projection_for_view__proxy: 'sync',
	jundot_webxr_get_projection_for_view__sig: 'iii',
	jundot_webxr_get_projection_for_view: function (p_view, r_transform) {
		if (!JundotWebXR.session || !JundotWebXR.pose) {
			return false;
		}

		const matrix = JundotWebXR.pose.views[p_view].projectionMatrix;
		for (let i = 0; i < 16; i++) {
			JundotRuntime.setHeapValue(r_transform + (i * 4), matrix[i], 'float');
		}

		return true;
	},

	jundot_webxr_get_color_texture__proxy: 'sync',
	jundot_webxr_get_color_texture__sig: 'i',
	jundot_webxr_get_color_texture: function () {
		const subimage = JundotWebXR.getSubImage();
		if (subimage === null) {
			return 0;
		}
		return JundotWebXR.getTextureId(subimage.colorTexture);
	},

	jundot_webxr_get_depth_texture__proxy: 'sync',
	jundot_webxr_get_depth_texture__sig: 'i',
	jundot_webxr_get_depth_texture: function () {
		const subimage = JundotWebXR.getSubImage();
		if (subimage === null) {
			return 0;
		}
		if (!subimage.depthStencilTexture) {
			return 0;
		}
		return JundotWebXR.getTextureId(subimage.depthStencilTexture);
	},

	jundot_webxr_get_velocity_texture__proxy: 'sync',
	jundot_webxr_get_velocity_texture__sig: 'i',
	jundot_webxr_get_velocity_texture: function () {
		const subimage = JundotWebXR.getSubImage();
		if (subimage === null) {
			return 0;
		}
		if (!subimage.motionVectorTexture) {
			return 0;
		}
		return JundotWebXR.getTextureId(subimage.motionVectorTexture);
	},

	jundot_webxr_update_input_source__proxy: 'sync',
	jundot_webxr_update_input_source__sig: 'iiiiiiiiiiiiiii',
	jundot_webxr_update_input_source: function (p_input_source_id, r_target_pose, r_target_ray_mode, r_touch_index, r_has_grip_pose, r_grip_pose, r_has_standard_mapping, r_button_count, r_buttons, r_axes_count, r_axes, r_has_hand_data, r_hand_joints, r_hand_radii) {
		if (!JundotWebXR.session || !JundotWebXR.frame) {
			return 0;
		}

		if (p_input_source_id < 0 || p_input_source_id >= JundotWebXR.input_sources.length || !JundotWebXR.input_sources[p_input_source_id]) {
			return false;
		}

		const input_source = JundotWebXR.input_sources[p_input_source_id];
		const frame = JundotWebXR.frame;
		const space = JundotWebXR.space;

		// Target pose.
		const target_pose = frame.getPose(input_source.targetRaySpace, space);
		if (!target_pose) {
			// This can mean that the controller lost tracking.
			return false;
		}
		const target_pose_matrix = target_pose.transform.matrix;
		for (let i = 0; i < 16; i++) {
			JundotRuntime.setHeapValue(r_target_pose + (i * 4), target_pose_matrix[i], 'float');
		}

		// Target ray mode.
		let target_ray_mode = 0;
		switch (input_source.targetRayMode) {
		case 'gaze':
			target_ray_mode = 1;
			break;

		case 'tracked-pointer':
			target_ray_mode = 2;
			break;

		case 'screen':
			target_ray_mode = 3;
			break;

		default:
		}
		JundotRuntime.setHeapValue(r_target_ray_mode, target_ray_mode, 'i32');

		// Touch index.
		JundotRuntime.setHeapValue(r_touch_index, JundotWebXR.getTouchIndex(input_source), 'i32');

		// Grip pose.
		let has_grip_pose = false;
		if (input_source.gripSpace) {
			const grip_pose = frame.getPose(input_source.gripSpace, space);
			if (grip_pose) {
				const grip_pose_matrix = grip_pose.transform.matrix;
				for (let i = 0; i < 16; i++) {
					JundotRuntime.setHeapValue(r_grip_pose + (i * 4), grip_pose_matrix[i], 'float');
				}
				has_grip_pose = true;
			}
		}
		JundotRuntime.setHeapValue(r_has_grip_pose, has_grip_pose ? 1 : 0, 'i32');

		// Gamepad data (mapping, buttons and axes).
		let has_standard_mapping = false;
		let button_count = 0;
		let axes_count = 0;
		if (input_source.gamepad) {
			if (input_source.gamepad.mapping === 'xr-standard') {
				has_standard_mapping = true;
			}

			button_count = Math.min(input_source.gamepad.buttons.length, 10);
			for (let i = 0; i < button_count; i++) {
				JundotRuntime.setHeapValue(r_buttons + (i * 4), input_source.gamepad.buttons[i].value, 'float');
			}

			axes_count = Math.min(input_source.gamepad.axes.length, 10);
			for (let i = 0; i < axes_count; i++) {
				JundotRuntime.setHeapValue(r_axes + (i * 4), input_source.gamepad.axes[i], 'float');
			}
		}
		JundotRuntime.setHeapValue(r_has_standard_mapping, has_standard_mapping ? 1 : 0, 'i32');
		JundotRuntime.setHeapValue(r_button_count, button_count, 'i32');
		JundotRuntime.setHeapValue(r_axes_count, axes_count, 'i32');

		// Hand tracking data.
		let has_hand_data = false;
		if (input_source.hand && r_hand_joints !== 0 && r_hand_radii !== 0) {
			const hand_joint_array = new Float32Array(25 * 16);
			const hand_radii_array = new Float32Array(25);
			if (frame.fillPoses(input_source.hand.values(), space, hand_joint_array) && frame.fillJointRadii(input_source.hand.values(), hand_radii_array)) {
				JundotRuntime.heapCopy(HEAPF32, hand_joint_array, r_hand_joints);
				JundotRuntime.heapCopy(HEAPF32, hand_radii_array, r_hand_radii);
				has_hand_data = true;
			}
		}
		JundotRuntime.setHeapValue(r_has_hand_data, has_hand_data ? 1 : 0, 'i32');

		return true;
	},

	jundot_webxr_get_visibility_state__proxy: 'sync',
	jundot_webxr_get_visibility_state__sig: 'i',
	jundot_webxr_get_visibility_state: function () {
		if (!JundotWebXR.session || !JundotWebXR.session.visibilityState) {
			return 0;
		}

		return JundotRuntime.allocString(JundotWebXR.session.visibilityState);
	},

	jundot_webxr_get_bounds_geometry__proxy: 'sync',
	jundot_webxr_get_bounds_geometry__sig: 'ii',
	jundot_webxr_get_bounds_geometry: function (r_points) {
		if (!JundotWebXR.space || !JundotWebXR.space.boundsGeometry) {
			return 0;
		}

		const point_count = JundotWebXR.space.boundsGeometry.length;
		if (point_count === 0) {
			return 0;
		}

		const buf = JundotRuntime.malloc(point_count * 3 * 4);
		for (let i = 0; i < point_count; i++) {
			const point = JundotWebXR.space.boundsGeometry[i];
			JundotRuntime.setHeapValue(buf + ((i * 3) + 0) * 4, point.x, 'float');
			JundotRuntime.setHeapValue(buf + ((i * 3) + 1) * 4, point.y, 'float');
			JundotRuntime.setHeapValue(buf + ((i * 3) + 2) * 4, point.z, 'float');
		}
		JundotRuntime.setHeapValue(r_points, buf, 'i32');

		return point_count;
	},

	jundot_webxr_get_frame_rate__proxy: 'sync',
	jundot_webxr_get_frame_rate__sig: 'i',
	jundot_webxr_get_frame_rate: function () {
		if (!JundotWebXR.session || JundotWebXR.session.frameRate === undefined) {
			return 0;
		}
		return JundotWebXR.session.frameRate;
	},

	jundot_webxr_update_target_frame_rate__proxy: 'sync',
	jundot_webxr_update_target_frame_rate__sig: 'vi',
	jundot_webxr_update_target_frame_rate: function (p_frame_rate) {
		if (!JundotWebXR.session || JundotWebXR.session.updateTargetFrameRate === undefined) {
			return;
		}

		JundotWebXR.session.updateTargetFrameRate(p_frame_rate).then(() => {
			const c_str = JundotRuntime.allocString('display_refresh_rate_changed');
			JundotWebXR.onsimpleevent(c_str);
			JundotRuntime.free(c_str);
		});
	},

	jundot_webxr_get_supported_frame_rates__proxy: 'sync',
	jundot_webxr_get_supported_frame_rates__sig: 'ii',
	jundot_webxr_get_supported_frame_rates: function (r_frame_rates) {
		if (!JundotWebXR.session || JundotWebXR.session.supportedFrameRates === undefined) {
			return 0;
		}

		const frame_rate_count = JundotWebXR.session.supportedFrameRates.length;
		if (frame_rate_count === 0) {
			return 0;
		}

		const buf = JundotRuntime.malloc(frame_rate_count * 4);
		for (let i = 0; i < frame_rate_count; i++) {
			JundotRuntime.setHeapValue(buf + (i * 4), JundotWebXR.session.supportedFrameRates[i], 'float');
		}
		JundotRuntime.setHeapValue(r_frame_rates, buf, 'i32');

		return frame_rate_count;
	},

};

autoAddDeps(JundotWebXR, '$JundotWebXR');
mergeInto(LibraryManager.library, JundotWebXR);
