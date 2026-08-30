/* Windows PnP inspection and CH375 driver installation helpers. */
#ifndef LIBSIGROK_WCH_DRIVER_WINDOWS_H
#define LIBSIGROK_WCH_DRIVER_WINDOWS_H

enum wch_driver_state {
	WCH_DRIVER_NO_DEVICE = 0,
	WCH_DRIVER_READY,
	WCH_DRIVER_MISSING,
	WCH_DRIVER_OTHER,
	WCH_DRIVER_ERROR,
};

enum wch_driver_state wch_driver_probe(void);
int wch_driver_install(void);

#endif
