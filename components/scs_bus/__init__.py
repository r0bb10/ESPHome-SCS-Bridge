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
AUTO_LOAD = ["binary_sensor", "button"]
MULTI_CONF = True

CONF_RX_INVERTED = "rx_inverted"
CONF_TX_INVERTED = "tx_inverted"
CONF_LOCAL_SYSTEM = "local_system"
CONF_LOCAL_ADDRESS = "local_address"
CONF_ON_TELEGRAM = "on_telegram"
CONF_DIAGNOSTICS = "diagnostics"

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
            cv.Optional(CONF_LOCAL_SYSTEM): cv.int_range(min=1, max=15),
            cv.Optional(CONF_LOCAL_ADDRESS): cv.int_range(min=0, max=0xFFFF),
            cv.Optional(CONF_DIAGNOSTICS, default="off"): cv.one_of("off", "telegrams", "verbose", lower=True),
            cv.Optional(CONF_ON_TELEGRAM): automation.validate_automation({}),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
    cv.only_with_framework("esp-idf"),
)

_CALLBACK_AUTOMATIONS = (
    automation.CallbackAutomation(
        CONF_ON_TELEGRAM,
        "add_on_telegram_callback",
        [(cg.std_vector.template(cg.uint8), "telegram")],
    ),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # ESPHome excludes this IDF driver unless a component explicitly needs it.
    include_builtin_idf_component("esp_driver_gpio")
    include_builtin_idf_component("esp_driver_gptimer")
    include_builtin_idf_component("esp_driver_rmt")

    cg.add(var.set_rx_pin(config[CONF_RX_PIN]))
    cg.add(var.set_tx_pin(config[CONF_TX_PIN]))
    cg.add(var.set_rx_inverted(config[CONF_RX_INVERTED]))
    cg.add(var.set_tx_inverted(config[CONF_TX_INVERTED]))
    diagnostics = {"off": 0, "telegrams": 1, "verbose": 2}
    cg.add(var.set_diagnostics(diagnostics[config[CONF_DIAGNOSTICS]]))
    if CONF_LOCAL_SYSTEM in config:
        cg.add(var.set_local_system(config[CONF_LOCAL_SYSTEM]))
    if CONF_LOCAL_ADDRESS in config:
        cg.add(var.set_local_address(config[CONF_LOCAL_ADDRESS]))
    await automation.build_callback_automations(var, config, _CALLBACK_AUTOMATIONS)
