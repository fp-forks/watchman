/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <fmt/core.h>
#include "watchman/Logging.h"
#include "watchman/Shutdown.h"

#ifdef HAVE_SYS_SIGLIST
#define w_strsignal(val) sys_siglist[(val)]
#else
#define w_strsignal(val) strsignal((val))
#endif

using namespace watchman;

#ifndef _WIN32
static void crash_handler(int signo, siginfo_t* si, void*) {
  const char* reason = "";
  if (si) {
    switch (si->si_signo) {
      case SIGILL:
        switch (si->si_code) {
          case ILL_ILLOPC:
            reason = "illegal opcode";
            break;
          case ILL_ILLOPN:
            reason = "illegal operand";
            break;
          case ILL_ILLADR:
            reason = "illegal addressing mode";
            break;
          case ILL_ILLTRP:
            reason = "illegal trap";
            break;
          case ILL_PRVOPC:
            reason = "privileged opcode";
            break;
          case ILL_PRVREG:
            reason = "privileged register";
            break;
          case ILL_COPROC:
            reason = "co-processor error";
            break;
          case ILL_BADSTK:
            reason = "internal stack error";
            break;
        }
        break;
      case SIGFPE:
        switch (si->si_code) {
          case FPE_INTDIV:
            reason = "integer divide by zero";
            break;
          case FPE_INTOVF:
            reason = "integer overflow";
            break;
          case FPE_FLTDIV:
            reason = "floating point divide by zero";
            break;
          case FPE_FLTOVF:
            reason = "floating point overflow";
            break;
          case FPE_FLTUND:
            reason = "floating point underflow";
            break;
          case FPE_FLTRES:
            reason = "floating point inexact result";
            break;
          case FPE_FLTINV:
            reason = "invalid floating point operation";
            break;
          case FPE_FLTSUB:
            reason = "subscript out of range";
            break;
        }
        break;
      case SIGSEGV:
        switch (si->si_code) {
          case SEGV_MAPERR:
            reason = "address not mapped to object";
            break;
          case SEGV_ACCERR:
            reason = "invalid permissions for mapped object";
            break;
        }
        break;
#ifdef SIGBUS
      case SIGBUS:
        switch (si->si_code) {
          case BUS_ADRALN:
            reason = "invalid address alignment";
            break;
          case BUS_ADRERR:
            reason = "non-existent physical address";
            break;
        }
        break;
#endif
    }
  }

  if (si) {
    logf_stderr(
        "Terminating due to signal {} {} generated by pid={} uid={} {} ({})\n",
        signo,
        w_strsignal(signo),
        si->si_pid,
        si->si_uid,
        reason,
        uintptr_t(si->si_value.sival_ptr));
  } else {
    logf_stderr(
        "Terminating due to signal {} {} {}\n",
        signo,
        w_strsignal(signo),
        reason);
  }

#if defined(HAVE_BACKTRACE) && defined(HAVE_BACKTRACE_SYMBOLS_FD)
  {
    void* array[24];
    size_t size = backtrace(array, sizeof(array) / sizeof(array[0]));
    backtrace_symbols_fd(array, size, STDERR_FILENO);
  }
#endif
  if (signo == SIGTERM) {
    w_request_shutdown();
    return;
  }
  // Resend the signal to terminate ourselves with it.
  // We register crash_handler with SA_RESETHAND so it will
  // not be invoked a second time.
  kill(getpid(), signo);
}
#endif

namespace {
[[noreturn]] void terminationHandler() {
  auto eptr = std::current_exception();
  if (eptr) {
    try {
      std::rethrow_exception(eptr);
    } catch (const std::exception& exc) {
      logf_stderr("std::terminate was called. Exception: {}\n", exc.what());
    } catch (...) {
      logf_stderr(
          "std::terminate was called. Exception is not a std::exception.\n");
    }
  } else {
    logf_stderr("std::terminate was called. There is no current exception.\n");
  }
  std::abort();
}
} // namespace

namespace watchman {

void setup_signal_handlers() {
#ifndef _WIN32
  struct sigaction sa;

  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = crash_handler;
  sa.sa_flags = SA_SIGINFO | SA_RESETHAND;

  sigaction(SIGSEGV, &sa, nullptr);
#ifdef SIGBUS
  sigaction(SIGBUS, &sa, nullptr);
#endif
  sigaction(SIGFPE, &sa, nullptr);
  sigaction(SIGILL, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
#else
  // Don't show error dialogs for background service failures
  SetErrorMode(SEM_FAILCRITICALERRORS);
  // also tell the C runtime that we should just abort when
  // we abort; don't do the crash reporting dialog.
  _set_abort_behavior(_WRITE_ABORT_MSG, ~0);
  // Force error output to stderr, don't use a msgbox.
  _set_error_mode(_OUT_TO_STDERR);
  // bridge OS exceptions into our FATAL logger so that we can
  // capture a stack trace.
  SetUnhandledExceptionFilter(exception_filter);
#endif

  std::set_terminate(terminationHandler);
}

} // namespace watchman

/* vim:ts=2:sw=2:et:
 */
