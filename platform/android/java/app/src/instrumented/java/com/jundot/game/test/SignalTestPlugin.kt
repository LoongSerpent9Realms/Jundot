package com.jundot.game.test

import android.util.Log
import org.jundotengine.jundot.Dictionary
import org.jundotengine.jundot.Jundot
import org.jundotengine.jundot.plugin.JundotPlugin
import org.jundotengine.jundot.plugin.SignalInfo
import org.jundotengine.jundot.plugin.UsedByJundot

class SignalTestPlugin(jundot: Jundot) : JundotPlugin(jundot) {

	companion object {
		private val EMISSION_TEST_SIGNAL = SignalInfo("emission_test_signal")
		private val LAUNCH_TESTS_SIGNAL = SignalInfo("launch_tests", java.lang.Boolean::class.java, String::class.java)
	}

	override fun getPluginName() = "SignalTestPlugin"

	override fun getPluginSignals(): Set<SignalInfo?> {
		return setOf(
			EMISSION_TEST_SIGNAL,
			LAUNCH_TESTS_SIGNAL
		)
	}

	@UsedByJundot
	fun triggerTestSignal1() {
		emitSignal(EMISSION_TEST_SIGNAL)
	}

	@UsedByJundot
	fun triggerLaunchTestSignal() {
		emitSignal(LAUNCH_TESTS_SIGNAL, true, "second message")
	}
}
