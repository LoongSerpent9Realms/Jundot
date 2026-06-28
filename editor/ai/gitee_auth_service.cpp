#include "gitee_auth_service.h"

#include "core/os/time.h"
#include "editor/ai/ai_settings.h"

GiteeAuthService *GiteeAuthService::singleton = nullptr;

GiteeAuthService *GiteeAuthService::get_singleton() {
	return singleton;
}

String GiteeAuthService::_get_authorize_url() const {
	return GITEE_OAUTH_AUTHORIZE_URL;
}

String GiteeAuthService::_get_token_url() const {
	return GITEE_OAUTH_TOKEN_URL;
}

String GiteeAuthService::_get_user_url() const {
	return GITEE_API_USER_URL;
}

String GiteeAuthService::_get_client_id() const {
	AISettingsData settings = AISettings::load();
	if (!settings.gitee_oauth_client_id.is_empty()) {
		return settings.gitee_oauth_client_id;
	}
	return GITEE_OAUTH_CLIENT_ID;
}

String GiteeAuthService::_get_client_secret() const {
	AISettingsData settings = AISettings::load();
	if (!settings.gitee_oauth_client_secret.is_empty()) {
		return settings.gitee_oauth_client_secret;
	}
	return GITEE_OAUTH_CLIENT_SECRET;
}

String GiteeAuthService::_get_callback_path() const {
	return GITEE_OAUTH_CALLBACK_PATH;
}

String GiteeAuthService::_get_scope() const {
	return "user_info projects";
}

AIOAuthToken GiteeAuthService::_parse_token_response(const Dictionary &p_response) const {
	AIOAuthToken token;
	token.access_token = p_response.get("access_token", String());
	token.refresh_token = p_response.get("refresh_token", String());
	token.token_type = p_response.get("token_type", "Bearer");
	token.scope = p_response.get("scope", String());

	int expires_in = p_response.get("expires_in", 0);
	if (expires_in > 0) {
		token.expires_at = Time::get_singleton()->get_unix_time_from_system() + expires_in;
	}
	return token;
}

AIOAuthUserInfo GiteeAuthService::_parse_user_response(const Dictionary &p_response) const {
	AIOAuthUserInfo user;
	user.login = p_response.get("login", String());
	user.name = p_response.get("name", String());
	user.avatar_url = p_response.get("avatar_url", String());
	user.html_url = p_response.get("html_url", String());
	user.email = p_response.get("email", String());
	return user;
}

void GiteeAuthService::_save_token_and_user(const AIOAuthToken &p_token, const AIOAuthUserInfo &p_user) {
	AISettingsData settings = AISettings::load();
	settings.gitee_token = p_token;
	settings.gitee_user = p_user;
	AISettings::save(settings);
}

bool GiteeAuthService::is_logged_in() const {
	AISettingsData settings = AISettings::load();
	return !settings.gitee_token.access_token.is_empty();
}

void GiteeAuthService::logout() {
	AISettingsData settings = AISettings::load();
	settings.gitee_token = AIOAuthToken();
	settings.gitee_user = AIOAuthUserInfo();
	AISettings::save(settings);
	AIOAuthService::logout();
}

GiteeAuthService::GiteeAuthService() {
	ERR_FAIL_COND(singleton != nullptr);
	singleton = this;
}

GiteeAuthService::~GiteeAuthService() {
	ERR_FAIL_COND(singleton != this);
	singleton = nullptr;
}
