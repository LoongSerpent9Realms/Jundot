/*  ai_feature_gate.h                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/

#pragma once

#include "editor/ai/ai_settings.h"

#include "core/string/ustring.h"
#include "core/variant/variant.h"

struct AIFeatureGateResult {
	String title;
	String summary;
	double universality_percent = 0.0;
	double necessity_score = 0.0;
	String workaround_cost;
	String philosophy_conflict;
	bool recommended = false;
	String rationale;
};

class AIFeatureGate {
public:
	static bool evaluate(AIFeatureGateResult &r_result, const AISettingsData &p_settings);
	static String build_report(const AIFeatureGateResult &p_result);
};
