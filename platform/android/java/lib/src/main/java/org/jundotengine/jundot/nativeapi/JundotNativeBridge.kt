/**************************************************************************/
/*  JundotNativeBridge.kt                                                  */
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

package org.jundotengine.jundot.nativeapi

import android.annotation.SuppressLint
import android.app.AlertDialog
import android.content.Context
import android.content.DialogInterface
import android.content.Intent
import android.content.res.Configuration
import android.os.Build
import android.os.VibrationEffect
import android.os.Vibrator
import android.util.Log
import android.util.Rational
import android.util.TypedValue
import androidx.annotation.Keep
import androidx.core.net.toUri
import org.jundotengine.jundot.Jundot
import org.jundotengine.jundot.JundotActivity
import org.jundotengine.jundot.R
import org.jundotengine.jundot.error.Error
import org.jundotengine.jundot.feature.PictureInPictureProvider
import org.jundotengine.jundot.io.FilePicker
import org.jundotengine.jundot.utils.DialogUtils
import org.jundotengine.jundot.utils.JundotNetUtils
import org.jundotengine.jundot.utils.beginBenchmarkMeasure
import org.jundotengine.jundot.utils.dumpBenchmark
import org.jundotengine.jundot.utils.endBenchmarkMeasure
import org.jundotengine.jundot.variant.Callable as JundotCallable

/**
 * Holds and expose Jundot apis to the native layer.
 *
 * All the methods in this class are accessed by the native code (java_jundot_wrapper.h) and as such are kept private to
 * not be accessible by the rest of the java/kotlin code.
 */
@Keep
internal class JundotNativeBridge(private val jundot: Jundot) {

	companion object {
		private val TAG = JundotNativeBridge::class.java.simpleName

	}

	private val vibratorService: Vibrator? by lazy { jundot.context.getSystemService(Context.VIBRATOR_SERVICE) as? Vibrator }

	/**
	 * Invoked on the render thread when the Jundot setup is complete.
	 */
	private fun onJundotSetupCompleted() {
		jundot.onJundotSetupCompleted()
	}

	/**
	 * Invoked on the render thread when the Jundot main loop has started.
	 */
	private fun onJundotMainLoopStarted() {
		jundot.onJundotMainLoopStarted()
	}

	/**
	 * Invoked on the render thread when the engine is about to terminate.
	 */
	private fun onJundotTerminating() = jundot.onJundotTerminating()

	/**
	 * Invoked from the render thread to toggle the immersive mode.
	 */
	private fun nativeEnableImmersiveMode(enabled: Boolean) {
		jundot.runOnHostThread {
			jundot.enableImmersiveMode(enabled)
		}
	}

	private fun isInImmersiveMode() = jundot.isInImmersiveMode()

	private fun isInEdgeToEdgeMode() = jundot.isInEdgeToEdgeMode()

	private fun setKeepScreenOn(enabled: Boolean) = jundot.setKeepScreenOn(enabled)

	private fun restart() { jundot.primaryHost?.onJundotRestartRequested(jundot) }

	private fun alert(message: String, title: String) {
		jundot.alert(message, title)
	}

	private fun forceQuit(instanceId: Int) = jundot.forceQuit(instanceId)

	/**
	 * Returns true if dark mode is supported, false otherwise.
	 */
	private fun isDarkModeSupported(): Boolean {
		return jundot.context.resources?.configuration?.uiMode?.and(Configuration.UI_MODE_NIGHT_MASK) != Configuration.UI_MODE_NIGHT_UNDEFINED
	}

	/**
	 * Returns true if dark mode is supported and enabled, false otherwise.
	 */
	private fun isDarkMode() = jundot.darkMode

	private fun showFilePicker(currentDirectory: String, filename: String, fileMode: Int, filters: Array<String>) {
		FilePicker.showFilePicker(jundot.context, jundot.getActivity(), currentDirectory, filename, fileMode, filters)
	}

	/**
	 * This method shows a dialog with multiple buttons.
	 *
	 * @param title The title of the dialog.
	 * @param message The message displayed in the dialog.
	 * @param buttons An array of button labels to display.
	 */
	private fun showDialog(title: String, message: String, buttons: Array<String>) {
		jundot.getActivity()?.let { DialogUtils.showDialog(it, title, message, buttons) }
	}

	/**
	 * This method shows a dialog with a text input field, allowing the user to input text.
	 *
	 * @param title The title of the input dialog.
	 * @param message The message displayed in the input dialog.
	 * @param existingText The existing text that will be pre-filled in the input field.
	 */
	private fun showInputDialog(title: String, message: String, existingText: String) {
		jundot.getActivity()?.let { DialogUtils.showInputDialog(it, title, message, existingText) }
	}

