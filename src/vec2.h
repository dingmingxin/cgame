#ifndef VEC2_H
#define VEC2_H

#include "scalar.h"
#include "saveload.h"
#include "script_export.h"

SCRIPT(vec2,

       typedef struct Vec2 Vec2;
       struct Vec2 { Scalar x; Scalar y; };

       EXPORT Vec2 vec2(Scalar x, Scalar y);
       EXPORT extern Vec2 vec2_zero;

       EXPORT Vec2 vec2_add(Vec2 u, Vec2 v);
       EXPORT Vec2 vec2_sub(Vec2 u, Vec2 v);
       EXPORT Vec2 vec2_mul(Vec2 u, Vec2 v); /* u * v componentwise */
       EXPORT Vec2 vec2_div(Vec2 u, Vec2 v); /* u / v componentwise */
       EXPORT Vec2 vec2_scalar_mul(Vec2 v, Scalar f);
       EXPORT Vec2 vec2_scalar_div(Vec2 v, Scalar f); /* (v.x / f, v.y / f) */
       EXPORT Vec2 scalar_vec2_div(Scalar f, Vec2 v); /* (f / v.x, f / v.y) */
       EXPORT Vec2 vec2_neg(Vec2 v);

       EXPORT Scalar vec2_len(Vec2 v);
       EXPORT Vec2 vec2_normalize(Vec2 v);
       EXPORT Scalar vec2_dot(Vec2 u, Vec2 v);
       EXPORT Scalar vec2_dist(Vec2 u, Vec2 v);

       EXPORT Vec2 vec2_rot(Vec2 v, Scalar rot);
       EXPORT Scalar vec2_atan2(Vec2 v);

       EXPORT void vec2_save(Vec2 *v, const char *n, Store *s);
       EXPORT bool vec2_load(Vec2 *v, const char *n, Vec2 d, Store *s);

    )

/* C inline stuff */

#define vec2(x, y) ((Vec2) { (x), (y) })

#endif
