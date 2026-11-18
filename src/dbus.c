/* libdbus helpers. Iterators are copied before use: libdbus advances them. */
#include "dbus.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

DBusMessage *ed_dbus_call(DBusConnection *bus, DBusMessage *msg, int timeout_ms,
			  char *err, size_t errcap)
{
	DBusError e;
	DBusMessage *reply;

	if (msg == NULL) {
		snprintf(err, errcap, "out of memory");
		return NULL;
	}
	dbus_error_init(&e);
	reply = dbus_connection_send_with_reply_and_block(bus, msg, timeout_ms, &e);
	dbus_message_unref(msg);
	if (reply == NULL) {
		snprintf(err, errcap, "%s: %s", e.name ? e.name : "error",
			 e.message ? e.message : "no reply");
		dbus_error_free(&e);
	}
	return reply;
}

int ed_dbus_call_void(DBusConnection *bus, const char *path, const char *iface,
		      const char *method, int timeout_ms, char *err, size_t errcap)
{
	DBusMessage *msg = dbus_message_new_method_call(ED_BLUEZ, path, iface, method);
	DBusMessage *reply = ed_dbus_call(bus, msg, timeout_ms, err, errcap);

	if (reply == NULL)
		return -1;
	dbus_message_unref(reply);
	return 0;
}

bool ed_dbus_err_is(const char *err, const char *suffix)
{
	char needle[96];

	snprintf(needle, sizeof(needle), ".%s:", suffix);
	return strstr(err, needle) != NULL;
}

bool ed_prop_find(const DBusMessageIter *props, const char *key, DBusMessageIter *value)
{
	DBusMessageIter it = *props;

	while (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter entry;
		const char *k;

		dbus_message_iter_recurse(&it, &entry);
		dbus_message_iter_get_basic(&entry, &k);
		if (strcmp(k, key) == 0) {
			dbus_message_iter_next(&entry);
			dbus_message_iter_recurse(&entry, value); /* into the variant */
			return true;
		}
		dbus_message_iter_next(&it);
	}
	return false;
}

const char *ed_prop_str(const DBusMessageIter *props, const char *key)
{
	DBusMessageIter v;
	const char *s = NULL;

	if (ed_prop_find(props, key, &v) && (dbus_message_iter_get_arg_type(&v) == DBUS_TYPE_STRING ||
					     dbus_message_iter_get_arg_type(&v) == DBUS_TYPE_OBJECT_PATH))
		dbus_message_iter_get_basic(&v, &s);
	return s;
}

bool ed_prop_bool(const DBusMessageIter *props, const char *key)
{
	DBusMessageIter v;
	dbus_bool_t b = FALSE;

	if (ed_prop_find(props, key, &v) && dbus_message_iter_get_arg_type(&v) == DBUS_TYPE_BOOLEAN)
		dbus_message_iter_get_basic(&v, &b);
	return b == TRUE;
}

bool ed_prop_has(const DBusMessageIter *props, const char *key)
{
	DBusMessageIter v;

	return ed_prop_find(props, key, &v);
}

int ed_prop_int16(const DBusMessageIter *props, const char *key, int fallback)
{
	DBusMessageIter v;
	dbus_int16_t n;

	if (!ed_prop_find(props, key, &v) || dbus_message_iter_get_arg_type(&v) != DBUS_TYPE_INT16)
		return fallback;
	dbus_message_iter_get_basic(&v, &n);
	return n;
}

bool ed_prop_strv_has(const DBusMessageIter *props, const char *key, const char *want)
{
	DBusMessageIter v, item;

	if (!ed_prop_find(props, key, &v) || dbus_message_iter_get_arg_type(&v) != DBUS_TYPE_ARRAY)
		return false;
	dbus_message_iter_recurse(&v, &item);
	while (dbus_message_iter_get_arg_type(&item) == DBUS_TYPE_STRING) {
		const char *s;

		dbus_message_iter_get_basic(&item, &s);
		if (strcasecmp(s, want) == 0)
			return true;
		dbus_message_iter_next(&item);
	}
	return false;
}

