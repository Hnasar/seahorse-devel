/*
 * Seahorse
 *
 * Copyright (C) 2006 Stefan Walter
 * Copyright (C) 2011 Collabora Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <libintl.h>

#include <glib/gi18n.h>

#include "seahorse-certificate.h"
#include "seahorse-pkcs11.h"
#include "seahorse-pkcs11-helpers.h"
#include "seahorse-pkcs11-operations.h"
#include "seahorse-token.h"

#include "seahorse-source.h"
#include "seahorse-registry.h"
#include "seahorse-util.h"

enum {
	PROP_0,
	PROP_LABEL,
	PROP_DESCRIPTION,
	PROP_ICON,
	PROP_SLOT,
	PROP_FLAGS,
	PROP_URI,
};

struct _SeahorseTokenPrivate {
	GckSlot *slot;
	gchar *uri;
	GHashTable *objects;
};

static void          receive_object                      (SeahorseToken *self,
                                                          GckObject *obj);

static void          seahorse_token_source_iface  (SeahorseSourceIface *iface);

static void          seahorse_token_collection_iface    (GcrCollectionIface *iface);

G_DEFINE_TYPE_EXTENDED (SeahorseToken, seahorse_token, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (GCR_TYPE_COLLECTION, seahorse_token_collection_iface);
                        G_IMPLEMENT_INTERFACE (SEAHORSE_TYPE_SOURCE, seahorse_token_source_iface);
);


typedef struct {
	SeahorseToken *token;
	GCancellable *cancellable;
	GHashTable *checks;
	GckEnumerator *enumerator;
} pkcs11_refresh_closure;

static void
pkcs11_refresh_free (gpointer data)
{
	pkcs11_refresh_closure *closure = data;
	g_object_unref (closure->token);
	g_clear_object (&closure->cancellable);
	g_hash_table_destroy (closure->checks);
	g_clear_object (&closure->enumerator);
	g_free (closure);
}

static gboolean
remove_each_object (gpointer key,
                    gpointer value,
                    gpointer user_data)
{
	SeahorseToken *token = SEAHORSE_TOKEN (user_data);
	GckObject *object = GCK_OBJECT (value);
	seahorse_token_remove_object (token, object);
	return TRUE;
}

static void
on_refresh_next_objects (GObject *source,
                         GAsyncResult *result,
                         gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	pkcs11_refresh_closure *closure = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;
	gulong handle;
	GList *objects;
	GList *l;

	objects = gck_enumerator_next_finish (closure->enumerator, result, &error);

	/* Remove all objects that were found, from the check table */
	for (l = objects; l; l = g_list_next (l)) {
		receive_object (closure->token, l->data);
		handle = gck_object_get_handle (l->data);
		g_hash_table_remove (closure->checks, &handle);
	}

	gck_list_unref_free (objects);

	/* If there was an error, report it */
	if (error != NULL) {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);

	/* Otherwise if there were objects go back and get more */
	} else if (objects != NULL) {
		gck_enumerator_next_async (closure->enumerator, 16, closure->cancellable,
		                           on_refresh_next_objects, g_object_ref (res));

	/* Otherwise we're done, remove everything not found */
	} else {
		g_hash_table_foreach_remove (closure->checks, remove_each_object, closure->token);
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);
}

void
seahorse_token_refresh_async (SeahorseToken *token,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
	GSimpleAsyncResult *res;
	pkcs11_refresh_closure *closure;
	GckAttributes *attrs;
	GckSlot *slot;
	GList *objects, *l;
	gulong handle;

	res = g_simple_async_result_new (G_OBJECT (token), callback, user_data,
	                                 seahorse_token_refresh_async);
	closure = g_new0 (pkcs11_refresh_closure, 1);
	closure->checks = g_hash_table_new_full (seahorse_pkcs11_ulong_hash,
	                                         seahorse_pkcs11_ulong_equal,
	                                         g_free, g_object_unref);
	closure->token = g_object_ref (token);
	closure->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
	g_simple_async_result_set_op_res_gpointer (res, closure, pkcs11_refresh_free);

	/* Make note of all the objects that were there */
	objects = gcr_collection_get_objects (GCR_COLLECTION (token));
	for (l = objects; l; l = g_list_next (l)) {
		handle = gck_object_get_handle (l->data);
		g_hash_table_insert (closure->checks,
		                     g_memdup (&handle, sizeof (handle)),
		                     g_object_ref (l->data));
	}
	g_list_free (objects);

	attrs = gck_attributes_new ();
	gck_attributes_add_boolean (attrs, CKA_TOKEN, TRUE);
	gck_attributes_add_ulong (attrs, CKA_CLASS, CKO_CERTIFICATE);

	slot = seahorse_token_get_slot (closure->token);
	closure->enumerator = gck_slot_enumerate_objects (slot, attrs, GCK_SESSION_READ_WRITE);
	g_return_if_fail (closure->enumerator != NULL);

	gck_attributes_unref (attrs);

	/* This enumerator creates objects of type SeahorseCertificate */
	gck_enumerator_set_object_type (closure->enumerator, SEAHORSE_TYPE_CERTIFICATE);

	gck_enumerator_next_async (closure->enumerator, 16, closure->cancellable,
	                           on_refresh_next_objects, g_object_ref (res));

	g_object_unref (res);
}

