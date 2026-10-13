import random
import cocotb
from cocotb.triggers import RisingEdge
from cocotb.clock import Clock

# CSR addreses
ADDR_CTRL      = 0  # 0x00
ADDR_STATUS    = 1  # 0x04
ADDR_DATA_A    = 2  # 0x08
ADDR_DATA_B    = 3  # 0x0C
ADDR_ACC_OUT   = 4  # 0x10
ADDR_DMA_SRC_A = 5  # 0x14
ADDR_DMA_SRC_B = 6  # 0x18
ADDR_DMA_LEN   = 7  # 0x1C

def pack_four_i8_to_word(byte0, byte1, byte2, byte3):
    return (byte0 & 0xFF) | ((byte1 & 0xFF) << 8) | ((byte2 & 0xFF) << 16) | ((byte3 & 0xFF) << 24)

async def wb_write(dut, adr, data):
    dut.wb_adr_i.value = adr
    dut.wb_dat_i.value = data
    dut.wb_we_i.value  = 1
    dut.wb_sel_i.value = 0xF
    dut.wb_cyc_i.value = 1
    dut.wb_stb_i.value = 1
    
    await RisingEdge(dut.wb_clk_i)
    while dut.wb_ack_o.value == 0:
        await RisingEdge(dut.wb_clk_i)
        
    dut.wb_cyc_i.value = 0
    dut.wb_stb_i.value = 0
    dut.wb_we_i.value  = 0

async def wb_read(dut, adr):
    dut.wb_adr_i.value = adr
    dut.wb_we_i.value  = 0
    dut.wb_sel_i.value = 0xF
    dut.wb_cyc_i.value = 1
    dut.wb_stb_i.value = 1
    
    await RisingEdge(dut.wb_clk_i)
    while dut.wb_ack_o.value == 0:
        await RisingEdge(dut.wb_clk_i)
        
    val = int(dut.wb_dat_o.value)
    dut.wb_cyc_i.value = 0
    dut.wb_stb_i.value = 0
    return val

async def reset_dut(dut):
    dut.wb_rst_i.value = 1
    dut.wb_cyc_i.value = 0
    dut.wb_stb_i.value = 0
    dut.wb_we_i.value  = 0
    dut.wb_adr_i.value = 0
    dut.wb_dat_i.value = 0
    dut.wb_sel_i.value = 0

    dut.m_wb_ack_i.value = 0
    dut.m_wb_dat_i.value = 0
    
    await RisingEdge(dut.wb_clk_i)
    await RisingEdge(dut.wb_clk_i)
    dut.wb_rst_i.value = 0
    await RisingEdge(dut.wb_clk_i)
    dut._log.info("reset done")

async def wb_memory_slave(dut, memory, latency=2):
    dut.m_wb_ack_i.value = 0
    dut.m_wb_dat_i.value = 0
    while True:
        await RisingEdge(dut.wb_clk_i)
        if dut.m_wb_cyc_o.value == 1 and dut.m_wb_stb_o.value == 1:
            adr = int(dut.m_wb_adr_o.value)

            # 2 cycles latency
            for _ in range(latency):
                await RisingEdge(dut.wb_clk_i)
            
            dut.m_wb_dat_i.value = memory.get(adr, 0)
            dut.m_wb_ack_i.value = 1
            await RisingEdge(dut.wb_clk_i)
            dut.m_wb_ack_i.value = 0

@cocotb.test()
async def test_wishbone_basic_registers(dut):
    cocotb.start_soon(Clock(dut.wb_clk_i, 10, unit="ns").start())
    await reset_dut(dut)

    status = await wb_read(dut, ADDR_STATUS)
    acc    = await wb_read(dut, ADDR_ACC_OUT)
    assert status == 0
    assert acc == 0

    test_val_a = 0x12345678
    test_val_b = 0x87654321
    await wb_write(dut, ADDR_DATA_A, test_val_a)
    await wb_write(dut, ADDR_DATA_B, test_val_b)
    
    read_a = await wb_read(dut, ADDR_DATA_A)
    read_b = await wb_read(dut, ADDR_DATA_B)
    assert read_a == test_val_a, f"got: {hex(read_a)} | expected: {hex(test_val_a)}"
    assert read_b == test_val_b, f"got: {hex(read_b)} | expected: {hex(test_val_b)}"


