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
default_log_handler(smf_log_level_t level, const char *msg, void *userdata)
{
  (void) userdata;
  if (level != SMF_LOG_LEVEL_DEBUG)
    fprintf(stderr, "%s\n", msg);
}

static void (*log_handler)(smf_log_level_t, const char *, void *) = default_log_handler;
static void  *log_handler_userdata = NULL;

#define MAX_WARNING_SIZE 1024

void
smf_log(smf_log_level_t level, const char *fmt, ...)
{
  char log_buf[MAX_WARNING_SIZE];
  va_list v;

  va_start(v, fmt);
  vsnprintf(log_buf, sizeof(log_buf), fmt, v);
  log_handler(level, log_buf, log_handler_userdata);
  va_end(v);
}

/**
 * Set the libsmf log handler.
 *
 * When libsmf encounters a problem, it issues log messages and calls the
 * log handler. The default handler outputs the messages to standard
 * error, except for debug messages, which are discarded. This function
 * allows you to supply your own log-message-handling function. NULL
 * restores the default handler.
 */
void
smf_set_log_handler(void (*handler)(smf_log_level_t level, const char *msg, void *userdata),
                    void  *userdata)
{
  log_handler = (handler != NULL) ? handler : default_log_handler;
  log_handler_userdata = userdata;
}
