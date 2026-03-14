print_regs: print_regs.c
	arm-linux-gnueabihf-gcc -static $< -o $@

install: print_regs
	scp $^ root@172.25.0.80:/root

clean:
	rm print_regs
