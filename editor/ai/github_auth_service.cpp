#include "github_auth_service.h"

#include "core/os/time.h"
#include "editor/ai/ai_settings.h"

GitHubAuthService *GitHubAuthService::singleton = nullptr;

GitHubAuthService *GitHubAuthService::get_singleton() {
	return singleton;
}

String GitHubAuthService::_get_authorize_url() const {
	return GITHUB_OAUTH_AUTHORIZE_URL;
}

String GitHubAuthService::_get_token_url() const {
	return GITHUB_OAUTH_TOKEN_URL;
}

String GitHubAuthService::_get_user_url() const {
	return GITHUB_API_USER_URL;
}

String GitHubAuthService::_get_client_id() const {
	AISettingsData settings = AISettings::load();
	if (!settings.github_oauth_client_id.is_empty()) {
		return settings.github_oauth_client_id;
	}
	return GITHUB_OAUTH_CLIENT_ID;
}

String GitHubAuthService::_get_client_secret() const {
	AISettingsData settings = AISettings::load();
	if (!settings.github_oauth_client_secret.is_empty()) {
		return settings.github_oauth_client_secret;
	}
	return GITHUB_OAUTH_CLIENT_SECRET;
}

String GitHubAuthService::_get_callback_path() const {
	return GITHUB_OAUTH_CALLBACK_PATH;
}

String GitHubAuthService::_get_scope() const {
	return "repo user";
}

AIOAuthToken GitHubAuthService::_parse_token_response(const Dictionary &p_response) const {
	AIOAuthToken token;
	token.access_token = p_response.get("access_token", String());
	token.token_type = p_response.get("token_type", String());
	token.scope = p_response.get("scope", String());
	token.expires_at = 0;
	return token;
}

AIOAuthUserInfo GitHubAuthService::_parse_user_response(const Dictionary &p_response) const {
	AIOAuthUserInfo user;
	user.login = p_response.get("login", String());
	user.name = p_response.get("name", String());
	user.avatar_url = p_response.get("avatar_url", String());
	user.html_url = p_response.get("html_url", String());
	user.email = p_response.get("email", String());
	return user;
}

void GitHubAuthService::_save_token_and_user(const AIOAuthToken &p_token, const AIOAuthUserInfo &p_user) {
	AISettingsData settings = AISettings::load();
	settings.github_token = p_token;
	settings.github_user = p_user;
	AISettings::save(settings);
}

bool GitHubAuthService::is_logged_in() const {
	AISettingsData settings = AISettings::load();
	return !settings.github_token.access_token.is_empty();
}

void GitHubAuthService::logout() {
	AISettingsData settings = AISettings::load();
	settings.github_token = AIOAuthToken();
	settings.github_user = AIOAuthUserInfo();
	AISettings::save(settings);
	AIOAuthService::logout();
}

GitHubAuthService::GitHubAuthService() {
	ERR_FAIL_COND(singleton != nullptr);
	singleton = this;
}

GitHubAuthService::~GitHubAuthService() {
	ERR_FAIL_COND(singleton != this);
	singleton = nullptr;
}
