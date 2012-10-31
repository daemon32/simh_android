/* vax610_sysdev.c: MicroVAX I system-specific logic

   Copyright (c) 2011-2012, Matt Burke
   This module incorporates code from SimH, Copyright (c) 1998-2008, Robert M Supnik

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   THE AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name(s) of the author(s) shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author(s).

   This module contains the MicroVAX I system-specific registers and devices.

   sysd         system devices

   15-Feb-2012  MB      First Version
*/

#include "vax_defs.h"
#include <time.h>

#ifndef DONT_USE_INTERNAL_ROM
#include "vax_ka610_bin.h"
#endif

/* MicroVAX I boot device definitions */

struct boot_dev {
    char                *name;
    int32               code;
    };

extern int32 R[16];
extern int32 in_ie;
extern int32 mchk_va, mchk_ref;
extern int32 int_req[IPL_HLVL];
extern jmp_buf save_env;
extern int32 p1;
extern int32 sim_switches;
extern FILE *sim_log;
extern CTAB *sim_vm_cmd;
extern int32 trpirq, mem_err;

int32 conisp, conpc, conpsl;                            /* console reg */
char cpu_boot_cmd[CBUFSIZE]  = { 0 };                   /* boot command */

static struct boot_dev boot_tab[] = {
    { "RQ", 0x00415544 },                               /* DUAn */
    { "XQ", 0x00415158 },                               /* XQAn */
    { NULL }
    };

t_stat sysd_reset (DEVICE *dptr);
t_stat vax610_boot (int32 flag, char *ptr);
t_stat vax610_boot_parse (int32 flag, char *ptr);
t_stat cpu_boot (int32 unitno, DEVICE *dptr);

extern int32 intexc (int32 vec, int32 cc, int32 ipl, int ei);
extern int32 iccs_rd (void);
extern int32 todr_rd (void);
extern int32 rxcs_rd (void);
extern int32 rxdb_rd (void);
extern int32 txcs_rd (void);
extern void iccs_wr (int32 dat);
extern void todr_wr (int32 dat);
extern void rxcs_wr (int32 dat);
extern void txcs_wr (int32 dat);
extern void txdb_wr (int32 dat);
extern void ioreset_wr (int32 dat);
extern int32 eval_int (void);

/* SYSD data structures

   sysd_dev     SYSD device descriptor
   sysd_unit    SYSD units
   sysd_reg     SYSD register list
*/

UNIT sysd_unit = { UDATA (NULL, 0, 0) };

REG sysd_reg[] = {
    { HRDATA (CONISP, conisp, 32) },
    { HRDATA (CONPC, conpc, 32) },
    { HRDATA (CONPSL, conpsl, 32) },
    { BRDATA (BOOTCMD, cpu_boot_cmd, 16, 8, CBUFSIZE), REG_HRO },
    { NULL }
    };

DEVICE sysd_dev = {
    "SYSD", &sysd_unit, sysd_reg, NULL,
    1, 16, 16, 1, 16, 8,
    NULL, NULL, &sysd_reset,
    NULL, NULL, NULL,
    NULL, 0
    };

/* Special boot command, overrides regular boot */

CTAB vax610_cmd[] = {
    { "BOOT", &vax610_boot, RU_BOOT,
      "bo{ot} <device>{/R5:flg} boot device\n", &run_cmd_message },
    { NULL }
    };

/* Read KA610 specific IPR's */

int32 ReadIPR (int32 rg)
{
int32 val;

switch (rg) {

    case MT_ICCS:                                       /* ICCS */
        val = iccs_rd ();
        break;

    case MT_RXCS:                                       /* RXCS */
        val = rxcs_rd ();
        break;

    case MT_RXDB:                                       /* RXDB */
        val = rxdb_rd ();
        break;

    case MT_TXCS:                                       /* TXCS */
        val = txcs_rd ();
        break;

    case MT_TXDB:                                       /* TXDB */
        val = 0;
        break;

    case MT_CONISP:                                     /* console ISP */
        val = conisp;
        break;

    case MT_CONPC:                                      /* console PC */
        val = conpc;
        break;

    case MT_CONPSL:                                     /* console PSL */
        val = conpsl;
        break;

    case MT_SID:                                        /* SID */
        val = (VAX610_SID | VAX610_FLOAT | VAX610_MREV | VAX610_HWREV);
        break;

    case MT_NICR:                                       /* NICR */
    case MT_ICR:                                        /* ICR */
    case MT_TODR:                                       /* TODR */
    case MT_CSRS:                                       /* CSRS */
    case MT_CSRD:                                       /* CSRD */
    case MT_CSTS:                                       /* CSTS */
    case MT_CSTD:                                       /* CSTD */
    case MT_TBDR:                                       /* TBDR */
    case MT_CADR:                                       /* CADR */
    case MT_MCESR:                                      /* MCESR */
    case MT_CAER:                                       /* CAER */
    case MT_SBIFS:                                      /* SBIFS */
    case MT_SBIS:                                       /* SBIS */
    case MT_SBISC:                                      /* SBISC */
    case MT_SBIMT:                                      /* SBIMT */
    case MT_SBIER:                                      /* SBIER */
    case MT_SBITA:                                      /* SBITA */
    case MT_SBIQC:                                      /* SBIQC */
    case MT_TBDATA:                                     /* TBDATA */
    case MT_MBRK:                                       /* MBRK */
    case MT_PME:                                        /* PME */
        val = 0;
        break;

    default:
        RSVD_OPND_FAULT;
        }

return val;
}

