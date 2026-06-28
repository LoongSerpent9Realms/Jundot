/**************************************************************************/
/*  JundotLib.java                                                         */
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

package org.jundotengine.jundot;

import org.jundotengine.jundot.gl.JundotRenderer;
import org.jundotengine.jundot.io.directory.DirectoryAccessHandler;
import org.jundotengine.jundot.io.file.FileAccessHandler;
import org.jundotengine.jundot.nativeapi.JundotNativeBridge;
import org.jundotengine.jundot.tts.JundotTTS;
import org.jundotengine.jundot.utils.JundotNetUtils;
import org.jundotengine.jundot.variant.Callable;

import android.app.Activity;
import android.content.res.AssetManager;
import android.hardware.SensorEvent;
import android.view.Surface;

import javax.microedition.khronos.opengles.GL10;

/**
 * Wrapper for native library
 */
public class JundotLib {
	static {
		System.loadLibrary("jundot_android");
	}

	/**
	 * Invoked on the main thread to initialize Jundot native layer.
	 */
	public static native boolean initialize(
			JundotNativeBridge nativeBridge,
			AssetManager assetManager,
			JundotIO jundotIO,
			JundotNetUtils netUtils,
			DirectoryAccessHandler directoryAccessHandler,
			FileAccessHandler fileAccessHandler,
			boolean useApkExpansion);

	/**
	 * Invoked on the main thread to clean up Jundot native layer.
	 * @see androidx.fragment.app.Fragment#onDestroy()
	 */
	public static native void ondestroy();

	/**
	 * Invoked on the GL thread to complete setup for the Jundot native layer logic.
	 * @param p_cmdline Command line arguments used to configure Jundot native layer components.
	 */
	public static native boolean setup(String[] p_cmdline, JundotTTS tts);

	/**
	 * Invoked on the GL thread when the underlying Android surface has changed size.
	 * @param p_surface
	 * @param p_width
	 * @param p_height
	 * @see org.jundotengine.jundot.gl.GLSurfaceView.Renderer#onSurfaceChanged(GL10, int, int)
	 */
	public static native void resize(Surface p_surface, int p_width, int p_height);

	/**
	 * Invoked on the render thread when the underlying Android surface is created or recreated.
	 * @param p_surface
	 */
	public static native void newcontext(Surface p_surface);

	/**
	 * Forward {@link Activity#onBackPressed()} event.
	 */
	public static native void back();

	/**
	 * Invoked on the GL thread to draw the current frame.
	 * @see org.jundotengine.jundot.gl.GLSurfaceView.Renderer#onDrawFrame(GL10)
	 */
	public static native boolean step();

	/**
	 * TTS callback.
	 */
	public static native void ttsCallback(int event, long id, int pos);

	/**
	 * Forward touch events.
	 */
	public static native void dispatchTouchEvent(int event, int pointer, int pointerCount, float[] positions, boolean doubleTap);

	/**
	 * Dispatch mouse events
	 */
	public static native void dispatchMouseEvent(int event, int buttonMask, float x, float y, float deltaX, float deltaY, boolean doubleClick, boolean sourceMouseRelative, float pressure, float tiltX, float tiltY);

	public static native void magnify(float x, float y, float factor);

	public static native void pan(float x, float y, float deltaX, float deltaY);

	/**
	 * Forward accelerometer sensor events.
	 * @see android.hardware.SensorEventListener#onSensorChanged(SensorEvent)
	 */
	public static native void accelerometer(float x, float y, float z);

	/**
	 * Forward gravity sensor events.
	 * @see android.hardware.SensorEventListener#onSensorChanged(SensorEvent)
	 */
	public static native void gravity(float x, float y, float z);

	/**
	 * Forward magnetometer sensor events.
	 * @see android.hardware.SensorEventListener#onSensorChanged(SensorEvent)
	 */
	public static native void magnetometer(float x, float y, float z);

	/**
	 * Forward gyroscope sensor events.
	 * @see android.hardware.SensorEventListener#onSensorChanged(SensorEvent)
	 */
	public static native void gyroscope(float x, float y, float z);

	/**
	 * Forward regular key events.
	 */
	public static native void key(int p_physical_keycode, int p_unicode, int p_key_label, boolean p_pressed, boolean p_echo);

	/**
	 * Forward game device's key events.
	 */
	public static native void joybutton(int p_device, int p_but, boolean p_pressed);

	/**
	 * Forward joystick devices axis motion events.
	 */
	public static native void joyaxis(int p_device, int p_axis, float p_value);

	/**
	 * Forward joystick devices hat motion events.
	 */
	public static native void joyhat(int p_device, int p_hat_x, int p_hat_y);

