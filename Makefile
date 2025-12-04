GCC=gcc -g -Wall -Wextra -pedantic -std=gnu11 -O

all: sim
rebuild: clean all

SIM_SRCS = main.c memory.c read_elf.c disassemble.c simulate.c

sim: $(SIM_SRCS)
	$(GCC) $(SIM_SRCS) -o sim

zip: ../src.zip

../src.zip: clean
	cd .. && zip -r src.zip src/Makefile src/*.c src/*.h

clean:
	rm -rf *.o sim  vgcore*
