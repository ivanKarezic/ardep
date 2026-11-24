#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

int charger_panel_init(void)
{
	const struct gpio_dt_spec reset =
		GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), panel_reset_gpios);
	int ret;

	if (!gpio_is_ready_dt(&reset)) {
		return -ENODEV;
	}
	ret = gpio_pin_configure_dt(&reset, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return ret;
	}
	k_msleep(20);
	ret = gpio_pin_set_dt(&reset, 0);
	if (ret < 0) {
		return ret;
	}
	k_msleep(10);
	return 0;
}

int charger_touch_init(void)
{
	return device_is_ready(DEVICE_DT_GET(DT_CHOSEN(zephyr_touch))) ? 0 : -ENODEV;
}
