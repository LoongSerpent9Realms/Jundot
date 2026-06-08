/**************************************************************************/
/*  AndroidRuntimePlugin.kt                                               */
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

package org.jundotengine.jundot.plugin

import android.content.Intent
import android.util.Log
import androidx.annotation.Keep
import androidx.core.net.toUri

import org.jundotengine.jundot.Jundot
import org.jundotengine.jundot.variant.Callable
import java.lang.reflect.InvocationHandler
import java.lang.reflect.Proxy

/**
 * Built-in Jundot Android plugin used to provide access to the Android runtime capabilities.
 *
 * @see <a href="https://docs.jundotengine.org/en/latest/tutorials/platform/android/javaclasswrapper_and_androidruntimeplugin.html">Integrating with Android APIs</a>
 */
class AndroidRuntimePlugin(jundot: Jundot) : JundotPlugin(jundot) {

	companion object {
		private val TAG = AndroidRuntimePlugin::class.java.simpleName

		/**
		 * Helper method used to generate Jundot Proxy instances.
		 */
		@JvmStatic
		@Keep
		private fun generateProxyInstance(interfaces: Array<String>, invocationHandler: InvocationHandler): Any? {
			try {
				val interfaceClasses = interfaces.map { Class.forName(it) }.toTypedArray()

				val proxy = Proxy.newProxyInstance(invocationHandler.javaClass.classLoader, interfaceClasses, invocationHandler)
				return proxy
			} catch (e: Exception) {
				Log.w(TAG, "Error generating Jundot proxy for interfaces ${interfaces.joinToString(",")}", e)
			}
			return null
		}

		/**
		 * Utility method used to create [java.lang.reflect.Proxy] instance wrapping a given Jundot [Callable].
		 *
		 * The [Proxy] instance is used to implement one SAM interface with the [Callable] serving as the delegate
		 * implementation for the SAM interface overridden methods.
		 */
		@JvmStatic
		@Keep
		private fun createProxyFromJundotCallable(interfaceName: String, jundotCallable: Callable): Any? {
			return generateProxyInstance(arrayOf(interfaceName)) { proxy, method, args ->
				when (method.name) {
					// We automatically handle 'toString', 'equals' and 'hashCode' to simplify the task of the caller
					// and provide consistency.
					"toString" -> "Jundot Callable Proxy for $interfaceName"
					"equals" -> proxy === args[0]
					"hashCode" -> jundotCallable.hashCode()

					// Invocation for the interface single abstract method falls here and is dispatched to the
					// Jundot [Callable].
					else -> jundotCallable.call(*(args ?: emptyArray()))
				}
			}
		}

		/**
		 * Utility method used to create [java.lang.reflect.Proxy] instance wrapping a given Jundot Object represented by
		 * its ObjectID.
		 *
		 * The [Proxy] instance is used to implement one or multiple interfaces with the Object represented by
		 * [jundotObjectID] serving as the delegate implementation for the interface(s) overridden methods.
		 */
		@JvmStatic
		@Keep
		private fun createProxyFromJundotObjectID(jundotObjectID: Long, interfaces: Array<String>): Any? {
			return generateProxyInstance(interfaces) { proxy, method, args ->
				when (val methodName = method.name) {
					// We automatically handle 'toString', 'equals' and 'hashCode' to simplify the task of the caller
					// and provide consistency.
					"toString" -> "Jundot Object Proxy for ${interfaces.joinToString(",")}"
					"equals" -> proxy === args[0]
					"hashCode" -> jundotObjectID.hashCode()

					// Invocation for the remaining interface(s) methods falls here and is dispatched to the
					// Jundot Object.
					else -> Callable.call(jundotObjectID, methodName, *(args ?: emptyArray()))
				}
			}
		}
	}

	override fun getPluginName() = "AndroidRuntime"

	/**
	 * Provides access to the application [android.content.Context] to GDScript
	 */
	@UsedByJundot
	fun getApplicationContext() = activity?.applicationContext

	/**
	 * Provides access to the host [android.app.Activity] to GDScript
	 */
	@UsedByJundot
	public override fun getActivity() = super.getActivity()

	/**
	 * Utility method used to create [Runnable] from Jundot [Callable].
	 */
	@UsedByJundot
	fun createRunnableFromJundotCallable(jundotCallable: Callable): Runnable {
		return Runnable { jundotCallable.call() }
	}

	/**
	 * Utility method used to create [java.util.concurrent.Callable] from Jundot [Callable].
	 */
	@UsedByJundot
	fun createCallableFromJundotCallable(jundotCallable: Callable): java.util.concurrent.Callable<Any> {
		return java.util.concurrent.Callable { jundotCallable.call() }
	}

	/**
	 * Helper method to take/release persistable URI permission.
	 */
	@UsedByJundot
	fun updatePersistableUriPermission(uriString: String, persist: Boolean): Boolean {
		try {
			val uri = uriString.toUri()
			val contentResolver = context.contentResolver
			if (persist) {
				contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
			} else {
				contentResolver.releasePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
			}
		} catch (e: RuntimeException) {
			Log.d(TAG, "Error updating persistable permission: ", e)
			return false
		}
		return true
	}
}