/* Write KA610 specific IPR's */

void WriteIPR (int32 rg, int32 val)
{
switch (rg) {

    case MT_ICCS:                                       /* ICCS */
        iccs_wr (val);
        break;

    case MT_RXCS:                                       /* RXCS */
        rxcs_wr (val);
        break;

    case MT_RXDB:                                       /* RXDB */
        break;

    case MT_TXCS:                                       /* TXCS */
        txcs_wr (val);
        break;

    case MT_TXDB:                                       /* TXDB */
        txdb_wr (val);
        break;

    case MT_IORESET:                                    /* IORESET */
        ioreset_wr (val);
        break;

    case MT_SID:
    case MT_CONISP:
    case MT_CONPC:
    case MT_CONPSL:                                     /* halt reg */
        RSVD_OPND_FAULT;

    case MT_NICR:                                       /* NICR */
    case MT_ICR:                                        /* ICR */
    case MT_TODR:                                       /* TODR */
    case MT_CSRS:                                       /* CSRS */
    case MT_CSRD:                                       /* CSRD */
    case MT_CSTS:                                       /* CSTS */
    case MT_CSTD:                                       /* CSTD */
    case MT_TBDR:                                       /* TBDR */
    case MT_CADR:                                       /* CADR */
    case MT_MCESR:                                      /* MCESR */
    case MT_CAER:                                       /* CAER */
    case MT_SBIFS:                                      /* SBIFS */
    case MT_SBIS:                                       /* SBIS */
    case MT_SBISC:                                      /* SBISC */
    case MT_SBIMT:                                      /* SBIMT */
    case MT_SBIER:                                      /* SBIER */
    case MT_SBITA:                                      /* SBITA */
    case MT_SBIQC:                                      /* SBIQC */
    case MT_TBDATA:                                     /* TBDATA */
    case MT_MBRK:                                       /* MBRK */
    case MT_PME:                                        /* PME */
        break;

    default:
        RSVD_OPND_FAULT;
        }

return;
}

/* Read/write I/O register space

   These routines are the 'catch all' for address space map.  Any
   address that doesn't explicitly belong to memory or I/O
   is given to these routines for processing.
*/

struct reglink {                                        /* register linkage */
    uint32      low;                                    /* low addr */
    uint32      high;                                   /* high addr */
    t_stat      (*read)(int32 pa);                      /* read routine */
    void        (*write)(int32 pa, int32 val, int32 lnt); /* write routine */
    };

struct reglink regtable[] = {
/*    { QVMBASE, QVMBASE+QVMSIZE, &qv_mem_rd, &qv_mem_wr }, */
    { 0, 0, NULL, NULL }
    };

/* ReadReg - read register space

   Inputs:
        pa      =       physical address
        lnt     =       length (BWLQ) - ignored
   Output:
        longword of data
*/

int32 ReadReg (uint32 pa, int32 lnt)
{
struct reglink *p;

for (p = &regtable[0]; p->low != 0; p++) {
    if ((pa >= p->low) && (pa < p->high) && p->read)
        return p->read (pa);
    }
MACH_CHECK (MCHK_READ);
}

/* WriteReg - write register space

   Inputs:
        pa      =       physical address
        val     =       data to write, right justified in 32b longword
        lnt     =       length (BWLQ)
   Outputs:
        none
*/

void WriteReg (uint32 pa, int32 val, int32 lnt)
{
struct reglink *p;

for (p = &regtable[0]; p->low != 0; p++) {
    if ((pa >= p->low) && (pa < p->high) && p->write) {
        p->write (pa, val, lnt);  
        return;
        }
    }
mem_err = 1;
SET_IRQL;
}

/* Special boot command - linked into SCP by initial reset

   Syntax: BOOT <device>{/R5:val}

   Sets up R0-R5, calls SCP boot processor with effective BOOT CPU
*/

t_stat vax610_boot (int32 flag, char *ptr)
{
t_stat r;

r = vax610_boot_parse (flag, ptr);                      /* parse the boot cmd */
if (r != SCPE_OK)                                       /* error? */
    return r;
strncpy (cpu_boot_cmd, ptr, CBUFSIZE);                  /* save for reboot */
return run_cmd (flag, "CPU");
}

/* Parse boot command, set up registers - also used on reset */

