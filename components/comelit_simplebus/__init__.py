"""Receive-only Simplebus2 decoder for the Comelit DIG477E TP28."""
from esphome import automation, pins
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_ID, CONF_TRIGGER_ID

DEPENDENCIES = ["esp32"]
AUTO_LOAD = ["binary_sensor"]
ns = cg.esphome_ns.namespace("comelit_simplebus")
ComelitSimplebus = ns.class_("ComelitSimplebus", cg.Component)
FrameTrigger = ns.class_("FrameTrigger", automation.Trigger.template(cg.uint8, cg.uint8, cg.uint8))
def validate_rx_pin(config):
    mode = config["mode"]
    if mode.get("output") or mode.get("open_drain"):
        raise cv.Invalid("TP28 is receive-only: output and open-drain modes are not allowed")
    return config

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(ComelitSimplebus),
    cv.Required("rx_pin"): cv.All(pins.internal_gpio_input_pin_schema, validate_rx_pin),
    cv.Optional("apartment_address", default=1): cv.int_range(min=0, max=255),
    cv.Optional("ring_duration", default="3s"): cv.All(cv.positive_time_period_milliseconds, cv.Range(max=cv.TimePeriod(milliseconds=60000))),
    cv.Optional("ring"): binary_sensor.binary_sensor_schema(icon="mdi:bell-ring"),
    cv.Optional("on_frame"): automation.validate_automation({
        cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(FrameTrigger),
    }),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    pin = await cg.gpio_pin_expression(config["rx_pin"])
    cg.add(var.set_rx_pin(pin))
    cg.add(var.set_apartment_address(config["apartment_address"]))
    cg.add(var.set_ring_duration(config["ring_duration"]))
    if "ring" in config:
        sensor = await binary_sensor.new_binary_sensor(config["ring"])
        cg.add(var.set_ring(sensor))
    for conf in config.get("on_frame", []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.add_frame_trigger(trigger))
        await automation.build_automation(trigger, [(cg.uint8, "command"), (cg.uint8, "address"), (cg.uint8, "checksum")], conf)
