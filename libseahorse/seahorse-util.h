/*
 * Seahorse
 *
 * Copyright (C) 2003 Jacob Perkins
 * Copyright (C) 2004-2005 Stefan Walter
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
 
/**
 * A bunch of miscellaneous utility functions.
 */

#ifndef __SEAHORSE_UTIL_H__
#define __SEAHORSE_UTIL_H__

#include <gtk/gtk.h>
#include <time.h>

#include "seahorse-context.h"

#ifdef WITH_SHARING
#include <avahi-client/client.h>
const AvahiPoll* seahorse_util_dns_sd_get_poll (void);
#endif

struct _SeahorseObject;

typedef guint64 SeahorseVersion;

#define SEAHORSE_EXT_ASC ".asc"
#define SEAHORSE_EXT_GPG ".gpg"

gchar*      seahorse_util_get_date_string           (const time_t time);
gchar*      seahorse_util_get_display_date_string   (const time_t time);


#define     SEAHORSE_ERROR                      (seahorse_util_error_domain ())

GQuark      seahorse_util_error_domain          (void);

void        seahorse_util_show_error            (GtkWidget          *parent,
                                                 const gchar        *heading,
                                                 const gchar        *message);

void        seahorse_util_handle_error          (GError **error,
                                                 gpointer parent,
                                                 const gchar* description,
                                                 ...);

gboolean    seahorse_util_prompt_delete         (const gchar *text,
                                                 GtkWidget *parent);

guchar*     seahorse_util_read_to_memory        (GInputStream *     input,
                                                 guint              *len);

guint       seahorse_util_read_data_block       (GString            *buf, 
                                                 GInputStream*      input, 
                                                 const gchar        *start, 
                                                 const gchar*       end);

gboolean    seahorse_util_print_fd          (int fd, 
                                             const char* data);

gboolean    seahorse_util_printf_fd         (int fd, 
                                             const char* data, ...);
                             
gchar*      seahorse_util_filename_for_objects (GList *objects);
                                             
gboolean    seahorse_util_uri_exists        (const gchar* uri);

gchar*      seahorse_util_uri_unique        (const gchar* uri);

const gchar* seahorse_util_uri_get_last     (const gchar* uri);

GQuark      seahorse_util_detect_mime_type   (const gchar *mime);

GQuark      seahorse_util_detect_data_type   (const gchar *data,
                                              guint length);

GQuark      seahorse_util_detect_file_type   (const gchar *uri);

gboolean    seahorse_util_write_file_private            (const gchar* filename,
                                                         const gchar* contents,
                                                         GError **err);

GtkDialog*  seahorse_util_chooser_open_new              (const gchar *title,
                                                         GtkWindow *parent);

GtkDialog*  seahorse_util_chooser_save_new              (const gchar *title,
                                                         GtkWindow *parent);

void        seahorse_util_chooser_show_key_files        (GtkDialog *dialog);

void        seahorse_util_chooser_show_archive_files    (GtkDialog *dialog);

void        seahorse_util_chooser_set_filename_full     (GtkDialog *dialog, 
                                                         GList *objects);

void        seahorse_util_chooser_set_filename          (GtkDialog *dialog, 
                                                         struct _SeahorseObject *object);

gchar*      seahorse_util_chooser_open_prompt           (GtkDialog *dialog);

gchar*      seahorse_util_chooser_save_prompt           (GtkDialog *dialog);

GList*      seahorse_util_objects_sort       (GList *objects);

GList*      seahorse_util_objects_splice     (GList *objects);

gboolean    seahorse_util_string_equals       (const gchar *s1, const gchar *s2);

void        seahorse_util_string_lower        (gchar *s);

gboolean    seahorse_util_string_is_whitespace (const gchar *text);

void        seahorse_util_string_trim_whitespace (gchar *text);

gchar*      seahorse_util_hex_encode (gconstpointer value, gsize length);

void        seahorse_util_determine_popup_menu_position  (GtkMenu *menu,
                                                           int *x,
                                                           int *y,
                                                           gboolean *push_in,
                                                           gpointer gdata);

SeahorseVersion seahorse_util_parse_version   (const char *version);

#define seahorse_util_version(a,b,c,d) ((SeahorseVersion)a << 48) + ((SeahorseVersion)b << 32) \
                                     + ((SeahorseVersion)c << 16) +  (SeahorseVersion)d

#define     seahorse_util_wait_until(expr)                \
    while (!(expr)) {                                     \
        while (g_main_context_pending(NULL) && !(expr))   \
            g_main_context_iteration (NULL, FALSE);       \
        g_thread_yield ();                                \
    }

#endif /* __SEAHORSE_UTIL_H__ */
