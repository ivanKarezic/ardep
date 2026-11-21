/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

int main(void)
{
	int err;

	if (!gpio_is_ready_dt(&led)) {
		printk("LED GPIO controller is unavailable\n");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (err < 0) {
		printk("LED GPIO configuration failed (%d)\n", err);
		return err;
	}

	while (true) {
		k_sleep(K_MSEC(500));
		err = gpio_pin_toggle_dt(&led);
		if (err < 0) {
			printk("LED GPIO toggle failed (%d)\n", err);
			return err;
		}
	}
}
