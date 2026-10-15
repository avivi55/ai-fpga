import os
from migen import Module, Signal, Instance, ClockSignal, ResetSignal
from litex.soc.interconnect import wishbone
from litex.soc.integration.soc import SoCRegion

PROJ_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

class IAAccelerator(Module):
    def __init__(self, platform):
        self.bus = wishbone.Interface(data_width=32, adr_width=30, addressing="word")
        
        self.dma_bus = wishbone.Interface(data_width=32, adr_width=30, addressing="word")

        self.irq = Signal()

        proj_root = PROJ_ROOT
        vhdl_fifo = os.path.join(proj_root, "rtl", "vhdl", "sync_fifo.vhd")
        vhdl_vec  = os.path.join(proj_root, "rtl", "vhdl", "vec_mac_unit.vhd")
        vhdl_dma  = os.path.join(proj_root, "rtl", "vhdl", "dma_reader.vhd")
        vhdl_wb   = os.path.join(proj_root, "rtl", "vhdl", "accel_wishbone.vhd")

        platform.add_source(vhdl_fifo)
        platform.add_source(vhdl_vec)
        platform.add_source(vhdl_dma)
        platform.add_source(vhdl_wb)

        self.specials += Instance(
            "accel_wishbone",
            i_wb_clk_i = ClockSignal("sys"),
            i_wb_rst_i = ResetSignal("sys"),
            
            i_wb_cyc_i = self.bus.cyc,
            i_wb_stb_i = self.bus.stb,
            i_wb_we_i  = self.bus.we,
            i_wb_sel_i = self.bus.sel,
            i_wb_adr_i = self.bus.adr[:4],
            i_wb_dat_i = self.bus.dat_w,
            o_wb_dat_o = self.bus.dat_r,
            o_wb_ack_o = self.bus.ack,
            
            o_irq_o    = self.irq,

            o_m_wb_cyc_o = self.dma_bus.cyc,
            o_m_wb_stb_o = self.dma_bus.stb,
            o_m_wb_we_o  = self.dma_bus.we,
            o_m_wb_sel_o = self.dma_bus.sel,
            o_m_wb_adr_o = self.dma_bus.adr,
            o_m_wb_dat_o = self.dma_bus.dat_w,
            i_m_wb_dat_i = self.dma_bus.dat_r,
            i_m_wb_ack_i = self.dma_bus.ack
        )

def add_ia_accelerator_to_soc(soc, origin=0x80002000):
    soc.submodules.ia_accel = IAAccelerator(soc.platform)
    
    soc.bus.add_slave(
        name   = "ia_accel",
        slave  = soc.ia_accel.bus,
        region = SoCRegion(origin=origin, size=0x1000, cached=False)
    )

    soc.bus.add_master(
        name   = "ia_accel_dma",
        master = soc.ia_accel.dma_bus
    )
    
    soc.irq.add("ia_accel", use_loc_if_exists=True)
    print(f"[INFO] Accélérateur IA ajouté (CSR: 0x{origin:08x}, Maître DMA connecté au bus)")

    if hasattr(soc, "generate_dts"):
        orig_generate_dts = soc.generate_dts
        def generate_dts_hook(board_name, *args, **kwargs):
            orig_generate_dts(board_name, *args, **kwargs)
            dts_path = os.path.join("build", board_name, f"{board_name}.dts")
            if os.path.exists(dts_path):
                with open(dts_path, "r") as f:
                    content = f.read()
                
                accel_node = f"""
            ia_accel0: ia-accel@{origin:x} {{
                compatible = "generic-uio";
                reg = <0x{origin:x} 0x1000>;
                interrupt-parent = <&intc0>;
                interrupts = <6>;
                status = "okay";
            }};
"""
                if f"ia-accel@{origin:x}" not in content:
                    content = content.replace(
                        'root=/dev/ram0 ',
                        'root=/dev/ram0 uio_pdrv_genirq.of_id=generic-uio '
                    )
                    marker = "        aliases {"
                    if marker in content:
                        parts = content.split(marker)
                        bus_end = parts[0].rfind("        };")
                        if bus_end != -1:
                            new_soc = parts[0][:bus_end] + accel_node + parts[0][bus_end:]
                            content = new_soc + marker + parts[1]

                cpio_gz = os.path.join(PROJ_ROOT, "images", "rootfs.cpio.gz")
                if os.path.exists(cpio_gz):
                    sz = os.path.getsize(cpio_gz)
                    end_addr = 0x41000000 + sz
                    import re
                    content = re.sub(r'linux,initrd-end\s*=\s*<0x[0-9a-fA-F]+>;', f'linux,initrd-end   = <0x{end_addr:x}>;', content)

                with open(dts_path, "w") as f:
                    f.write(content)
                print(f"[INFo] Noeud UIO injecté et initrd-end mis à jour dans {dts_path}")

                dtc_cmd = f"dtc -O dtb -o {os.path.join(PROJ_ROOT, 'images', 'rv32.dtb')} {dts_path}"
                os.system(dtc_cmd)
        soc.generate_dts = generate_dts_hook