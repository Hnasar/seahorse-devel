/*
 * Seahorse
 *
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

#include "seahorse-sidebar.h"

#include "seahorse-registry.h"

#include "gkr/seahorse-gkr.h"
#include "pgp/seahorse-pgp.h"
#include "pkcs11/seahorse-pkcs11.h"
#include "ssh/seahorse-ssh.h"

#include <glib/gi18n.h>

typedef enum {
	SEAHORSE_SIDEBAR_MODE_COMBINED,
	SEAHORSE_SIDEBAR_MODE_CHECKED,
	SEAHORSE_SIDEBAR_MODE_SELECTED
} SeahorseSidebarMode;

struct _SeahorseSidebar {
	GtkScrolledWindow parent;

	GtkTreeView *tree_view;

	GtkListStore *store;
	GPtrArray *backends;
	SeahorseSidebarMode mode;
	GHashTable *checked;
	GcrUnionCollection *objects;
	GcrCollection *selected;

	guint update_places_sig;
};

struct _SeahorseSidebarClass {
	GtkScrolledWindowClass parent_class;
};

enum {
	PROP_0,
	PROP_COLLECTION,
	PROP_COMBINED
};

typedef enum {
	TYPE_BACKEND,
	TYPE_PLACE,
} RowType;

enum {
	SIDEBAR_ROW_TYPE,
	SIDEBAR_ICON,
	SIDEBAR_LABEL,
	SIDEBAR_TOOLTIP,
	SIDEBAR_EDITABLE,
	SIDEBAR_CATEGORY,
	SIDEBAR_COLLECTION,
	SIDEBAR_N_COLUMNS
};

static GType column_types[] = {
	G_TYPE_UINT,
	0 /* later */,
	G_TYPE_STRING,
	G_TYPE_STRING,
	G_TYPE_BOOLEAN,
	G_TYPE_STRING,
	0 /* later */,
};

G_DEFINE_TYPE (SeahorseSidebar, seahorse_sidebar, GTK_TYPE_SCROLLED_WINDOW);

static void
seahorse_sidebar_init (SeahorseSidebar *self)
{
	g_assert (SIDEBAR_N_COLUMNS == G_N_ELEMENTS (column_types));
	column_types[SIDEBAR_ICON] = G_TYPE_ICON;
	column_types[SIDEBAR_COLLECTION] = GCR_TYPE_COLLECTION;
	self->store = gtk_list_store_newv (SIDEBAR_N_COLUMNS, column_types);

	self->backends = g_ptr_array_new_with_free_func (g_object_unref);
	self->checked = g_hash_table_new (g_direct_hash, g_direct_equal);
	self->objects = GCR_UNION_COLLECTION (gcr_union_collection_new ());
	self->mode = SEAHORSE_SIDEBAR_MODE_SELECTED;
}

static void
next_or_append_row (SeahorseSidebar *self,
                    GtkTreeIter *iter,
                    const gchar *category,
                    GcrCollection *collection)
{
	GcrCollection *row_collection;
	gchar *row_category;
	gboolean found;

	/*
	 * We try to keep the same row in order to preserve checked state
	 * and selections. So if the next row matches the values we want to
	 * set on it, then just keep that row.
	 *
	 * This is complicated by the fact that the first row being inserted
	 * doesn't have a valid iter, and we don't have a standard way to
	 * detect that an iter isn't valid.
	 */

	/* A marker that tells us the iter is not yet valid */
	if (iter->stamp == GPOINTER_TO_INT (iter) && iter->user_data3 == iter &&
	    iter->user_data2 == iter && iter->user_data == iter) {
		if (!gtk_tree_model_get_iter_first (GTK_TREE_MODEL (self->store), iter))
			gtk_list_store_append (self->store, iter);
		return;
	}

	if (!gtk_tree_model_iter_next (GTK_TREE_MODEL (self->store), iter)) {
		gtk_list_store_append (self->store, iter);
		return;
	}

	for (;;) {
		gtk_tree_model_get (GTK_TREE_MODEL (self->store), iter,
		                    SIDEBAR_CATEGORY, &row_category,
		                    SIDEBAR_COLLECTION, &row_collection,
		                    -1);

		found = (g_strcmp0 (row_category, category) == 0 && row_collection == collection);

		g_free (row_category);
		g_clear_object (&row_collection);

		if (found)
			return;

		g_hash_table_remove (self->checked, row_collection);
		if (!gtk_list_store_remove (self->store, iter)) {
			gtk_list_store_append (self->store, iter);
			return;
		}
	}
}

