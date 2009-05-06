/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2008-2009  Intel Corporation. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdio.h>
#include <time.h>

#include <dbus/dbus.h>
#include <glib.h>
#include <gdbus.h>

#include "ofono.h"

#include "driver.h"
#include "common.h"
#include "dbus-gsm.h"
#include "modem.h"

#define CALL_METER_INTERFACE "org.ofono.CallMeter"

#define CALL_METER_FLAG_CACHED 0x1
#define CALL_METER_FLAG_HAVE_PUCT 0x2

struct call_meter_data {
	struct ofono_call_meter_ops *ops;
	int flags;
	DBusMessage *pending;

	int call_meter;
	int acm;
	int acm_max;
	double ppu;
	char currency[4];
	char *passwd;
};

static struct call_meter_data *call_meter_create(void)
{
	struct call_meter_data *cm = g_try_new0(struct call_meter_data, 1);

	return cm;
}

static void call_meter_destroy(gpointer userdata)
{
	struct ofono_modem *modem = userdata;
	struct call_meter_data *cm = modem->call_meter;

	g_free(cm);

	modem->call_meter = NULL;
}

static void set_call_meter(struct ofono_modem *modem, int value)
{
	struct call_meter_data *cm = modem->call_meter;

	if (cm->call_meter != value) {
		DBusConnection *conn = dbus_gsm_connection();

		cm->call_meter = value;

		dbus_gsm_signal_property_changed(conn, modem->path,
							CALL_METER_INTERFACE,
							"CallMeter",
							DBUS_TYPE_UINT32,
							&cm->call_meter);
	}
}

static void set_acm(struct ofono_modem *modem, int value)
{
	struct call_meter_data *cm = modem->call_meter;

	if (cm->acm != value) {
		DBusConnection *conn = dbus_gsm_connection();

		cm->acm = value;

		dbus_gsm_signal_property_changed(conn, modem->path,
							CALL_METER_INTERFACE,
							"AccumulatedCallMeter",
							DBUS_TYPE_UINT32,
							&cm->acm);
	}
}

static void set_acm_max(struct ofono_modem *modem, int value)
{
	struct call_meter_data *cm = modem->call_meter;

	if (cm->acm_max != value) {
		DBusConnection *conn = dbus_gsm_connection();

		cm->acm_max = value;

		dbus_gsm_signal_property_changed(conn, modem->path,
							CALL_METER_INTERFACE,
							"AccumulatedCallMeterMaximum",
							DBUS_TYPE_UINT32,
							&cm->acm_max);
	}
}

static void set_ppu(struct ofono_modem *modem, double value)
{
	struct call_meter_data *cm = modem->call_meter;

	if (cm->ppu != value) {
		DBusConnection *conn = dbus_gsm_connection();

		cm->ppu = value;

		dbus_gsm_signal_property_changed(conn, modem->path,
							CALL_METER_INTERFACE,
							"PricePerUnit",
							DBUS_TYPE_DOUBLE,
							&cm->ppu);
	}
}

static void set_currency(struct ofono_modem *modem, const char *value)
{
	struct call_meter_data *cm = modem->call_meter;

	if (strlen(value) > 3) {
		ofono_error("Currency reported with size > 3: %s", value);
		return;
	}

	if (strcmp(cm->currency, value)) {
		DBusConnection *conn = dbus_gsm_connection();
		const char *dbusval = cm->currency;

		strncpy(cm->currency, value, 3);
		cm->currency[3] = '\0';

		dbus_gsm_signal_property_changed(conn, modem->path,
							CALL_METER_INTERFACE,
							"Currency",
							DBUS_TYPE_STRING,
							&dbusval);
	}
}

