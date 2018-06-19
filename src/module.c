/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2018,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

#include "common/thread/thread.h"
#include "common/avl/avl.h"

#include "refobject.h"
#include "module.h"

struct module_tag {
    refobject_base_t __base;
    mutex_t lock;
    const module_client_handler_t *client_handlers;
    size_t client_handlers_len;
    module_setup_handler_t freecb;
    void *userdata;
};


struct module_container_tag {
    refobject_base_t __base;
    mutex_t lock;
    avl_tree *module;
};

static int compare_refobject_t_name(void *arg, void *a, void *b)
{
    return strcmp(refobject_get_name(a), refobject_get_name(b));
}

static void __module_container_free(refobject_t self, void **userdata)
{
    module_container_t *cont = REFOBJECT_TO_TYPE(self, module_container_t *);
    thread_mutex_destroy(&(cont->lock));
    avl_tree_free(cont->module, (avl_free_key_fun_type)refobject_unref);
}

module_container_t *    module_container_new(void)
{
    module_container_t *ret = REFOBJECT_TO_TYPE(refobject_new(sizeof(module_container_t), __module_container_free, NULL, NULL, NULL), module_container_t *);

    if (!ret)
        return NULL;

    thread_mutex_create(&(ret->lock));

    ret->module = avl_tree_new(compare_refobject_t_name, NULL);

    return ret;
}

int                     module_container_add_module(module_container_t *self, module_t *module)
{
    if (!self)
        return -1;

    if (refobject_ref(module) != 0)
        return -1;

    thread_mutex_lock(&(self->lock));
    avl_insert(self->module, module);
    thread_mutex_unlock(&(self->lock));

    return 0;
}

int                     module_container_delete_module(module_container_t *self, const char *name)
{
    module_t *module;

    if (!self || !name)
        return -1;

    module = module_container_get_module(self, name);
    if (!module)
        return -1;

    thread_mutex_lock(&(self->lock));
    avl_delete(self->module, module, (avl_free_key_fun_type)refobject_unref);
    thread_mutex_unlock(&(self->lock));

    refobject_unref(module);

    return 0;
}

module_t *              module_container_get_module(module_container_t *self, const char *name)
{
    refobject_t search;
    module_t *ret;

    if (!self || !name)
        return NULL;

    search = refobject_new(sizeof(refobject_base_t), NULL, NULL, name, NULL);

    if (avl_get_by_key(self->module, REFOBJECT_TO_TYPE(search, void *), (void**)&ret) != 0) {
        ret = NULL;
    }

    refobject_unref(search);
    refobject_ref(ret);

    return ret;
}

static void __module_free(refobject_t self, void **userdata)
{
    module_t *mod = REFOBJECT_TO_TYPE(self, module_t *);

    if (mod->freecb)
        mod->freecb(mod, &(mod->userdata));

    if (mod->userdata)
        free(mod->userdata);

    thread_mutex_destroy(&(mod->lock));
}

module_t *              module_new(const char *name, module_setup_handler_t newcb, module_setup_handler_t freecb, void *userdata)
{
    module_t *ret = REFOBJECT_TO_TYPE(refobject_new(sizeof(module_t), __module_free, NULL, name, NULL), module_t *);

    if (!ret)
        return NULL;

    thread_mutex_create(&(ret->lock));

    ret->userdata = userdata;
    ret->freecb = freecb;

    if (newcb) {
        if (newcb(ret, &(ret->userdata)) != 0) {
            refobject_unref(ret);
            return NULL;
        }
    }

    return ret;
}

const module_client_handler_t * module_get_client_handler(module_t *self, const char *name)
{
    size_t i;

    if (!self || !name)
        return NULL;

    thread_mutex_lock(&(self->lock));
    for (i = 0; i < self->client_handlers_len; i++) {
        if (self->client_handlers[i].name && strcmp(self->client_handlers[i].name, name) == 0) {
            thread_mutex_unlock(&(self->lock));
            return &(self->client_handlers[i]);
        }
    }
    thread_mutex_unlock(&(self->lock));

    return NULL;
}

int                             module_add_client_handler(module_t *self, const module_client_handler_t *handlers, size_t len)
{
    if (!self)
        return -1;

    thread_mutex_lock(&(self->lock));
    self->client_handlers = handlers;
    self->client_handlers_len = len;
    thread_mutex_unlock(&(self->lock));

    return 0;
}