int ed_dbus_get_bool(DBusConnection *bus, const char *path, const char *iface,
		     const char *prop, bool *out, char *err, size_t errcap)
{
	DBusMessage *msg = dbus_message_new_method_call(ED_BLUEZ, path,
							"org.freedesktop.DBus.Properties", "Get");
	DBusMessage *reply;
	DBusMessageIter it, v;
	dbus_bool_t b;

	if (msg == NULL || !dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface,
						     DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID)) {
		if (msg)
			dbus_message_unref(msg);
		snprintf(err, errcap, "out of memory");
		return -1;
	}
	reply = ed_dbus_call(bus, msg, DBUS_TIMEOUT_USE_DEFAULT, err, errcap);
	if (reply == NULL)
		return -1;
	dbus_message_iter_init(reply, &it);
	dbus_message_iter_recurse(&it, &v);
	if (dbus_message_iter_get_arg_type(&v) != DBUS_TYPE_BOOLEAN) {
		dbus_message_unref(reply);
		snprintf(err, errcap, "%s is not a boolean", prop);
		return -1;
	}
	dbus_message_iter_get_basic(&v, &b);
	*out = b == TRUE;
	dbus_message_unref(reply);
	return 0;
}

int ed_dbus_walk(DBusConnection *bus, ed_object_cb cb, void *ctx, char *err, size_t errcap)
{
	DBusMessage *msg = dbus_message_new_method_call(ED_BLUEZ, "/",
							"org.freedesktop.DBus.ObjectManager",
							"GetManagedObjects");
	DBusMessage *reply = ed_dbus_call(bus, msg, DBUS_TIMEOUT_USE_DEFAULT, err, errcap);
	DBusMessageIter top, objects;

	if (reply == NULL)
		return -1;
	/* a{oa{sa{sv}}}: object path -> interface -> properties */
	dbus_message_iter_init(reply, &top);
	dbus_message_iter_recurse(&top, &objects);
	while (dbus_message_iter_get_arg_type(&objects) == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter object, ifaces;
		const char *path;

		dbus_message_iter_recurse(&objects, &object);
		dbus_message_iter_get_basic(&object, &path);
		dbus_message_iter_next(&object);
		dbus_message_iter_recurse(&object, &ifaces);
		while (dbus_message_iter_get_arg_type(&ifaces) == DBUS_TYPE_DICT_ENTRY) {
			DBusMessageIter entry, props;
			const char *iface;

			dbus_message_iter_recurse(&ifaces, &entry);
			dbus_message_iter_get_basic(&entry, &iface);
			dbus_message_iter_next(&entry);
			dbus_message_iter_recurse(&entry, &props);
			cb(path, iface, &props, ctx);
			dbus_message_iter_next(&ifaces);
		}
		dbus_message_iter_next(&objects);
	}
	dbus_message_unref(reply);
	return 0;
}

void ed_dict_open(DBusMessageIter *args, DBusMessageIter *dict)
{
	dbus_message_iter_open_container(args, DBUS_TYPE_ARRAY, "{sv}", dict);
}

static void dict_entry(DBusMessageIter *dict, const char *key, const char *sig,
		       DBusMessageIter *entry, DBusMessageIter *variant)
{
	dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, entry);
	dbus_message_iter_append_basic(entry, DBUS_TYPE_STRING, &key);
	dbus_message_iter_open_container(entry, DBUS_TYPE_VARIANT, sig, variant);
}

void ed_dict_add_str(DBusMessageIter *dict, const char *key, const char *value)
{
	DBusMessageIter entry, variant;

	dict_entry(dict, key, "s", &entry, &variant);
	dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &value);
	dbus_message_iter_close_container(&entry, &variant);
	dbus_message_iter_close_container(dict, &entry);
}

void ed_dict_add_strv(DBusMessageIter *dict, const char *key, const char *const *values, int n)
{
	DBusMessageIter entry, variant, array;

	dict_entry(dict, key, "as", &entry, &variant);
	dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "s", &array);
	for (int i = 0; i < n; i++)
		dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &values[i]);
	dbus_message_iter_close_container(&variant, &array);
	dbus_message_iter_close_container(&entry, &variant);
	dbus_message_iter_close_container(dict, &entry);
}

void ed_dict_close(DBusMessageIter *args, DBusMessageIter *dict)
{
	dbus_message_iter_close_container(args, dict);
}
