/* vi:set expandtab sw=2 sts=2: */
/*-
 * Copyright (c) 2008 Jannis Pohlmann <jannis@xfce.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <glib/gi18n.h>

#include <gst/audio/mixerutils.h>
#include <gst/interfaces/mixer.h>

#include "libxfce4mixer.h"



static gboolean _xfce_mixer_filter_mixer  (GstMixer *mixer,
                                           gpointer  user_data);
static void     _xfce_mixer_destroy_mixer (GstMixer *mixer);



static guint       refcount = 0;
static GList      *mixers = NULL;
#ifdef HAVE_GST_MIXER_NOTIFICATION
static GstBus     *bus = NULL;
static GstElement *selected_card = NULL;
#endif


/* functions copied verbatim from volumealsabt.c */

#define BLUEALSA_DEV (-99)

static char *get_string (const char *fmt, ...)
{
    char *cmdline, *line = NULL, *res = NULL;
    size_t len = 0;

    va_list arg;
    va_start (arg, fmt);
    g_vasprintf (&cmdline, fmt, arg);
    va_end (arg);

    FILE *fp = popen (cmdline, "r");
    if (fp)
    {
        if (getline (&line, &len, fp) > 0)
        {
            res = line;
            while (*res++) if (g_ascii_isspace (*res)) *res = 0;
            res = g_strdup (line);
        }
        pclose (fp);
        g_free (line);
    }
    g_free (cmdline);
    return res ? res : g_strdup ("");
}

static int vsystem (const char *fmt, ...)
{
    char *cmdline;
    int res;

    va_list arg;
    va_start (arg, fmt);
    g_vasprintf (&cmdline, fmt, arg);
    va_end (arg);
    res = system (cmdline);
    g_free (cmdline);
    return res;
}

static gboolean find_in_section (char *file, char *sec, char *seek)
{
    char *cmd = g_strdup_printf ("sed -n '/%s/,/}/p' %s 2>/dev/null | grep -q %s", sec, file, seek);
    int res = system (cmd);
    g_free (cmd);
    if (res == 0) return TRUE;
    else return FALSE;
}

static int asound_get_default_card (void)
{
    char *user_config_file = g_build_filename (g_get_home_dir (), "/.asoundrc", NULL);
    char *res;
    int val;

    /* does .asoundrc exist? if not, default device is 0 */
    if (!g_file_test (user_config_file, G_FILE_TEST_IS_REGULAR))
    {
        g_free (user_config_file);
        return 0;
    }

    /* does .asoundrc use type asym? */
    if (find_in_section (user_config_file, "pcm.!default", "asym"))
    {
        /* look in pcm.output section for bluealsa */
        if (find_in_section (user_config_file, "pcm.output", "bluealsa"))
        {
            g_free (user_config_file);
            return BLUEALSA_DEV;
        }

        /* otherwise parse pcm.output section for card number */
        res = get_string ("sed -n '/pcm.output/,/}/{/card/p}' %s 2>/dev/null | cut -d ' ' -f 2", user_config_file);
        if (sscanf (res, "%d", &val) != 1) val = -1;
    }
    else
    {
        /* first check to see if Bluetooth is in use */
        if (find_in_section (user_config_file, "pcm.!default", "bluealsa"))
        {
            g_free (user_config_file);
            return BLUEALSA_DEV;
        }

        /* if not, check for new format file */
        res = get_string ("sed -n '/pcm.!default/,/}/{/slave.pcm/p}' %s 2>/dev/null | cut -d '\"' -f 2 | cut -d : -f 2", user_config_file);
        if (sscanf (res, "%d", &val) == 1) goto DONE;
        g_free (res);

        /* if not, check for old format file */
        res = get_string ("sed -n '/pcm.!default/,/}/{/card/p}' %s 2>/dev/null | cut -d ' ' -f 2", user_config_file);
        if (sscanf (res, "%d", &val) == 1) goto DONE;

        /* nothing valid found, default device is 0 */
        val = 0;
    }

    DONE: g_free (res);
    g_free (user_config_file);
    return val;
}

/* Standard text blocks used in .asoundrc for ALSA (_A) and Bluetooth (_B) devices */
#define PREFIX      "pcm.!default {\n\ttype asym\n\tplayback.pcm {\n\t\ttype plug\n\t\tslave.pcm \"output\"\n\t}\n\tcapture.pcm {\n\t\ttype plug\n\t\tslave.pcm \"input\"\n\t}\n}"
#define OUTPUT_A    "\npcm.output {\n\ttype hw\n\tcard %d\n}"
#define INPUT_A     "\npcm.input {\n\ttype hw\n\tcard %d\n}"
#define CTL_A       "\nctl.!default {\n\ttype hw\n\tcard %d\n}"

