#include "entity.h"

#include <assert.h>
#include <stdlib.h>

#include "saveload.h"
#include "entitymap.h"
#include "pool.h"

static int dummy;
static void *dummy_ptr = &dummy; /* put in EntityMap to differentiate from
                                    NULL */

static EntityMap *emap;

static EntityMap *destroyed_map; /* whether entity is destroyed */
typedef struct DestroyEntry DestroyEntry;
struct DestroyEntry
{
    Entity ent;
    unsigned int pass;
};
union { Pool pool; DestroyEntry *array; } destroyed; /* list of destroyed, will
                                                        _remove() */

/*
 * life cycle
 * ----------
 *
 *     doesn't exist (or removed):
 *         emap[ent] = NULL
 *         destroyed_map[ent] = NULL
 *         ent not in destroyed
 *
 *         on entity_create() go to exists
 *
 *     exists:
 *         emap[ent] = dummy_ptr
 *         destroyed_map[ent] = NULL
 *         ent not in destroyed
 *
 *         on entity_destroy() go to destroyed
 *
 *     destroyed:
 *         emap[ent] = NULL
 *         destroyed_map[ent] = dummy_ptr
 *         { ent, pass } in destroyed
 *
 *         each update if ++pass >= 2 then _remove() (go back to doesn't exist)
 */

/* ------------------------------------------------------------------------- */

static inline bool _exists(Entity ent)
{
    return entitymap_get(emap, ent) == dummy_ptr;
}

Entity entity_create()
{
    unsigned int e = 0;

    while (_exists(e))
        ++e;

    entitymap_set(emap, e, dummy_ptr);
    printf("new id: %u\n", e);
    return e;
}

/* actually remove an entity entirely */
static inline void _remove(Entity ent)
{
    printf("%u removed\n", ent);
    entitymap_set(emap, ent, NULL);
    entitymap_set(destroyed_map, ent, NULL);
}

void entity_destroy(Entity ent)
{
    DestroyEntry *entry;

    if (!_exists(ent))
        return;
    if (entitymap_get(destroyed_map, ent) == dummy_ptr)
        return; /* already noted */

    entitymap_set(destroyed_map, ent, dummy_ptr);
    entry = pool_new_obj(&destroyed.pool);
    *entry = (DestroyEntry) { ent, 0 };
}

bool entity_destroyed(Entity ent)
{
    return entitymap_get(destroyed_map, ent) == dummy_ptr;
}

/* ------------------------------------------------------------------------- */

void entity_init()
{
    emap = entitymap_new(NULL);
    destroyed_map = entitymap_new(NULL);
    pool_init(&destroyed.pool, sizeof(DestroyEntry), NULL);
}
void entity_deinit()
{
    pool_deinit(&destroyed.pool);
    entitymap_free(destroyed_map);
    entitymap_free(emap);
}

void entity_update_all()
{
    /* check destroyeds -- first pass we let go, second pass we remove */
    for (unsigned int i = 0; i < destroyed.pool.num; ++i)
        if (destroyed.array[i].pass == 0)
        {
            printf("%u first pass\n", destroyed.array[i].ent);
            ++destroyed.array[i].pass;
        }
        else
        {
            _remove(destroyed.array[i].ent);
            pool_free_obj(&destroyed.pool, &destroyed.array[i]);
        }
}

void entity_save(Entity *ent, FILE *file)
{
    uint_save(ent, file);
}
void entity_load(Entity *ent, FILE *file)
{
    uint_load(ent, file);
}

