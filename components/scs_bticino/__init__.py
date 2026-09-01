import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.const import CONF_ID

CONF_RX_PIN = "rx_pin"
CONF_TX_PIN = "tx_pin"

scs_bticino_ns = cg.esphome_ns.namespace("scs_bticino")
ScsBticinoController = scs_bticino_ns.class_("ScsBticinoController", cg.Component)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ScsBticinoController),
            cv.Required(CONF_RX_PIN): pins.internal_gpio_input_pin_schema,
            cv.Required(CONF_TX_PIN): pins.internal_gpio_output_pin_schema,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_rx_pin(await cg.gpio_pin_expression(config[CONF_RX_PIN])))
    cg.add(var.set_tx_pin(await cg.gpio_pin_expression(config[CONF_TX_PIN])))