gboolean
seahorse_token_refresh_finish (SeahorseToken *token,
                               GAsyncResult *result,
                               GError **error)
{
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (token),
	                      seahorse_token_refresh_async), FALSE);

	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	return TRUE;

}

static void
seahorse_token_init (SeahorseToken *self)
{
	self->pv = (G_TYPE_INSTANCE_GET_PRIVATE (self, SEAHORSE_TYPE_TOKEN, SeahorseTokenPrivate));
	self->pv->objects = g_hash_table_new_full (seahorse_pkcs11_ulong_hash,
	                                           seahorse_pkcs11_ulong_equal,
	                                           g_free, g_object_unref);
}

static void
seahorse_token_constructed (GObject *obj)
{
	SeahorseToken *self = SEAHORSE_TOKEN (obj);
	GckUriData *data;

	G_OBJECT_CLASS (seahorse_token_parent_class)->constructed (obj);

	g_return_if_fail (self->pv->slot != NULL);

	data = gck_uri_data_new ();
	data->token_info = gck_slot_get_token_info (self->pv->slot);
	self->pv->uri = gck_uri_build (data, GCK_URI_FOR_TOKEN);
	gck_uri_data_free (data);

	seahorse_token_refresh_async (self, NULL, NULL, NULL);
}

static void
seahorse_token_get_property (GObject *object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
	SeahorseToken *self = SEAHORSE_TOKEN (object);
	GckTokenInfo *token;

	switch (prop_id) {
	case PROP_LABEL:
		token = gck_slot_get_token_info (self->pv->slot);
		if (token == NULL)
			g_value_set_string (value, _("Unknown"));
		else
			g_value_set_string (value, token->label);
		gck_token_info_free (token);
		break;
	case PROP_DESCRIPTION:
		token = gck_slot_get_token_info (self->pv->slot);
		if (token == NULL)
			g_value_set_string (value, NULL);
		else
			g_value_set_string (value, token->manufacturer_id);
		gck_token_info_free (token);
		break;
	case PROP_ICON:
		token = gck_slot_get_token_info (self->pv->slot);
		if (token == NULL)
			g_value_take_object (value, g_themed_icon_new (GTK_STOCK_DIALOG_QUESTION));
		else
			g_value_take_object (value, gcr_icon_for_token (token));
		gck_token_info_free (token);
		break;
	case PROP_SLOT:
		g_value_set_object (value, self->pv->slot);
		break;
	case PROP_FLAGS:
		g_value_set_uint (value, 0);
		break;
	case PROP_URI:
		g_value_set_string (value, self->pv->uri);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
seahorse_token_set_property (GObject *object,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
	SeahorseToken *self = SEAHORSE_TOKEN (object);

	switch (prop_id) {
	case PROP_SLOT:
		g_return_if_fail (!self->pv->slot);
		self->pv->slot = g_value_get_object (value);
		g_return_if_fail (self->pv->slot);
		g_object_ref (self->pv->slot);
		break;
	};
}

static void
seahorse_token_dispose (GObject *obj)
{
	SeahorseToken *self = SEAHORSE_TOKEN (obj);

	/* The keyring object */
	if (self->pv->slot)
		g_object_unref (self->pv->slot);
	self->pv->slot = NULL;

	G_OBJECT_CLASS (seahorse_token_parent_class)->dispose (obj);
}

static void
seahorse_token_finalize (GObject *obj)
{
	SeahorseToken *self = SEAHORSE_TOKEN (obj);

	g_hash_table_destroy (self->pv->objects);
	g_assert (self->pv->slot == NULL);
	g_free (self->pv->uri);

	G_OBJECT_CLASS (seahorse_token_parent_class)->finalize (obj);
}

static void
seahorse_token_class_init (SeahorseTokenClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (SeahorseTokenPrivate));

	gobject_class->constructed = seahorse_token_constructed;
	gobject_class->dispose = seahorse_token_dispose;
	gobject_class->finalize = seahorse_token_finalize;
	gobject_class->set_property = seahorse_token_set_property;
	gobject_class->get_property = seahorse_token_get_property;

	g_object_class_override_property (gobject_class, PROP_LABEL, "label");
	g_object_class_override_property (gobject_class, PROP_DESCRIPTION, "description");
	g_object_class_override_property (gobject_class, PROP_URI, "uri");
	g_object_class_override_property (gobject_class, PROP_ICON, "icon");

	g_object_class_install_property (gobject_class, PROP_SLOT,
	         g_param_spec_object ("slot", "Slot", "Pkcs#11 SLOT",
	                              GCK_TYPE_SLOT, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (gobject_class, PROP_FLAGS,
	         g_param_spec_uint ("flags", "Flags", "Object Token flags.",
	                            0, G_MAXUINT, 0, G_PARAM_READABLE));
}

static void
seahorse_token_source_iface (SeahorseSourceIface *iface)
{

}

static guint
seahorse_token_get_length (GcrCollection *collection)
{
	SeahorseToken *self = SEAHORSE_TOKEN (collection);
	return g_hash_table_size (self->pv->objects);
}

static GList *
seahorse_token_get_objects (GcrCollection *collection)
{
	SeahorseToken *self = SEAHORSE_TOKEN (collection);
	return g_hash_table_get_values (self->pv->objects);
}

static gboolean
seahorse_token_contains (GcrCollection *collection,
                         GObject *object)
{
	SeahorseToken *self = SEAHORSE_TOKEN (collection);
	gulong handle;

	if (!GCK_IS_OBJECT (object))
		return FALSE;

	handle = gck_object_get_handle (GCK_OBJECT (object));
	return g_hash_table_lookup (self->pv->objects, &handle) == object;
}

static void
seahorse_token_collection_iface (GcrCollectionIface *iface)
{
	iface->get_length = seahorse_token_get_length;
	iface->get_objects = seahorse_token_get_objects;
	iface->contains = seahorse_token_contains;
}

/* --------------------------------------------------------------------------
 * PUBLIC
 */

SeahorseToken *
seahorse_token_new (GckSlot *slot)
{
	return g_object_new (SEAHORSE_TYPE_TOKEN,
	                     "slot", slot,
	                     NULL);
}

GckSlot *
seahorse_token_get_slot (SeahorseToken *self)
{
	g_return_val_if_fail (SEAHORSE_IS_TOKEN (self), NULL);
	return self->pv->slot;
}

static void
receive_object (SeahorseToken *self,
                GckObject *obj)
{
	GckAttributes *attrs;
	GckObject *prev;
	gulong handle;

	g_return_if_fail (SEAHORSE_IS_TOKEN (self));

	handle = gck_object_get_handle (obj);
	prev = g_hash_table_lookup (self->pv->objects, &handle);
	if (prev != NULL) {
		attrs = gck_object_attributes_get_attributes (GCK_OBJECT_ATTRIBUTES (obj));
		gck_object_attributes_set_attributes (GCK_OBJECT_ATTRIBUTES (prev), attrs);
		gck_attributes_unref (attrs);
	} else {
		g_hash_table_insert (self->pv->objects,
		                     g_memdup (&handle, sizeof (handle)),
		                     g_object_ref (obj));
		gcr_collection_emit_added (GCR_COLLECTION (self), G_OBJECT (obj));
	}
}

void
seahorse_token_remove_object (SeahorseToken *self,
                              GckObject *object)
{
	gulong handle;

	g_return_if_fail (SEAHORSE_IS_TOKEN (self));
	g_return_if_fail (GCK_IS_OBJECT (object));

	g_object_ref (object);

	handle = gck_object_get_handle (object);
	g_return_if_fail (g_hash_table_lookup (self->pv->objects, &handle) == object);

	g_hash_table_remove (self->pv->objects, &handle);
	gcr_collection_emit_removed (GCR_COLLECTION (self), G_OBJECT (object));

	g_object_unref (object);
}