static void cm_get_properties_reply(struct ofono_modem *modem)
{
	struct call_meter_data *cm = modem->call_meter;
	//struct call_meter_property *property;
	DBusMessage *reply;
	DBusMessageIter iter, dict;
	const char *currency = cm->currency;

	reply = dbus_message_new_method_return(cm->pending);
	if (!reply)
		return;

	dbus_message_iter_init_append(reply, &iter);

	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
			PROPERTIES_ARRAY_SIGNATURE, &dict);

	dbus_gsm_dict_append(&dict, "CallMeter", DBUS_TYPE_UINT32,
				&cm->call_meter);

	dbus_gsm_dict_append(&dict, "AccumulatedCallMeter", DBUS_TYPE_UINT32,
				&cm->acm);

	dbus_gsm_dict_append(&dict, "AccumulatedCallMeterMaximum",
				DBUS_TYPE_UINT32, &cm->acm_max);

	dbus_gsm_dict_append(&dict, "PricePerUnit", DBUS_TYPE_DOUBLE, &cm->ppu);

	dbus_gsm_dict_append(&dict, "Currency", DBUS_TYPE_STRING, &currency);

	dbus_message_iter_close_container(&iter, &dict);

	dbus_gsm_pending_reply(&cm->pending, reply);
}

static void query_call_meter_callback(const struct ofono_error *error, int value,
					void *data)
{
	struct ofono_modem *modem = data;
	struct call_meter_data *cm = modem->call_meter;

	if (error->type == OFONO_ERROR_TYPE_NO_ERROR)
		set_call_meter(modem, value);

	if (cm->pending)
		cm_get_properties_reply(modem);
}

static gboolean query_call_meter(gpointer user)
{
	struct ofono_modem *modem = user;
	struct call_meter_data *cm = modem->call_meter;

	if (!cm->ops->call_meter_query) {
		if (cm->pending)
			cm_get_properties_reply(modem);

		return FALSE;
	}

	cm->ops->call_meter_query(modem, query_call_meter_callback, modem);

	return FALSE;
}

static void query_acm_callback(const struct ofono_error *error, int value,
					void *data)
{
	struct ofono_modem *modem = data;
	//struct call_meter_data *cm = modem->call_meter;

	if (error->type == OFONO_ERROR_TYPE_NO_ERROR)
		set_acm(modem, value);

	g_timeout_add(0, query_call_meter, modem);
}

static gboolean query_acm(gpointer user)
{
	struct ofono_modem *modem = user;
	struct call_meter_data *cm = modem->call_meter;

	if (!cm->ops->acm_query) {
		query_call_meter(modem);
		return FALSE;
	}

	cm->ops->acm_query(modem, query_acm_callback, modem);

	return FALSE;
}

static void query_acm_max_callback(const struct ofono_error *error, int value,
					void *data)
{
	struct ofono_modem *modem = data;
	struct call_meter_data *cm = modem->call_meter;

	if (error->type == OFONO_ERROR_TYPE_NO_ERROR)
		set_acm_max(modem, value);

	cm->flags |= CALL_METER_FLAG_CACHED;

	g_timeout_add(0, query_acm, modem);
}

static gboolean query_acm_max(gpointer user)
{
	struct ofono_modem *modem = user;
	struct call_meter_data *cm = modem->call_meter;

	if (!cm->ops->acm_max_query) {
		cm->flags |= CALL_METER_FLAG_CACHED;

		query_acm(modem);
		return FALSE;
	}

	cm->ops->acm_max_query(modem, query_acm_max_callback, modem);

	return FALSE;
}

static void query_puct_callback(const struct ofono_error *error,
				const char *currency, double ppu, void *data)
{
	struct ofono_modem *modem = data;
	struct call_meter_data *cm = modem->call_meter;

	if (error->type == OFONO_ERROR_TYPE_NO_ERROR) {
		cm->flags |= CALL_METER_FLAG_HAVE_PUCT;
		set_currency(modem, currency);
		set_ppu(modem, ppu);
	}

	g_timeout_add(0, query_acm_max, modem);
}

static gboolean query_puct(gpointer user)
{
	struct ofono_modem *modem = user;
	struct call_meter_data *cm = modem->call_meter;

	if (!cm->ops->puct_query) {
		query_acm_max(modem);
		return FALSE;
	}

	cm->ops->puct_query(modem, query_puct_callback, modem);

	return FALSE;
}

