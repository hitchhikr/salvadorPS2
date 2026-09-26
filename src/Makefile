all: 
	ee-as -march=mips32 -o zx0.bin zx0_mips.s
	ee-strip --output-target=binary zx0.bin
	bin2c zx0.bin zx0.h zx0_bin

clean:
	@rm -f *.bin