static void
update_backend (SeahorseSidebar *self,
                GcrCollection *backend,
                GtkTreeIter *iter)
{
	GList *collections, *l;
	GParamSpec *spec;
	gchar *category;
	gchar *tooltip;
	gchar *label;
	GIcon *icon = NULL;

	collections = gcr_collection_get_objects (backend);

	/* Ignore categories that have nothing */
	if (collections == NULL)
		return;

	g_object_get (backend,
	              "name", &category,
	              "label", &label,
	              "description", &tooltip,
	              NULL);

	next_or_append_row (self, iter, category, GCR_COLLECTION (backend));
	gtk_list_store_set (self->store, iter,
	                    SIDEBAR_ROW_TYPE, TYPE_BACKEND,
	                    SIDEBAR_CATEGORY, category,
	                    SIDEBAR_LABEL, label,
	                    SIDEBAR_TOOLTIP, tooltip,
	                    SIDEBAR_EDITABLE, FALSE,
	                    SIDEBAR_COLLECTION, backend,
	                    -1);

	g_free (label);
	g_free (tooltip);

	for (l = collections; l != NULL; l = g_list_next (l)) {

		label = tooltip = NULL;
		g_object_get (l->data,
		              "label", &label,
		              "description", &tooltip,
		              "icon", &icon,
		              NULL);

		spec = g_object_class_find_property (G_OBJECT_GET_CLASS (l->data), "label");
		g_return_if_fail (spec != NULL);

		next_or_append_row (self, iter, category, l->data);
		gtk_list_store_set (self->store, iter,
		                    SIDEBAR_ROW_TYPE, TYPE_PLACE,
		                    SIDEBAR_CATEGORY, category,
		                    SIDEBAR_LABEL, label,
		                    SIDEBAR_TOOLTIP, tooltip,
		                    SIDEBAR_ICON, icon,
		                    SIDEBAR_EDITABLE, (spec->flags & G_PARAM_WRITABLE) ? TRUE : FALSE,
		                    SIDEBAR_COLLECTION, l->data,
		                    -1);

		g_clear_object (&icon);
		g_free (label);
		g_free (tooltip);
	}

	g_free (category);
	g_list_free (collections);
}

static void
update_objects_in_collection (SeahorseSidebar *self)
{
	GList *collections;
	gboolean include;
	gboolean have;
	GList *l;
	guint i;

	for (i = 0; i < self->backends->len; i++) {
		collections = gcr_collection_get_objects (self->backends->pdata[i]);
		for (l = collections; l != NULL; l = g_list_next (l)) {
			switch (self->mode) {
			case SEAHORSE_SIDEBAR_MODE_COMBINED:
				include = TRUE;
				break;
			case SEAHORSE_SIDEBAR_MODE_CHECKED:
				include = (g_hash_table_lookup (self->checked, l->data) != NULL);
				break;
			case SEAHORSE_SIDEBAR_MODE_SELECTED:
				include = (l->data == self->selected);
				break;
			default:
				g_assert_not_reached ();
			}

			have = gcr_union_collection_have (self->objects, l->data);
			if (include && !have)
				gcr_union_collection_add (self->objects, l->data);
			else if (!include && have)
				gcr_union_collection_remove (self->objects, l->data);
		}
		g_list_free (collections);
	}
}

