/* Find, connect and open the speaker's control channel through bluetoothd.
 * Waits poll BlueZ's object tree instead of subscribing to signals: fewer
 * moving parts, and the waits are seconds long anyway. */
#define _DEFAULT_SOURCE /* strcasecmp, nanosleep on older C libraries */
#include "bluez.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "dbus.h"

/* The reference design scanned for 30 s; the advertising interval is not known. */
#define DISCOVERY_MS 30000
/* LE connect took 10-26 s on the unit; leave room above that. */
#define CONNECT_MS 60000
/* GATT discovery after connect; the reference design waited 40 s. */
#define RESOLVE_MS 40000
#define POLL_STEP_MS 250
/* BlueZ copies a write into a 512-byte buffer before sending it. */
#define BLUEZ_WRITE_BUF 512

volatile sig_atomic_t ed_link_stop;

struct ed_link {
	DBusConnection *bus;
	char device[160];
	int rx_fd, tx_fd;
	uint16_t mtu;
	bool we_connected;
};

/* State collected by one walk over BlueZ's objects. */
struct survey {
	const struct ed_model *model;
	const char *address; /* wanted address, or NULL to match by UUID */
	const char *device;  /* device path, set when looking for characteristics */
	char adapter[160];
	bool powered;
	char found[160];     /* matching device path */
	char found_addr[18];
	char others[160];    /* addresses of further matches, for the error message */
	int matches;
	bool connected, seen;
	char rx[200], tx[200];
};

static void sleep_ms(long ms)
{
	struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};

	nanosleep(&ts, NULL);
}

static long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static bool device_matches(const struct survey *s, const DBusMessageIter *props)
{
	const char *addr = ed_prop_str(props, "Address");

	if (s->address != NULL)
		return addr != NULL && strcasecmp(addr, s->address) == 0;
	return ed_prop_strv_has(props, "UUIDs", s->model->adv_uuid) ||
	       ed_prop_strv_has(props, "UUIDs", s->model->service_uuid);
}

static void survey_object(const char *path, const char *iface, const DBusMessageIter *props, void *ctx)
{
	struct survey *s = ctx;

	if (strcmp(iface, "org.bluez.Adapter1") == 0 && s->adapter[0] == '\0') {
		snprintf(s->adapter, sizeof(s->adapter), "%s", path);
		s->powered = ed_prop_bool(props, "Powered");
	} else if (strcmp(iface, "org.bluez.Device1") == 0 && device_matches(s, props)) {
		bool connected = ed_prop_bool(props, "Connected");
		/* RSSI is present only while the device is heard in the current scan. */
		bool seen = connected || ed_prop_has(props, "RSSI");
		const char *addr = ed_prop_str(props, "Address");

		if (!seen)
			return;
		if (s->matches++ == 0) {
			snprintf(s->found, sizeof(s->found), "%s", path);
			snprintf(s->found_addr, sizeof(s->found_addr), "%s", addr ? addr : "?");
			s->connected = connected;
			s->seen = true;
		} else {
			size_t used = strlen(s->others);

			snprintf(s->others + used, sizeof(s->others) - used, " %s", addr ? addr : "?");
		}
	} else if (strcmp(iface, "org.bluez.GattCharacteristic1") == 0 && s->device != NULL &&
		   strncmp(path, s->device, strlen(s->device)) == 0 &&
		   path[strlen(s->device)] == '/') {
		const char *uuid = ed_prop_str(props, "UUID");

		if (uuid != NULL && strcasecmp(uuid, s->model->rx_uuid) == 0)
			snprintf(s->rx, sizeof(s->rx), "%s", path);
		else if (uuid != NULL && strcasecmp(uuid, s->model->tx_uuid) == 0)
			snprintf(s->tx, sizeof(s->tx), "%s", path);
	}
}

static int survey_run(DBusConnection *bus, struct survey *s, char *err, size_t errcap)
{
	s->adapter[0] = s->found[0] = s->others[0] = s->rx[0] = s->tx[0] = '\0';
	s->matches = 0;
	s->connected = s->seen = s->powered = false;
	return ed_dbus_walk(bus, survey_object, s, err, errcap);
}

