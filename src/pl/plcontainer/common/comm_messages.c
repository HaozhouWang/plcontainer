/*
Copyright 1994 The PL-J Project. All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE PL-J PROJECT ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
THE PL-J PROJECT OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
   OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
   OF THE POSSIBILITY OF SUCH DAMAGE.

The views and conclusions contained in the software and documentation are those of the authors and should not be
interpreted as representing official policies, either expressed or implied, of the PL-J Project.
*/

/**
 * file:			commm_messages.c
 * description:		very minimal functionality from the original
 * 				    libpq implementation. Structures and
 * 				    functionalities are extremely simplified.
 * author:			PostgreSQL developement group.
 * author:			Laszlo Hornyak
 */

#include <stdlib.h>

#include "messages/message_callreq.h"
#include "messages/message_result.h"

void
free_callreq(callreq req) {
    int i;

    /* free the procedure */
    free(req->proc.name);
    free(req->proc.src);

    /* free the arguments */
    for (i = 0; i < req->nargs; i++) {
        free(req->args[i].name);
        free(req->args[i].value);
        free(req->args[i].type);
    }
    free(req->args);

    /* free the top-level request */
    free(req);
}

void
free_result(plcontainer_result res) {
    int i;

    /* free the types array */
    for (i = 0; i < res->cols; i++) {
        free(res->types[i]);
        free(res->names[i]);
    }
    free(res->types);
    free(res->names);

    /* free the data array */
    for (i = 0; i < res->rows; i++) {
        /* free the row */
        free(res->data[i]);
    }
    free(res->data);

    free(res);
}