static void
update_objects_for_selection (SeahorseSidebar *self,
                              GtkTreeSelection *selection)
{
	GcrCollection *selected = NULL;
	GtkTreeModel *model;
	GtkTreeIter iter;

	if (gtk_tree_selection_get_selected (selection, &model, &iter))
		gtk_tree_model_get (model, &iter,
		                    SIDEBAR_COLLECTION, &selected,
		                    -1);

	if (selected != self->selected) {
		g_clear_object (&self->selected);
		self->selected = selected ? g_object_ref (selected) : NULL;
		if (self->mode == SEAHORSE_SIDEBAR_MODE_SELECTED)
			update_objects_in_collection (self);
	}

	g_clear_object (&selected);
}

static void
update_objects_for_checked (SeahorseSidebar *self,
                            GcrCollection *place)
{
	g_hash_table_insert (self->checked, place, place);

	if (self->mode == SEAHORSE_SIDEBAR_MODE_SELECTED)
		self->mode = SEAHORSE_SIDEBAR_MODE_CHECKED;
	if (self->mode != SEAHORSE_SIDEBAR_MODE_COMBINED)
		update_objects_in_collection (self);
}

static void
update_objects_for_unchecked (SeahorseSidebar *self,
                              GcrCollection *place)
{
	if (!g_hash_table_remove (self->checked, place))
		g_assert_not_reached ();

	if (self->mode == SEAHORSE_SIDEBAR_MODE_CHECKED &&
	    g_hash_table_size (self->checked) == 0)
		self->mode = SEAHORSE_SIDEBAR_MODE_SELECTED;
	if (self->mode != SEAHORSE_SIDEBAR_MODE_COMBINED)
		update_objects_in_collection (self);
}

static void
update_objects_for_combine (SeahorseSidebar *self)
{
	if (self->mode != SEAHORSE_SIDEBAR_MODE_COMBINED) {
		self->mode = SEAHORSE_SIDEBAR_MODE_COMBINED;
		update_objects_in_collection (self);
	}
}

static void
update_objects_for_uncombine (SeahorseSidebar *self)
{
	if (self->mode == SEAHORSE_SIDEBAR_MODE_COMBINED) {
		if (g_hash_table_size (self->checked) > 0)
			self->mode = SEAHORSE_SIDEBAR_MODE_CHECKED;
		else
			self->mode = SEAHORSE_SIDEBAR_MODE_SELECTED;
		update_objects_in_collection (self);
	}
}

static void
update_places (SeahorseSidebar *self)
{
	GtkTreeIter iter;
	guint i;

	/* A marker that tells us the iter is not yet valid */
	iter.stamp = GPOINTER_TO_INT (&iter);
	iter.user_data3 = iter.user_data2 = iter.user_data = &iter;

	for (i = 0; i < self->backends->len; i++)
		update_backend (self, GCR_COLLECTION (self->backends->pdata[i]), &iter);

	/* Update selection */
	update_objects_in_collection (self);
}

static gboolean
on_idle_update_places (gpointer user_data)
{
	SeahorseSidebar *self = SEAHORSE_SIDEBAR (user_data);

	self->update_places_sig = 0;
	update_places (self);

	return FALSE; /* don't call again */
}

static void
update_places_later (SeahorseSidebar *self)
{
	if (!self->update_places_sig)
		self->update_places_sig = g_idle_add (on_idle_update_places, self);
}

static void
on_cell_renderer_heading_visible (GtkTreeViewColumn *column,
                                  GtkCellRenderer *cell,
                                  GtkTreeModel *model,
                                  GtkTreeIter *iter,
                                  gpointer user_data)
{
	RowType type;
	gtk_tree_model_get (model, iter,
	                    SIDEBAR_ROW_TYPE, &type,
	                    -1);
	g_object_set (cell,
	              "visible", (type == TYPE_BACKEND),
	              NULL);
}

