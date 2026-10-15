import random
import cocotb
from cocotb.triggers import RisingEdge
from cocotb.clock import Clock

async def reset_dut(dut):
    dut.reset_active_low.value = 0
    dut.enable.value = 1
    dut.clear_accumulator.value = 0
    dut.valid_input.value = 0
    dut.activation_data_input.value = 0
    dut.weight_data_input.value = 0
    
    await RisingEdge(dut.clock)
    await RisingEdge(dut.clock)
    dut.reset_active_low.value = 1
    await RisingEdge(dut.clock)
    dut._log.info("reset done")

@cocotb.test()
async def test_mac_directed_cases(dut):
    cocotb.start_soon(Clock(dut.clock, 10, unit="ns").start())
    await reset_dut(dut)

    corner_cases = [
        (0, 0),
        (127, 127),
        (-128, -128),
        (-128, 127),
    ]

    expected_accumulator = 0

    for activation, weight in corner_cases:
        dut.clear_accumulator.value = 0
        dut.valid_input.value = 1
        dut.activation_data_input.value = activation
        dut.weight_data_input.value = weight
        expected_accumulator += (activation * weight)
        await RisingEdge(dut.clock)

    dut.valid_input.value = 0
    dut.activation_data_input.value = 0
    dut.weight_data_input.value = 0

    while dut.valid_output.value == 0:
        await RisingEdge(dut.clock)
    
    while dut.valid_output.value == 1:
        await RisingEdge(dut.clock)

    hardware_accumulator = int(dut.accumulator_output.value.to_signed())
    dut._log.info(f"res: {hardware_accumulator} | expected: {expected_accumulator}")
    assert hardware_accumulator == expected_accumulator


@cocotb.test()
async def test_mac_random_dot_product(dut):
    cocotb.start_soon(Clock(dut.clock, 10, unit="ns").start())
    await reset_dut(dut)

    vector_length = 64
    activations = [random.randint(-128, 127) for _ in range(vector_length)]
    weights = [random.randint(-128, 127) for _ in range(vector_length)]

    expected_accumulator = sum(activation * weight for activation, weight in zip(activations, weights))

    dut.clear_accumulator.value = 1
    dut.valid_input.value = 1
    dut.activation_data_input.value = activations[0]
    dut.weight_data_input.value = weights[0]
    await RisingEdge(dut.clock)

    dut.clear_accumulator.value = 0
    for element_index in range(1, vector_length):
        dut.activation_data_input.value = activations[element_index]
        dut.weight_data_input.value = weights[element_index]
        await RisingEdge(dut.clock)

    dut.valid_input.value = 0
    dut.activation_data_input.value = 0
    dut.weight_data_input.value = 0

    while dut.valid_output.value == 0:
        await RisingEdge(dut.clock)

    while dut.valid_output.value == 1:
        await RisingEdge(dut.clock)

    hardware_accumulator = int(dut.accumulator_output.value.to_signed())
    dut._log.info(f"dot product len:{vector_length}  res: {hardware_accumulator} | expected: {expected_accumulator}")
    assert hardware_accumulator == expected_accumulator