static DBusMessage *cm_get_properties(DBusConnection *conn, DBusMessage *msg,
					void *data)
{
	struct ofono_modem *modem = data;
	struct call_meter_data *cm = modem->call_meter;

	if (cm->pending)
		return dbus_gsm_busy(msg);

	cm->pending = dbus_message_ref(msg);

	/* We don't need to query ppu, currency & acm_max every time
	 * Not sure if we have to query acm & call_meter every time
	 * so lets play on the safe side and query them.  They should be
	 * fast to query anyway
	 */
	if (cm->flags & CALL_METER_FLAG_CACHED)
		query_acm(modem);
	else
		query_puct(modem);

	return NULL;
}

static void set_acm_max_query_callback(const struct ofono_error *error, int value,
					void *data)
{
	struct ofono_modem *modem = data;
	struct call_meter_data *cm = modem->call_meter;
	DBusMessage *reply;

	if (!cm->pending)
		return;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		ofono_error("Setting acm_max successful, but query was not");

		cm->flags &= ~CALL_METER_FLAG_CACHED;

		dbus_gsm_pending_reply(&cm->pending,
					dbus_gsm_failed(cm->pending));
		return;
	}

	reply = dbus_message_new_method_return(cm->pending);
	dbus_gsm_pending_reply(&cm->pending, reply);

	set_acm_max(modem, value);
}

static void set_acm_max_callback(const struct ofono_error *error, void *data)
{
	struct ofono_modem *modem = data;
	struct call_meter_data *cm = modem->call_meter;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		ofono_debug("Setting acm_max failed");
		dbus_gsm_pending_reply(&cm->pending,
					dbus_gsm_failed(cm->pending));
		return;
	}

	/* Assume if we have acm_reset, we have acm_query */
	cm->ops->acm_max_query(modem, set_acm_max_query_callback, modem);
}

static DBusMessage *prop_set_acm_max(DBusMessage *msg, struct ofono_modem *modem,
					DBusMessageIter *dbus_value,
					const char *pin2)
{
	struct call_meter_data *cm = modem->call_meter;
	dbus_uint32_t value;

	if (!cm->ops->acm_max_set)
		return dbus_gsm_not_implemented(msg);

	dbus_message_iter_get_basic(dbus_value, &value);

	cm->pending = dbus_message_ref(msg);

	cm->ops->acm_max_set(modem, value, pin2, set_acm_max_callback, modem);

	return NULL;
}

static void set_puct_query_callback(const struct ofono_error *error,
					const char *currency, double ppu,
					void *data)
{
	struct ofono_modem *modem = data;
	struct call_meter_data *cm = modem->call_meter;
	DBusMessage *reply;

	if (!cm->pending)
		return;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		ofono_error("Setting PUCT successful, but query was not");

		cm->flags &= ~CALL_METER_FLAG_CACHED;

		dbus_gsm_pending_reply(&cm->pending,
					dbus_gsm_failed(cm->pending));
		return;
	}

	reply = dbus_message_new_method_return(cm->pending);
	dbus_gsm_pending_reply(&cm->pending, reply);

	set_currency(modem, currency);
	set_ppu(modem, ppu);
}

static void set_puct_callback(const struct ofono_error *error, void *data)
{
	struct ofono_modem *modem = data;
	struct call_meter_data *cm = modem->call_meter;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		ofono_debug("setting puct failed");
		dbus_gsm_pending_reply(&cm->pending,
					dbus_gsm_failed(cm->pending));
		return;
	}

	/* Assume if we have puct_set, we have puct_query */
	cm->ops->puct_query(modem, set_puct_query_callback, modem);
}

/* This function is for the really bizarre case of someone trying to call
 * SetProperty before GetProperties.  But we must handle it...
 */
static void set_puct_initial_query_callback(const struct ofono_error *error,
						const char *currency,
						double ppu, void *data)
{
	struct ofono_modem *modem = data;
	struct call_meter_data *cm = modem->call_meter;
	DBusMessageIter iter;
	DBusMessageIter var;
	const char *name;
	const char *pin2;

	if (!cm->pending)
		return;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		dbus_gsm_pending_reply(&cm->pending,
					dbus_gsm_failed(cm->pending));
		return;
	}

	set_currency(modem, currency);
	set_ppu(modem, ppu);

	cm->flags |= CALL_METER_FLAG_HAVE_PUCT;

	dbus_message_iter_init(cm->pending, &iter);
	dbus_message_iter_get_basic(&iter, &name);
	dbus_message_iter_next(&iter);
	dbus_message_iter_recurse(&iter, &var);
	dbus_message_iter_next(&iter);
	dbus_message_iter_get_basic(&iter, &pin2);

	if (!strcmp(name, "PricePerUnit"))
		dbus_message_iter_get_basic(&var, &ppu);
	else
		dbus_message_iter_get_basic(&var, &currency);

	cm->ops->puct_set(modem, currency, ppu, pin2,
				set_puct_callback, modem);
}

