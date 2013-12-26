#include "camera.h"

#include <stdbool.h>

#include "transform.h"

static bool camera_exists = false;
static Entity camera_entity;

static Mat3 inverse_view_matrix = mat3(
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f
        );

/* ------------------------------------------------------------------------- */

void camera_add(Entity ent)
{
    camera_exists = true;
    camera_entity = ent;
}
void camera_remove()
{
    camera_exists = false;
}
Entity camera_get()
{
    return camera_entity;
}

void camera_set_viewport_size(Vec2 dim)
{
    if (camera_exists)
        transform_set_scale(camera_entity, vec2_scalar_mul(dim, 0.5f));
}

Mat3 camera_get_inverse_view_matrix()
{
    return inverse_view_matrix;
}

Mat3 *camera_get_inverse_view_matrix_ptr()
{
    return &inverse_view_matrix;
}

/* ------------------------------------------------------------------------- */

void camera_update_all()
{
    if (camera_exists)
        inverse_view_matrix = mat3_inverse(
                transform_get_world_matrix(camera_entity));
}
