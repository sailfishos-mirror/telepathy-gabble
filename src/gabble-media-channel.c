/*
 * gabble-media-channel.c - Source for GabbleMediaChannel
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>

#include "gabble-connection.h"
#include "telepathy-helpers.h"
#include "telepathy-interfaces.h"

#include "gabble-media-channel.h"
#include "gabble-media-channel-signals-marshal.h"

#include "gabble-media-channel-glue.h"

#include "gabble-media-session-handler.h"

G_DEFINE_TYPE(GabbleMediaChannel, gabble_media_channel, G_TYPE_OBJECT)

/* signal enum */
enum
{
    CLOSED,
    NEW_MEDIA_SESSION_HANDLER,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_OBJECT_PATH,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  LAST_PROPERTY
};

/* private structure */
typedef struct _GabbleMediaChannelPrivate GabbleMediaChannelPrivate;

struct _GabbleMediaChannelPrivate
{
  GabbleConnection *connection;
  gchar *object_path;
  GabbleHandle handle;

  GHashTable *session_handlers;

  gboolean closed;
  gboolean dispose_has_run;
};

#define GABBLE_MEDIA_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_MEDIA_CHANNEL, GabbleMediaChannelPrivate))

static void
gabble_media_channel_init (GabbleMediaChannel *obj)
{
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (obj);
  
  priv->session_handlers = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                                  NULL, g_object_unref);

  priv->closed = FALSE;
}

static GObject *
gabble_media_channel_constructor (GType type, guint n_props,
                                  GObjectConstructParam *props)
{
  GObject *obj;
  GabbleMediaChannelPrivate *priv;
  DBusGConnection *bus;
  GabbleHandleRepo *handles;
  gboolean valid;
  
  obj = G_OBJECT_CLASS (gabble_media_channel_parent_class)->
           constructor (type, n_props, props);
  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (GABBLE_MEDIA_CHANNEL (obj));

  handles = _gabble_connection_get_handles (priv->connection);
  valid = gabble_handle_ref (handles, TP_HANDLE_TYPE_CONTACT, priv->handle);
  g_assert (valid);

  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  return obj;
}