static DBusMessage *prop_set_ppu(DBusMessage *msg, struct ofono_modem *modem,
				DBusMessageIter *var, const char *pin2)
{
	struct call_meter_data *cm = modem->call_meter;
	double ppu;

	if (!cm->ops->puct_set || !cm->ops->puct_query)
		return dbus_gsm_not_implemented(msg);

	dbus_message_iter_get_basic(var, &ppu);

	if (ppu < 0.0)
		return dbus_gsm_invalid_format(msg);

	cm->pending = dbus_message_ref(msg);

	if (cm->flags & CALL_METER_FLAG_HAVE_PUCT)
		cm->ops->puct_set(modem, cm->currency, ppu, pin2,
					set_puct_callback, modem);
	else
		cm->ops->puct_query(modem, set_puct_initial_query_callback,
					modem);

	return NULL;
}

static DBusMessage *prop_set_cur(DBusMessage *msg, struct ofono_modem *modem,
				DBusMessageIter *var, const char *pin2)
{
	struct call_meter_data *cm = modem->call_meter;
	const char *value;

	if (!cm->ops->puct_set || !cm->ops->puct_query)
		return dbus_gsm_not_implemented(msg);

	dbus_message_iter_get_basic(var, &value);

	if (strlen(value) > 3)
		return dbus_gsm_invalid_format(msg);

	cm->pending = dbus_message_ref(msg);

	if (cm->flags & CALL_METER_FLAG_HAVE_PUCT)
		cm->ops->puct_set(modem, value, cm->ppu, pin2,
					set_puct_callback, modem);
	else
		cm->ops->puct_query(modem, set_puct_initial_query_callback,
					modem);

	return NULL;
}

struct call_meter_property {
	const char *name;
	int type;
	DBusMessage* (*set)(DBusMessage *msg, struct ofono_modem *modem,
				DBusMessageIter *var, const char *pin2);
};

static struct call_meter_property cm_properties[] = {
	{ "AccumulatedCallMeterMaximum",DBUS_TYPE_UINT32,	prop_set_acm_max },
	{ "PricePerUnit",		DBUS_TYPE_DOUBLE,	prop_set_ppu },
	{ "Currency",			DBUS_TYPE_STRING,	prop_set_cur },
	{ NULL, 0, 0 },
};

static DBusMessage *cm_set_property(DBusConnection *conn, DBusMessage *msg,
					void *data)
{
	struct ofono_modem *modem = data;
	struct call_meter_data *cm = modem->call_meter;
	DBusMessageIter iter;
	DBusMessageIter var;
	const char *name, *passwd = "";
	struct call_meter_property *property;

	if (cm->pending)
		return dbus_gsm_busy(msg);

	if (!dbus_message_iter_init(msg, &iter))
		return dbus_gsm_invalid_args(msg);

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
		return dbus_gsm_invalid_args(msg);

	dbus_message_iter_get_basic(&iter, &name);

	dbus_message_iter_next(&iter);

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT)
		return dbus_gsm_invalid_args(msg);

	dbus_message_iter_recurse(&iter, &var);

	if (!dbus_message_iter_next(&iter))
		return dbus_gsm_invalid_args(msg);

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
			return dbus_gsm_invalid_args(msg);

	dbus_message_iter_get_basic(&iter, &passwd);

	if (!is_valid_pin(passwd))
		return dbus_gsm_invalid_format(msg);

	for (property = cm_properties; property->name; property++) {
		if (strcmp(name, property->name))
			continue;

		if (dbus_message_iter_get_arg_type(&var) != property->type)
			return dbus_gsm_invalid_format(msg);

		return property->set(msg, modem, &var, passwd);
	}

	return dbus_gsm_invalid_args(msg);
}

