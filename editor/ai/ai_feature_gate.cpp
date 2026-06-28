/*  ai_feature_gate.cpp                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/

#include "ai_feature_gate.h"

bool AIFeatureGate::evaluate(AIFeatureGateResult &r_result, const AISettingsData &p_settings) {
	const bool universality_ok = r_result.universality_percent >= p_settings.feature_universality_threshold;
	const bool necessity_ok = r_result.necessity_score >= p_settings.feature_necessity_threshold;
	const bool philosophy_ok = !p_settings.feature_design_philosophy_check || r_result.philosophy_conflict.strip_edges().is_empty();

	r_result.recommended = universality_ok && necessity_ok && philosophy_ok;

	if (r_result.recommended) {
		r_result.rationale = TTR("Suggested for inclusion: the feature meets the configured universality and necessity thresholds and has no design philosophy conflict.");
	} else if (!universality_ok) {
		r_result.rationale = vformat(TTR("Not suggested: universality %.1f%% is below the configured %.1f%% threshold."), r_result.universality_percent, p_settings.feature_universality_threshold);
	} else if (!necessity_ok) {
		r_result.rationale = vformat(TTR("Not suggested: necessity %.2f is below the configured %.2f threshold."), r_result.necessity_score, p_settings.feature_necessity_threshold);
	} else {
		r_result.rationale = TTR("Not suggested: the feature conflicts with Jundot design philosophy.");
	}

	return r_result.recommended;
}

String AIFeatureGate::build_report(const AIFeatureGateResult &p_result) {
	String report;
	report += TTR("Feature:") + " " + p_result.title + "\n";
	report += TTR("Summary:") + " " + p_result.summary + "\n";
	report += vformat(TTR("Universality: %.1f%%"), p_result.universality_percent) + "\n";
	report += vformat(TTR("Necessity: %.2f"), p_result.necessity_score) + "\n";
	if (!p_result.workaround_cost.is_empty()) {
		report += TTR("Workaround cost:") + " " + p_result.workaround_cost + "\n";
	}
	if (!p_result.philosophy_conflict.is_empty()) {
		report += TTR("Design philosophy conflict:") + " " + p_result.philosophy_conflict + "\n";
	}
	report += TTR("Decision:") + " " + String(p_result.recommended ? TTR("Suggested for inclusion") : TTR("Not suggested")) + "\n";
	report += TTR("Rationale:") + " " + p_result.rationale;
	return report;
}
