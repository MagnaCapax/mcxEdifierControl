/* Small libdbus helpers for talking to bluetoothd: blocking method calls,
 * property lookup in a{sv} dictionaries and a walk over BlueZ's objects. */
#ifndef ED_DBUS_H
#define ED_DBUS_H

#include <stdbool.h>
#include <stddef.h>

#include <dbus/dbus.h>

#define ED_BLUEZ "org.bluez"

/* Sends msg (consumed) and waits up to timeout_ms. On failure returns NULL
 * and writes "org.bluez.Error.Name: message" into err. */
DBusMessage *ed_dbus_call(DBusConnection *bus, DBusMessage *msg, int timeout_ms,
			  char *err, size_t errcap);
/* Calls a method that takes no arguments and returns nothing. 0 or -1. */
int ed_dbus_call_void(DBusConnection *bus, const char *path, const char *iface,
		      const char *method, int timeout_ms, char *err, size_t errcap);
/* True when err names the given D-Bus error suffix, e.g. "InProgress". */
bool ed_dbus_err_is(const char *err, const char *suffix);

/* Lookup in an a{sv} iterator positioned inside the array. */
bool ed_prop_find(const DBusMessageIter *props, const char *key, DBusMessageIter *value);
const char *ed_prop_str(const DBusMessageIter *props, const char *key);
bool ed_prop_bool(const DBusMessageIter *props, const char *key);
bool ed_prop_has(const DBusMessageIter *props, const char *key);
int ed_prop_int16(const DBusMessageIter *props, const char *key, int fallback);
bool ed_prop_strv_has(const DBusMessageIter *props, const char *key, const char *want);

/* Reads one boolean property with org.freedesktop.DBus.Properties.Get. */
int ed_dbus_get_bool(DBusConnection *bus, const char *path, const char *iface,
		     const char *prop, bool *out, char *err, size_t errcap);

/* Calls cb for every (object path, interface, properties) BlueZ exports. */
typedef void (*ed_object_cb)(const char *path, const char *iface,
			     const DBusMessageIter *props, void *ctx);
int ed_dbus_walk(DBusConnection *bus, ed_object_cb cb, void *ctx, char *err, size_t errcap);

/* Dictionary building for method arguments of type a{sv}. */
void ed_dict_open(DBusMessageIter *args, DBusMessageIter *dict);
void ed_dict_add_str(DBusMessageIter *dict, const char *key, const char *value);
void ed_dict_add_strv(DBusMessageIter *dict, const char *key, const char *const *values, int n);
void ed_dict_close(DBusMessageIter *args, DBusMessageIter *dict);

#endif
