#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include "utils.c"

enum
{
    TRAP_GETC = 0x20,  /* get character from keyboard, not echoed onto the terminal */
    TRAP_OUT = 0x21,   /* output a character */
    TRAP_PUTS = 0x22,  /* output a word string */
    TRAP_IN = 0x23,    /* get character from keyboard, echoed onto the terminal */
    TRAP_PUTSP = 0x24, /* output a byte string */
    TRAP_HALT = 0x25   /* halt the program */
};

#define MEMORY_MAX (1 << 16)
uint16_t memory[MEMORY_MAX];

enum
{
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC,
    R_COND,
    R_COUNT
};
uint16_t reg[R_COUNT];

enum
{
    FL_POS = 1 << 0, /* P */
    FL_ZRO = 1 << 1, /* Z */
    FL_NEG = 1 << 2, /* N */
};

enum
{
    OP_BR = 0, /* branch */
    OP_ADD,    /* add  */
    OP_LD,     /* load */
    OP_ST,     /* store */
    OP_JSR,    /* jump register */
    OP_AND,    /* bitwise and */
    OP_LDR,    /* load register */
    OP_STR,    /* store register */
    OP_RTI,    /* unused */
    OP_NOT,    /* bitwise not */
    OP_LDI,    /* load indirect */
    OP_STI,    /* store indirect */
    OP_JMP,    /* jump */
    OP_RES,    /* reserved (unused) */
    OP_LEA,    /* load effective address */
    OP_TRAP    /* execute trap */
};

uint16_t sign_extend(uint16_t x, int bit_count)
{
    if ((x >> (bit_count - 1)) & 1) {
        x |= (0xFFFF << bit_count);
    }
    return x;
}

void update_flags(uint16_t r)
{
    if (reg[r] == 0)
    {
        reg[R_COND] = FL_ZRO;
    }
    else if (reg[r] >> 15) /* a 1 in the left-most bit indicates negative */
    {
        reg[R_COND] = FL_NEG;
    }
    else
    {
        reg[R_COND] = FL_POS;
    }
}

#define R_BITMASK 0x7
#define BOOL_BITMASK 0x1

