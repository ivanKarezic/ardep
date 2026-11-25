#include "pm_control.h"
#include "gui.h"

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/can.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pm_control, CONFIG_PM_CONTROL_LOG_LEVEL);


/// TYPE DEFINITIONS

typedef struct {
    const struct device *can_dev;
    struct gpio_dt_spec enable_gpio;
    struct k_work_delayable pdo_transmit_work;
    struct k_work_delayable nmt_transmit_work;
    struct k_work_delayable hb_transmit_work;
    struct k_work_delayable sync_transmit_work;
} pm_control_t;


/// FUNCTION PROTOTYPES

static int pm_control_init(void);

static void pm_control_nmt_transmit_work_handler(struct k_work *work);
static void pm_control_hb_transmit_work_handler(struct k_work *work);
static void pm_control_sync_transmit_work_handler(struct k_work *work);
static void pm_control_pdo_transmit_work_handler(struct k_work *work);
static void pm_control_can_tx_callback(const struct device *dev, int error, void *user_data);
static void pm_control_can_rx_callback_hb_pm_1(const struct device *dev, struct can_frame *frame, void *user_data);
static void pm_control_can_rx_callback_pdo_all_pms(const struct device *dev, struct can_frame *frame, void *user_data);


/// STATIC DATA

static pm_control_t pm_control_config = {
    .can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus)),
    .enable_gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(lm_enable), gpios),
    .pdo_transmit_work = Z_WORK_DELAYABLE_INITIALIZER(pm_control_pdo_transmit_work_handler),
    .nmt_transmit_work = Z_WORK_DELAYABLE_INITIALIZER(pm_control_nmt_transmit_work_handler),
    .hb_transmit_work = Z_WORK_DELAYABLE_INITIALIZER(pm_control_hb_transmit_work_handler),
    .sync_transmit_work = Z_WORK_DELAYABLE_INITIALIZER(pm_control_sync_transmit_work_handler),
};

static const struct can_filter receive_frame_filter_hb_node_10 = {
  .flags = 0,
  .id = 0x70A,
  .mask = CAN_STD_ID_MASK,
};

static int can_filter_id_hb_node_10;


/// FUNCTION DEFINITIONS

static int pm_control_init(void)
{
    int err;

    err = device_is_ready(pm_control_config.can_dev);
    if (err < 0) {
        LOG_ERR("CAN device not ready, err: %d", err);
        return err;
    }

    /* UDS library starts the CAN interface, don't start it here, otherwise library initialization will fail */
    /*
    err = can_start(pm_control_config.can_dev);
    if ((err < 0) && (err != -EALREADY)) {
        LOG_ERR("Error starting CAN controller, err: %d", err);
        return err;
    }
    */

    err = device_is_ready(pm_control_config.enable_gpio.port);
    if (err < 0) {
        LOG_ERR("Enable GPIO device not ready, err: %d", err);
        return err;
    }

    // Set LM_enable to low (disable power modules)
    err = gpio_pin_configure_dt(&pm_control_config.enable_gpio, GPIO_OUTPUT_INACTIVE); 
    if (err < 0) {
        LOG_ERR("Failed to configure enable GPIO: %d", err);
        return err;
    }

    can_filter_id_hb_node_10 =
      can_add_rx_filter(pm_control_config.can_dev, pm_control_can_rx_callback_hb_pm_1, (void*) &can_filter_id_hb_node_10, &receive_frame_filter_hb_node_10);

    if (can_filter_id_hb_node_10 < 0) {
        LOG_ERR("Error adding CAN filter for CAN ID 0x%x (err %d)", receive_frame_filter_hb_node_10.id, can_filter_id_hb_node_10);
        return can_filter_id_hb_node_10;
    }

    struct can_filter receive_frame_filter_pdo_all_pms = {
        .flags = 0,
        .id = 0x18A,
        .mask = CAN_STD_ID_MASK,
    };

    for (int i = 0; i < CONFIG_PM_CONTROL_NUM_POWER_MODULES; i++) {
        int filter_id;

        // Add CAN filters for each power module's PDO message (0x18A - 0x190)
        receive_frame_filter_pdo_all_pms.id += i;
        filter_id = can_add_rx_filter(pm_control_config.can_dev, pm_control_can_rx_callback_pdo_all_pms, (void*)(uintptr_t)(i+1), &receive_frame_filter_pdo_all_pms);
        if (filter_id < 0) {
            LOG_ERR("Error adding CAN filter for CAN ID 0x%x (err %d)", receive_frame_filter_pdo_all_pms.id, filter_id);
            return filter_id;
        }
    }

    // Start the NMT transmit work
    k_work_schedule(&pm_control_config.nmt_transmit_work, K_MSEC(CONFIG_PM_CONTROL_POWER_UP_DELAY_MS));

    return 0;
}

