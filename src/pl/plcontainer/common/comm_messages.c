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
 * file:            commm_messages.c
 * author:            PostgreSQL developement group.
 * author:            Laszlo Hornyak
 */

#include <stdlib.h>

#include "comm_utils.h"
#include "messages/messages.h"

void free_callreq(callreq req) {
    int i;

    /* free the procedure */
    pfree(req->proc.name);
    pfree(req->proc.src);

    /* free the arguments */
    for (i = 0; i < req->nargs; i++) {
        pfree(req->args[i].name);
        pfree(req->args[i].data.value);
    }
    pfree(req->args);

    /* free the top-level request */
    pfree(req);
}

void free_result(plcontainer_result res) {
    int i,j;

    /* free the types and names arrays */
    pfree(res->types);
    for (i = 0; i < res->cols; i++)
        pfree(res->names[i]);
    pfree(res->names);

    /* free the data array */
    for (i = 0; i < res->rows; i++) {
        for (j = 0; j < res->cols; j++) {
            if (res->data[i][j].value) {
                /* free the data if it is not null */
                pfree(res->data[i][j].value);
            }
        }
        /* free the row */
        pfree(res->data[i]);
    }
    pfree(res->data);

    pfree(res);
}

plcontainer_array plc_alloc_array(int ndims) {
    plcontainer_array arr;
    arr = (plcontainer_array)pmalloc(sizeof(str_plcontainer_array));
    arr->meta = (plcontainer_array_meta)pmalloc(sizeof(str_plcontainer_array_meta));
    arr->meta->ndims = ndims;
    arr->meta->dims = (int*)pmalloc(ndims * sizeof(int));
    arr->meta->size = 0;
    return arr;
}

void plc_free_array(plcontainer_array arr) {
    int i;
    if (arr->meta->type == PLC_DATA_TEXT) {
        for (i = 0; i < arr->meta->size; i++)
            if ( ((char**)arr->data)[i] != NULL )
                pfree(((char**)arr->data)[i]);
    }
    pfree(arr->data);
    pfree(arr->nulls);
    pfree(arr->meta->dims);
    pfree(arr->meta);
    pfree(arr);
}
