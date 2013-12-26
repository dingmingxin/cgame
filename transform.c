#include "transform.h"

#include <stdio.h>
#include <math.h>

typedef struct Transform Transform;
struct Transform
{
    Vec2 position;
    float rotation;
};

static unsigned int max_entity = 0;
static Transform transforms[ENTITY_MAX];

/* ------------------------------------------------------------------------- */

void transform_add(Entity ent)
{
    transforms[ent].position = vec2(0.0f, 0.0f);
    transforms[ent].rotation = 0.0f;

    if (ent > max_entity)
        max_entity = ent;
}
void transform_remove(Entity ent)
{
}

void transform_set_position(Entity ent, Vec2 pos)
{
    transforms[ent].position = pos;
}
Vec2 transform_get_position(Entity ent)
{
    return transforms[ent].position;
}

void transform_set_rotation(Entity ent, float rot)
{
    transforms[ent].rotation = rot;
}
void transform_rotate(Entity ent, float rot)
{
    transforms[ent].rotation += rot;
}
float transform_get_rotation(Entity ent)
{
    return transforms[ent].rotation;
}

Mat3 transform_get_world_matrix(Entity ent)
{
    return mat3_translation_rotation(transforms[ent].position,
            transforms[ent].rotation);
}

void transform_save_all(FILE *file)
{
    fwrite(&max_entity, sizeof(max_entity), 1, file);
    fwrite(transforms, sizeof(Transform), max_entity + 1, file);
}

void transform_load_all(FILE *file)
{
    fread(&max_entity, sizeof(max_entity), 1, file);
    fread(transforms, sizeof(Transform), max_entity + 1, file);
}

