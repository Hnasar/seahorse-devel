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

#include "seahorse-pgp-key.h"
#include "seahorse-unknown-source.h"

#include "seahorse-place.h"
#include "seahorse-registry.h"
#include "seahorse-unknown.h"

#include <gcr/gcr-base.h>

#include <glib/gi18n.h>

enum {
	PROP_0,
	PROP_LABEL,
	PROP_DESCRIPTION,
	PROP_ICON,
	PROP_URI,
	PROP_ACTIONS
};

struct _SeahorseUnknownSource {
	GObject parent;
	GHashTable *keys;
};

struct _SeahorseUnknownSourceClass {
	GObjectClass parent_class;
};

static void      seahorse_unknown_source_collection_iface      (GcrCollectionIface *iface);

static void      seahorse_unknown_source_place_iface           (SeahorsePlaceIface *iface);

G_DEFINE_TYPE_WITH_CODE (SeahorseUnknownSource, seahorse_unknown_source, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GCR_TYPE_COLLECTION, seahorse_unknown_source_collection_iface);
                         G_IMPLEMENT_INTERFACE (SEAHORSE_TYPE_PLACE, seahorse_unknown_source_place_iface);
);

static void
seahorse_unknown_source_init (SeahorseUnknownSource *self)
{
	self->keys = g_hash_table_new_full (seahorse_pgp_keyid_hash,
	                                    seahorse_pgp_keyid_equal,
	                                    g_free, g_object_unref);
}

static void
seahorse_unknown_source_get_property (GObject *obj,
                                      guint prop_id,
                                      GValue *value,
                                      GParamSpec *pspec)
{
	switch (prop_id) {
	case PROP_LABEL:
		g_value_set_string (value, "");
		break;
	case PROP_DESCRIPTION:
	case PROP_URI:
		g_value_set_string (value, NULL);
		break;
	case PROP_ICON:
		g_value_set_object (value, NULL);
		break;
	case PROP_ACTIONS:
		g_value_set_object (value, NULL);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
seahorse_unknown_source_finalize (GObject *obj)
{
	SeahorseUnknownSource *self = SEAHORSE_UNKNOWN_SOURCE (obj);

	g_hash_table_destroy (self->keys);

	G_OBJECT_CLASS (seahorse_unknown_source_parent_class)->finalize (obj);
}

static void
seahorse_unknown_source_class_init (SeahorseUnknownSourceClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	gobject_class->get_property = seahorse_unknown_source_get_property;
	gobject_class->finalize = seahorse_unknown_source_finalize;

	g_object_class_override_property (gobject_class, PROP_LABEL, "label");
	g_object_class_override_property (gobject_class, PROP_DESCRIPTION, "description");
	g_object_class_override_property (gobject_class, PROP_ICON, "icon");
	g_object_class_override_property (gobject_class, PROP_ACTIONS, "actions");
	g_object_class_override_property (gobject_class, PROP_URI, "uri");
}

static guint
seahorse_unknown_source_get_length (GcrCollection *collection)
{
	SeahorseUnknownSource *self = SEAHORSE_UNKNOWN_SOURCE (collection);
	return g_hash_table_size (self->keys);
}

static GList *
seahorse_unknown_source_get_objects (GcrCollection *collection)
{
	SeahorseUnknownSource *self = SEAHORSE_UNKNOWN_SOURCE (collection);
	return g_hash_table_get_values (self->keys);
}

static gboolean
seahorse_unknown_source_contains (GcrCollection *collection,
                                  GObject *object)
{
	SeahorseUnknownSource *self = SEAHORSE_UNKNOWN_SOURCE (collection);
	const gchar *identifier = seahorse_object_get_identifier (SEAHORSE_OBJECT (object));
	return g_hash_table_lookup (self->keys, identifier) == object;
}

static void
seahorse_unknown_source_collection_iface (GcrCollectionIface *iface)
{
	iface->contains = seahorse_unknown_source_contains;
	iface->get_length = seahorse_unknown_source_get_length;
	iface->get_objects = seahorse_unknown_source_get_objects;
}

static void
seahorse_unknown_source_place_iface (SeahorsePlaceIface *iface)
{
	/* no implementation */
}

SeahorseUnknownSource*
seahorse_unknown_source_new (void)
{
	return g_object_new (SEAHORSE_TYPE_UNKNOWN_SOURCE, NULL);
}

static void
on_cancellable_gone (gpointer user_data,
                     GObject *where_the_object_was)
{
	/* TODO: Change the icon */
}

SeahorseObject *
seahorse_unknown_source_add_object (SeahorseUnknownSource *self,
                                    const gchar *keyid,
                                    GCancellable *cancellable)
{
	SeahorseObject *object;

	g_return_val_if_fail (keyid != NULL, NULL);
	g_return_val_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable), NULL);

	object = g_hash_table_lookup (self->keys, keyid);
	if (object == NULL) {
		object = SEAHORSE_OBJECT (seahorse_unknown_new (self, keyid, NULL));
		g_hash_table_insert (self->keys, g_strdup (keyid), object);
	}

	if (cancellable)
		g_object_weak_ref (G_OBJECT (cancellable), on_cancellable_gone, object);

	return object;
}