static DBusConnection *bus_open(char *err, size_t errcap)
{
	DBusError e;
	DBusConnection *bus;

	dbus_error_init(&e);
	bus = dbus_bus_get_private(DBUS_BUS_SYSTEM, &e);
	if (bus == NULL) {
		snprintf(err, errcap, "system D-Bus: %s", e.message ? e.message : "unavailable");
		dbus_error_free(&e);
		return NULL;
	}
	dbus_connection_set_exit_on_disconnect(bus, FALSE);
	return bus;
}

static void bus_close(DBusConnection *bus)
{
	dbus_connection_close(bus);
	dbus_connection_unref(bus);
}

/* LE-only discovery, filtered on the model's UUIDs unless an address is given. */
static int discovery_start(DBusConnection *bus, const struct survey *s, char *err, size_t errcap)
{
	const char *uuids[] = {s->model->adv_uuid, s->model->service_uuid};
	DBusMessage *msg = dbus_message_new_method_call(ED_BLUEZ, s->adapter, "org.bluez.Adapter1",
							"SetDiscoveryFilter");
	DBusMessageIter args, dict;
	DBusMessage *reply;

	if (msg == NULL) {
		snprintf(err, errcap, "out of memory");
		return -1;
	}
	dbus_message_iter_init_append(msg, &args);
	ed_dict_open(&args, &dict);
	ed_dict_add_str(&dict, "Transport", "le");
	if (s->address == NULL)
		ed_dict_add_strv(&dict, "UUIDs", uuids, 2);
	ed_dict_close(&args, &dict);
	reply = ed_dbus_call(bus, msg, DBUS_TIMEOUT_USE_DEFAULT, err, errcap);
	if (reply == NULL)
		return -1;
	dbus_message_unref(reply);
	/* Another program may already be scanning; that serves us too. */
	if (ed_dbus_call_void(bus, s->adapter, "org.bluez.Adapter1", "StartDiscovery",
			      DBUS_TIMEOUT_USE_DEFAULT, err, errcap) < 0 &&
	    !ed_dbus_err_is(err, "InProgress"))
		return -1;
	return 0;
}

static void discovery_stop(DBusConnection *bus, const struct survey *s)
{
	char ignored[256];

	ed_dbus_call_void(bus, s->adapter, "org.bluez.Adapter1", "StopDiscovery",
			  DBUS_TIMEOUT_USE_DEFAULT, ignored, sizeof(ignored));
}

/* Finds the adapter and checks it is powered; then finds the speaker, scanning if needed. */
static int locate(DBusConnection *bus, struct survey *s, char *err, size_t errcap)
{
	if (survey_run(bus, s, err, errcap) < 0)
		return -1;
	if (s->adapter[0] == '\0') {
		snprintf(err, errcap, "no Bluetooth adapter found (is bluetoothd running?)");
		return -1;
	}
	if (!s->powered) {
		snprintf(err, errcap, "Bluetooth adapter %s is powered off", s->adapter);
		return -1;
	}
	if (s->matches == 0) {
		long deadline = now_ms() + DISCOVERY_MS;

		fprintf(stderr, "edifierctl: scanning for %s\n", s->address ? s->address : s->model->name);
		if (discovery_start(bus, s, err, errcap) < 0)
			return -1;
		while (s->matches == 0 && now_ms() < deadline && !ed_link_stop) {
			sleep_ms(POLL_STEP_MS * 2);
			if (survey_run(bus, s, err, errcap) < 0)
				break;
		}
		discovery_stop(bus, s);
	}
	if (s->matches == 0) {
		snprintf(err, errcap, "speaker not found (powered on, in range, not connected to another BLE client?)");
		return -1;
	}
	if (s->matches > 1) {
		snprintf(err, errcap, "several speakers found: %s%s; pick one with -a", s->found_addr, s->others);
		return -1;
	}
	return 0;
}

