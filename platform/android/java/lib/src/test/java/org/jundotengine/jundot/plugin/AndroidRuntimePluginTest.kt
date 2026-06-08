/**************************************************************************/
/*  AndroidRuntimePluginTest.kt                                           */
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

package org.jundotengine.jundot.plugin

import org.jundotengine.jundot.variant.Callable
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

class AndroidRuntimePluginTest {

	private val createProxyFromJundotCallable = AndroidRuntimePlugin::class.java
		.getDeclaredMethod("createProxyFromJundotCallable", String::class.java, Callable::class.java)
		.apply { isAccessible = true }

	private val createProxyFromJundotObjectID = AndroidRuntimePlugin::class.java
		.getDeclaredMethod("createProxyFromJundotObjectID", Long::class.javaPrimitiveType, Array<String>::class.java)
		.apply { isAccessible = true }

	private fun newCallable(): Callable {
		val ctor = Callable::class.java.getDeclaredConstructor(Long::class.javaPrimitiveType)
		ctor.isAccessible = true
		return ctor.newInstance(0L)
	}

	private fun callableProxy(): Any {
		val proxy = createProxyFromJundotCallable.invoke(null, Runnable::class.java.name, newCallable())
		assertNotNull(proxy)
		return proxy!!
	}

	private fun objectIDProxy(jundotObjectID: Long = 0L): Any {
		val proxy = createProxyFromJundotObjectID.invoke(null, jundotObjectID, arrayOf(Runnable::class.java.name))
		assertNotNull(proxy)
		return proxy!!
	}

	// Regression tests for the infinite recursion in the proxy equals handler.
	// Kotlin's `proxy == args[0]` compiles to `proxy.equals(args[0])`, which the
	// JDK proxy dispatches back through the InvocationHandler — the previous
	// implementation hit the same branch and recursed into StackOverflowError.

	@Test
	fun `Given Callable proxy, When equals is invoked with itself, Then returns true without recursion`() {
		val proxy = callableProxy()
		assertTrue(proxy.equals(proxy))
	}

	@Test
	fun `Given Callable proxy, When equals is invoked with another instance, Then returns false without recursion`() {
		val proxy = callableProxy()
		val otherProxy = callableProxy()
		assertFalse(proxy.equals(otherProxy))
		assertFalse(proxy.equals(Any()))
		assertFalse(proxy.equals(null))
	}

	@Test
	fun `Given ObjectID proxy, When equals is invoked with itself, Then returns true without recursion`() {
		val proxy = objectIDProxy()
		assertTrue(proxy.equals(proxy))
	}

	@Test
	fun `Given ObjectID proxy, When equals is invoked with another instance, Then returns false without recursion`() {
		val proxy = objectIDProxy()
		val otherProxy = objectIDProxy()
		assertFalse(proxy.equals(otherProxy))
		assertFalse(proxy.equals(Any()))
		assertFalse(proxy.equals(null))
	}

	// Sanity checks that the proxy's toString / hashCode handlers still operate
	// as documented and that they do not get caught up in the equals fix.

	@Test
	fun `Given Callable proxy, When toString is invoked, Then returns the documented label`() {
		val proxy = callableProxy()
		assertEquals("Jundot Callable Proxy for ${Runnable::class.java.name}", proxy.toString())
	}

	@Test
	fun `Given ObjectID proxy, When toString is invoked, Then returns the documented label`() {
		val proxy = objectIDProxy()
		assertEquals("Jundot Object Proxy for ${Runnable::class.java.name}", proxy.toString())
	}

	@Test
	fun `Given Callable proxy, When hashCode is invoked, Then returns the Callable hashCode`() {
		val callable = newCallable()
		val proxy = createProxyFromJundotCallable.invoke(null, Runnable::class.java.name, callable)!!
		assertEquals(callable.hashCode(), proxy.hashCode())
	}

	@Test
	fun `Given ObjectID proxy, When hashCode is invoked, Then returns the ObjectID hashCode`() {
		val jundotObjectID = 0x1_0000_002AL
		val proxy = objectIDProxy(jundotObjectID)
		assertEquals(jundotObjectID.hashCode(), proxy.hashCode())
	}
}
