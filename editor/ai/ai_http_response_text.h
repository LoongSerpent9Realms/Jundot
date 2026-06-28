/*  ai_http_response_text.h                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/

#pragma once

#include "core/string/ustring.h"
#include "core/variant/variant.h"

String ai_decode_http_response_text(const PackedByteArray &p_body, const PackedStringArray &p_headers);
