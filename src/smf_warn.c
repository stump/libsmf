/* libsmf: Standard MIDI File library
 * Copyright (c) 2013 John Stumpo <stump@stump.io>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * ALTHOUGH THIS SOFTWARE IS MADE OF WIN AND SCIENCE, IT IS PROVIDED BY THE
 * AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * Warning-message-handling functions.
 *
 */

#include "smf.h"
#include <stdarg.h>

static void
default_warning_handler(const char *msg, void *userdata)
{
  (void) userdata;
  fprintf(stderr, "%s\n", msg);
}

static void (*warning_handler)(const char *, void *) = default_warning_handler;
static void  *warning_handler_userdata = NULL;

#define MAX_WARNING_SIZE 1024

void
smf_warn(const char *fmt, ...)
{
  char warn_buf[MAX_WARNING_SIZE];
  va_list v;

  va_start(v, fmt);
  vsnprintf(warn_buf, sizeof(warn_buf), fmt, v);
  warning_handler(warn_buf, warning_handler_userdata);
  va_end(v);
}

/**
 * Set the libsmf warning handler.
 *
 * When libsmf encounters a problem, it issues warning messages and
 * calls the warning handler. The default handler outputs the messages
 * to standard error. This function allows you to supply your own
 * warning-message-handling function. NULL restores the default handler.
 */
void
smf_set_warning_handler(void (*handler)(const char *msg, void *userdata),
                        void  *userdata)
{
  warning_handler = (handler != NULL) ? handler : default_warning_handler;
  warning_handler_userdata = userdata;
}