	private fun getAccentColor(): Int {
		val value = TypedValue()
		jundot.context.theme.resolveAttribute(android.R.attr.colorAccent, value, true)
		return value.data
	}

	private fun getBaseColor(): Int {
		val value = TypedValue()
		jundot.context.theme.resolveAttribute(android.R.attr.colorBackground, value, true)
		return value.data
	}

	private fun requestPermission(name: String?) = jundot.requestPermission(name)

	private fun requestPermissions() = jundot.requestPermissions()

	private fun getGrantedPermissions() = jundot.getGrantedPermissions()

	/**
	 * Used by the native code (java_jundot_wrapper.h) to vibrate the device.
	 * @param durationMs
	 */
	@SuppressLint("MissingPermission")
	private fun vibrate(durationMs: Int, amplitude: Int) {
		if (durationMs > 0 && jundot.requestPermission("VIBRATE")) {
			try {
				if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
					if (amplitude <= -1) {
						vibratorService?.vibrate(
							VibrationEffect.createOneShot(
								durationMs.toLong(),
								VibrationEffect.DEFAULT_AMPLITUDE
							)
						)
					} else {
						vibratorService?.vibrate(
							VibrationEffect.createOneShot(
								durationMs.toLong(),
								amplitude
							)
						)
					}
				} else {
					// deprecated in API 26
					vibratorService?.vibrate(durationMs.toLong())
				}
			} catch (e: SecurityException) {
				Log.w(
					TAG,
					"SecurityException: VIBRATE permission not found. Make sure it is declared in the manifest or enabled in the export preset."
				)
			}
		}
	}

	/**
	 * Internal method used to query whether the host or the registered plugins supports a given feature.
	 *
	 * This is invoked by the native code, and should not be confused with [hasFeature] which is the Android version of
	 * https://docs.jundotengine.org/en/stable/classes/class_os.html#class-os-method-has-feature
	 */
	private fun checkInternalFeatureSupport(feature: String): Boolean {
		if (jundot.primaryHost?.supportsFeature(feature) == true) {
			return true
		}

		for (plugin in jundot.pluginRegistry.allPlugins) {
			if (plugin.supportsFeature(feature)) {
				return true
			}
		}
		return false
	}

	/**
	 * Get the list of gdextension modules to register.
	 */
	private fun getGDExtensionConfigFiles(): Array<String> {
		val configFiles = mutableSetOf<String>()
		for (plugin in jundot.pluginRegistry.allPlugins) {
			configFiles.addAll(plugin.pluginGDExtensionLibrariesPaths)
		}

		return configFiles.toTypedArray()
	}

	private fun getCACertificates(): String {
		return JundotNetUtils.getCACertificates()
	}

	private fun getActivity() = jundot.getActivity()

	private fun getRenderView() = jundot.renderView

	private fun getClipboard() = jundot.getClipboard()

	private fun setClipboard(text: String) = jundot.setClipboard(text)

	private fun hasClipboard() = jundot.hasClipboard()

	private fun setWindowColor(color: String) = jundot.setWindowColor(color)

	/**
	 * Used by the native code (java_jundot_wrapper.h) to access the input fallback mapping.
	 * @return The input fallback mapping for the current XR mode.
	 */
	private fun getInputFallbackMapping(): String? {
		return jundot.xrMode.inputFallbackMapping
	}

	private fun initInputDevices() {
		jundot.jundotInputHandler.initInputDevices()
	}

	private fun createNewJundotInstance(args: Array<String>): Int {
		return jundot.primaryHost?.onNewJundotInstanceRequested(args) ?: -1
	}

	private fun nativeBeginBenchmarkMeasure(scope: String, label: String) {
		beginBenchmarkMeasure(scope, label)
	}

	private fun nativeEndBenchmarkMeasure(scope: String, label: String) {
		endBenchmarkMeasure(scope, label)
	}

	private fun nativeDumpBenchmark(benchmarkFile: String) {
		dumpBenchmark(jundot.fileAccessHandler, benchmarkFile)
	}

	private fun nativeSignApk(
		inputPath: String,
		outputPath: String,
		keystorePath: String,
		keystoreUser: String,
		keystorePassword: String
	): Int {
		val signResult = jundot.primaryHost?.signApk(inputPath, outputPath, keystorePath, keystoreUser, keystorePassword)
			?: org.jundotengine.jundot.error.Error.ERR_UNAVAILABLE
		return signResult.toNativeValue()
	}

	private fun nativeVerifyApk(apkPath: String): Int {
		val verifyResult = jundot.primaryHost?.verifyApk(apkPath) ?: Error.ERR_UNAVAILABLE
		return verifyResult.toNativeValue()
	}

	private fun nativeOnEditorWorkspaceSelected(workspace: String) {
		jundot.primaryHost?.onEditorWorkspaceSelected(workspace)
	}

	private fun nativeOnDistractionFreeModeChanged(enabled: Boolean) {
		jundot.primaryHost?.onDistractionFreeModeChanged(enabled)
	}

	private fun nativeBuildEnvConnect(callback: JundotCallable): Boolean {
		try {
			val buildProvider = jundot.primaryHost?.buildProvider
			val success = buildProvider?.buildEnvConnect(callback) ?: false
			if (!success) {
				val activity = jundot.getActivity() ?: return false
				jundot.runOnHostThread {
					val builder = AlertDialog.Builder(activity)
						.setMessage(activity.getString(R.string.gabe_connection_error_message))
						.setTitle(activity.getString(R.string.gabe_connection_error_title))
						.setCancelable(false)
						.setPositiveButton(activity.getString(R.string.dialog_download)) { dialog: DialogInterface, _: Int ->
							val intent = Intent(Intent.ACTION_VIEW, "https://jundotengine.org/download/android#gabe".toUri())
							intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
							activity.startActivity(intent)
							dialog.cancel()
						}
						.setNegativeButton(activity.getString(R.string.dialog_cancel)) { dialog: DialogInterface, _: Int ->
							dialog.dismiss()
						}
					val dialog = builder.create()
					dialog.show()
				}
			}
			return success
		} catch (e: Exception) {
			Log.e(TAG, "Unable to connect to build environment", e)
			return false
		}
	}

	private fun nativeBuildEnvDisconnect() {
		try {
			val buildProvider = jundot.primaryHost?.buildProvider
			buildProvider?.buildEnvDisconnect()
		} catch (e: Exception) {
			Log.e(TAG, "Unable to disconnect from build environment", e)
		}
	}

	private fun nativeBuildEnvExecute(
		buildTool: String,
		arguments: Array<String>,
		projectPath: String,
		buildDir: String,
		outputCallback: JundotCallable,
		resultCallback: JundotCallable
	): Int {
		try {
			val buildProvider = jundot.primaryHost?.buildProvider
			return buildProvider?.buildEnvExecute(
				buildTool,
				arguments,
				projectPath,
				buildDir,
				outputCallback,
				resultCallback
			) ?: -1
		} catch (e: Exception) {
			Log.e(TAG, "Unable to execute Gradle command in build environment", e);
			return -1
		}
	}

	private fun nativeBuildEnvCancel(jobId: Int) {
		try {
			val buildProvider = jundot.primaryHost?.buildProvider
			buildProvider?.buildEnvCancel(jobId)
		} catch (e: Exception) {
			Log.e(TAG, "Unable to cancel command in build environment", e)
		}
	}

	private fun nativeBuildEnvCleanProject(projectPath: String, buildDir: String, callback: JundotCallable) {
		try {
			val buildProvider = jundot.primaryHost?.buildProvider
			buildProvider?.buildEnvCleanProject(projectPath, buildDir, callback)
		} catch (e: Exception) {
			Log.e(TAG, "Unable to clean project in build environment", e)
		}
	}

	private fun nativeIsPiPModeSupported(): Boolean {
		val hostActivity = jundot.getActivity()
		if (hostActivity is PictureInPictureProvider) {
			return hostActivity.isPiPModeSupported()
		}
		return false
	}

	private fun nativeIsInPiPMode(): Boolean {
		val hostActivity = jundot.getActivity()
		if (hostActivity is JundotActivity) {
			return hostActivity.isInPictureInPictureMode
		}
		return false
	}

	private fun nativeEnterPiPMode() {
		val hostActivity = jundot.getActivity()
		if (hostActivity is PictureInPictureProvider) {
			jundot.runOnHostThread {
				hostActivity.enterPiPMode()
			}
		}
	}

	private fun nativeSetPiPModeAspectRatio(numerator: Int, denominator: Int) {
		val hostActivity = jundot.getActivity()
		if (hostActivity is JundotActivity) {
			jundot.runOnHostThread {
				hostActivity.updatePiPParams(aspectRatio = Rational(numerator, denominator))
			}
		}
	}

	private fun nativeSetAutoEnterPiPModeOnBackground(autoEnterPiPOnBackground: Boolean) {
		val hostActivity = jundot.getActivity()
		if (hostActivity is JundotActivity) {
			jundot.runOnHostThread {
				hostActivity.updatePiPParams(enableAutoEnter = autoEnterPiPOnBackground)
			}
		}
	}
}