static void
on_padding_cell_renderer (GtkTreeViewColumn *column,
                          GtkCellRenderer *cell,
                          GtkTreeModel *model,
                          GtkTreeIter *iter,
                          gpointer user_data)
{
	RowType type;
	gtk_tree_model_get (model, iter,
	                    SIDEBAR_ROW_TYPE, &type,
	                    -1);

	if (type == TYPE_BACKEND) {
		g_object_set (cell,
		              "visible", FALSE,
		              "xpad", 0,
		              "ypad", 0,
		              NULL);
	} else {
		g_object_set (cell,
		              "visible", TRUE,
		              "xpad", 3,
		              "ypad", 3,
		              NULL);
	}
}

static void
on_cell_renderer_heading_not_visible (GtkTreeViewColumn *column,
                                      GtkCellRenderer *cell,
                                      GtkTreeModel *model,
                                      GtkTreeIter *iter,
                                      gpointer user_data)
{
	RowType type;
	gtk_tree_model_get (model, iter,
	                    SIDEBAR_ROW_TYPE, &type,
	                    -1);
	g_object_set (cell,
	              "visible", (type != TYPE_BACKEND),
	              NULL);
}

static void
on_cell_renderer_check (GtkTreeViewColumn *column,
                        GtkCellRenderer *cell,
                        GtkTreeModel *model,
                        GtkTreeIter *iter,
                        gpointer user_data)
{
	SeahorseSidebar *self = SEAHORSE_SIDEBAR (user_data);
	GcrCollection *collection;
	RowType type;
	gboolean active;
	gboolean inconsistent;
	gboolean visible;

	gtk_tree_model_get (model, iter,
	                    SIDEBAR_ROW_TYPE, &type,
	                    SIDEBAR_COLLECTION, &collection,
	                    -1);

	active = FALSE;
	inconsistent = FALSE;

	if (type == TYPE_BACKEND) {
		visible = FALSE;
	} else if (collection != NULL && g_hash_table_size (self->checked) > 0) {
		active = g_hash_table_lookup (self->checked, collection) != NULL;
		visible = TRUE;
	} else {
		visible = (collection == self->selected);
	}

	/* self->mnemonics_visible */
	g_object_set (cell,
	              "visible", visible,
	              "active", active,
	              "inconsistent", inconsistent,
	              NULL);

	g_clear_object (&collection);
}

static void
invalidate_all_rows (GtkTreeModel *model)
{
	GtkTreeIter iter;
	GtkTreePath *path;

	if (gtk_tree_model_get_iter_first (model, &iter)) {
		do {
			path = gtk_tree_model_get_path (model, &iter);
			gtk_tree_model_row_changed (model, path, &iter);
			gtk_tree_path_free (path);
		} while (gtk_tree_model_iter_next (model, &iter));
	}
}

static gboolean
on_tree_selection_validate (GtkTreeSelection *selection,
                            GtkTreeModel *model,
                            GtkTreePath *path,
                            gboolean path_currently_selected,
                            gpointer user_data)
{
	GtkTreeIter iter;
	RowType row_type;

	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter,
	                    SIDEBAR_ROW_TYPE, &row_type,
	                    -1);
	if (row_type == TYPE_BACKEND)
		return FALSE;

	return TRUE;
}

static void
on_tree_selection_changed (GtkTreeSelection *selection,
                           gpointer user_data)
{
	SeahorseSidebar *self = SEAHORSE_SIDEBAR (user_data);
	update_objects_for_selection (self, selection);
	invalidate_all_rows (GTK_TREE_MODEL (self->store));
}

static void
on_place_changed (GObject *obj,
                  GParamSpec *spec,
                  gpointer user_data)
{
	SeahorseSidebar *self = SEAHORSE_SIDEBAR (user_data);
	update_places_later (self);
}

static void
on_backend_changed (GObject *obj,
                    GParamSpec *spec,
                    gpointer user_data)
{
	SeahorseSidebar *self = SEAHORSE_SIDEBAR (user_data);
	update_places_later (self);
}

static void
on_place_added (GcrCollection *places,
                GObject *place,
                gpointer user_data)
{
	SeahorseSidebar *self = SEAHORSE_SIDEBAR (user_data);
	g_signal_connect (place, "notify", G_CALLBACK (on_place_changed), self);
	update_places_later (self);
}

