/*
 * auth-manager.c - TpChannelManager implementation for auth channels
 * Copyright (C) 2010 Collabora Ltd.
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

#include "config.h"
#include "auth-manager.h"

#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>

#define DEBUG_FLAG GABBLE_DEBUG_AUTH

#include "extensions/extensions.h"

#include "caps-channel-manager.h"
#include "server-sasl-channel.h"
#include "connection.h"
#include "debug.h"
#include "util.h"

static void channel_manager_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleAuthManager, gabble_auth_manager,
    WOCKY_TYPE_AUTH_REGISTRY,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
      channel_manager_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_CAPS_CHANNEL_MANAGER,
      NULL));

/* properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

struct _GabbleAuthManagerPrivate
{
  GabbleConnection *conn;

  GabbleServerSaslChannel *channel;
  gulong closed_id;

  gboolean dispose_has_run;
};

static void
gabble_auth_manager_init (GabbleAuthManager *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, GABBLE_TYPE_AUTH_MANAGER,
      GabbleAuthManagerPrivate);
}

static void
gabble_auth_manager_close_all (GabbleAuthManager *self)
{
  DEBUG ("called");

  if (self->priv->channel != NULL)
    gabble_server_sasl_channel_close (self->priv->channel);

  /* that results in the signal-driven-object-clearing dance */
  g_assert (self->priv->channel == NULL);
}

static void
connection_status_changed_cb (GabbleConnection *conn,
                              guint status,
                              guint reason,
                              GabbleAuthManager *self)
{
  if (status == TP_CONNECTION_STATUS_DISCONNECTED)
    gabble_auth_manager_close_all (self);
}

static void
auth_channel_closed_cb (GabbleServerSaslChannel *channel,
    GabbleAuthManager *self)
{
  tp_channel_manager_emit_channel_closed_for_object (self,
      TP_EXPORTABLE_CHANNEL (channel));

  g_assert (self->priv->channel == channel);
  g_signal_handler_disconnect (self->priv->channel, self->priv->closed_id);
  tp_clear_object (&self->priv->channel);
}

