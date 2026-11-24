#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

int charger_panel_init(void)
{
	/* The OTM8009A display driver resets and initializes this panel at boot. */
	return 0;
}

int charger_touch_init(void)
{
	return device_is_ready(DEVICE_DT_GET(DT_CHOSEN(zephyr_touch))) ? 0 : -ENODEV;
}
