from esphome import automation, pins
import esphome.codegen as cg
from esphome.components.esp32 import include_builtin_idf_component
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_RX_PIN,
    CONF_TX_PIN,
)

DEPENDENCIES = ["esp32"]
MULTI_CONF = True

CONF_RX_INVERTED = "rx_inverted"
CONF_TX_INVERTED = "tx_inverted"
CONF_ACK_TIMEOUT = "ack_timeout"
CONF_MAX_RETRIES = "max_retries"
CONF_ON_FRAME = "on_frame"

scs_bus_ns = cg.esphome_ns.namespace("scs_bus")
ScsBus = scs_bus_ns.class_("ScsBus", cg.Component)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.declare_id(ScsBus),
            cv.Required(CONF_RX_PIN): pins.internal_gpio_input_pin_number,
            cv.Required(CONF_TX_PIN): pins.internal_gpio_output_pin_number,
            cv.Optional(CONF_RX_INVERTED, default=False): cv.boolean,
            cv.Optional(CONF_TX_INVERTED, default=False): cv.boolean,
            cv.Optional(
                CONF_ACK_TIMEOUT, default="100ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_MAX_RETRIES, default=3): cv.int_range(min=0, max=255),
            cv.Optional(CONF_ON_FRAME): automation.validate_automation({}),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
    cv.only_with_framework("esp-idf"),
)

_CALLBACK_AUTOMATIONS = (
    automation.CallbackAutomation(
        CONF_ON_FRAME,
        "add_on_frame_callback",
        [(cg.std_vector.template(cg.uint8), "frame")],
    ),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # ESPHome excludes this IDF driver unless a component explicitly needs it.
    include_builtin_idf_component("esp_driver_rmt")

    cg.add(var.set_rx_pin(config[CONF_RX_PIN]))
    cg.add(var.set_tx_pin(config[CONF_TX_PIN]))
    cg.add(var.set_rx_inverted(config[CONF_RX_INVERTED]))
    cg.add(var.set_tx_inverted(config[CONF_TX_INVERTED]))
    cg.add(var.set_ack_timeout(config[CONF_ACK_TIMEOUT].total_milliseconds))
    cg.add(var.set_max_retries(config[CONF_MAX_RETRIES]))

    await automation.build_callback_automations(var, config, _CALLBACK_AUTOMATIONS)
