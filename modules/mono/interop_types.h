/**************************************************************************/
/*  interop_types.h                                                       */
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

#pragma once

#include "core/math/math_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

// This is taken from the old GDNative, which was removed.

#define JUNDOT_VARIANT_SIZE (sizeof(real_t) * 4 + sizeof(int64_t))

typedef struct {
	uint8_t _dont_touch_that[JUNDOT_VARIANT_SIZE];
} jundot_variant;

#define JUNDOT_ARRAY_SIZE sizeof(void *)

typedef struct {
	uint8_t _dont_touch_that[JUNDOT_ARRAY_SIZE];
} jundot_array;

#define JUNDOT_DICTIONARY_SIZE sizeof(void *)

typedef struct {
	uint8_t _dont_touch_that[JUNDOT_DICTIONARY_SIZE];
} jundot_dictionary;

#define JUNDOT_STRING_SIZE sizeof(void *)

typedef struct {
	uint8_t _dont_touch_that[JUNDOT_STRING_SIZE];
} jundot_string;

#define JUNDOT_STRING_NAME_SIZE sizeof(void *)

typedef struct {
	uint8_t _dont_touch_that[JUNDOT_STRING_NAME_SIZE];
} jundot_string_name;

#define JUNDOT_PACKED_ARRAY_SIZE (2 * sizeof(void *))

typedef struct {
	uint8_t _dont_touch_that[JUNDOT_PACKED_ARRAY_SIZE];
} jundot_packed_array;

#define JUNDOT_VECTOR2_SIZE (sizeof(real_t) * 2)

typedef struct {
	uint8_t _dont_touch_that[JUNDOT_VECTOR2_SIZE];
} jundot_vector2;

#define JUNDOT_VECTOR2I_SIZE (sizeof(int32_t) * 2)

typedef struct {
	uint8_t _dont_touch_that[JUNDOT_VECTOR2I_SIZE];
} jundot_vector2i;

#define JUNDOT_RECT2_SIZE (sizeof(real_t) * 4)

typedef struct jundot_rect2 {
	uint8_t _dont_touch_that[JUNDOT_RECT2_SIZE];
} jundot_rect2;

#define JUNDOT_RECT2I_SIZE (sizeof(int32_t) * 4)

typedef struct jundot_rect2i {
	uint8_t _dont_touch_that[JUNDOT_RECT2I_SIZE];
} jundot_rect2i;

#define JUNDOT_VECTOR3_SIZE (sizeof(real_t) * 3)

typedef struct {
	uint8_t _dont_touch_that[JUNDOT_VECTOR3_SIZE];
} jundot_vector3;

#define JUNDOT_VECTOR3I_SIZE (sizeof(int32_t) * 3)

typedef struct {
	uint8_t _dont_touch_that[JUNDOT_VECTOR3I_SIZE];
} jundot_vector3i;

#define JUNDOT_TRANSFORM2D_SIZE (sizeof(real_t) * 6)

typedef struct {
	uint8_t _dont_touch_that[JUNDOT_TRANSFORM2D_SIZE];
} jundot_transform2d;

#define JUNDOT_VECTOR4_SIZE (sizeof(real_t) * 4)

typedef struct {
	uint8_t _dont_touch_that[JUNDOT_VECTOR4_SIZE];
} jundot_vector4;

#define JUNDOT_VECTOR4I_SIZE (sizeof(int32_t) * 4)

typedef struct {
	uint8_t _dont_touch_that[JUNDOT_VECTOR4I_SIZE];
} jundot_vector4i;

#define JUNDOT_PLANE_SIZE (sizeof(real_t) * 4)

typedef struct {
	uint8_t _dont_touch_that[JUNDOT_PLANE_SIZE];
} jundot_plane;

#define JUNDOT_QUATERNION_SIZE (sizeof(real_t) * 4)

typedef struct {
	uint8_t _dont_touch_that[JUNDOT_QUATERNION_SIZE];
} jundot_quaternion;

#define JUNDOT_AABB_SIZE (sizeof(real_t) * 6)

typedef struct {
	uint8_t _dont_touch_that[JUNDOT_AABB_SIZE];
} jundot_aabb;

#define JUNDOT_BASIS_SIZE (sizeof(real_t) * 9)

typedef struct {
	uint8_t _dont_touch_that[JUNDOT_BASIS_SIZE];
} jundot_basis;

#define JUNDOT_TRANSFORM3D_SIZE (sizeof(real_t) * 12)

typedef struct {
	uint8_t _dont_touch_that[JUNDOT_TRANSFORM3D_SIZE];
} jundot_transform3d;

#define JUNDOT_PROJECTION_SIZE (sizeof(real_t) * 4 * 4)

typedef struct {
	uint8_t _dont_touch_that[JUNDOT_PROJECTION_SIZE];
} jundot_projection;

// Colors should always use 32-bit floats, so don't use real_t here.
#define JUNDOT_COLOR_SIZE (sizeof(float) * 4)

typedef struct {
	uint8_t _dont_touch_that[JUNDOT_COLOR_SIZE];
} jundot_color;

#define JUNDOT_NODE_PATH_SIZE sizeof(void *)

typedef struct {
	uint8_t _dont_touch_that[JUNDOT_NODE_PATH_SIZE];
} jundot_node_path;

#define JUNDOT_RID_SIZE sizeof(uint64_t)

typedef struct {
	uint8_t _dont_touch_that[JUNDOT_RID_SIZE];
} jundot_rid;

// Alignment hardcoded in `core/variant/callable.h`.
#define JUNDOT_CALLABLE_SIZE (16)

typedef struct {
	uint8_t _dont_touch_that[JUNDOT_CALLABLE_SIZE];
} jundot_callable;

// Alignment hardcoded in `core/variant/callable.h`.
#define JUNDOT_SIGNAL_SIZE (16)

typedef struct {
	uint8_t _dont_touch_that[JUNDOT_SIGNAL_SIZE];
} jundot_signal;

#ifdef __cplusplus
}
#endif
