set shell := ["bash", "-c"]

project_root := justfile_directory()
vivado_dir := "/home/jacques/Xilinx/2026.1/Vivado"
vitis_dir := "/home/jacques/Xilinx/2026.1/Vitis"

export XILINX_VIVADO := vivado_dir
export XILINX_HLS := vitis_dir
export XILINX_VITIS := vitis_dir

export VIRTUAL_ENV := project_root + "/.venv"
export PATH := project_root + "/toolchain/bin:" + project_root + "/.venv/bin:" + vivado_dir + "/bin:" + vitis_dir + "/bin:" + env_var('PATH')
export COCOTB_LOG_LEVEL := "INFO"

all: check-tools

setup:
	@chmod +x scripts/*.sh 2>/dev/null || true
	@bash scripts/01_setup_env.sh

setup-litex:
	@bash scripts/02_setup_litex.sh

setup-toolchain:
	@bash scripts/03_setup_toolchain.sh

get-images:
	@bash scripts/04_get_linux_images.sh

soc-gen:
	@bash scripts/05_build_soc.sh

soc-build:
	@bash scripts/05_build_soc.sh --build

soc-load:
	@bash scripts/06_program_fpga.sh

tftp:
	@sudo uv run --with tftpy python scripts/tftp_server.py

boot-serial:
	@uv run litex_term --speed 1000000 --serial-boot --images=images/boot.json /dev/ttyUSB1

train-mnist:
	@uv run python scripts/export_mnist_model.py

sw-build:
	riscv64-elf-gcc -march=rv32ima -mabi=ilp32 -mno-relax -O2 -nostdlib -fno-builtin -Isw sw/libaccel.c sw/nn_mnist.c -o sw/nn_mnist
	riscv64-elf-strip sw/nn_mnist
	riscv64-elf-gcc -march=rv32ima -mabi=ilp32 -mno-relax -O2 -nostdlib -fno-builtin -Isw sw/libaccel.c sw/accel_app.c -o sw/accel_app
	riscv64-elf-strip sw/accel_app

sw-package:
	@uv run python scripts/package_rootfs.py

sw: sw-build sw-package

check-tools:
	echo -n "Python (venv uv) : "; which python3 2>/dev/null || echo "Manquant (lancez 'just setup')"; \
	echo -n "Vivado           : "; vivado -version 2>/dev/null | head -n 1 || echo "Non détecté dans PATH"; \
	echo -n "GHDL             : "; ghdl --version 2>/dev/null | head -n 1 || echo "Non détecté"; \
	echo -n "cocotb-config    : "; which cocotb-config 2>/dev/null || echo "Non installé dans le venv"; \
	echo -n "RISC-V GCC       : "; which riscv64-unknown-elf-gcc riscv64-elf-gcc riscv32-unknown-elf-gcc riscv64-linux-gnu-gcc 2>/dev/null | head -n 1 || echo "Non détecté (lancez 'just setup-toolchain')"; \
	echo -n "DTC (DeviceTree) : "; which dtc 2>/dev/null || echo "Non détecté (requis pour Linux sur LiteX)"; \

test:
	@just check-tools
	cd rtl/sim && just test

clean:
	@rm -rf *.jou *.log *.pb .Xil
	cd rtl/sim && just clean

distclean: clean
	@rm -rf .venv toolchain
