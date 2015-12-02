/*
 * Copyright (c) 2015 Travis Geiselbrecht
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
#include <trace.h>
#include <debug.h>
#include <stdint.h>
#include <bits.h>
#include <kernel/thread.h>
#include <arch/mips.h>

#define LOCAL_TRACE 1

extern enum handler_return platform_irq(struct mips_iframe *iframe, uint num);

void mips_gen_exception(struct mips_iframe *iframe)
{
    uint32_t excode = BITS_SHIFT(iframe->cause, 6, 2);
    if (excode == 0x8) {
        LTRACEF("SYSCALL, EPC 0x%x\n", iframe->epc);
        iframe->epc += 4;
    } else {
        LTRACEF("status 0x%x\n", iframe->status);
        LTRACEF("cause 0x%x\n", iframe->cause);
        LTRACEF("\texcode 0x%x\n", excode);
        LTRACEF("epc 0x%x\n", iframe->epc);
        for (;;);
    }
}

void mips_irq(struct mips_iframe *iframe, uint num)
{
    LTRACEF("IRQ %u, EPC 0x%x\n", num, iframe->epc);

    enum handler_return ret = INT_NO_RESCHEDULE;
    switch (num) {
        case 2: // XXX qemu specific
            ret = platform_irq(iframe, num);
            break;
        case 7: // builtin timer
            //ret = mips_timer_irq();
            break;
    }

    if (ret != INT_NO_RESCHEDULE)
        thread_preempt();
}

