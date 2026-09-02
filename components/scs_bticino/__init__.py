import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome import automation
from esphome.const import CONF_ID
from esphome.core import ID

CONF_RX_PIN = "rx_pin"
CONF_TX_PIN = "tx_pin"
CONF_PAYLOAD = "payload"
CONF_TYPE = "type"

scs_bticino_ns = cg.esphome_ns.namespace("scs_bticino")
ScsBticinoController = scs_bticino_ns.class_("ScsBticinoController", cg.Component)
ScsBticinoSendAction = scs_bticino_ns.class_("ScsBticinoSendAction", automation.Action)

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


SEND_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ID): cv.use_id(ScsBticinoController),
        cv.Required(CONF_PAYLOAD): cv.All(
            [cv.Any(cv.hex_uint8_t, cv.uint8_t)],
            cv.Any(cv.Length(min=4, max=4), cv.Length(min=8, max=8)),
        ),
        cv.Required(CONF_TYPE): cv.one_of("response", "short", "extended", "extended_alt", lower=True),
    }
)


@automation.register_action("scs_bticino.send", ScsBticinoSendAction, SEND_SCHEMA, synchronous=True)
async def scs_bticino_send_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg)
    payload = cg.static_const_array(
        ID(f"{var.base}_payload", is_declaration=True, type=cg.uint8),
        cg.ArrayInitializer(*config[CONF_PAYLOAD]),
    )
    types = {"response": 0, "short": 1, "extended": 2, "extended_alt": 3}
    cg.add(var.set_parent(parent))
    cg.add(var.set_payload(payload, len(config[CONF_PAYLOAD])))
    cg.add(var.set_type(types[config[CONF_TYPE]]))
    return var