static void pm_control_nmt_transmit_work_handler(struct k_work *work)
{
    struct can_frame nmt_frame = {
        .id = 0x000, // NMT message ID
        .dlc = 2,
        .data = {0x01, 0x00} // NMT Start command (broadcast to all nodes)
    };

    int ret = can_send(pm_control_config.can_dev, &nmt_frame, K_MSEC(CONFIG_PM_CONTROL_CAN_TRANSMIT_TIMEOUT_MS), pm_control_can_tx_callback, NULL);
    if (ret < 0) {
        LOG_ERR("Failed to send NMT Start command: %d", ret);
        // Reschedule the work to try again after a delay
        k_work_reschedule(&pm_control_config.nmt_transmit_work, K_MSEC(CONFIG_PM_CONTROL_CAN_TRANSMIT_TIMEOUT_MS));
    }
    else {
        LOG_INF("NMT Start command sent successfully. Waiting for heartbeat from node 10 to start periodic message transmission.");
    }
}

static void pm_control_hb_transmit_work_handler(struct k_work *work)
{
    int err;
    struct can_frame hb_frame = {
        .id = 0x701, // HB message ID for node ID 1
        .dlc = 1,
        .data = {0x05} // Operational state
    };

    err = can_send(pm_control_config.can_dev, &hb_frame, K_MSEC(CONFIG_PM_CONTROL_CAN_TRANSMIT_TIMEOUT_MS), pm_control_can_tx_callback, NULL);
    if (err < 0) {
        LOG_ERR("Failed to send HB message: %d", err);
    }

    // Reschedule the HB transmit work
    k_work_reschedule(&pm_control_config.hb_transmit_work, K_MSEC(CONFIG_PM_CONTROL_HB_PERIOD_MS));
}

static void pm_control_sync_transmit_work_handler(struct k_work *work)
{
    int err;
    struct can_frame sync_frame = {
        .id = 0x080, // SYNC message ID
        .dlc = 0,
        .data = {0} // No data for SYNC message
    };

    err = can_send(pm_control_config.can_dev, &sync_frame, K_MSEC(CONFIG_PM_CONTROL_CAN_TRANSMIT_TIMEOUT_MS), pm_control_can_tx_callback, NULL);
    if (err < 0) {
        LOG_ERR("Failed to send SYNC message: %d", err);
    }

    // Reschedule the SYNC transmit work
    k_work_reschedule(&pm_control_config.sync_transmit_work, K_MSEC(CONFIG_PM_CONTROL_SYNC_PERIOD_MS));
}