	/**
	 * Fires when a joystick device is added or removed.
	 */
	public static native void joyconnectionchanged(int p_device, boolean p_connected, String p_name);

	/**
	 * Invoked when the Android app resumes.
	 * @see androidx.fragment.app.Fragment#onResume()
	 */
	public static native void focusin();

	/**
	 * Invoked when the Android app pauses.
	 * @see androidx.fragment.app.Fragment#onPause()
	 */
	public static native void focusout();

	/**
	 * Used to access Jundot global properties.
	 * @param p_key Property key
	 * @return String value of the property
	 */
	public static native String getGlobal(String p_key);

	/**
	 * Used to get info about the current rendering system.
	 *
	 * @return A String array with three elements:
	 *         [0] Rendering driver name chosen for rendering.
	 *         [1] Rendering driver name chosen before any fallbacks were applied.
	 *         [2] Rendering method.
	 *         [3] Source where the rendering driver was chosen from.
	 *         [4] Source where the rendering method was chosen from.
	 */
	public static native String[] getRendererInfo(boolean p_vulkan_requirements_met);

	/**
	 * Used to access Jundot's editor settings.
	 * @param settingKey Setting key
	 * @return String value of the setting
	 */
	public static native String getEditorSetting(String settingKey);

	/**
	 * Update the 'key' editor setting with the given data. Must be called on the render thread.
	 * @param key
	 * @param data
	 */
	public static native void setEditorSetting(String key, Object data);

	/**
	 * Used to access project metadata from the editor settings. Must be accessed on the render thread.
	 * @param section
	 * @param key
	 * @param defaultValue
	 * @return
	 */
	public static native Object getEditorProjectMetadata(String section, String key, Object defaultValue);

	/**
	 * Set the project metadata to the editor settings. Must be accessed on the render thread.
	 * @param section
	 * @param key
	 * @param data
	 */
	public static native void setEditorProjectMetadata(String section, String key, Object data);

	/**
	 * Invoke method |p_method| on the Jundot object specified by |p_id|
	 * @param p_id Id of the Jundot object to invoke
	 * @param p_method Name of the method to invoke
	 * @param p_params Parameters to use for method invocation
	 *
	 * @deprecated Use {@link Callable#call(long, String, Object...)} instead.
	 */
	@Deprecated
	public static void callobject(long p_id, String p_method, Object[] p_params) {
		Callable.call(p_id, p_method, p_params);
	}

	/**
	 * Invoke method |p_method| on the Jundot object specified by |p_id| during idle time.
	 * @param p_id Id of the Jundot object to invoke
	 * @param p_method Name of the method to invoke
	 * @param p_params Parameters to use for method invocation
	 *
	 * @deprecated Use {@link Callable#callDeferred(long, String, Object...)} instead.
	 */
	@Deprecated
	public static void calldeferred(long p_id, String p_method, Object[] p_params) {
		Callable.callDeferred(p_id, p_method, p_params);
	}

	/**
	 * Forward the results from a permission request.
	 * @see Activity#onRequestPermissionsResult(int, String[], int[])
	 * @param p_permission Request permission
	 * @param p_result True if the permission was granted, false otherwise
	 */
	public static native void requestPermissionResult(String p_permission, boolean p_result);

	/**
	 * Invoked on the theme light/dark mode change.
	 */
	public static native void onNightModeChanged();

	/**
	 * Invoked on the hardware keyboard connected/disconnected.
	 */
	public static native void hardwareKeyboardConnected(boolean connected);

	/**
	 * Invoked on the file picker closed.
	 */
	public static native void filePickerCallback(boolean p_ok, String[] p_selected_paths);

	/**
	 * Invoked on the GL thread to configure the height of the virtual keyboard.
	 */
	public static native void setVirtualKeyboardHeight(int p_height);

	/**
	 * Invoked on the GL thread when the {@link JundotRenderer} has been resumed.
	 * @see JundotRenderer#onActivityResumed()
	 */
	public static native void onRendererResumed();

	/**
	 * Invoked on the GL thread when the {@link JundotRenderer} has been paused.
	 * @see JundotRenderer#onActivityPaused()
	 */
	public static native void onRendererPaused();

	/**
	 * Invoked when the screen orientation changes.
	 * @param orientation the new screen orientation
	 */
	static native void onOrientationChange(int orientation);

	/**
	 * @return true if input must be dispatched from the render thread. If false, input is
	 * dispatched from the UI thread.
	 */
	public static native boolean shouldDispatchInputToRenderThread();

	/**
	 * @return the project resource directory
	 */
	public static native String getProjectResourceDir();

	static native boolean isEditorHint();

	static native boolean isProjectManagerHint();

	static native boolean hasFeature(String feature);

	static native void onPictureInPictureModeChanged(boolean isInPictureInPictureMode);
}
