/**************************************************************************/
/*  JundotFragment.java                                                    */
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

import org.jundotengine.jundot.error.Error;
import org.jundotengine.jundot.plugin.JundotPlugin;
import org.jundotengine.jundot.utils.BenchmarkUtils;

import android.content.Context;
import android.content.Intent;
import android.content.res.Configuration;
import android.os.Bundle;
import android.text.TextUtils;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import androidx.annotation.CallSuper;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;

import java.util.Collections;
import java.util.List;
import java.util.Set;

/**
 * Base fragment for Android apps intending to use Jundot for part of the app's UI.
 */
public class JundotFragment extends Fragment implements JundotHost {
	private static final String TAG = JundotFragment.class.getSimpleName();

	private FrameLayout jundotContainerLayout;

	@Nullable
	private JundotHost parentHost;
	private Jundot jundot;

	@Override
	public Jundot getJundot() {
		return jundot;
	}

	@Override
	public void onAttach(@NonNull Context context) {
		super.onAttach(context);
		if (getParentFragment() instanceof JundotHost) {
			parentHost = (JundotHost)getParentFragment();
		} else if (getActivity() instanceof JundotHost) {
			parentHost = (JundotHost)getActivity();
		}
	}

	@Override
	public void onDetach() {
		if (jundotContainerLayout != null && jundotContainerLayout.getParent() != null) {
			Log.d(TAG, "Cleaning up Jundot container layout during detach.");
			((ViewGroup)jundotContainerLayout.getParent()).removeView(jundotContainerLayout);
		}

		super.onDetach();
		parentHost = null;
	}

	@CallSuper
	@Override
	public void onConfigurationChanged(Configuration newConfig) {
		super.onConfigurationChanged(newConfig);
		jundot.onConfigurationChanged(newConfig);
	}

	@CallSuper
	@Override
	public void onActivityResult(int requestCode, int resultCode, Intent data) {
		super.onActivityResult(requestCode, resultCode, data);
		jundot.onActivityResult(requestCode, resultCode, data);
	}

	@CallSuper
	@Override
	public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
		super.onRequestPermissionsResult(requestCode, permissions, grantResults);
		jundot.onRequestPermissionsResult(requestCode, permissions, grantResults);
	}

	@Override
	public void onCreate(Bundle icicle) {
		BenchmarkUtils.beginBenchmarkMeasure("Startup", "JundotFragment::onCreate");
		super.onCreate(icicle);

		if (parentHost != null) {
			jundot = parentHost.getJundot();
		}
		if (jundot == null) {
			jundot = Jundot.getInstance(requireContext());
		}
		performEngineInitialization();
		BenchmarkUtils.endBenchmarkMeasure("Startup", "JundotFragment::onCreate");
	}

	private void performEngineInitialization() {
		try {
			if (!jundot.initEngine(this, getCommandLine(), getHostPlugins(jundot))) {
				throw new IllegalStateException("Unable to initialize Jundot engine");
			}

			jundotContainerLayout = jundot.onInitRenderView(this);
			if (jundotContainerLayout == null) {
				throw new IllegalStateException("Unable to initialize engine render view");
			}
		} catch (Exception e) {
			Log.e(TAG, "Engine initialization failed", e);
			final String errorMessage = TextUtils.isEmpty(e.getMessage())
					? getString(R.string.error_engine_setup_message)
					: e.getMessage();
			jundot.alert(errorMessage, getString(R.string.text_error_title), jundot::destroyAndKillProcess);
		}
	}

	@Override
	public View onCreateView(@NonNull LayoutInflater inflater, ViewGroup container, Bundle icicle) {
		if (jundotContainerLayout != null && jundotContainerLayout.getParent() != null) {
			Log.w(TAG, "Jundot container layout already has a parent, removing it.");
			((ViewGroup)jundotContainerLayout.getParent()).removeView(jundotContainerLayout);
		}

		return jundotContainerLayout;
	}

	@Override
	public void onDestroy() {
		if (jundotContainerLayout != null && jundotContainerLayout.getParent() != null) {
			Log.w(TAG, "Removing Jundot container layout from parent during destruction.");
			((ViewGroup)jundotContainerLayout.getParent()).removeView(jundotContainerLayout);
		}

		jundot.onDestroy(this);
		super.onDestroy();
	}

	@Override
	public void onPause() {
		super.onPause();
		jundot.onPause(this);
	}

	@Override
	public void onStop() {
		super.onStop();
		jundot.onStop(this);
	}

	@Override
	public void onStart() {
		super.onStart();
		jundot.onStart(this);
	}

	@Override
	public void onResume() {
		super.onResume();
		jundot.onResume(this);
	}

	public void onBackPressed() {
		jundot.onBackPressed();
	}

	@CallSuper
	@Override
	public List<String> getCommandLine() {
		return parentHost != null ? parentHost.getCommandLine() : Collections.emptyList();
	}

	@CallSuper
	@Override
	public void onJundotSetupCompleted() {
		if (parentHost != null) {
			parentHost.onJundotSetupCompleted();
		}
	}

	@CallSuper
	@Override
	public void onJundotMainLoopStarted() {
		if (parentHost != null) {
			parentHost.onJundotMainLoopStarted();
		}
	}

	@Override
	public void onJundotForceQuit(Jundot instance) {
		if (parentHost != null) {
			parentHost.onJundotForceQuit(instance);
		}
	}

	@Override
	public boolean onJundotForceQuit(int jundotInstanceId) {
		return parentHost != null && parentHost.onJundotForceQuit(jundotInstanceId);
	}

	@Override
	public void onJundotRestartRequested(Jundot instance) {
		if (parentHost != null) {
			parentHost.onJundotRestartRequested(instance);
		}
	}

	@Override
	public int onNewJundotInstanceRequested(String[] args) {
		if (parentHost != null) {
			return parentHost.onNewJundotInstanceRequested(args);
		}
		return -1;
	}

	@Override
	@CallSuper
	public Set<JundotPlugin> getHostPlugins(Jundot engine) {
		if (parentHost != null) {
			return parentHost.getHostPlugins(engine);
		}
		return Collections.emptySet();
	}

	@Override
	public Error signApk(@NonNull String inputPath, @NonNull String outputPath, @NonNull String keystorePath, @NonNull String keystoreUser, @NonNull String keystorePassword) {
		if (parentHost != null) {
			return parentHost.signApk(inputPath, outputPath, keystorePath, keystoreUser, keystorePassword);
		}
		return Error.ERR_UNAVAILABLE;
	}

	@Override
	public Error verifyApk(@NonNull String apkPath) {
		if (parentHost != null) {
			return parentHost.verifyApk(apkPath);
		}
		return Error.ERR_UNAVAILABLE;
	}

	@Override
	public boolean supportsFeature(String featureTag) {
		if (parentHost != null) {
			return parentHost.supportsFeature(featureTag);
		}
		return false;
	}

	@Override
	public void onEditorWorkspaceSelected(String workspace) {
		if (parentHost != null) {
			parentHost.onEditorWorkspaceSelected(workspace);
		}
	}

	@Override
	public void onDistractionFreeModeChanged(Boolean enabled) {
		if (parentHost != null) {
			parentHost.onDistractionFreeModeChanged(enabled);
		}
	}

	@Override
	public BuildProvider getBuildProvider() {
		if (parentHost != null) {
			return parentHost.getBuildProvider();
		}
		return null;
	}
}