@cocotb.test()
async def test_wishbone_dot_product_with_irq(dut):
    cocotb.start_soon(Clock(dut.wb_clk_i, 10, unit="ns").start())
    await reset_dut(dut)

    a_vec = [10, 20, 30, 40]
    b_vec = [1, 2, 3, 4]
    expected = sum(a * b for a, b in zip(a_vec, b_vec))

    await wb_write(dut, ADDR_DATA_A, pack_four_i8_to_word(*a_vec))
    await wb_write(dut, ADDR_DATA_B, pack_four_i8_to_word(*b_vec))

    # Bit 0 = START_PIO
    # Bit 1 = CLR_ACC
    # Bit 2 = IRQ_EN
    await wb_write(dut, ADDR_CTRL, 0x07)

    done = 0
    for _ in range(10):
        status = await wb_read(dut, ADDR_STATUS)
        done = (status >> 1) & 0x1
        if done:
            break
        await RisingEdge(dut.wb_clk_i)

    assert done == 1
    assert dut.irq_o.value == 1

    raw_acc = await wb_read(dut, ADDR_ACC_OUT)
    hw_acc = raw_acc if raw_acc < (1 << 31) else raw_acc - (1 << 32)
    assert hw_acc == expected

    await wb_write(dut, ADDR_STATUS, 0x04)
    await RisingEdge(dut.wb_clk_i)
    assert dut.irq_o.value == 0


@cocotb.test()
async def test_wishbone_dma_vector_stream(dut):
    cocotb.start_soon(Clock(dut.wb_clk_i, 10, unit="ns").start())
    await reset_dut(dut)

    NUM_WORDS = 64  # 64 mots de 4 INT8 = 256 éléments
    a_elements = [random.randint(-50, 50) for _ in range(NUM_WORDS * 4)]
    b_elements = [random.randint(-50, 50) for _ in range(NUM_WORDS * 4)]
    expected_acc = sum(a * b for a, b in zip(a_elements, b_elements))

    PHYS_ADDR_A = 0x48000000
    PHYS_ADDR_B = 0x48001000
    WORD_ADDR_A = PHYS_ADDR_A >> 2
    WORD_ADDR_B = PHYS_ADDR_B >> 2

    memory = {}
    for i in range(NUM_WORDS):
        word_a = pack_four_i8_to_word(*a_elements[i*4 : (i+1)*4])
        word_b = pack_four_i8_to_word(*b_elements[i*4 : (i+1)*4])
        memory[WORD_ADDR_A + i] = word_a
        memory[WORD_ADDR_B + i] = word_b

    cocotb.start_soon(wb_memory_slave(dut, memory, latency=2))

    dut._log.info(f"SRC_A=0x{PHYS_ADDR_A:08X}, SRC_B=0x{PHYS_ADDR_B:08X}, LEN={NUM_WORDS}")
    await wb_write(dut, ADDR_DMA_SRC_A, PHYS_ADDR_A)
    await wb_write(dut, ADDR_DMA_SRC_B, PHYS_ADDR_B)
    await wb_write(dut, ADDR_DMA_LEN,   NUM_WORDS)

    # Bit 2 = IRQ_EN
    # Bit 3 = START_DMA
    await wb_write(dut, ADDR_CTRL, 0x0C)

    # dut._log.info("waiting interupt")
    for _ in range(2000):
        status = await wb_read(dut, ADDR_STATUS)
        dma_done = (status >> 4) & 0x1
        if dma_done:
            break
        await RisingEdge(dut.wb_clk_i)

    assert dma_done == 1
    assert dut.irq_o.value == 1

    raw_acc = await wb_read(dut, ADDR_ACC_OUT)
    hw_acc = raw_acc if raw_acc < (1 << 31) else raw_acc - (1 << 32)

    dut._log.info(f"got: {hw_acc} | expected: {expected_acc}")
    assert hw_acc == expected_acc