static int acquire(DBusConnection *bus, const char *path, const char *method, int *fd,
		   uint16_t *mtu, char *err, size_t errcap)
{
	DBusMessage *msg = dbus_message_new_method_call(ED_BLUEZ, path, "org.bluez.GattCharacteristic1", method);
	DBusMessageIter args, dict;
	DBusMessage *reply;
	DBusError e;

	if (msg == NULL) {
		snprintf(err, errcap, "out of memory");
		return -1;
	}
	dbus_message_iter_init_append(msg, &args);
	ed_dict_open(&args, &dict);
	ed_dict_close(&args, &dict);
	reply = ed_dbus_call(bus, msg, DBUS_TIMEOUT_USE_DEFAULT, err, errcap);
	if (reply == NULL)
		return -1;
	dbus_error_init(&e);
	if (!dbus_message_get_args(reply, &e, DBUS_TYPE_UNIX_FD, fd, DBUS_TYPE_UINT16, mtu,
				   DBUS_TYPE_INVALID)) {
		snprintf(err, errcap, "%s: unexpected reply: %s", method, e.message ? e.message : "?");
		dbus_error_free(&e);
		dbus_message_unref(reply);
		return -1;
	}
	dbus_message_unref(reply);
	return 0;
}

/* Connect if needed, wait for GATT, then acquire the two descriptors. */
static int open_channel(struct ed_link *l, struct survey *s, char *err, size_t errcap)
{
	bool resolved = false;
	long deadline;

	if (!s->connected) {
		fprintf(stderr, "edifierctl: connecting to %s\n", s->found_addr);
		if (ed_dbus_call_void(l->bus, l->device, "org.bluez.Device1", "Connect", CONNECT_MS,
				      err, errcap) < 0 &&
		    !ed_dbus_err_is(err, "AlreadyConnected"))
			return -1;
		l->we_connected = true;
	}
	deadline = now_ms() + RESOLVE_MS;
	while (!ed_link_stop && now_ms() < deadline) {
		if (ed_dbus_get_bool(l->bus, l->device, "org.bluez.Device1", "ServicesResolved",
				     &resolved, err, errcap) < 0)
			return -1;
		if (resolved)
			break;
		sleep_ms(POLL_STEP_MS);
	}
	if (!resolved) {
		snprintf(err, errcap, "GATT services not resolved within %d s", RESOLVE_MS / 1000);
		return -1;
	}
	s->device = l->device;
	if (survey_run(l->bus, s, err, errcap) < 0)
		return -1;
	if (s->rx[0] == '\0' || s->tx[0] == '\0') {
		snprintf(err, errcap, "control service not found on %s", s->found_addr);
		return -1;
	}
	if (acquire(l->bus, s->rx, "AcquireNotify", &l->rx_fd, &l->mtu, err, errcap) < 0) {
		if (ed_dbus_err_is(err, "NotPermitted"))
			snprintf(err, errcap, "another program holds the speaker's notifications (a running bluetoothctl or daemon?)");
		return -1;
	}
	return acquire(l->bus, s->tx, "AcquireWrite", &l->tx_fd, &l->mtu, err, errcap);
}

struct ed_link *ed_link_open(const struct ed_model *m, const char *address, char *err, size_t errcap)
{
	struct survey s = {.model = m, .address = address};
	struct ed_link *l = calloc(1, sizeof(*l));

	if (l == NULL) {
		snprintf(err, errcap, "out of memory");
		return NULL;
	}
	l->rx_fd = l->tx_fd = -1;
	l->bus = bus_open(err, errcap);
	if (l->bus == NULL) {
		free(l);
		return NULL;
	}
	if (locate(l->bus, &s, err, errcap) < 0) {
		ed_link_close(l);
		return NULL;
	}
	snprintf(l->device, sizeof(l->device), "%s", s.found);
	if (open_channel(l, &s, err, errcap) < 0) {
		if (ed_link_stop)
			snprintf(err, errcap, "interrupted");
		ed_link_close(l);
		return NULL;
	}
	return l;
}

