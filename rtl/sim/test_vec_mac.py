# sim/test_vec_mac.py
import random
import cocotb
from cocotb.triggers import RisingEdge
from cocotb.clock import Clock

def pack_four_i8_to_word(byte0, byte1, byte2, byte3):
    return (byte0 & 0xFF) | ((byte1 & 0xFF) << 8) | ((byte2 & 0xFF) << 16) | ((byte3 & 0xFF) << 24)

async def reset_dut(dut):
    dut.reset_active_low.value = 0
    dut.enable.value = 1
    dut.clear_accumulator.value = 0
    dut.valid_input.value = 0
    dut.vector_data_a_input.value = 0
    dut.vector_data_b_input.value = 0
    await RisingEdge(dut.clock)
    await RisingEdge(dut.clock)
    dut.reset_active_low.value = 1
    await RisingEdge(dut.clock)
    dut._log.info("reset done")

@cocotb.test()
async def test_vector_dot_product_64(dut):
    cocotb.start_soon(Clock(dut.clock, 10, unit="ns").start())
    await reset_dut(dut)

    total_element_count = 64
    activations = [random.randint(-128, 127) for _ in range(total_element_count)]
    weights     = [random.randint(-128, 127) for _ in range(total_element_count)]
    expected_accumulator = sum(activation * weight for activation, weight in zip(activations, weights))

    word_activation_0 = pack_four_i8_to_word(activations[0], activations[1], activations[2], activations[3])
    word_weight_0     = pack_four_i8_to_word(weights[0], weights[1], weights[2], weights[3])
    
    dut.clear_accumulator.value = 1
    dut.valid_input.value = 1
    dut.vector_data_a_input.value = word_activation_0
    dut.vector_data_b_input.value = word_weight_0
    await RisingEdge(dut.clock)

    dut.clear_accumulator.value = 0
    for chunk_index in range(1, total_element_count // 4):
        base_index = chunk_index * 4
        word_activation = pack_four_i8_to_word(activations[base_index], activations[base_index + 1], activations[base_index + 2], activations[base_index + 3])
        word_weight     = pack_four_i8_to_word(weights[base_index], weights[base_index + 1], weights[base_index + 2], weights[base_index + 3])
        dut.vector_data_a_input.value = word_activation
        dut.vector_data_b_input.value = word_weight
        await RisingEdge(dut.clock)

    dut.valid_input.value = 0
    dut.vector_data_a_input.value = 0
    dut.vector_data_b_input.value = 0

    while dut.valid_output.value == 0:
        await RisingEdge(dut.clock)

    while dut.valid_output.value == 1:
        await RisingEdge(dut.clock)

    hardware_accumulator = int(dut.accumulator_output.value.to_signed())
    dut._log.info(f"res: {hardware_accumulator} | got: {expected_accumulator}")
    assert hardware_accumulator == expected_accumulator