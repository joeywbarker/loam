CC     = gcc
CFLAGS = -m64 -ffreestanding -fno-pic -fno-pie -mno-red-zone -mno-sse -mno-mmx -fno-stack-protector \
         -fno-tree-loop-distribute-patterns -O2 -Wall -Wextra -nostdlib -static

all: loam.img

boot.bin: boot.asm
	nasm -f bin boot.asm -o boot.bin

kernel.elf: kernel.c loam.h kernel.ld
	$(CC) $(CFLAGS) -T kernel.ld -o kernel.elf kernel.c

kernel.bin: kernel.elf
	objcopy -O binary kernel.elf kernel.bin
	@test `stat -c %s kernel.bin` -le 32768 || (echo "kernel exceeds 32 KiB window"; exit 1)
	@echo "kernel: `stat -c %s kernel.bin` bytes"

loam.img: boot.bin kernel.bin
	dd if=/dev/zero of=loam.img bs=512 count=2048 2>/dev/null
	dd if=boot.bin of=loam.img conv=notrunc 2>/dev/null
	dd if=kernel.bin of=loam.img bs=512 seek=1 conv=notrunc 2>/dev/null

run: loam.img
	qemu-system-x86_64 -drive file=loam.img,format=raw,if=ide

clean:
	rm -f boot.bin kernel.elf kernel.bin loam.img host_sim

test:
	gcc -O2 -Wall -o test_isa test_isa.c && ./test_isa
	gcc -O2 -o host_sim host_sim.c && ./host_sim 100000 | tail -1