static void
on_place_removed (GcrCollection *places,
                  GObject *object,
                  gpointer user_data)
{
	SeahorseSidebar *self = SEAHORSE_SIDEBAR (user_data);
	g_signal_handlers_disconnect_by_func (object, on_place_changed, self);
	update_places_later (self);
}

static gint
order_from_backend (GObject *backend)
{
	gchar *name;
	gint order;

	g_object_get (backend, "name", &name, NULL);

	if (name == NULL)
		order = 10;
	else if (g_str_equal (name, SEAHORSE_GKR_NAME))
		order = 0;
	else if (g_str_equal (name, SEAHORSE_PGP_NAME))
		order = 1;
	else if (g_str_equal (name, SEAHORSE_PKCS11_NAME))
		order = 2;
	else if (g_str_equal (name, SEAHORSE_SSH_NAME))
		order = 3;
	else
		order = 10;

	g_free (name);
	return order;
}

static gint
on_sort_backends (gconstpointer a,
                  gconstpointer b)
{
	gint ordera = order_from_backend (G_OBJECT (*((gpointer *)a)));
	gint orderb = order_from_backend (G_OBJECT (*((gpointer *)b)));
	return ordera - orderb;
}

static void
load_backends (SeahorseSidebar *self)
{
	GList *backends, *l;
	GList *places, *p;

	backends = seahorse_registry_object_instances (NULL, "backend", NULL);
	for (l = backends; l != NULL; l = g_list_next (l)) {
		g_ptr_array_add (self->backends, l->data);
		g_signal_connect (l->data, "added", G_CALLBACK (on_place_added), self);
		g_signal_connect (l->data, "removed", G_CALLBACK (on_place_removed), self);
		g_signal_connect (l->data, "notify", G_CALLBACK (on_backend_changed), self);

		places = gcr_collection_get_objects (l->data);
		for (p = places; p != NULL; p = g_list_next (p))
			on_place_added (l->data, p->data, self);
		g_list_free (places);
	}
	g_ptr_array_sort (self->backends, on_sort_backends);
	g_list_free (backends);
}

static void
on_checked_toggled  (GtkCellRendererToggle *renderer,
                     gchar *path_string,
                     gpointer user_data)
{
	SeahorseSidebar *self = SEAHORSE_SIDEBAR (user_data);
	GcrCollection *collection;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreePath *path;
	gboolean checked;
	gboolean have;

	model = GTK_TREE_MODEL (self->store);
	path = gtk_tree_path_new_from_string (path_string);
	gtk_tree_model_get_iter (model, &iter, path);

	checked = !gtk_cell_renderer_toggle_get_active (renderer);
	gtk_tree_model_get (model, &iter,
	                    SIDEBAR_COLLECTION, &collection,
	                    -1);
	have = g_hash_table_lookup (self->checked, collection) != NULL;

	if (checked && !have) {
		update_objects_for_checked (self, collection);
		if (g_hash_table_size (self->checked) == 1)
			invalidate_all_rows (model);
		else
			gtk_tree_model_row_changed (model, path, &iter);
	} else if (!checked && have) {
		update_objects_for_unchecked (self, collection);
		if (g_hash_table_size (self->checked) == 0)
			invalidate_all_rows (model);
		else
			gtk_tree_model_row_changed (model, path, &iter);
	}

	g_object_unref (collection);
	gtk_tree_path_free (path);
}