static void asound_set_default_card (int num)
{
    char *user_config_file = g_build_filename (g_get_home_dir (), "/.asoundrc", NULL);

    /* does .asoundrc exist? if not, write default contents and exit */
    if (!g_file_test (user_config_file, G_FILE_TEST_IS_REGULAR))
    {
        vsystem ("echo '" PREFIX "\n" OUTPUT_A "\n" CTL_A "' >> %s", num, num, user_config_file);
        goto DONE;
    }

    /* does .asoundrc use type asym? if not, replace file with default contents and exit */
    if (!find_in_section (user_config_file, "pcm.!default", "asym"))
    {
        vsystem ("echo '" PREFIX "\n" OUTPUT_A "\n" CTL_A "' > %s", num, num, user_config_file);
        goto DONE;
    }

    /* is there a pcm.output section? update it if so; if not, append one */
    if (!find_in_section (user_config_file, "pcm.output", "type"))
        vsystem ("echo '" OUTPUT_A "' >> %s", num, user_config_file);
    else
        vsystem ("sed -i '/pcm.output/,/}/c pcm.output {\\n\\ttype hw\\n\\tcard %d\\n}' %s", num, user_config_file);

    /* is there a ctl.!default section? update it if so; if not, append one */
    if (!find_in_section (user_config_file, "ctl.!default", "type"))
        vsystem ("echo '" CTL_A "' >> %s", num, user_config_file);
    else
        vsystem ("sed -i '/ctl.!default/,/}/c ctl.!default {\\n\\ttype hw\\n\\tcard %d\\n}' %s", num, user_config_file);

    DONE: g_free (user_config_file);
    vsystem ("pimixer --refresh");
}

static void asound_set_default_input (int num)
{
    char *user_config_file = g_build_filename (g_get_home_dir (), "/.asoundrc", NULL);

    /* does .asoundrc exist? if not, write default contents and exit */
    if (!g_file_test (user_config_file, G_FILE_TEST_IS_REGULAR))
    {
        vsystem ("echo '" PREFIX "\n" OUTPUT_A "\n" INPUT_A "\n" CTL_A "' >> %s", 0, num, 0, user_config_file);
        goto DONE;
    }

    /* does .asoundrc use type asym? if not, replace file with default contents and exit */
    if (!find_in_section (user_config_file, "pcm.!default", "asym"))
    {
        vsystem ("echo '" PREFIX "\n" OUTPUT_A "\n" INPUT_A "\n" CTL_A"' > %s", 0, num, 0, user_config_file);
        goto DONE;
    }

    /* is there a pcm.input section? update it if so; if not, append one */
    if (!find_in_section (user_config_file, "pcm.input", "type"))
        vsystem ("echo '" INPUT_A "' >> %s", num, user_config_file);
    else
        vsystem ("sed -i '/pcm.input/,/}/c pcm.input {\\n\\ttype hw\\n\\tcard %d\\n}' %s", num, user_config_file);

    DONE: g_free (user_config_file);
}

static char *asound_get_bt_device (void)
{
    char *user_config_file = g_build_filename (g_get_home_dir (), "/.asoundrc", NULL);
    char *res, *ret = NULL;

    /* first check the pcm.output section */
    res = get_string ("sed -n '/pcm.output/,/}/{/device/p}' %s 2>/dev/null | cut -d '\"' -f 2 | tr : _", user_config_file);
    if (strlen (res) == 17) goto DONE;
    else g_free (res);

    /* if nothing there, check the default block */
    res = get_string ("sed -n '/pcm.!default/,/}/{/device/p}' %s 2>/dev/null | cut -d '\"' -f 2 | tr : _", user_config_file);
    if (strlen (res) == 17) goto DONE;
    else g_free (res);

    res = NULL;
    DONE: g_free (user_config_file);
    if (res)
    {
        ret = g_strdup_printf ("/org/bluez/hci0/dev_%s", res);
        g_free (res);
    }
    return ret;
}

static char *asound_get_bt_input (void)
{
    char *user_config_file = g_build_filename (g_get_home_dir (), "/.asoundrc", NULL);
    char *res, *ret = NULL;

    /* check the pcm.input section */
    res = get_string ("sed -n '/pcm.input/,/}/{/device/p}' %s 2>/dev/null | cut -d '\"' -f 2 | tr : _", user_config_file);
    if (strlen (res) == 17) goto DONE;
    else g_free (res);

    res = NULL;
    DONE: g_free (user_config_file);
    if (res)
    {
        ret = g_strdup_printf ("/org/bluez/hci0/dev_%s", res);
        g_free (res);
    }
    return ret;
}

