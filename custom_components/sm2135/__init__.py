import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.const import (CONF_CLOCK_PIN, CONF_DATA_PIN, CONF_ID,
                           CONF_NUM_CHANNELS, CONF_NUM_CHIPS)

AUTO_LOAD = ['output']
sm2135_ns = cg.esphome_ns.namespace('sm2135')
SM2135 = sm2135_ns.class_('SM2135', cg.Component)

MULTI_CONF = True
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(SM2135),
    cv.Required(CONF_DATA_PIN): pins.gpio_output_pin_schema,
    cv.Required(CONF_CLOCK_PIN): pins.gpio_output_pin_schema,
}).extend(cv.COMPONENT_SCHEMA)


def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)

    data = yield cg.gpio_pin_expression(config[CONF_DATA_PIN])
    cg.add(var.set_data_pin(data))
    clock = yield cg.gpio_pin_expression(config[CONF_CLOCK_PIN])
    cg.add(var.set_clock_pin(clock))
