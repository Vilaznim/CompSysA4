CC = riscv64-unknown-elf-gcc
CFLAGS = -march=rv32im -mabi=ilp32 -O0 -nostartfiles -nostdlib -Ttext=0x0
TESTS = r_tests i_tests mem_branch_tests m_tests syscall_tests
OUTDIR = build
SIM ?= ../sim

all: $(OUTDIR) $(patsubst %, $(OUTDIR)/%.elf, $(TESTS))

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(OUTDIR)/%.elf: %.s | $(OUTDIR)
	$(CC) $(CFLAGS) $< -o $@

.PHONY: all run clean tests
clean:
	rm -rf $(OUTDIR)

run: all
	@if [ -x "$(SIM)" ]; then \
		for t in $(TESTS); do $(SIM) $(OUTDIR)/$$t.elf -s $(OUTDIR)/$$t.log || true; done; \
	else \
		echo "Simulator '$(SIM)' not found or not executable."; \
		echo "Attempting to build simulator in parent directory..."; \
		$(MAKE) -C .. || { echo "Failed to build simulator in parent dir"; exit 1; }; \
		if [ -x "$(SIM)" ]; then \
			for t in $(TESTS); do $(SIM) $(OUTDIR)/$$t.elf -s $(OUTDIR)/$$t.log || true; done; \
		else \
			echo "Build completed but '$(SIM)' still not found or not executable."; \
			echo "Set SIM=/path/to/sim if it's located elsewhere."; \
			exit 1; \
		fi; \
	fi

tests: run