/* modified version of bt_disconnect_device from volumealsabt.c */

static void disconnect_device (char *device)
{
    GDBusObjectManager *objmanager = g_dbus_object_manager_client_new_for_bus_sync (G_BUS_TYPE_SYSTEM, 0, "org.bluez", "/", NULL, NULL, NULL, NULL, NULL);
    if (objmanager)
    {
        GDBusInterface *interface = g_dbus_object_manager_get_interface (objmanager, device, "org.bluez.Device1");
        if (interface)
        {
            // call the disconnect method on BlueZ
            g_dbus_proxy_call (G_DBUS_PROXY (interface), "Disconnect", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
            g_object_unref (interface);
        }
    }
}

void
xfce_mixer_init (void)
{
  GtkIconTheme *icon_theme;
  gint          counter = 0;

  if (G_LIKELY (refcount++ == 0))
    {
      /* Append application icons to the search path */
      icon_theme = gtk_icon_theme_get_default ();
      gtk_icon_theme_append_search_path (icon_theme, MIXER_DATADIR G_DIR_SEPARATOR_S "icons");

      /* Get list of all available mixer devices */
      mixers = gst_audio_default_registry_mixer_filter (_xfce_mixer_filter_mixer, FALSE, &counter);

#ifdef HAVE_GST_MIXER_NOTIFICATION
      /* Create a GstBus for notifications */
      bus = gst_bus_new ();
      gst_bus_add_signal_watch (bus);
#endif
    }
}



void
xfce_mixer_shutdown (void)
{
  if (G_LIKELY (--refcount <= 0))
    {
      g_list_foreach (mixers, (GFunc) _xfce_mixer_destroy_mixer, NULL);
      g_list_free (mixers);

#ifdef HAVE_GST_MIXER_NOTIFICATION
      gst_bus_remove_signal_watch (bus);
      gst_object_unref (bus);
#endif
    }
}



GList *
xfce_mixer_get_cards (void)
{
  g_return_val_if_fail (refcount > 0, NULL);
  return mixers;
}



GstElement *
xfce_mixer_get_card (const gchar *name)
{
  GstElement *element = NULL;
  GList      *iter;
  gchar      *card_name;

  g_return_val_if_fail (refcount > 0, NULL);

  if (G_UNLIKELY (name == NULL))
    return NULL;

  for (iter = g_list_first (mixers); iter != NULL; iter = g_list_next (iter))
    {
      card_name = g_object_get_data (G_OBJECT (iter->data), "xfce-mixer-internal-name");

      if (G_UNLIKELY (g_utf8_collate (name, card_name) == 0))
        {
          element = iter->data;
          break;
        }
    }

  return element;
}



const gchar *
xfce_mixer_get_card_display_name (GstElement *card)
{
  g_return_val_if_fail (GST_IS_MIXER (card), NULL);
  if (xfce_mixer_is_default_card (card))
    return g_strconcat (g_object_get_data (G_OBJECT (card), "xfce-mixer-name"), " (Default)", NULL);
  else
    return g_object_get_data (G_OBJECT (card), "xfce-mixer-name");
}



const gchar *
xfce_mixer_get_card_internal_name (GstElement *card)
{
  g_return_val_if_fail (GST_IS_MIXER (card), NULL);
  return g_object_get_data (G_OBJECT (card), "xfce-mixer-internal-name");
}


const gchar *
xfce_mixer_get_card_id (GstElement *card)
{
  g_return_val_if_fail (GST_IS_MIXER (card), NULL);
  return g_object_get_data (G_OBJECT (card), "xfce-mixer-id");
}

int xfce_mixer_get_card_num (const char *id)
{
  int num;
  if (sscanf (id, "hw:%d", &num) == 1) return num;
  return -1;
}

void
xfce_mixer_select_card (GstElement *card)
{
  g_return_if_fail (GST_IS_MIXER (card));

#ifdef HAVE_GST_MIXER_NOTIFICATION
  gst_element_set_bus (card, bus);
  selected_card = card;
#endif
}



GstMixerTrack *
xfce_mixer_get_track (GstElement  *card,
                      const gchar *track_name)
{
  GstMixerTrack *track = NULL;
  const GList   *iter;
  gchar         *label;

  g_return_val_if_fail (GST_IS_MIXER (card), NULL);
  g_return_val_if_fail (track_name != NULL, NULL);

  for (iter = gst_mixer_list_tracks (GST_MIXER (card)); iter != NULL; iter = g_list_next (iter))
    {
      g_object_get (GST_MIXER_TRACK (iter->data), "label", &label, NULL);

      if (g_utf8_collate (label, track_name) == 0)
        {
          track = iter->data;
          g_free (label);
          break;
        }

      g_free (label);
    }

  return track;
}



#ifdef HAVE_GST_MIXER_NOTIFICATION
guint
xfce_mixer_bus_connect (GCallback callback,
                        gpointer  user_data)
{
  g_return_val_if_fail (refcount > 0, 0);
  return g_signal_connect (bus, "message::element", callback, user_data);
}



void
xfce_mixer_bus_disconnect (guint signal_handler_id)
{
  g_return_if_fail (refcount > 0);
  if (signal_handler_id != 0)
    g_signal_handler_disconnect (bus, signal_handler_id);
}
#endif



gint
xfce_mixer_get_max_volume (gint *volumes,
                           gint  num_channels)
{
  gint max = 0;

  g_return_val_if_fail (volumes != NULL, 0);

  for (--num_channels; num_channels >= 0; --num_channels)
    if (volumes[num_channels] > max)
      max = volumes[num_channels];

  return max;
}



static gboolean
_xfce_mixer_filter_mixer (GstMixer *mixer,
                          gpointer  user_data)
{
  GstElementFactory *factory;
  const gchar       *long_name;
  gchar             *device_name = NULL;
  gchar             *internal_name;
  gchar             *name;
  gchar             *p;
  gint               length;
  gint              *counter = user_data;
  gchar *device;

  /* Get long name of the mixer element */
  factory = gst_element_get_factory (GST_ELEMENT (mixer));
  long_name = gst_element_factory_get_longname (factory);

  /* Get the device name of the mixer element */
  if (g_object_class_find_property (G_OBJECT_GET_CLASS (G_OBJECT (mixer)), "device-name"))
    g_object_get (mixer, "device-name", &device_name, NULL);

  if (g_object_class_find_property (G_OBJECT_GET_CLASS (G_OBJECT (mixer)), "device"))
    g_object_get (mixer, "device", &device, NULL);

  /* Fall back to default name if neccessary */
  if (G_UNLIKELY (device_name == NULL))
    device_name = g_strdup_printf (_("Unknown Volume Control %d"), (*counter)++);

  /* Build display name */
  name = g_strdup_printf ("%s (%s)", device_name, long_name);

  /* Free device name */
  g_free (device_name);

  /* Set name to be used by xfce4-mixer */
  g_object_set_data_full (G_OBJECT (mixer), "xfce-mixer-name", name, (GDestroyNotify) g_free);
  g_object_set_data_full (G_OBJECT (mixer), "xfce-mixer-id", device, (GDestroyNotify) g_free);

  /* Count alpha-numeric characters in the name */
  for (length = 0, p = name; *p != '\0'; ++p)
    if (g_ascii_isalnum (*p))
      ++length;

  /* Generate internal name */
  internal_name = g_new0 (gchar, length+1);
  for (length = 0, p = name; *p != '\0'; ++p)
    if (g_ascii_isalnum (*p))
      internal_name[length++] = *p;
  internal_name[length] = '\0';

  /* Remember name for use by xfce4-mixer */
  g_object_set_data_full (G_OBJECT (mixer), "xfce-mixer-internal-name", internal_name, (GDestroyNotify) g_free);

  /* Keep the mixer (we want all devices to be visible) */
  return TRUE;
}



static void
_xfce_mixer_destroy_mixer (GstMixer *mixer)
{
  gst_element_set_state (GST_ELEMENT (mixer), GST_STATE_NULL);
  gst_object_unref (GST_OBJECT (mixer));
}


void xfce_mixer_set_default_card (char *id)
{
  char *obt = asound_get_bt_device ();
  char *ibt = asound_get_bt_input ();
  int num = xfce_mixer_get_card_num (id);

  asound_set_default_card (num);
  asound_set_default_input (num);

  if (obt) disconnect_device (obt);
  if (ibt && g_strcmp0 (ibt, obt)) disconnect_device (ibt);

  if (obt) g_free (obt);
  if (ibt) g_free (ibt);
}


guint
xfce_mixer_is_default_card (GstElement *card)
{
  g_return_val_if_fail (GST_IS_MIXER (card), 0);

  return (xfce_mixer_get_card_num (xfce_mixer_get_card_id (card)) == asound_get_default_card ());
}


