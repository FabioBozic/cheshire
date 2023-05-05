# Copyright 2022 ETH Zurich and University of Bologna.
# Licensed under the Apache License, Version 2.0, see LICENSE for details.
# SPDX-License-Identifier: Apache-2.0
#
# Nicole Narr <narrn@student.ethz.ch>
# Christopher Reinwardt <creinwar@student.ethz.ch>

BENDER       ?= bender

PROJECT      ?= cheshire
# {vcu128, genesys2}
BOARD        ?= vcu128
ip-dir       := xilinx

# Select board specific variables
ifeq ($(BOARD),vcu128)
	XILINX_PART  ?= xcvu37p-fsvh2892-2L-e
	XILINX_BOARD ?= xilinx.com:vcu128:part0:1.0
	ips-names    := xlnx_mig_ddr4
endif
ifeq ($(BOARD),genesys2)
	XILINX_PART  ?= xc7k325tffg900-2
	XILINX_BOARD ?= digilentinc.com:genesys2:part0:1.1
	ips-names    := xlnx_mig_7_ddr3
endif

out := out
bit := $(out)/cheshire_top_xilinx.bit
mcs := $(out)/cheshire_top_xilinx.mcs

VIVADOENV ?=  PROJECT=$(PROJECT)            \
              BOARD=$(BOARD)                \
              XILINX_PART=$(XILINX_PART)    \
              XILINX_BOARD=$(XILINX_BOARD)  \
              BIT=$(bit)

# select IIS-internal tool commands if we run on IIS machines
ifneq (,$(wildcard /etc/iis.version))
	VIVADO ?= vitis-2022.1 vivado
else
	VIVADO ?= vivado
endif

VIVADOFLAGS ?= -nojournal -mode batch -source scripts/prologue.tcl

# Derive from ips
ips := $(addsuffix .xci ,$(basename $(ips-names)))
bender-targets := -t $(BOARD) $(addprefix -t ,$(basename $(ips))) # Used to add defines through bender

all: $(bit)

# Include Xilinx IPs simulation makefile
include sim/simulate.mk

# Generate mcs from bitstream
$(mcs): $(bit)
	$(VIVADOENV) $(VIVADO) $(VIVADOFLAGS) -source scripts/write_cfgmem.tcl -tclargs $@ $^

$(bit): $(ips) scripts/add_sources.tcl
	@mkdir -p $(out)
	$(VIVADOENV) $(VIVADO) $(VIVADOFLAGS) -source scripts/run.tcl
	cp $(PROJECT).runs/impl_1/$(PROJECT)* ./$(out)

%.xci:
	@echo "Generating IP $(basename $@)"
	cd $(ip-dir)/$(basename $@) && $(MAKE) clean && $(VIVADOENV) VIVADO="$(VIVADO)" $(MAKE)
	cp $(ip-dir)/$(basename $@)/$(basename $@).srcs/sources_1/ip/$(basename $@)/$@ $@

gui:
	@echo "Starting $(vivado) GUI"
	@$(VIVADOENV) $(VIVADO) -nojournal -mode gui $(PROJECT).xpr &

program: #$(bit)
	@echo "Programming board $(BOARD) ($(XILINX_PART))"
	$(VIVADOENV) $(VIVADO) $(VIVADOFLAGS) -source scripts/program.tcl

clean:
	rm -rf *.log *.jou *.str *.mif *.xci *.xpr .Xil/ $(out) $(PROJECT).cache $(PROJECT).hw $(PROJECT).ioplanning $(PROJECT).ip_user_files $(PROJECT).runs $(PROJECT).sim

# Clean only top and copy back the IPs output here
rebuild_top:
	${MAKE} clean
	rm -f scripts/add_sources.tcl
	find xilinx -wholename "**/*.srcs/**/*.xci" | xargs -n 1 -I {} cp {} .
	${MAKE} $(bit)

# Bender
scripts/add_sources.tcl: ../../Bender.yml
	$(BENDER) script vivado -t fpga -t cv64a6_imafdcsclic_sv39 -t cva6 $(bender-targets) > $@

.PHONY: clean sim