static void
gabble_auth_manager_constructed (GObject *object)
{
  GabbleAuthManager *self = GABBLE_AUTH_MANAGER (object);

  if (G_OBJECT_CLASS (gabble_auth_manager_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (gabble_auth_manager_parent_class)->constructed (object);

  self->priv->dispose_has_run = FALSE;

  gabble_signal_connect_weak (self->priv->conn, "status-changed",
      G_CALLBACK (connection_status_changed_cb), object);
}

static void
gabble_auth_manager_dispose (GObject *object)
{
  GabbleAuthManager *self = GABBLE_AUTH_MANAGER (object);
  GabbleAuthManagerPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  gabble_auth_manager_close_all (self);

  if (G_OBJECT_CLASS (gabble_auth_manager_parent_class)->dispose)
    G_OBJECT_CLASS (
        gabble_auth_manager_parent_class)->dispose (object);
}

static void
gabble_auth_manager_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  GabbleAuthManager *self = GABBLE_AUTH_MANAGER (object);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, self->priv->conn);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_auth_manager_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  GabbleAuthManager *self = GABBLE_AUTH_MANAGER (object);

  switch (property_id) {
    case PROP_CONNECTION:
      self->priv->conn = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_auth_manager_start_auth_async (WockyAuthRegistry *registry,
    const GSList *mechanisms,
    gboolean allow_plain,
    gboolean is_secure_channel,
    const gchar *username,
    const gchar *password,
    const gchar *server,
    const gchar *session_id,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleAuthManager *self = GABBLE_AUTH_MANAGER (registry);

  /* assumption: Wocky's API guarantees that we never have more than one
   * auth request outstanding */
  g_assert (self->priv->channel == NULL);

  if (password == NULL || username == NULL)
    {
      GPtrArray *mech_array = g_ptr_array_new ();
      const GSList *iter;

      for (iter = mechanisms; iter != NULL; iter = iter->next)
        g_ptr_array_add (mech_array, iter->data);

      g_ptr_array_add (mech_array, NULL);

      self->priv->channel = gabble_server_sasl_channel_new (self->priv->conn,
          (GStrv) mech_array->pdata, is_secure_channel, session_id);
      g_ptr_array_unref (mech_array);

      self->priv->closed_id = tp_g_signal_connect_object (self->priv->channel,
          "closed", G_CALLBACK (auth_channel_closed_cb), self, 0);

      wocky_auth_registry_start_auth_async (
          WOCKY_AUTH_REGISTRY (self->priv->channel),
          mechanisms, allow_plain, is_secure_channel, username, password,
          server, session_id, callback, user_data);

      g_assert (gabble_server_sasl_channel_is_open (
            self->priv->channel));
      tp_channel_manager_emit_new_channel (self,
          TP_EXPORTABLE_CHANNEL (self->priv->channel), NULL);
    }
  else
    {
      WOCKY_AUTH_REGISTRY_CLASS (
          gabble_auth_manager_parent_class)->start_auth_async_func (
              registry, mechanisms, allow_plain, is_secure_channel,
              username, password, server, session_id, callback, user_data);
    }
}

static gboolean
gabble_auth_manager_start_auth_finish (WockyAuthRegistry *registry,
    GAsyncResult *result,
    WockyAuthRegistryStartData **start_data,
    GError **error)
{
  GabbleAuthManager *self = GABBLE_AUTH_MANAGER (registry);

  if (self->priv->channel != NULL)
    {
      return wocky_auth_registry_start_auth_finish (
          WOCKY_AUTH_REGISTRY (self->priv->channel),
          result, start_data, error);
    }
  else
    {
      return WOCKY_AUTH_REGISTRY_CLASS
        (gabble_auth_manager_parent_class)->start_auth_finish_func (
            registry, result, start_data, error);
    }

}

static void
gabble_auth_manager_challenge_async (WockyAuthRegistry *registry,
    const GString *challenge_data,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleAuthManager *self = GABBLE_AUTH_MANAGER (registry);

  if (self->priv->channel != NULL)
    {
      wocky_auth_registry_challenge_async (
          WOCKY_AUTH_REGISTRY (self->priv->channel),
          challenge_data, callback, user_data);
    }
  else
    {
      WOCKY_AUTH_REGISTRY_CLASS (
          gabble_auth_manager_parent_class)->challenge_async_func (
              registry, challenge_data, callback, user_data);
    }
}

static gboolean
gabble_auth_manager_challenge_finish (WockyAuthRegistry *registry,
    GAsyncResult *result,
    GString **response,
    GError **error)
{
  GabbleAuthManager *self = GABBLE_AUTH_MANAGER (registry);

  if (self->priv->channel != NULL)
    {
      return wocky_auth_registry_challenge_finish (
          WOCKY_AUTH_REGISTRY (self->priv->channel),
          result, response, error);
    }
  else
    {
      return WOCKY_AUTH_REGISTRY_CLASS
        (gabble_auth_manager_parent_class)->challenge_finish_func (
            registry, result, response, error);
    }
}

static void
gabble_auth_manager_success_async (WockyAuthRegistry *registry,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleAuthManager *self = GABBLE_AUTH_MANAGER (registry);

  if (self->priv->channel != NULL)
    {
      wocky_auth_registry_success_async (
          WOCKY_AUTH_REGISTRY (self->priv->channel),
          callback, user_data);
    }
  else
    {
      WOCKY_AUTH_REGISTRY_CLASS (
          gabble_auth_manager_parent_class)->success_async_func (
              registry, callback, user_data);
    }
}

static gboolean
gabble_auth_manager_success_finish (WockyAuthRegistry *registry,
    GAsyncResult *result,
    GError **error)
{
  GabbleAuthManager *self = GABBLE_AUTH_MANAGER (registry);

  if (self->priv->channel != NULL)
    {
      return wocky_auth_registry_success_finish (
          WOCKY_AUTH_REGISTRY (self->priv->channel),
          result, error);
    }
  else
    {
      return WOCKY_AUTH_REGISTRY_CLASS
        (gabble_auth_manager_parent_class)->success_finish_func (
            registry, result, error);
    }
}

static void
gabble_auth_manager_failure (WockyAuthRegistry *registry,
    GError *error)
{
  GabbleAuthManager *self = GABBLE_AUTH_MANAGER (registry);

  if (self->priv->channel != NULL)
    {
      wocky_auth_registry_failure (
          WOCKY_AUTH_REGISTRY (self->priv->channel), error);
    }
  else
    {
      void (*chain_up)(WockyAuthRegistry *, GError *) =
        WOCKY_AUTH_REGISTRY_CLASS (gabble_auth_manager_parent_class)->
        failure_func;

      if (chain_up != NULL)
        chain_up (registry, error);
    }
}

static void
gabble_auth_manager_class_init (GabbleAuthManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  WockyAuthRegistryClass *registry_class = WOCKY_AUTH_REGISTRY_CLASS (klass);

  GParamSpec *param_spec;

  g_type_class_add_private (klass,
      sizeof (GabbleAuthManagerPrivate));

  object_class->constructed = gabble_auth_manager_constructed;
  object_class->dispose = gabble_auth_manager_dispose;

  object_class->get_property = gabble_auth_manager_get_property;
  object_class->set_property = gabble_auth_manager_set_property;

  registry_class->start_auth_async_func = gabble_auth_manager_start_auth_async;
  registry_class->start_auth_finish_func =
    gabble_auth_manager_start_auth_finish;

  registry_class->challenge_async_func = gabble_auth_manager_challenge_async;
  registry_class->challenge_finish_func = gabble_auth_manager_challenge_finish;

  registry_class->success_async_func = gabble_auth_manager_success_async;
  registry_class->success_finish_func = gabble_auth_manager_success_finish;

  registry_class->failure_func = gabble_auth_manager_failure;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
      "Gabble connection object that owns this manager.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);
}

static void
gabble_auth_manager_foreach_channel (TpChannelManager *manager,
    TpExportableChannelFunc func,
    gpointer user_data)
{
  GabbleAuthManager *self = GABBLE_AUTH_MANAGER (manager);

  if (self->priv->channel != NULL)
    func (TP_EXPORTABLE_CHANNEL (self->priv->channel), user_data);
}

static void
channel_manager_iface_init (gpointer g_iface,
                            gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->foreach_channel = gabble_auth_manager_foreach_channel;

  /* These channels are not requestable. */
  iface->ensure_channel = NULL;
  iface->create_channel = NULL;
  iface->request_channel = NULL;
  iface->foreach_channel_class = NULL;
}
