#pragma once

#include "editor/ai/ai_oauth_service.h"

class GiteeAuthService : public AIOAuthService {
	GDCLASS(GiteeAuthService, AIOAuthService);

protected:

	virtual String _get_authorize_url() const override;
	virtual String _get_token_url() const override;
	virtual String _get_user_url() const override;
	virtual String _get_client_id() const override;
	virtual String _get_client_secret() const override;
	virtual String _get_callback_path() const override;
	virtual String _get_scope() const override;
	virtual AIOAuthToken _parse_token_response(const Dictionary &p_response) const override;
	virtual AIOAuthUserInfo _parse_user_response(const Dictionary &p_response) const override;
	virtual void _save_token_and_user(const AIOAuthToken &p_token, const AIOAuthUserInfo &p_user) override;

public:
	static GiteeAuthService *get_singleton();

	bool is_logged_in() const;
	void logout();

	GiteeAuthService();
	~GiteeAuthService();

private:
	static GiteeAuthService *singleton;
};