static void reset_acm_query_callback(const struct ofono_error *error, int value,
					void *data)
{
	struct ofono_modem *modem = data;
	struct call_meter_data *cm = modem->call_meter;
	DBusMessage *reply;

	if (!cm->pending)
		return;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		ofono_error("Reseting ACM successful, but query was not");

		cm->flags &= ~CALL_METER_FLAG_CACHED;

		dbus_gsm_pending_reply(&cm->pending,
					dbus_gsm_failed(cm->pending));
		return;
	}

	reply = dbus_message_new_method_return(cm->pending);
	dbus_gsm_pending_reply(&cm->pending, reply);

	set_acm(modem, value);
}

static void acm_reset_callback(const struct ofono_error *error, void *data)
{
	struct ofono_modem *modem = data;
	struct call_meter_data *cm = modem->call_meter;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		ofono_debug("reseting acm failed");
		dbus_gsm_pending_reply(&cm->pending,
					dbus_gsm_failed(cm->pending));
		return;
	}

	/* Assume if we have acm_reset, we have acm_query */
	cm->ops->acm_query(modem, reset_acm_query_callback, modem);
}

static DBusMessage *cm_acm_reset(DBusConnection *conn, DBusMessage *msg,
					void *data)
{
	struct ofono_modem *modem = data;
	struct call_meter_data *cm = modem->call_meter;
	DBusMessageIter iter;
	const char *pin2;

	if (cm->pending)
		return dbus_gsm_busy(msg);

	if (!dbus_message_iter_init(msg, &iter))
		return dbus_gsm_invalid_args(msg);

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
		return dbus_gsm_invalid_args(msg);

	dbus_message_iter_get_basic(&iter, &pin2);

	if (!is_valid_pin(pin2))
		return dbus_gsm_invalid_format(msg);

	if (!cm->ops->acm_reset)
		return dbus_gsm_not_implemented(msg);

	cm->pending = dbus_message_ref(msg);

	cm->ops->acm_reset(modem, pin2, acm_reset_callback, modem);

	return NULL;
}

static GDBusMethodTable cm_methods[] = {
	{ "GetProperties",	"",	"a{sv}",	cm_get_properties,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ "SetProperty",	"svs",	"",		cm_set_property,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ "Reset", 		"s",	"",		cm_acm_reset,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ }
};

static GDBusSignalTable cm_signals[] = {
	{ "PropertyChanged",	"sv" },
	{ "NearMaximumWarning",	"" },
	{ }
};

void ofono_call_meter_changed_notify(struct ofono_modem *modem, int new_value)
{
	set_call_meter(modem, new_value);
}

void ofono_call_meter_maximum_notify(struct ofono_modem *modem)
{
	DBusConnection *conn = dbus_gsm_connection();
	DBusMessage *signal;

	signal = dbus_message_new_signal(modem->path,
			CALL_METER_INTERFACE, "NearMaximumWarning");
	if (!signal) {
		ofono_error("Unable to allocate new %s.NearMaximumWarning "
				"signal", CALL_METER_INTERFACE);
		return;
	}

	g_dbus_send_message(conn, signal);
}

int ofono_call_meter_register(struct ofono_modem *modem,
				struct ofono_call_meter_ops *ops)
{
	DBusConnection *conn = dbus_gsm_connection();

	if (!modem || !ops)
		return -1;

	modem->call_meter = call_meter_create();

	if (!modem->call_meter)
		return -1;

	modem->call_meter->ops = ops;

	if (!g_dbus_register_interface(conn, modem->path, CALL_METER_INTERFACE,
					cm_methods, cm_signals, NULL, modem,
					call_meter_destroy)) {
		ofono_error("Could not create %s interface",
				CALL_METER_INTERFACE);
		call_meter_destroy(modem);

		return -1;
	}

	modem_add_interface(modem, CALL_METER_INTERFACE);

	return 0;
}

void ofono_call_meter_unregister(struct ofono_modem *modem)
{
	DBusConnection *conn = dbus_gsm_connection();

	if (!modem->call_meter)
		return;

	modem_remove_interface(modem, CALL_METER_INTERFACE);
	g_dbus_unregister_interface(conn, modem->path, CALL_METER_INTERFACE);

	modem->call_meter = NULL;
}