static void
seahorse_sidebar_constructed (GObject *obj)
{
	SeahorseSidebar *self = SEAHORSE_SIDEBAR (obj);
	GtkTreeSelection *selection;
	GtkTreeViewColumn *col;
	GtkTreeView *tree_view;
	GtkCellRenderer *cell;

	G_OBJECT_CLASS (seahorse_sidebar_parent_class)->constructed (obj);

	gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self)),
	                             GTK_STYLE_CLASS_SIDEBAR);

	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self),
	                                GTK_POLICY_NEVER,
	                                GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_hadjustment (GTK_SCROLLED_WINDOW (self), NULL);
	gtk_scrolled_window_set_vadjustment (GTK_SCROLLED_WINDOW (self), NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (self), GTK_SHADOW_NONE);
	gtk_style_context_set_junction_sides (gtk_widget_get_style_context (GTK_WIDGET (self)),
	                                      GTK_JUNCTION_RIGHT | GTK_JUNCTION_LEFT);

	/* tree view */
	tree_view = GTK_TREE_VIEW (gtk_tree_view_new ());
	col = gtk_tree_view_column_new ();

	/* initial padding */
	cell = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (col, cell, FALSE);
	g_object_set (cell,
	              "xpad", 6,
	              NULL);

	/* headings */
	cell = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (col, cell, FALSE);
	gtk_tree_view_column_set_attributes (col, cell,
	                                     "text", SIDEBAR_LABEL,
	                                     NULL);
	g_object_set (cell,
	              "weight", PANGO_WEIGHT_BOLD,
	              "weight-set", TRUE,
	              "ypad", 6,
	              "xpad", 0,
	              NULL);
	gtk_tree_view_column_set_cell_data_func (col, cell,
	                                         on_cell_renderer_heading_visible,
	                                         self, NULL);

	/* icon padding */
	cell = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (col, cell, FALSE);
	gtk_tree_view_column_set_cell_data_func (col, cell,
	                                         on_padding_cell_renderer,
	                                         self, NULL);

	/* icon renderer */
	cell = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (col, cell, FALSE);
	gtk_tree_view_column_set_attributes (col, cell,
	                                     "gicon", SIDEBAR_ICON,
	                                     NULL);
	gtk_tree_view_column_set_cell_data_func (col, cell,
	                                         on_cell_renderer_heading_not_visible,
	                                         self, NULL);

	/* normal text renderer */
	cell = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (col, cell, TRUE);
	g_object_set (G_OBJECT (cell), "editable", FALSE, NULL);
	gtk_tree_view_column_set_attributes (col, cell,
	                                     "text", SIDEBAR_LABEL,
	                                     NULL);
	gtk_tree_view_column_set_cell_data_func (col, cell,
	                                         on_cell_renderer_heading_not_visible,
	                                         self, NULL);
	g_object_set (cell,
	              "ellipsize", PANGO_ELLIPSIZE_END,
	              "ellipsize-set", TRUE,
	              NULL);

	/* check renderer */
	cell = gtk_cell_renderer_toggle_new ();
	g_object_set (cell,
	              "xpad", 6,
	              "xalign", 1.0,
	              NULL);
	gtk_tree_view_column_pack_end (col, cell, FALSE);
	gtk_tree_view_column_set_cell_data_func (col, cell,
	                                         on_cell_renderer_check,
	                                         self, NULL);
	g_signal_connect (cell, "toggled", G_CALLBACK (on_checked_toggled), self);

	gtk_tree_view_column_set_max_width (GTK_TREE_VIEW_COLUMN (col), 24);
	gtk_tree_view_append_column (tree_view, col);

	gtk_tree_view_set_headers_visible (tree_view, FALSE);
	gtk_tree_view_set_tooltip_column (tree_view, SIDEBAR_TOOLTIP);
	gtk_tree_view_set_search_column (tree_view, SIDEBAR_LABEL);
	gtk_tree_view_set_model (tree_view, GTK_TREE_MODEL (self->store));

	gtk_container_add (GTK_CONTAINER (self), GTK_WIDGET (tree_view));
	gtk_widget_show (GTK_WIDGET (tree_view));
	self->tree_view = tree_view;

	selection = gtk_tree_view_get_selection (tree_view);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
	gtk_tree_selection_set_select_function (selection, on_tree_selection_validate, self, NULL);
	g_signal_connect (selection, "changed", G_CALLBACK (on_tree_selection_changed), self);


	load_backends (self);
}