int main(int argc, const char* argv[])
{
    if (argc < 2)
    {
        /* show usage string */
        printf("lc3 [image-file1] ...\n");
        exit(2);
    }

    for (int j = 1; j < argc; ++j)
    {
        if (!read_image(argv[j]))
        {
            printf("failed to load image: %s\n", argv[j]);
            exit(1);
        }
    }

    signal(SIGINT, handle_interrupt);
    disable_input_buffering();

    /* since exactly one condition flag should be set at any given time, set the Z flag */
    reg[R_COND] = FL_ZRO;

    /* set the PC to starting position */
    /* 0x3000 is the default */
    enum { PC_START = 0x3000 };
    reg[R_PC] = PC_START;

    int running = 1;
    while (running)
    {
        /* FETCH */
        uint16_t instr = mem_read(reg[R_PC]++);
        uint16_t op = instr >> 12;

        switch (op)
        {
            case OP_ADD:
                uint16_t r0 = (instr >> 9) & R_BITMASK;
                uint16_t r1 = (instr >> 6) & R_BITMASK;
                uint16_t imm_flag = (instr >> 5) & BOOL_BITMASK;
                
                if (imm_flag)
                {
                    uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                    reg[r0] = reg[r1] + imm5;
                }
                else
                {
                    uint16_t r2 = instr & R_BITMASK;
                    reg[r0] = reg[r1] + reg[r2];
                }

                update_flags(r0);
                break;

            case OP_AND:
                uint16_t r0 = (instr >> 9) & R_BITMASK;
                uint16_t r1 = (instr >> 6) & R_BITMASK;
                uint16_t imm_flag = (instr >> 5) & BOOL_BITMASK;
                
                if (imm_flag)
                {
                    uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                    reg[r0] = reg[r1] & imm5;
                }
                else
                {
                    uint16_t r2 = instr & R_BITMASK;
                    reg[r0] = reg[r1] & reg[r2];
                }

                update_flags(r0);
                break;
            case OP_NOT:
                uint16_t r0 = (instr >> 9) & R_BITMASK;
                uint16_t r1 = (instr >> 6) & R_BITMASK;
                
                reg[r0] = ~r1;

                update_flags(r0);
                break;
            case OP_BR:
                uint16_t n = (instr >> 11) & BOOL_BITMASK;
                uint16_t z = (instr >> 10) & BOOL_BITMASK;
                uint16_t p = (instr >> 9) & BOOL_BITMASK;

                if (n && (reg[R_COND] == FL_NEG) || z && (reg[R_COND] == FL_ZRO) || p && (reg[R_COND] == FL_POS))
                {
                    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                    reg[R_PC] += pc_offset;
                }                            
                
                break;
            case OP_JMP:
                uint16_t base_r = (instr >> 6) & R_BITMASK;
                reg[R_PC] = reg[base_r];
            
                break;
            case OP_JSR:
                reg[R_R7] = reg[R_PC];
                uint16_t flag = (instr >> 11) & BOOL_BITMASK;
                if (!flag)
                {
                    uint16_t base_r = (instr >> 6) & R_BITMASK;
                    reg[R_PC] = reg[base_r];
                } 
                else 
                {
                    uint16_t pc_offset = sign_extend(instr & 0x7FF, 11);
                    reg[R_PC] = pc_offset + reg[R_R7];
                }

                break;
            case OP_LD:
                uint16_t r0 = (instr >> 9) & R_BITMASK;
                uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);

                reg[r0] = mem_read(reg[R_PC] + pc_offset);
                update_flags(r0);

                break;
            case OP_LDI:
                uint16_t r0 = (instr >> 9) & R_BITMASK;
                uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);

                reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offset));
                update_flags(r0);
                break;

            case OP_LDR:
                uint16_t r0 = (instr >> 9) & R_BITMASK;
                uint16_t base_r = (instr >> 6) & R_BITMASK;
                uint16_t offset = sign_extend(instr & 0x3F, 6);

                reg[r0] = mem_read(reg[base_r] + offset);
                update_flags(r0);

                break;
            case OP_LEA:
                uint16_t r0 = (instr >> 9) & R_BITMASK;
                uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);

                reg[r0] = reg[R_PC] + pc_offset;
                update_flags(r0);

                break;
            case OP_ST:
                uint16_t sr = (instr >> 9) & R_BITMASK;
                uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                
                mem_write(reg[R_PC] + pc_offset, sr);

                break;
            case OP_STI:
                uint16_t sr = (instr >> 9) & R_BITMASK;
                uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                
                mem_write(mem_read(reg[R_PC + pc_offset]), reg[sr]);

                break;
            case OP_STR:
                uint16_t sr = (instr >> 9) & R_BITMASK;
                uint16_t base_r = (instr >> 6) & R_BITMASK;
                uint16_t offset = sign_extend(instr & 0x3F, 6);

                mem_write(reg[base_r] + offset, sr);

                break;
            case OP_TRAP:
                reg[R_R7] = reg[R_PC];

                switch (instr & 0xFF)
                {
                    case TRAP_GETC:
                        @{TRAP GETC}
                        break;
                    case TRAP_OUT:
                        @{TRAP OUT}
                        break;
                    case TRAP_PUTS:
                        uint16_t* c = memory + reg[R_R0];
                        while (*c)
                        {
                            putc((char)*c, stdout);
                            ++c;
                        }
                        fflush(stdout);

                        break;
                    case TRAP_IN:
                        @{TRAP IN}
                        break;
                    case TRAP_PUTSP:
                        @{TRAP PUTSP}
                        break;
                    case TRAP_HALT:
                        @{TRAP HALT}
                        break;
                }
            case OP_RES:
            case OP_RTI:
            default:
                abort();
                break;
        }
    }
    @{Shutdown}
}
