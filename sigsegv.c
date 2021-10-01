/**
 * This source file is used to print out a stack-trace when your program
 * segfaults.  It is relatively reliable and spot-on accurate.
 *
 * This code is in the public domain.  Use it as you see fit, some credit
 * would be appreciated, but is not a prerequisite for usage.  Feedback
 * on it's use would encourage further development and maintenance.
 *
 * Author:  Jaco Kroon <jaco@kroon.co.za>
 *
 * Copyright (C) 2005 - 2008 Jaco Kroon
 */

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#define NO_CPP_DEMANGLE
#define SIGSEGV_NO_AUTO_INIT

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include <memory.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <ucontext.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <errno.h>
#ifndef NO_CPP_DEMANGLE
//#include <cxxabi.h>
char * __cxa_demangle(const char * __mangled_name, char * __output_buffer, size_t * __length, int * __status);
#endif

#include <stdbool.h>
#include "log.h"

#if defined(REG_RIP)
# define SIGSEGV_STACK_IA64
# define REGFORMAT "%016lx"
#elif defined(REG_EIP)
# define SIGSEGV_STACK_X86
# define REGFORMAT "%08x"
#else
# define SIGSEGV_STACK_GENERIC
# define REGFORMAT "%x"
#endif

static void signal_segv(int signum, siginfo_t* info, void*ptr) {
    static const char *si_codes[3] = {"", "SEGV_MAPERR", "SEGV_ACCERR"};

    size_t i;
    ucontext_t *ucontext = (ucontext_t*)ptr;

#if defined(SIGSEGV_STACK_X86) || defined(SIGSEGV_STACK_IA64)
    int f = 0;
    Dl_info dlinfo;
    void **bp = 0;
    void *ip = 0;
#else
    void *bt[20];
    char **strings;
    size_t sz;
#endif

    if (signum == SIGSEGV)
    {
        a2j_error("Segmentation Fault!");
    }
    else if (signum == SIGABRT)
    {
        a2j_error("Abort!");
    }
    else if (signum == SIGILL)
    {
        a2j_error("Illegal instruction!");
    }
    else if (signum == SIGFPE)
    {
        a2j_error("Floating point exception!");
    }
    else
    {
        a2j_error("Unknown bad signal caught!");
    }

    a2j_error("info.si_signo = %d", signum);
    a2j_error("info.si_errno = %d", info->si_errno);
    a2j_error("info.si_code  = %d (%s)", info->si_code, si_codes[info->si_code]);
    a2j_error("info.si_addr  = %p", info->si_addr);
#if !defined(__alpha__) && !defined(__ia64__) && \
    !defined(__FreeBSD_kernel__) && !defined(__arm__) && !defined(__hppa__) && \
    !defined(__sh__) && !defined(__aarch64__) && !defined(__riscv)
    for(i = 0; i < NGREG; i++)
        a2j_error("reg[%02d]       = 0x" REGFORMAT, i,
#if defined(__powerpc__) && !defined(__powerpc64__)
#if defined(__GLIBC__)
                ucontext->uc_mcontext.uc_regs->gregs[i]
#else
                ucontext->uc_mcontext.gregs[i]
#endif
#elif defined(__powerpc64__)
                ucontext->uc_mcontext.gp_regs[i]
#elif defined(__sparc__) && defined(__arch64__)
                ucontext->uc_mcontext.mc_gregs[i]
#else
                ucontext->uc_mcontext.gregs[i]
#endif
                );
#endif /* alpha, ia64, kFreeBSD, arm, hppa, aarch64, riscv */

#if defined(SIGSEGV_STACK_X86) || defined(SIGSEGV_STACK_IA64)
# if defined(SIGSEGV_STACK_IA64)
    ip = (void*)ucontext->uc_mcontext.gregs[REG_RIP];
    bp = (void**)ucontext->uc_mcontext.gregs[REG_RBP];
# elif defined(SIGSEGV_STACK_X86)
    ip = (void*)ucontext->uc_mcontext.gregs[REG_EIP];
    bp = (void**)ucontext->uc_mcontext.gregs[REG_EBP];
# endif

    a2j_error("Stack trace:");
    while(bp && ip) {
        if(!dladdr(ip, &dlinfo))
            break;

        const char *symname = dlinfo.dli_sname;
#ifndef NO_CPP_DEMANGLE
        int status;
        char *tmp = __cxa_demangle(symname, NULL, 0, &status);

        if(status == 0 && tmp)
            symname = tmp;
#endif

        a2j_error("% 2d: %p <%s+%u> (%s)",
                ++f,
                ip,
                symname,
                (unsigned)(ip - dlinfo.dli_saddr),
                dlinfo.dli_fname);

#ifndef NO_CPP_DEMANGLE
        if(tmp)
            free(tmp);
#endif

        if(dlinfo.dli_sname && !strcmp(dlinfo.dli_sname, "main"))
            break;

        ip = bp[1];
        bp = (void**)bp[0];
    }
#else
    a2j_error("Stack trace (non-dedicated):");
    sz = backtrace(bt, 20);
    strings = backtrace_symbols(bt, sz);

    for(i = 0; i < sz; ++i)
        a2j_error("%s", strings[i]);
#endif
    a2j_error("End of stack trace");
    exit (-1);
}

int setup_sigsegv() {
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_sigaction = signal_segv;
    action.sa_flags = SA_SIGINFO;
    if(sigaction(SIGSEGV, &action, NULL) < 0) {
        a2j_error("sigaction failed. errno is %d (%s)", errno, strerror(errno));
        return 0;
    }

    if(sigaction(SIGILL, &action, NULL) < 0) {
        a2j_error("sigaction failed. errno is %d (%s)", errno, strerror(errno));
        return 0;
    }

    if(sigaction(SIGABRT, &action, NULL) < 0) {
        a2j_error("sigaction failed. errno is %d (%s)", errno, strerror(errno));
        return 0;
    }

    if(sigaction(SIGFPE, &action, NULL) < 0) {
        a2j_error("sigaction failed. errno is %d (%s)", errno, strerror(errno));
        return 0;
    }

    return 1;
}

#ifndef SIGSEGV_NO_AUTO_INIT
static void __attribute((constructor)) init(void) {
    setup_sigsegv();
}
#endif
