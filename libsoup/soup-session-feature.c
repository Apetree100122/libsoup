/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-session-feature.c: Miscellaneous session feature-provider interface
 *
 * Copyright (C) 2008 Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "soup-session-feature.h"
#include "soup.h"
#include "soup-message-private.h"

/**
 * SECTION:soup-session-feature
 * @short_description: Interface for miscellaneous session features
 *
 * #SoupSessionFeature is the interface used by classes that extend
 * the functionality of a #SoupSession. Some features like HTTP
 * authentication handling are implemented internally via
 * #SoupSessionFeature<!-- -->s. Other features can be added to the session
 * by the application. (Eg, #SoupLogger, #SoupCookieJar.)
 *
 * See soup_session_add_feature(), etc, to add a feature to a session.
 **/

/**
 * SoupSessionFeature:
 *
 * An object that implement some sort of optional feature for
 * #SoupSession.
 *
 * Since: 2.24
 **/

/**
 * SoupSessionFeatureInterface:
 * @parent: The parent interface.
 * @attach: Perform setup when a feature is added to a session
 * @detach: Perform cleanup when a feature is removed from a session
 * @request_queued: Proxies the session's #SoupSession::request_queued signal
 * @request_unqueued: Proxies the session's #SoupSession::request_unqueued signal
 * @add_feature: adds a sub-feature to the main feature
 * @remove_feature: removes a sub-feature from the main feature
 * @has_feature: tests if the feature includes a sub-feature
 *
 * The interface implemented by #SoupSessionFeature<!-- -->s.
 *
 * Since: 2.24
 **/

G_DEFINE_INTERFACE (SoupSessionFeature, soup_session_feature, G_TYPE_OBJECT)

void
soup_session_feature_attach (SoupSessionFeature *feature,
			     SoupSession        *session)
{
	SoupSessionFeatureInterface *iface;

	g_return_if_fail (SOUP_IS_SESSION_FEATURE (feature));
	g_return_if_fail (SOUP_IS_SESSION (session));

	iface = SOUP_SESSION_FEATURE_GET_IFACE (feature);
	if (iface->attach)
		iface->attach (feature, session);
}

void
soup_session_feature_detach (SoupSessionFeature *feature,
			     SoupSession        *session)
{
	SoupSessionFeatureInterface *iface;

	g_return_if_fail (SOUP_IS_SESSION_FEATURE (feature));
	g_return_if_fail (SOUP_IS_SESSION (session));

	iface = SOUP_SESSION_FEATURE_GET_IFACE (feature);
	if (iface->detach)
		iface->detach (feature, session);
}

static void
soup_session_feature_default_init (SoupSessionFeatureInterface *iface)
{
}

void
soup_session_feature_request_queued (SoupSessionFeature *feature,
				     SoupSession        *session,
				     SoupMessage        *msg)
{
	SoupSessionFeatureInterface *iface;

	g_return_if_fail (SOUP_IS_SESSION_FEATURE (feature));
	g_return_if_fail (SOUP_IS_SESSION (session));
	g_return_if_fail (SOUP_IS_MESSAGE (msg));

	if (soup_message_disables_feature (msg, feature))
                return;

	iface = SOUP_SESSION_FEATURE_GET_IFACE (feature);
	if (iface->request_queued)
		iface->request_queued (feature, session, msg);
}

void
soup_session_feature_request_unqueued (SoupSessionFeature *feature,
				       SoupSession        *session,
				       SoupMessage        *msg)
{
	SoupSessionFeatureInterface *iface;

	g_return_if_fail (SOUP_IS_SESSION_FEATURE (feature));
	g_return_if_fail (SOUP_IS_SESSION (session));
	g_return_if_fail (SOUP_IS_MESSAGE (msg));

	if (soup_message_disables_feature (msg, feature))
                return;

	iface = SOUP_SESSION_FEATURE_GET_IFACE (feature);
        if (iface->request_unqueued)
                iface->request_unqueued (feature, session, msg);
}

/**
 * soup_session_feature_add_feature:
 * @feature: the "base" feature
 * @type: the #GType of a "sub-feature"
 *
 * Adds a "sub-feature" of type @type to the base feature @feature.
 * This is used for features that can be extended with multiple
 * different types. Eg, the authentication manager can be extended
 * with subtypes of #SoupAuth.
 *
 * Return value: %TRUE if @feature accepted @type as a subfeature.
 *
 * Since: 2.34
 */
gboolean
soup_session_feature_add_feature (SoupSessionFeature *feature,
				  GType               type)
{
	SoupSessionFeatureInterface *feature_iface =
              SOUP_SESSION_FEATURE_GET_IFACE (feature);

	if (feature_iface->add_feature)
		return feature_iface->add_feature (feature, type);
	else
		return FALSE;
}

/**
 * soup_session_feature_remove_feature:
 * @feature: the "base" feature
 * @type: the #GType of a "sub-feature"
 *
 * Removes the "sub-feature" of type @type from the base feature
 * @feature. See soup_session_feature_add_feature().
 *
 * Return value: %TRUE if @type was removed from @feature
 *
 * Since: 2.34
 */
gboolean
soup_session_feature_remove_feature (SoupSessionFeature *feature,
				     GType               type)
{
	SoupSessionFeatureInterface *feature_iface =
              SOUP_SESSION_FEATURE_GET_IFACE (feature);

	if (feature_iface->remove_feature)
		return feature_iface->remove_feature (feature, type);
	else
		return FALSE;
}

/**
 * soup_session_feature_has_feature:
 * @feature: the "base" feature
 * @type: the #GType of a "sub-feature"
 *
 * Tests if @feature has a "sub-feature" of type @type. See
 * soup_session_feature_add_feature().
 *
 * Return value: %TRUE if @feature has a subfeature of type @type
 *
 * Since: 2.34
 */
gboolean
soup_session_feature_has_feature (SoupSessionFeature *feature,
				  GType               type)
{
	SoupSessionFeatureInterface *feature_iface =
              SOUP_SESSION_FEATURE_GET_IFACE (feature);

	if (feature_iface->has_feature)
		return feature_iface->has_feature (feature, type);
	else
		return FALSE;
}