static void
seahorse_sidebar_get_property (GObject *obj,
                               guint prop_id,
                               GValue *value,
                               GParamSpec *pspec)
{
	SeahorseSidebar *self = SEAHORSE_SIDEBAR (obj);

	switch (prop_id) {
	case PROP_COLLECTION:
		g_value_set_object (value, seahorse_sidebar_get_collection (self));
		break;
	case PROP_COMBINED:
		g_value_set_boolean (value, seahorse_sidebar_get_combined (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
seahorse_sidebar_set_property (GObject *obj,
                               guint prop_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
	SeahorseSidebar *self = SEAHORSE_SIDEBAR (obj);

	switch (prop_id) {
	case PROP_COMBINED:
		seahorse_sidebar_set_combined (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
seahorse_sidebar_dispose (GObject *obj)
{
	SeahorseSidebar *self = SEAHORSE_SIDEBAR (obj);
	GList *places, *l;
	gint i;

	for (i = 0; i < self->backends->len; i++) {
		g_signal_handlers_disconnect_by_func (self->backends->pdata[i], on_place_added, self);
		g_signal_handlers_disconnect_by_func (self->backends->pdata[i], on_place_removed, self);
		g_signal_handlers_disconnect_by_func (self->backends->pdata[i], on_backend_changed, self);

		places = gcr_collection_get_objects (self->backends->pdata[i]);
		for (l = places; l != NULL; l = g_list_next (l))
			on_place_removed (self->backends->pdata[i], l->data, self);
		g_list_free (places);
	}

	G_OBJECT_CLASS (seahorse_sidebar_parent_class)->dispose (obj);
}

static void
seahorse_sidebar_finalize (GObject *obj)
{
	SeahorseSidebar *self = SEAHORSE_SIDEBAR (obj);

	g_clear_object (&self->selected);
	g_hash_table_destroy (self->checked);
	g_object_unref (self->objects);

	if (self->update_places_sig)
		g_source_remove (self->update_places_sig);

	g_ptr_array_unref (self->backends);
	g_object_unref (self->store);

	G_OBJECT_CLASS (seahorse_sidebar_parent_class)->finalize (obj);
}

static void
seahorse_sidebar_class_init (SeahorseSidebarClass *klass)
{
	GObjectClass *gobject_class= G_OBJECT_CLASS (klass);

	gobject_class->constructed = seahorse_sidebar_constructed;
	gobject_class->dispose = seahorse_sidebar_dispose;
	gobject_class->finalize = seahorse_sidebar_finalize;
	gobject_class->get_property = seahorse_sidebar_get_property;
	gobject_class->set_property = seahorse_sidebar_set_property;

	g_object_class_install_property (gobject_class, PROP_COLLECTION,
	        g_param_spec_object ("collection", "Collection", "Collection of objects sidebar represents",
	                             GCR_TYPE_COLLECTION, G_PARAM_READABLE));

	g_object_class_install_property (gobject_class, PROP_COMBINED,
	        g_param_spec_boolean ("combined", "Combined", "Collection shows all objects combined",
	                              FALSE, G_PARAM_READWRITE));
}

SeahorseSidebar *
seahorse_sidebar_new (void)
{
	return g_object_new (SEAHORSE_TYPE_SIDEBAR,
	                     NULL);
}

GcrCollection *
seahorse_sidebar_get_collection (SeahorseSidebar *self)
{
	g_return_val_if_fail (SEAHORSE_IS_SIDEBAR (self), NULL);
	return GCR_COLLECTION (self->objects);
}

gboolean
seahorse_sidebar_get_combined (SeahorseSidebar *self)
{
	g_return_val_if_fail (SEAHORSE_IS_SIDEBAR (self), FALSE);
	return self->mode == SEAHORSE_SIDEBAR_MODE_COMBINED;
}

void
seahorse_sidebar_set_combined (SeahorseSidebar *self,
                               gboolean combined)
{
	g_return_if_fail (SEAHORSE_IS_SIDEBAR (self));
	if (combined)
		update_objects_for_combine (self);
	else
		update_objects_for_uncombine (self);
	g_object_notify (G_OBJECT (self), "combined");
}
