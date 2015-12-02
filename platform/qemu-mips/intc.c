/*
 * Copyright (c) 2009 Corey Tabaka
 * Copyright (c) 2015 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <sys/types.h>
#include <debug.h>
#include <trace.h>
#include <err.h>
#include <reg.h>
#include <kernel/thread.h>
#include <platform/interrupts.h>
#include <arch/ops.h>
#include <arch/mips.h>
#include <platform/qemu-mips.h>

#define LOCAL_TRACE 1

static spin_lock_t lock;

#define PIC1 0x20
#define PIC2 0xA0

#define ICW1 0x11
#define ICW4 0x01

struct int_handler_struct {
    int_handler handler;
    void *arg;
};

#define PIC1_BASE 0
#define PIC2_BASE 8
#define INT_PIC2 2

static struct int_handler_struct int_handler_table[INT_VECTORS];

/*
 * Cached IRQ mask (enabled/disabled)
 */
static uint8_t irqMask[2];

/*
 * init the PICs and remap them
 */
static void map(uint32_t pic1, uint32_t pic2)
{
    /* send ICW1 */
    isa_write_8(PIC1, ICW1);
    isa_write_8(PIC2, ICW1);

    /* send ICW2 */
    isa_write_8(PIC1 + 1, pic1);   /* remap */
    isa_write_8(PIC2 + 1, pic2);   /*  pics */

    /* send ICW3 */
    isa_write_8(PIC1 + 1, 4);  /* IRQ2 -> connection to slave */
    isa_write_8(PIC2 + 1, 2);

    /* send ICW4 */
    isa_write_8(PIC1 + 1, 5);
    isa_write_8(PIC2 + 1, 1);

    /* disable all IRQs */
    isa_write_8(PIC1 + 1, 0xff);
    isa_write_8(PIC2 + 1, 0xff);

    irqMask[0] = 0xff;
    irqMask[1] = 0xff;
}

static void enable(unsigned int vector, bool enable)
{
    if (vector >= PIC1_BASE && vector < PIC1_BASE + 8) {
        vector -= PIC1_BASE;

        uint8_t bit = 1 << vector;

        if (enable && (irqMask[0] & bit)) {
            irqMask[0] = isa_read_8(PIC1 + 1);
            irqMask[0] &= ~bit;
            isa_write_8(PIC1 + 1, irqMask[0]);
            irqMask[0] = isa_read_8(PIC1 + 1);
        } else if (!enable && !(irqMask[0] & bit)) {
            irqMask[0] = isa_read_8(PIC1 + 1);
            irqMask[0] |= bit;
            isa_write_8(PIC1 + 1, irqMask[0]);
            irqMask[0] = isa_read_8(PIC1 + 1);
        }
    } else if (vector >= PIC2_BASE && vector < PIC2_BASE + 8) {
        vector -= PIC2_BASE;

        uint8_t bit = 1 << vector;

        if (enable && (irqMask[1] & bit)) {
            irqMask[1] = isa_read_8(PIC2 + 1);
            irqMask[1] &= ~bit;
            isa_write_8(PIC2 + 1, irqMask[1]);
            irqMask[1] = isa_read_8(PIC2 + 1);
        } else if (!enable && !(irqMask[1] & bit)) {
            irqMask[1] = isa_read_8(PIC2 + 1);
            irqMask[1] |= bit;
            isa_write_8(PIC2 + 1, irqMask[1]);
            irqMask[1] = isa_read_8(PIC2 + 1);
        }

        bit = 1 << (INT_PIC2 - PIC1_BASE);

        if (irqMask[1] != 0xff && (irqMask[0] & bit)) {
            irqMask[0] = isa_read_8(PIC1 + 1);
            irqMask[0] &= ~bit;
            isa_write_8(PIC1 + 1, irqMask[0]);
            irqMask[0] = isa_read_8(PIC1 + 1);
        } else if (irqMask[1] == 0 && !(irqMask[0] & bit)) {
            irqMask[0] = isa_read_8(PIC1 + 1);
            irqMask[0] |= bit;
            isa_write_8(PIC1 + 1, irqMask[0]);
            irqMask[0] = isa_read_8(PIC1 + 1);
        }
    } else {
        //dprintf(DEBUG, "Invalid PIC interrupt: %02x\n", vector);
    }
}

void issueEOI(unsigned int vector)
{
    if (vector >= PIC1_BASE && vector <= PIC1_BASE + 7) {
        isa_write_8(PIC1, 0x20);
    } else if (vector >= PIC2_BASE && vector <= PIC2_BASE + 7) {
        isa_write_8(PIC2, 0x20);
        isa_write_8(PIC1, 0x20);   // must issue both for the second PIC
    }
}

void platform_init_interrupts(void)
{
    // rebase the PIC out of the way of processor exceptions
    map(PIC1_BASE, PIC2_BASE);
}

status_t mask_interrupt(unsigned int vector)
{
    if (vector >= INT_VECTORS)
        return ERR_INVALID_ARGS;

//  dprintf(DEBUG, "%s: vector %d\n", __PRETTY_FUNCTION__, vector);

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&lock, state);

    enable(vector, false);

    spin_unlock_irqrestore(&lock, state);

    return NO_ERROR;
}


void platform_mask_irqs(void)
{
    irqMask[0] = isa_read_8(PIC1 + 1);
    irqMask[1] = isa_read_8(PIC2 + 1);

    isa_write_8(PIC1 + 1, 0xff);
    isa_write_8(PIC2 + 1, 0xff);

    irqMask[0] = isa_read_8(PIC1 + 1);
    irqMask[1] = isa_read_8(PIC2 + 1);
}

status_t unmask_interrupt(unsigned int vector)
{
    if (vector >= INT_VECTORS)
        return ERR_INVALID_ARGS;

//  dprintf("%s: vector %d\n", __PRETTY_FUNCTION__, vector);

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&lock, state);

    enable(vector, true);

    spin_unlock_irqrestore(&lock, state);

    return NO_ERROR;
}

enum handler_return platform_irq(struct mips_iframe *iframe, uint vector)
{
    THREAD_STATS_INC(interrupts);

    LTRACEF("vector %u\n", vector);

    // deliver the interrupt
    enum handler_return ret = INT_NO_RESCHEDULE;

    if (int_handler_table[vector].handler)
        ret = int_handler_table[vector].handler(int_handler_table[vector].arg);

    // ack the interrupt
    issueEOI(vector);

    return ret;
}

void register_int_handler(unsigned int vector, int_handler handler, void *arg)
{
    if (vector >= INT_VECTORS)
        panic("register_int_handler: vector out of range %d\n", vector);

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&lock, state);

    int_handler_table[vector].arg = arg;
    int_handler_table[vector].handler = handler;

    spin_unlock_irqrestore(&lock, state);
}