size_t ed_link_max_write(const struct ed_link *l)
{
	size_t att = l->mtu > 3 ? (size_t)l->mtu - 3 : 20;

	return att < BLUEZ_WRITE_BUF ? att : BLUEZ_WRITE_BUF;
}

int ed_link_write(struct ed_link *l, const uint8_t *buf, size_t n)
{
	if (n > ed_link_max_write(l))
		return -1;
	return write(l->tx_fd, buf, n) == (ssize_t)n ? 0 : -1;
}

ssize_t ed_link_read(struct ed_link *l, uint8_t *buf, size_t cap, int timeout_ms)
{
	struct pollfd p = {.fd = l->rx_fd, .events = POLLIN};
	int r = poll(&p, 1, timeout_ms);
	ssize_t n;

	if (r < 0)
		return errno == EINTR ? 0 : -1;
	if (r == 0)
		return 0;
	if (!(p.revents & POLLIN))
		return -1; /* HUP or error: the link is gone */
	n = read(l->rx_fd, buf, cap);
	return n > 0 ? n : -1;
}

void ed_link_close(struct ed_link *l)
{
	char ignored[256];

	if (l == NULL)
		return;
	if (l->rx_fd >= 0)
		close(l->rx_fd);
	if (l->tx_fd >= 0)
		close(l->tx_fd);
	/* Leave a connection we did not make; free one we made for the next client. */
	if (l->we_connected)
		ed_dbus_call_void(l->bus, l->device, "org.bluez.Device1", "Disconnect",
				  DBUS_TIMEOUT_USE_DEFAULT, ignored, sizeof(ignored));
	bus_close(l->bus);
	free(l);
}

/* scan: one discovery run, printing each matching device the first time it is heard. */
struct scan_state {
	struct survey s;
	ed_scan_cb cb;
	void *ctx;
	char printed[1024];
};

static void scan_object(const char *path, const char *iface, const DBusMessageIter *props, void *ctx)
{
	struct scan_state *st = ctx;
	const char *addr, *name;

	(void)path;
	if (strcmp(iface, "org.bluez.Device1") != 0 || !device_matches(&st->s, props) ||
	    !ed_prop_has(props, "RSSI"))
		return;
	addr = ed_prop_str(props, "Address");
	name = ed_prop_str(props, "Name");
	if (addr == NULL || strstr(st->printed, addr) != NULL)
		return;
	if (strlen(st->printed) + strlen(addr) + 2 < sizeof(st->printed)) {
		strcat(st->printed, addr);
		strcat(st->printed, " ");
	}
	st->cb(addr, name ? name : "", ed_prop_int16(props, "RSSI", 0), st->ctx);
}

int ed_scan(const struct ed_model *m, ed_scan_cb cb, void *ctx, char *err, size_t errcap)
{
	struct scan_state st = {.s = {.model = m}, .cb = cb, .ctx = ctx};
	DBusConnection *bus = bus_open(err, errcap);
	long deadline = now_ms() + DISCOVERY_MS;
	int rc = 0;

	if (bus == NULL)
		return -1;
	if (survey_run(bus, &st.s, err, errcap) < 0 || st.s.adapter[0] == '\0' || !st.s.powered) {
		if (st.s.adapter[0] == '\0' || !st.s.powered)
			snprintf(err, errcap, "no powered Bluetooth adapter found");
		bus_close(bus);
		return -1;
	}
	if (discovery_start(bus, &st.s, err, errcap) < 0) {
		bus_close(bus);
		return -1;
	}
	while (now_ms() < deadline && !ed_link_stop) {
		if (ed_dbus_walk(bus, scan_object, &st, err, errcap) < 0) {
			rc = -1;
			break;
		}
		sleep_ms(POLL_STEP_MS * 2);
	}
	discovery_stop(bus, &st.s);
	bus_close(bus);
	return rc;
}
