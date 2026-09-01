import esphome.codegen as cg
from esphome.components import remote_base
import esphome.config_validation as cv
from esphome.core import ID

CONF_ACK = "ack"
CONF_PAYLOAD = "payload"
CONF_PAYLOAD_ID = "payload_id"

scs_bticino_ns = cg.esphome_ns.namespace("scs_bticino")
ScsBticinoData = scs_bticino_ns.struct("ScsBticinoData")
ScsBticinoTrigger = scs_bticino_ns.class_(
    "ScsBticinoTrigger", remote_base.RemoteReceiverTrigger
)
ScsBticinoDumper = scs_bticino_ns.class_(
    "ScsBticinoDumper", remote_base.RemoteReceiverDumperBase
)
ScsBticinoAction = scs_bticino_ns.class_(
    "ScsBticinoAction", remote_base.RemoteTransmitterActionBase
)

CONFIG_SCHEMA = cv.Schema({})


def validate_ack(value):
    if not cv.boolean(value):
        raise cv.Invalid("ack must be true")
    return True


TRANSMIT_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_ACK): validate_ack,
        cv.Optional(CONF_PAYLOAD): cv.All(
            [cv.Any(cv.hex_uint8_t, cv.uint8_t)],
            cv.Any(cv.Length(min=4, max=4), cv.Length(min=8, max=8)),
        ),
        cv.GenerateID(CONF_PAYLOAD_ID): cv.declare_id(cg.uint8),
    }
).add_extra(
    cv.has_exactly_one_key(CONF_ACK, CONF_PAYLOAD)
)


@remote_base.register_trigger("scs_bticino", ScsBticinoTrigger, ScsBticinoData)
def scs_bticino_trigger(var, config):
    pass


@remote_base.register_dumper("scs_bticino", ScsBticinoDumper)
def scs_bticino_dumper(var, config):
    pass


@remote_base.register_action("scs_bticino", ScsBticinoAction, TRANSMIT_SCHEMA)
async def scs_bticino_action(var, config, args):
    cg.add(var.set_ack(await cg.templatable(config.get(CONF_ACK, False), args, cg.bool_)))
    if CONF_PAYLOAD in config:
        payload = cg.static_const_array(
            ID(f"{var.base}_payload", is_declaration=True, type=cg.uint8),
            cg.ArrayInitializer(*config[CONF_PAYLOAD]),
        )
        cg.add(var.set_payload(payload, len(config[CONF_PAYLOAD])))