static void
gabble_media_channel_get_property (GObject    *object,
                                   guint       property_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  GabbleMediaChannel *chan = GABBLE_MEDIA_CHANNEL (object);
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->connection);
      break;
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_CHANNEL_TYPE:
      g_value_set_string (value, TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA);
      break;
    case PROP_HANDLE_TYPE:
      g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
      break;
    case PROP_HANDLE:
      g_value_set_uint (value, priv->handle);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_media_channel_set_property (GObject     *object,
                                   guint        property_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  GabbleMediaChannel *chan = GABBLE_MEDIA_CHANNEL (object);
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->connection = g_value_get_object (value);
      break;
    case PROP_OBJECT_PATH:
      if (priv->object_path)
        g_free (priv->object_path);

      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_HANDLE:
      priv->handle = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void gabble_media_channel_dispose (GObject *object);
static void gabble_media_channel_finalize (GObject *object);

static void
gabble_media_channel_class_init (GabbleMediaChannelClass *gabble_media_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_media_channel_class);
  GParamSpec *param_spec;
  
  g_type_class_add_private (gabble_media_channel_class, sizeof (GabbleMediaChannelPrivate));

  object_class->constructor = gabble_media_channel_constructor;

  object_class->get_property = gabble_media_channel_get_property;
  object_class->set_property = gabble_media_channel_set_property;

  object_class->dispose = gabble_media_channel_dispose;
  object_class->finalize = gabble_media_channel_finalize;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object that owns this "
                                    "IM channel object.",
                                    GABBLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_string ("object-path", "D-Bus object path",
                                    "The D-Bus object path used for this "
                                    "object on the bus.",
                                    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_OBJECT_PATH, param_spec);

  param_spec = g_param_spec_string ("channel-type", "Telepathy channel type",
                                    "The D-Bus interface representing the "
                                    "type of this channel.",
                                    NULL,
                                    G_PARAM_READABLE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CHANNEL_TYPE, param_spec);

  param_spec = g_param_spec_uint ("handle-type", "Contact handle type",
                                  "The TpHandleType representing a "
                                  "contact handle.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_READABLE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_HANDLE_TYPE, param_spec);

  param_spec = g_param_spec_uint ("handle", "Contact handle",
                                  "The GabbleHandle representing the contact "
                                  "with whom this channel communicates.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_HANDLE, param_spec);
  
  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (gabble_media_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_channel_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[NEW_MEDIA_SESSION_HANDLER] =
    g_signal_new ("new-media-session-handler",
                  G_OBJECT_CLASS_TYPE (gabble_media_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_channel_marshal_VOID__INT_STRING_STRING,
                  G_TYPE_NONE, 3, G_TYPE_UINT, DBUS_TYPE_G_OBJECT_PATH, G_TYPE_STRING);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (gabble_media_channel_class), &dbus_glib_gabble_media_channel_object_info);
}

void
gabble_media_channel_dispose (GObject *object)
{
  GabbleMediaChannel *self = GABBLE_MEDIA_CHANNEL (object);
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;
  
  g_assert (g_hash_table_size (priv->session_handlers) == 0);
  g_hash_table_destroy (priv->session_handlers);

  priv->dispose_has_run = TRUE;

  if (!priv->closed)
    g_signal_emit (self, signals[CLOSED], 0);

  if (G_OBJECT_CLASS (gabble_media_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_media_channel_parent_class)->dispose (object);
}

void
gabble_media_channel_finalize (GObject *object)
{
  GabbleMediaChannel *self = GABBLE_MEDIA_CHANNEL (object);
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (self);
  GabbleHandleRepo *handles;

  handles = _gabble_connection_get_handles (priv->connection);
  gabble_handle_unref (handles, TP_HANDLE_TYPE_CONTACT, priv->handle);

  g_free (priv->object_path);

  G_OBJECT_CLASS (gabble_media_channel_parent_class)->finalize (object);
}



/**
 * gabble_media_channel_close
 *
 * Implements DBus method Close
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_media_channel_close (GabbleMediaChannel *obj, GError **error)
{
  GabbleMediaChannelPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_CHANNEL (obj));
  
  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (obj);
  priv->closed = TRUE;

  g_signal_emit(obj, signals[CLOSED], 0);
  
  return TRUE;
}


/**
 * gabble_media_channel_get_channel_type
 *
 * Implements DBus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_media_channel_get_channel_type (GabbleMediaChannel *obj, gchar ** ret, GError **error)
{
  *ret = g_strdup (TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA);

  return TRUE;
}


/**
 * gabble_media_channel_get_handle
 *
 * Implements DBus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_media_channel_get_handle (GabbleMediaChannel *obj, guint* ret, guint* ret1, GError **error)
{
  GabbleMediaChannelPrivate *priv;
  
  g_assert (GABBLE_IS_MEDIA_CHANNEL (obj));

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (obj);

  *ret = TP_HANDLE_TYPE_CONTACT;
  *ret1 = priv->handle;
  
  return TRUE;
}


/**
 * gabble_media_channel_get_interfaces
 *
 * Implements DBus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_media_channel_get_interfaces (GabbleMediaChannel *obj, gchar *** ret, GError **error)
{
  const gchar *interfaces[] = { NULL };

  g_debug ("%s called", G_STRFUNC);

  *ret = g_strdupv ((gchar **) interfaces);

  return TRUE;
}

/**
 * get_session_handlers_hash_foreach:
 * @key: iterated key
 * @value: iterated value
 * @data: data attached to this key/value pair
 *
 * Called by the exported GetSessionHandlers method, this should iterate over
 * the handle/GabbleMediaSessionHandler pairs in a hash, and for each add a
 * GValueArray containing the following:
 *  an integer handle representing the member the MediaSessionHandler is created for
 *  a D-Bus object path for the MediaSessionHandler object on this service
 *  a string indicating the type of session
 */
static void
get_session_handlers_hash_foreach (gpointer key,
                                   gpointer value,
                                   gpointer data)
{
  GabbleHandle member = GPOINTER_TO_UINT (key);
  GObject *session_handler = G_OBJECT (value);
  GPtrArray *handlers = (GPtrArray *) data;
  GValueArray *vals;
  gchar *path;

  vals = g_value_array_new (3);
  
  g_value_array_append (vals, NULL);
  g_value_init (g_value_array_get_nth (vals, 0), G_TYPE_UINT);
  g_value_set_uint (g_value_array_get_nth (vals, 0), member);
  
  g_value_array_append (vals, NULL);
  g_value_init (g_value_array_get_nth (vals, 1), DBUS_TYPE_G_OBJECT_PATH);
  g_object_get (session_handler, "object-path", &path, NULL);
  g_value_set_boxed (g_value_array_get_nth (vals, 1), path);
  g_free (path);

  g_value_array_append (vals, NULL);
  g_value_init (g_value_array_get_nth (vals, 2), G_TYPE_STRING);
  g_value_set_string (g_value_array_get_nth (vals, 2), "rtp");

  g_ptr_array_add (handlers, vals);
}

/**
 * gabble_media_channel_get_session_handlers
 *
 * Implements DBus method GetSessionHandlers
 * on interface org.freedesktop.Telepathy.Channel.Type.StreamedMedia
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_media_channel_get_session_handlers (GabbleMediaChannel *obj, GPtrArray ** ret, GError **error)
{
  GabbleMediaChannelPrivate *priv;
  guint count;
  GPtrArray *handlers;
  
  g_debug ("%s called", G_STRFUNC);
  
  g_assert (GABBLE_IS_MEDIA_CHANNEL (obj));

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (obj);
  
  count = g_hash_table_size (priv->session_handlers);
  handlers = g_ptr_array_sized_new (count);
  dbus_g_collection_set_signature (handlers, "(uos)");
  
  g_hash_table_foreach (priv->session_handlers,
      get_session_handlers_hash_foreach,
      handlers);

  *ret = handlers;
  
  return TRUE;
}

/**
 * gabble_media_channel_create_session_handler
 *
 * Creates a SessionHandler object for given member.
 */
void gabble_media_channel_create_session_handler (GabbleMediaChannel *channel, GabbleHandle member)
{
  GabbleMediaChannelPrivate *priv;
  GabbleMediaSessionHandler *session;
  gchar *object_path;

  g_assert (GABBLE_IS_MEDIA_CHANNEL (channel));

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (channel);
  
  object_path = g_strdup_printf ("%s/MediaSessionHandler%u", priv->object_path, member);

  session = g_object_new (GABBLE_TYPE_MEDIA_SESSION_HANDLER,
                          "media-channel", channel,
                          "object-path", object_path,
                          NULL);
  
  g_hash_table_insert (priv->session_handlers, GINT_TO_POINTER (member), session);

  g_signal_emit (channel, signals[NEW_MEDIA_SESSION_HANDLER], 0,
                 member, object_path, "rtp");

  g_free (object_path);
}

/*
 * JingleCandidate
 */

JingleCandidate *jingle_candidate_new (const gchar *name,
                                       const gchar *address,
                                       guint16 port,
                                       const gchar *username,
                                       const gchar *password,
                                       gfloat preference,
                                       const gchar *protocol,
                                       const gchar *type,
                                       guchar network,
                                       guchar generation)
{
  JingleCandidate *candidate = g_new (JingleCandidate, 1);

  candidate->name = g_strdup (name);
  candidate->address = g_strdup (address);
  candidate->port = port;
  candidate->username = g_strdup (username);
  candidate->password = g_strdup (password);
  candidate->preference = preference;
  candidate->protocol = g_strdup (protocol);
  candidate->type = g_strdup (type);
  candidate->network = network;
  candidate->generation = generation;

  return candidate;
}

void jingle_candidate_free (JingleCandidate *candidate)
{
  g_free (candidate->name);
  g_free (candidate->address);
  g_free (candidate->username);
  g_free (candidate->password);
  g_free (candidate->protocol);
  g_free (candidate->type);
  
  g_free (candidate);
}


/*
 * JingleCodec
 */

JingleCodec *
jingle_codec_new (guchar id, const gchar *name)
{
  JingleCodec *codec = g_new (JingleCodec, 1);

  codec->id = id;
  codec->name = g_strdup (name);

  return codec;
}

void
jingle_codec_free (JingleCodec *codec)
{
  g_free (codec->name);

  g_free (codec);
}