t_stat vax610_boot_parse (int32 flag, char *ptr)
{
char gbuf[CBUFSIZE];
char *slptr, *regptr;
int32 i, r5v, unitno;
DEVICE *dptr;
UNIT *uptr;
DIB *dibp;
t_stat r;

regptr = get_glyph (ptr, gbuf, 0);                      /* get glyph */
if ((slptr = strchr (gbuf, '/'))) {                     /* found slash? */
    regptr = strchr (ptr, '/');                         /* locate orig */
    *slptr = 0;                                         /* zero in string */
    }
dptr = find_unit (gbuf, &uptr);                         /* find device */
if ((dptr == NULL) || (uptr == NULL))
    return SCPE_ARG;
dibp = (DIB *) dptr->ctxt;                              /* get DIB */
if (dibp == NULL)
    return SCPE_ARG;
unitno = (int32) (uptr - dptr->units);
r5v = 0;
if ((strncmp (regptr, "/R5:", 4) == 0) ||
    (strncmp (regptr, "/R5=", 4) == 0) ||
    (strncmp (regptr, "/r5:", 4) == 0) ||
    (strncmp (regptr, "/r5=", 4) == 0)) {
    r5v = (int32) get_uint (regptr + 4, 16, LMASK, &r);
    if (r != SCPE_OK)
        return r;
    }
else if (*regptr != 0)
    return SCPE_ARG;
for (i = 0; boot_tab[i].name != NULL; i++) {
    if (strcmp (dptr->name, boot_tab[i].name) == 0) {
        R[0] = boot_tab[i].code | (('0' + unitno) << 24);
        R[1] = 0xC0;
        R[2] = 0;
        R[3] = 0;
        R[4] = 0;
        R[5] = r5v;
        return SCPE_OK;
        }
    }
return SCPE_NOFNC;
}

int32 sysd_hlt_enb (void)
{
return 1;
}

/* Machine check */

int32 machine_check (int32 p1, int32 opc, int32 cc, int32 delta)
{
int32 p2, acc;

p2 = mchk_va + 4;                                       /* save vap */
cc = intexc (SCB_MCHK, cc, 0, IE_EXC);                  /* take exception */
acc = ACC_MASK (KERN);                                  /* in kernel mode */
in_ie = 1;
SP = SP - 16;                                           /* push 4 words */
Write (SP, 12, L_LONG, WA);                             /* # bytes */
Write (SP + 4, p1, L_LONG, WA);                         /* mcheck type */
Write (SP + 8, p2, L_LONG, WA);                         /* parameter 1 */
Write (SP + 12, p2, L_LONG, WA);                        /* parameter 2 */
in_ie = 0;
return cc;
}

/* Console entry */

int32 con_halt (int32 code, int32 cc)
{
if ((cpu_boot_cmd[0] == 0) ||                           /* saved boot cmd? */
    (vax610_boot_parse (0, cpu_boot_cmd) != SCPE_OK) || /* reparse the boot cmd */ 
    (reset_all (0) != SCPE_OK) ||                       /* reset the world */
    (cpu_boot (0, NULL) != SCPE_OK))                    /* set up boot code */
    ABORT (STOP_BOOT);                                  /* any error? */
printf ("Rebooting...\n");
if (sim_log)
    fprintf (sim_log, "Rebooting...\n");
return cc;
}

/* Bootstrap */

t_stat cpu_boot (int32 unitno, DEVICE *dptr)
{
t_stat r;

printf ("Loading boot code from ka610.bin\n");
if (sim_log) fprintf (sim_log, 
    "Loading boot code from ka610.bin\n");
r = load_cmd (0, "-O ka610.bin 200");
if (r != SCPE_OK) {
#ifndef DONT_USE_INTERNAL_ROM
    FILE *f;

    if ((f = sim_fopen ("ka610.bin", "wb"))) {
        printf ("Saving boot code to ka610.bin\n");
        if (sim_log)
            fprintf (sim_log, "Saving boot code to ka610.bin\n");
        sim_fwrite (vax_ka610_bin, sizeof(vax_ka610_bin[0]), sizeof(vax_ka610_bin)/sizeof(vax_ka610_bin[0]), f);
        fclose (f);
        printf ("Loading boot code from ka610.bin\n");
        if (sim_log)
            fprintf (sim_log, "Loading boot code from ka610.bin\n");
        r = load_cmd (0, "-O ka610.bin 200");
        if (r == SCPE_OK)
            SP = PC = 512;
        }
#endif
    return r;
    }
SP = PC = 512;
AP = 1;
return SCPE_OK;
}

/* SYSD reset */

t_stat sysd_reset (DEVICE *dptr)
{
sim_vm_cmd = vax610_cmd;
return SCPE_OK;
}

t_stat cpu_set_model (UNIT *uptr, int32 val, char *cptr, void *desc)
{
return SCPE_NOFNC;
}

t_stat cpu_show_model (FILE *st, UNIT *uptr, int32 val, void *desc)
{
fprintf (st, "model=MicroVAX I");
return SCPE_OK;
}