static void pm_control_pdo_transmit_work_handler(struct k_work *work)
{
    int err;
    uint16_t current_setpoint_100mA = 0u; // Current setpoint in 100mA units
    uint16_t fan_pwm_setpoint_pct = 0u; // Fan PWM setpoint in percentage
    bool power_module_enable = false;
    struct can_frame pdo_frame = {
        .id = 0x20A, // PDO message ID for node ID 10
        .dlc = 8,
        .data = {0} // Zero-initialize
    };

    power_module_enable = gui_get_power_module_enable();
    if(!power_module_enable) {
        // If power module is disabled, set current and fan PWM to 0
        current_setpoint_100mA = 0u;
        fan_pwm_setpoint_pct = 0u;

        // Set LM_enable to low (disable power modules)
        gpio_pin_set_dt(&pm_control_config.enable_gpio, 0); 
    } else {
        // Get the current setpoint and fan PWM setpoint from the GUI
        // Divide by number of power modules to get per-module setpoint
        current_setpoint_100mA = gui_get_current_setpoint_100ma() / CONFIG_PM_CONTROL_NUM_POWER_MODULES;
        fan_pwm_setpoint_pct = gui_get_fan_pwm_setpoint_pct();

        // Set LM_enable to high (enable power modules)
        gpio_pin_set_dt(&pm_control_config.enable_gpio, 1); 

    }

    // Fill PDO frame with data and send it to each power module
    for(int i = 0; i < CONFIG_PM_CONTROL_NUM_POWER_MODULES; i++) {

        pdo_frame.id += i; // Set the PDO message ID for each power module

        pdo_frame.data[0] = (current_setpoint_100mA) & 0xFFu;
        pdo_frame.data[1] = (current_setpoint_100mA >> 8) & 0xFFu;
        pdo_frame.data[2] = (fan_pwm_setpoint_pct) & 0xFFu;
        pdo_frame.data[3] = (fan_pwm_setpoint_pct >> 8) & 0xFFu;
        pdo_frame.data[4] = 0u;
        pdo_frame.data[5] = 0u;
        pdo_frame.data[6] = 0u;
        pdo_frame.data[7] = 0u;

        err = can_send(pm_control_config.can_dev, &pdo_frame, K_MSEC(CONFIG_PM_CONTROL_CAN_TRANSMIT_TIMEOUT_MS), pm_control_can_tx_callback, NULL);
        if (err < 0) {
            LOG_ERR("Failed to send PDO message: %d", err);
        }
    }

    // Reschedule the PDO transmit work
    k_work_reschedule(&pm_control_config.pdo_transmit_work, K_MSEC(CONFIG_PM_CONTROL_PDO_PERIOD_MS));
}

static void pm_control_can_tx_callback(const struct device *dev, int error, void *user_data)
{
    if (error != 0) {
        LOG_ERR("CAN transmission error: %d", error);
    }
}

static void pm_control_can_rx_callback_hb_pm_1(const struct device *dev, struct can_frame *frame, void *user_data) {
    LOG_INF("Received node 10 HB! Starting periodic transmission of messages (HB, SYNC, PDO)");

    // Don't listen to HB messages from node 10 anymore
    can_remove_rx_filter(dev, *((int*)user_data));

    // Start the HB transmit work
    k_work_schedule(&pm_control_config.hb_transmit_work, K_NO_WAIT);

    // Start the SYNC transmit work
    k_work_schedule(&pm_control_config.sync_transmit_work, K_NO_WAIT);

    // Start the PDO transmit work
    k_work_schedule(&pm_control_config.pdo_transmit_work, K_NO_WAIT);
}

static void pm_control_can_rx_callback_pdo_all_pms(const struct device *dev, struct can_frame *frame, void *user_data)
{
    int pm_index = (int)(uintptr_t)user_data; // Power module index (1-based)

    uint16_t actual_current_100mA = ((uint16_t) frame->data[0]) | ((uint16_t) frame->data[1] << 8);
    uint16_t actual_voltage_10mV = ((uint16_t) frame->data[2]) | ((uint16_t) frame->data[3] << 8);
    int8_t actual_temperature_2C = (int8_t) frame->data[4];
    uint16_t status_bitfield = ((uint16_t) frame->data[5]) | ((uint16_t) frame->data[6] << 8);

    LOG_DBG("Received PDO message from power module %d: ID=0x%03X, DLC=%d", pm_index, frame->id, frame->dlc);

    // Display the received values on the GUI for power module 1 only (for demonstration purposes)
    if(pm_index == 1)
    {
        gui_set_actual_current_100ma(actual_current_100mA);
        gui_set_actual_voltage_10mv(actual_voltage_10mV);
        gui_set_actual_temperature_2c(actual_temperature_2C);
        gui_set_status_bitfield(status_bitfield);
    }
}


/// INITIALIZATION

SYS_INIT(pm_control_init, APPLICATION, CONFIG_PM_CONTROL_INIT_PRIORITY);