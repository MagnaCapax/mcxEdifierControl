/* BLE transport over BlueZ's D-Bus API (libdbus). bluetoothd does scanning,
 * connecting and GATT; this module finds the speaker, connects, and acquires
 * two socket file descriptors: one that delivers notifications from the RX
 * characteristic and one whose writes go out as write-without-response on
 * the TX characteristic (BlueZ AcquireNotify / AcquireWrite). */
#ifndef ED_BLUEZ_H
#define ED_BLUEZ_H

#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "model.h"

struct ed_link;

/* Set from a signal handler to make scans and waits return early. */
extern volatile sig_atomic_t ed_link_stop;

typedef void (*ed_scan_cb)(const char *address, const char *name, int rssi, void *ctx);

/* Discovers devices that advertise the model's UUIDs for a fixed time and
 * reports each once. Returns 0, or -1 with a message in err. */
int ed_scan(const struct ed_model *m, ed_scan_cb cb, void *ctx, char *err, size_t errcap);

/* Connects to address, or to the one speaker advertising the model's UUIDs
 * when address is NULL. Returns NULL with a message in err on failure. */
struct ed_link *ed_link_open(const struct ed_model *m, const char *address,
			     char *err, size_t errcap);
/* Largest frame ed_link_write accepts (from the negotiated MTU). */
size_t ed_link_max_write(const struct ed_link *l);
/* Returns 0, or -1 on error. */
int ed_link_write(struct ed_link *l, const uint8_t *buf, size_t n);
/* Waits up to timeout_ms for one notification. Returns its length, 0 on
 * timeout or interruption by a signal, -1 when the link is gone. */
ssize_t ed_link_read(struct ed_link *l, uint8_t *buf, size_t cap, int timeout_ms);
/* Releases the descriptors and disconnects if this process connected. */
void ed_link_close(struct ed_link *l);

#endif
