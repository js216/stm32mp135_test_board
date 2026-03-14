// SPDX-License-Identifier: BSD-3-Clause
// print_regs.c - print STM32 registers from Linux

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

struct reg_entry {
    const char *name;
    uintptr_t addr;
};

// define the registers to read
static struct reg_entry regs[] = {
    {"`TIM1_CR1`",          0x44000000},
    {"`TIM1_SR`",           0x44000010},
    {"`TIM1_CCMR2`",        0x4400001C},
    {"`TIM1_CCER`",         0x44000020},
    {"`TIM1_CNT`",          0x44000024},
    {"`TIM1_PSC`",          0x44000028},
    {"`TIM1_ARR`",          0x4400002C},
    {"`TIM1_CCR3`",         0x4400003C},
    {"`TIM1_BDTR`",         0x44000044},
    {"`TIM1_DMAR`",         0x4400004C},
    {"`TIM1_AF1`",          0x44000060},
    {"`TIM1_AF2`",          0x44000064},
    {"`TIM1_VERR`",         0x440003F4},
    {"`TIM1_IPIDR`",        0x440003F8},
    {"`TIM1_SIDR`",         0x440003FC},
    {"`GPIOB_MODER`",       0x50003000},
    {"`GPIOB_AFR[1]`",      0x50003024},
    {"`GPIOC_IDR`",         0x50004010},
    {"`RCC_MP_APB2ENSETR`", 0x50000708},
    {"`RCC_MP_APB2ENCLRR`", 0x5000070C},
};

#define NUM_REGS (sizeof(regs)/sizeof(regs[0]))
#define PAGE_SIZE 4096
#define PAGE_MASK (~(PAGE_SIZE-1))

static uint32_t read_reg(int mem_fd, uintptr_t addr)
{
    uintptr_t page_base = addr & PAGE_MASK;
    uintptr_t page_offset = addr & (PAGE_SIZE-1);

    void *map_base = mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, mem_fd, page_base);
    if (map_base == MAP_FAILED) {
        perror("mmap");
        return 0;
    }

    volatile uint32_t *reg_ptr = (volatile uint32_t *)((char *)map_base + page_offset);
    uint32_t val = *reg_ptr;

    munmap(map_base, PAGE_SIZE);
    return val;
}

int main(void)
{
    int mem_fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (mem_fd < 0) {
        perror("open /dev/mem");
        return 1;
    }

    printf("| %-19s | %-10s |\n", "Register", "Value");
    printf("| ------------------- |------------|\n");

    for (size_t i = 0; i < NUM_REGS; i++) {
        uint32_t val = read_reg(mem_fd, regs[i].addr);
        printf("| %-19s | 0x%08" PRIx32 " |\n", regs[i].name, val);
    }

    close(mem_fd);
    return 0;
}
