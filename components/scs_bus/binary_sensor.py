import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_ADDRESS, CONF_ID

from . import ScsBus, scs_bus_ns

DEPENDENCIES = ["scs_bus"]

CONF_SCS_BUS_ID = "scs_bus_id"
CONF_FUNCTION = "function"

ScsBinarySensor = scs_bus_ns.class_(
    "ScsBinarySensor", binary_sensor.BinarySensor, cg.Component
)
ScsBinarySensorFunction = scs_bus_ns.enum("ScsBinarySensorFunction")

FUNCTIONS = {"doorbell": ScsBinarySensorFunction.DOORBELL}

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(ScsBinarySensor).extend(
    {
        cv.Required(CONF_SCS_BUS_ID): cv.use_id(ScsBus),
        cv.Required(CONF_ADDRESS): cv.int_range(min=0, max=255),
        cv.Required(CONF_FUNCTION): cv.enum(FUNCTIONS, lower=True),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await binary_sensor.register_binary_sensor(var, config)
    await cg.register_component(var, config)

    parent = await cg.get_variable(config[CONF_SCS_BUS_ID])
    cg.add(var.set_scs_bus(parent))
    cg.add(var.set_address(config[CONF_ADDRESS]))
    cg.add(var.set_function(config[CONF_FUNCTION]))
