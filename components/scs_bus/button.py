import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_ADDRESS, CONF_ID

from . import ScsBus, scs_bus_ns

DEPENDENCIES = ["scs_bus"]

CONF_SCS_BUS_ID = "scs_bus_id"
CONF_COMMAND = "command"

ScsButton = scs_bus_ns.class_("ScsButton", button.Button, cg.Component)
ScsButtonCommand = scs_bus_ns.enum("ScsButtonCommand")

COMMANDS = {"door_unlock": ScsButtonCommand.DOOR_UNLOCK}

CONFIG_SCHEMA = button.button_schema(ScsButton).extend(
    {
        cv.Required(CONF_SCS_BUS_ID): cv.use_id(ScsBus),
        cv.Required(CONF_ADDRESS): cv.int_range(min=0, max=255),
        cv.Required(CONF_COMMAND): cv.enum(COMMANDS, lower=True),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await button.register_button(var, config)
    await cg.register_component(var, config)

    parent = await cg.get_variable(config[CONF_SCS_BUS_ID])
    cg.add(var.set_scs_bus(parent))
    cg.add(var.set_address(config[CONF_ADDRESS]))
    cg.add(var.set_command(config[CONF_COMMAND]))
