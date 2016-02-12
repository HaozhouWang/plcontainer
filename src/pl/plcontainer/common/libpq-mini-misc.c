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
 * file:			libpq-mini-misc.c
 * description:		very minimal functionality from the original
 * 				    libpq implementation. Structures and
 * 				    functionalities are extremely simplified.
 * author:			PostgreSQL developement group.
 * author:			Laszlo Hornyak
 */

/*-------------------------------------------------------------------------
 *
 *	 FILE
 *		fe-misc.c
 *
 *	 DESCRIPTION
 *		 miscellaneous useful functions
 *
 * The communication routines here are analogous to the ones in
 * backend/libpq/pqcomm.c and backend/libpq/pqcomprim.c, but operate
 * in the considerably different environment of the frontend libpq.
 * In particular, we work with a bare nonblock-mode socket, rather than
 * a stdio stream, so that we can avoid unwanted blocking of the application.
 *
 * XXX: MOVE DEBUG PRINTOUT TO HIGHER LEVEL.  As is, block and restart
 * will cause repeat printouts.
 *
 * We must speak the same transmitted data representations as the backend
 * routines.
 *
 *
 * Portions Copyright (c) 1996-2003, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  $Header: /cvsroot/pgsql-server/src/interfaces/libpq/fe-misc.c,v 1.103
 *2003/10/19 21:36:41 tgl Exp $
 *
 *-------------------------------------------------------------------------
 */

/* #include "postgres_fe.h" */

#include <c.h>

#include "comm_logging.h"
#include "libpq-mini-misc.h"

#include <errno.h>
#include <signal.h>
#include <time.h>

#if !defined(_MSC_VER) && !defined(__BORLANDC__)
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include <sys/time.h>
#include <unistd.h>

/* #ifdef HAVE_POLL_H */

/* TODO: gebasz: perhaps this will run only on linux. (should fix the C build)
 */
#include <poll.h>

/* #endif */
#ifdef HAVE_SYS_POLL_H
#include <sys/poll.h>
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

/* #include "libpq-fe.h" */

/* #include "libpq-int.h" */

/* #include "pqsignal.h" */
#include "mb/pg_wchar.h"

#include "libpq-mini.h"
#include <stdio.h>

#include "libpq-mini-secure.h"

/*	*/

/*	this is a fragment from libpq-int.h */

/*	*/
#ifdef WIN32
#warning this software has never been tested on MS-OS, so IMHO it won`t work
#define SOCK_ERRNO (WSAGetLastError())
#define SOCK_STRERROR winsock_strerror
#define SOCK_ERRNO_SET(e) WSASetLastError(e)
#else
#define SOCK_ERRNO errno
#define SOCK_STRERROR pqStrerror
#define SOCK_ERRNO_SET(e) errno = e
#endif

static int pqmSendSome(PGconn_min *conn, int len);
static int pqmCheckInBufferSpace(int bytes_needed, PGconn_min *conn);
static int pqmSocketPoll(int sock, int forRead, int forWrite, time_t end_time);
static int pqmReadReady(PGconn_min *conn);
static int pqmWait(int forRead, int forWrite, PGconn_min *conn);
static int pqmWaitTimed(int forRead, int forWrite, PGconn_min *conn,
                       time_t finish_time);
static int pqmSocketCheck(PGconn_min *conn, int forRead, int forWrite,
                         time_t end_time);

int
pqmGetSome(PGconn_min *conn) {
    int ret;
    pqmCheckInBufferSpace(1024, conn);
    ret = read(conn->sock, conn->inBuffer + conn->inCursor, 1024);
    conn->inEnd += ret;
    //	lprintf(DEBUG1, "pqmGetSome: got %d bytes", ret);
    return ret;
}

/*
 * pqmGetc: get 1 character from the connection
 *
 *	All these routines return 0 on success, EOF on error.
 *	Note that for the Get routines, EOF only means there is not enough
 *	data in the buffer, not that there is necessarily a hard error.
 */
int
pqmGetc(char *result, PGconn_min *conn) {
    if (conn->inCursor >= conn->inEnd) {
        // return EOF;
        if (pqmGetSome(conn) == 0)
            return EOF;
    }

    *result = conn->inBuffer[conn->inCursor++];
    if (conn->Pfdebug) {
        fprintf(conn->Pfdebug, "From backend> %c\n", (unsigned char)*result);
        fflush(conn->Pfdebug);
    }
    return 0;
}

/*
 * pqmPutc: write 1 char to the current message
 */
int
pqmPutc(char c, PGconn_min *conn) {
    if (pqmPutMsgBytes(&c, 1, conn))
        return EOF;
    if (conn->Pfdebug)
        fprintf(conn->Pfdebug, "To backend> %c\n", c);
    return 0;
}

/*
 * pqmGets:
 * get a null-terminated string from the connection,
 * and store it in an expansible PQExpBuffer.
 * If we run out of memory, all of the string is still read,
 * but the excess characters are silently discarded.
 */
/* int */
/* pqmGets(PQExpBuffer buf, PGconn_min *conn) { */
/*     /\* */
/*      * Copy conn data to locals for faster search loop */
/*      *\/ */
/*     char *inBuffer = conn->inBuffer; */
/*     int inCursor = conn->inCursor; */
/*     int inEnd = conn->inEnd; */
/*     int slen; */

/*     while (inCursor < inEnd && inBuffer[inCursor]) inCursor++; */

/*     if (inCursor >= inEnd) { */
/*         return EOF; */
/*     } */

/*     slen = inCursor - conn->inCursor; */

/*     resetPQExpBuffer(buf); */
/*     appendBinaryPQExpBuffer(buf, inBuffer + conn->inCursor, slen); */

/*     conn->inCursor = ++inCursor; */

/*     if (conn->Pfdebug) fprintf(conn->Pfdebug, "From backend> \"%s\"\n",
 * buf->data); */

/*     return 0; */
/* } */

/*
 * pqmPuts: write a null-terminated string to the current message
 */
int
pqmPuts(const char *s, PGconn_min *conn) {
    if (pqmPutMsgBytes(s, strlen(s) + 1, conn))
        return EOF;

    if (conn->Pfdebug)
        fprintf(conn->Pfdebug, "To backend> '%s'\n", s);

    return 0;
}

/*
 * pqmPutnchar:
 *	write exactly len bytes to the current message
 */
int
pqmPutnchar(const char *s, size_t len, PGconn_min *conn) {
    if (pqmPutMsgBytes(s, len, conn))
        return EOF;

    if (conn->Pfdebug)
        fprintf(conn->Pfdebug, "To backend> %.*s\n", (int)len, s);

    return 0;
}

/*
* pqmGetnchar:
*	get a string of exactly len bytes in buffer s, no null termination
*/
int
pqmGetnchar(char *s, size_t len, PGconn_min *conn) {
    /* lprintf(DEBUG1, "pqmGetnchar: %ld", len); */
    if (len > (size_t)(conn->inEnd - conn->inCursor)) {
        int    ret;
        size_t r;
        // ret = pqmReadData(conn);
        ret = pqmCheckInBufferSpace(len, conn);
        // lprintf(DEBUG1, "pqmCheckInBufferSpace: %d", ret);

        r = conn->inEnd - conn->inCursor;
        while (r < len) {
            ret =  // pqmsecure_read(conn, conn -> inBuffer + conn -> inCursor,
                   // len);
                read(conn->sock, conn->inBuffer + conn->inCursor + r, len - r);
            r += ret;
        }

        if (ret == -1) {
            return EOF;
        }
    }

    /* lprintf(DEBUG1, "1 pqmGetnchar: %ld", len); */

    memcpy(s, conn->inBuffer + conn->inCursor, len);
    /*
     * no terminating null
     */

    /* lprintf(DEBUG1, "1/5 pqmGetnchar: %ld", len); */
    conn->inCursor += len;

    if (conn->inCursor > conn->inEnd) {
        conn->inEnd = conn->inCursor;
    }

    /* lprintf(DEBUG1, "2 pqmGetnchar: %ld", len); */
    if (conn->Pfdebug) {
        fprintf(conn->Pfdebug, "From backend (%lu)> %.*s\n", (unsigned long)len,
                (int)len, s);
    }
    return 0;
}

/*
 * pqmGetInt
 *	read a 2 or 4 byte integer and convert from network byte order
 *	to local byte order
 */
int
pqmGetInt(int *result, size_t bytes, PGconn_min *conn) {
    uint16 tmp2;
    uint32 tmp4;

    switch (bytes) {
    case 2:
        if (conn->inCursor + 2 > conn->inEnd)
            return EOF;
        memcpy(&tmp2, conn->inBuffer + conn->inCursor, 2);
        *result = (int)ntohs(tmp2);
        conn->inCursor += 2;
        break;
    case 4:
        memcpy(&tmp4, conn->inBuffer + conn->inCursor, 4);
        *result = (int)ntohl(tmp4);
        conn->inCursor += 4;
        break;
    default:
        lprintf(ERROR, "unsupported number of bytes: %ld", bytes);
    }

    if (conn->Pfdebug)
        fprintf(conn->Pfdebug, "From backend (#%lu)> %d\n",
                (unsigned long)bytes, *result);

    return 0;
}

void
pqmMessageRecvd(PGconn_min *conn) {
    memset(conn->inBuffer, 0, conn->inBufSize);
    conn->inBuffer  = realloc(conn->inBuffer, 8192);
    conn->inBufSize = 8192;
    conn->inCursor  = 0;
    conn->inEnd     = 0;
};

/*
 * pqmPutInt
 * write an integer of 2 or 4 bytes, converting from host byte order
 * to network byte order.
 */
int
pqmPutInt(int value, size_t bytes, PGconn_min *conn) {
    uint16 tmp2;
    uint32 tmp4;

    switch (bytes) {
    case 2:
        tmp2 = htons((uint16)value);
        if (pqmPutMsgBytes((const char *)&tmp2, 2, conn))
            return EOF;
        break;
    case 4:
        tmp4 = htonl((uint32)value);
        if (pqmPutMsgBytes((const char *)&tmp4, 4, conn))
            return EOF;
        break;
    default:
        /*
         * TODO: handle this correctly
         * Is it really the right solution? Sending error
         * from here? (this can't happen, anyway)
         */
        // pljlogging_error = 1;
        //			pljlprintf(ERROR, "Cant send integer of size
        //%d",
        // bytes);
        return EOF;
    }

    if (conn->Pfdebug)
        fprintf(conn->Pfdebug, "To backend (%lu#)> %d\n", (unsigned long)bytes,
                value);

    return 0;
}

/*
 * Make sure conn's output buffer can hold bytes_needed bytes (caller must
 * include already-stored data into the value!)
 *
 * Returns 0 on success, EOF if failed to enlarge buffer
 */
static int
pqmCheckOutBufferSpace(int bytes_needed, PGconn_min *conn) {
    int   newsize = conn->outBufSize;
    char *newbuf;

    if (bytes_needed <= newsize)
        return 0;

    /*
     * If we need to enlarge the buffer, we first try to double it in
     * size; if that doesn't work, enlarge in multiples of 8K.  This
     * avoids thrashing the malloc pool by repeated small enlargements.
     *
     * Note: tests for newsize > 0 are to catch integer overflow.
     */
    do
        newsize *= 2;
    while (bytes_needed > newsize && newsize > 0);

    if (bytes_needed <= newsize) {
        newbuf = realloc(conn->outBuffer, newsize);
        if (newbuf) {
            /*
             * realloc succeeded
             */
            conn->outBuffer  = newbuf;
            conn->outBufSize = newsize;
            return 0;
        }
    }

    newsize = conn->outBufSize;
    do
        newsize += 8192;
    while (bytes_needed > newsize && newsize > 0);

    if (bytes_needed <= newsize) {
        newbuf = realloc(conn->outBuffer, newsize);
        if (newbuf) {
            /*
             * realloc succeeded
             */
            conn->outBuffer  = newbuf;
            conn->outBufSize = newsize;
            return 0;
        }
    }

    return EOF;  // this won't get the control
}

/*
 * Make sure conn's input buffer can hold bytes_needed bytes (caller must
 * include already-stored data into the value!)
 *
 * Returns 0 on success, EOF if failed to enlarge buffer
 */
static int
pqmCheckInBufferSpace(int bytes_needed, PGconn_min *conn) {
    int   newsize = conn->inBufSize;
    char *newbuf;

    /*
     * lprintf(DEBUG1, "pqmCheckInBufferSpace: needed %d now %d", bytes_needed,
     * conn->inBufSize);
     */

    if (bytes_needed <= newsize)
        return 0;

    /*
     * If we need to enlarge the buffer, we first try to double it in
     * size; if that doesn't work, enlarge in multiples of 8K.  This
     * avoids thrashing the malloc pool by repeated small enlargements.
     *
     * Note: tests for newsize > 0 are to catch integer overflow.
     */
    do
        newsize *= 2;
    while (bytes_needed > newsize && newsize > 0);

    if (bytes_needed <= newsize) {
        newbuf = realloc(conn->inBuffer, newsize);
        if (newbuf) {
            /*
             * realloc succeeded
             */
            conn->inBuffer  = newbuf;
            conn->inBufSize = newsize;
            return 0;
        }
    }

    newsize = conn->inBufSize;
    do
        newsize += 8192;
    while (bytes_needed > newsize && newsize > 0);

    if (bytes_needed <= newsize) {
        newbuf = realloc(conn->inBuffer, newsize);
        if (newbuf) {
            /*
             * realloc succeeded
             */
            conn->inBuffer  = newbuf;
            conn->inBufSize = newsize;
            return 0;
        }
    }

    /*
     * realloc failed. Probably out of memory
     */
    /*
     * TODO: handle correctly
     */

    // pljlogging_error = 1;
    //	pljlprintf(FATAL, "Out of inbuffer.");

    return EOF;  // this won't get control
}

/*
 * pqmPutMsgStart: begin construction of a message to the server
 *
 * msg_type is the message type byte, or 0 for a message without type byte
 * (only startup messages have no type byte)
 *
 * force_len forces the message to have a length word; otherwise, we add
 * a length word if protocol 3.
 *
 * Returns 0 on success, EOF on error
 *
 * The idea here is that we construct the message in conn->outBuffer,
 * beginning just past any data already in outBuffer (ie, at
 * outBuffer+outCount).  We enlarge the buffer as needed to hold the message.
 * When the message is complete, we fill in the length word (if needed) and
 * then advance outCount past the message, making it eligible to send.
 *
 * The state variable conn->outMsgStart points to the incomplete message's
 * length word: it is either outCount or outCount+1 depending on whether
 * there is a type byte.  If we are sending a message without length word
 * (pre protocol 3.0 only), then outMsgStart is -1.  The state variable
 * conn->outMsgEnd is the end of the data collected so far.
 */
int
pqmPutMsgStart(char msg_type, PGconn_min *conn) {
    int endPos;

    /*
     * allow room for message type byte
     */
    if (msg_type)
        endPos = conn->outCount + 1;
    else
        endPos = conn->outCount;

    /*
     * make sure there is room for message header
     */
    if (pqmCheckOutBufferSpace(endPos, conn))
        return EOF;
    /*
     * okay, save the message type byte if any
     */
    if (msg_type)
        conn->outBuffer[conn->outCount] = msg_type;
    /*
     * set up the message pointers
     */
    conn->outMsgStart = 0;
    conn->outMsgEnd   = endPos;
    /*
     * length word, if needed, will be filled in by pqmPutMsgEnd
     */

    if (conn->Pfdebug)
        fprintf(conn->Pfdebug, "To backend> Msg %c\n",
                msg_type ? msg_type : ' ');

    return 0;
}

/*
 * pqmPutMsgBytes: add bytes to a partially-constructed message
 *
 * Returns 0 on success, EOF on error
 */
int
pqmPutMsgBytes(const void *buf, size_t len, PGconn_min *conn) {
    /*
     * make sure there is room for it
     */
    if (pqmCheckOutBufferSpace(conn->outMsgEnd + len, conn))
        return EOF;
    /*
     * okay, save the data
     */
    memcpy(conn->outBuffer + conn->outMsgEnd, buf, len);
    conn->outMsgEnd += len;
    /*
     * no Pfdebug call here, caller should do it
     */
    if (conn->Pfdebug)
        fprintf(conn->Pfdebug, "To backend> %ld bytes\n", len);

    return 0;
}

/*
 * pqmPutMsgEnd: finish constructing a message and possibly send it
 *
 * Returns 0 on success, EOF on error
 *
 * We don't actually send anything here unless we've accumulated at least
 * 8K worth of data (the typical size of a pipe buffer on Unix systems).
 * This avoids sending small partial packets.  The caller must use pqmFlush
 * when it's important to flush all the data out to the server.
 */
int
pqmPutMsgEnd(PGconn_min *conn) {
    if (conn->Pfdebug)
        fprintf(conn->Pfdebug, "To backend> Msg complete, length %u\n",
                conn->outMsgEnd - conn->outCount);

    /*
     * Make message eligible to send
     */
    conn->outCount = conn->outMsgEnd;

    if (conn->outCount >= 8192) {
        int toSend = conn->outCount - (conn->outCount % 8192);

        if (pqmSendSome(conn, toSend) < 0)
            return EOF;
        /*
         * in nonblock mode, don't complain if unable to send it all
         */
    }

    return 0;
}

int
pqmReadData(PGconn_min *conn) {
    int nread;
    int someread;
    conn->inStart = conn->inCursor = conn->inEnd = 0;

    /*
     * If the buffer is fairly full, enlarge it. We need to be able to
     * enlarge the buffer in case a single message exceeds the initial
     * buffer size.  We enlarge before filling the buffer entirely so as
     * to avoid asking the kernel for a partial packet. The magic constant
     * here should be large enough for a TCP packet or Unix pipe
     * bufferload.	8K is the usual pipe buffer size, so...
     */
    if (conn->inBufSize - conn->inEnd < 8192) {
        if (pqmCheckInBufferSpace(conn->inEnd + 8192, conn)) {
            /*
             * We don't insist that the enlarge worked, but we need some
             * room
             */
            if (conn->inBufSize - conn->inEnd < 100)
                return -1; /* errorMessage already set */
        }
    }

/*
 * OK, try to read some data
 */
retry3:
    /*
     * lprintf(DEBUG1, "pqmReadData: now calling pqmsecure_read");
     */
    nread = pqmsecure_read(conn, conn->inBuffer + conn->inEnd,
                          conn->inBufSize - conn->inEnd);
    /*
     * lprintf(DEBUG1, "pqmReadData: pqmsecure_read returned: %d", nread);
     */
    if (nread < 0) {
        if (SOCK_ERRNO == EINTR)
            goto retry3;
/*
 * Some systems return EAGAIN/EWOULDBLOCK for no data
 */
#ifdef EAGAIN
        if (SOCK_ERRNO == EAGAIN)
            return someread;
#endif
#if defined(EWOULDBLOCK) && (!defined(EAGAIN) || (EWOULDBLOCK != EAGAIN))
        if (SOCK_ERRNO == EWOULDBLOCK)
            return someread;
#endif
/*
 * We might get ECONNRESET here if using TCP and backend died
 */
#ifdef ECONNRESET
        if (SOCK_ERRNO == ECONNRESET)
            goto definitelyFailed;
#endif

        return -1;
    }
    if (nread > 0) {
        conn->inEnd += nread;

        /*
         * Hack to deal with the fact that some kernels will only give us
         * back 1 packet per recv() call, even if we asked for more and
         * there is more available.  If it looks like we are reading a
         * long message, loop back to recv() again immediately, until we
         * run out of data or buffer space.  Without this, the
         * block-and-restart behavior of libpq's higher levels leads to
         * O(N^2) performance on long messages.
         *
         * Since we left-justified the data above, conn->inEnd gives the
         * amount of data already read in the current message.	We
         * consider the message "long" once we have acquired 32k ...
         */
        if (conn->inEnd > 32768 && (conn->inBufSize - conn->inEnd) >= 8192) {
            someread = 1;
            goto retry3;
        }
        return 1;
    }

    if (someread)
        return 1; /* got a zero read after successful tries */

    /*
     * A return value of 0 could mean just that no data is now available,
     * or it could mean EOF --- that is, the server has closed the
     * connection. Since we have the socket in nonblock mode, the only way
     * to tell the difference is to see if select() is saying that the
     * file is ready. Grumble.	Fortunately, we don't expect this path to
     * be taken much, since in normal practice we should not be trying to
     * read data unless the file selected for reading already.
     *
     * In SSL mode it's even worse: SSL_read() could say WANT_READ and then
     * data could arrive before we make the pqmReadReady() test.  So we
     * must play dumb and assume there is more data, relying on the SSL
     * layer to detect true EOF.
     */

    switch (pqmReadReady(conn)) {
    case 0:
        /*
         * definitely no data available
         */
        return 0;
    case 1:
        /*
         * ready for read
         */
        break;
    default:
        goto definitelyFailed;
    }

/*
 * Still not sure that it's EOF, because some data could have just
 * arrived.
 */
retry4:
    /*
     * lprintf(DEBUG1, "pqmsecure_read in retry4");
     */
    nread = pqmsecure_read(conn, conn->inBuffer + conn->inEnd,
                          conn->inBufSize - conn->inEnd);
    if (nread < 0) {
        if (SOCK_ERRNO == EINTR)
            goto retry4;
/*
 * Some systems return EAGAIN/EWOULDBLOCK for no data
 */
#ifdef EAGAIN
        if (SOCK_ERRNO == EAGAIN)
            return 0;
#endif
#if defined(EWOULDBLOCK) && (!defined(EAGAIN) || (EWOULDBLOCK != EAGAIN))
        if (SOCK_ERRNO == EWOULDBLOCK)
            return 0;
#endif
/*
 * We might get ECONNRESET here if using TCP and backend died
 */
#ifdef ECONNRESET
        if (SOCK_ERRNO == ECONNRESET)
            goto definitelyFailed;
#endif

        /*
         * TODO: handle correctly
         */

        /*		printfPQExpBuffer(&conn->errorMessage, */

        /*			   libpq_gettext("could not receive data from
         * server: %s\n"), */

        /*						SOCK_STRERROR(SOCK_ERRNO,
         * sebuf,
         * sizeof(sebuf))); */
        return -1;
    }
    if (nread > 0) {
        conn->inEnd += nread;
        return 1;
    }

/*
 * OK, we are getting a zero read even though select() says ready.
 * This means the connection has been closed.  Cope.
 */
definitelyFailed:
    /*
     * TODO handle correctly!
     */

    /*	printfPQExpBuffer(&conn->errorMessage, */

    /*					  libpq_gettext( */

    /*							"server closed the
     * connection
     * unexpectedly\n"
     */

    /*			   "\tThis probably means the server terminated
     * abnormally\n"
     */

    /*						 "\tbefore or while processing
     * the
     * request.\n"));
     */
    closesocket(conn->sock);
    conn->sock = -1;

    return -1;
}

/*
 * pqmSendSome: send data waiting in the output buffer.
 *
 * len is how much to try to send (typically equal to outCount, but may
 * be less).
 *
 * Return 0 on success, -1 on failure and 1 when not all data could be sent
 * because the socket would block and the connection is non-blocking.
 */
static int
pqmSendSome(PGconn_min *conn, int len) {
    char *ptr       = conn->outBuffer;
    int   remaining = conn->outCount;
    int   result    = 0;

    if (conn->sock < 0) {
        /*
         * lprintf(DEBUG1, "pqSendSome: socket is not open");
         */
        /*
         * TODO: handle correctly
         */

        /*		printfPQExpBuffer(&conn->errorMessage, */

        /*						  libpq_gettext("connection
         * not
         * open\n")); */
        return -1;
    }

    /*
     * while there's still data to send
     */
    while (len > 0) {
        int sent;

        sent = pqmsecure_write(conn, ptr, len);

        if (sent < 0) {
            /*
             * Anything except EAGAIN/EWOULDBLOCK/EINTR is trouble. If
             * it's EPIPE or ECONNRESET, assume we've lost the backend
             * connection permanently.
             */
            switch (SOCK_ERRNO) {
#ifdef EAGAIN
            case EAGAIN:
                break;
#endif
#if defined(EWOULDBLOCK) && (!defined(EAGAIN) || (EWOULDBLOCK != EAGAIN))
            case EWOULDBLOCK:
                break;
#endif
            case EINTR:
                continue;

            case EPIPE:
#ifdef ECONNRESET
            case ECONNRESET:
#endif
                /*
                 * TODO: handle error message correctly
                 */

                /*					printfPQExpBuffer(&conn->errorMessage,
                 */

                /*									  libpq_gettext(
                 */

                /*							"server
                 * closed
                 * the
                 * connection
                 * unexpectedly\n"
                 */

                /*													"\tThis
                 * probably means the server terminated abnormally\n" */

                /*						 "\tbefore or
                 * while
                 * processing the
                 * request.\n"));
                 */

                /*
                 * We used to close the socket here, but that's a bad
                 * idea since there might be unread data waiting
                 * (typically, a NOTICE message from the backend
                 * telling us it's committing hara-kiri...).  Leave
                 * the socket open until pqReadData finds no more data
                 * can be read.  But abandon attempt to send data.
                 */
                conn->outCount = 0;
                return -1;

            default:
                /*
                 * TODO: handle error message
                 */

                /*					printfPQExpBuffer(&conn->errorMessage,
                 */

                /* /					libpq_gettext("could not
                 * send
                 * data to server: %s\n"),
                 */

                /*						SOCK_STRERROR(SOCK_ERRNO,
                 * sebuf,
                 * sizeof(sebuf)));
                 */
                /*
                 * We don't assume it's a fatal error...
                 */
                conn->outCount = 0;
                return -1;
            }
        } else {
            ptr += sent;
            len -= sent;
            remaining -= sent;
        }

        if (len > 0) {
            /*
             * We didn't send it all, wait till we can send more.
             *
             * If the connection is in non-blocking mode we don't wait, but
             * return 1 to indicate that data is still pending.
             */
            if (pqmIsnonblocking(conn)) {
                result = 1;
                break;
            }

            /*
             * There are scenarios in which we can't send data because the
             * communications channel is full, but we cannot expect the server
             * to clear the channel eventually because it's blocked trying to
             * send data to us.  (This can happen when we are sending a large
             * amount of COPY data, and the server has generated lots of
             * NOTICE responses.)  To avoid a deadlock situation, we must be
             * prepared to accept and buffer incoming data before we try
             * again.  Furthermore, it is possible that such incoming data
             * might not arrive until after we've gone to sleep.  Therefore,
             * we wait for either read ready or write ready.
             */
            if (pqmReadData(conn) < 0) {
                result = -1; /* error message already set up */
                break;
            }
            if (pqmWait(TRUE, TRUE, conn)) {
                result = -1;
                break;
            }
        }
    }

    /*
     * shift the remaining contents of the buffer
     */
    if (remaining > 0)
        memmove(conn->outBuffer, ptr, remaining);
    conn->outCount = remaining;

    return result;
}

/*
 * pqmFlush: send any data waiting in the output buffer
 *
 * Return 0 on success, -1 on failure and 1 when not all data could be sent
 * because the socket would block and the connection is non-blocking.
 */
int
pqmFlush(PGconn_min *conn) {
    if (conn->Pfdebug)
        fflush(conn->Pfdebug);

    if (conn->outCount > 0)
        return pqmSendSome(conn, conn->outCount);
    else {
        /*
         * lprintf(DEBUG1,"pqmFlush: not sent any ");
         */
    }
    return 0;
}

/*
 * pqmWait: wait until we can read or write the connection socket
 *
 * JAB: If SSL enabled and used and forRead, buffered bytes short-circuit the
 * call to select().
 *
 * We also stop waiting and return if the kernel flags an exception condition
 * on the socket.  The actual error condition will be detected and reported
 * when the caller tries to read or write the socket.
 */
int
pqmWait(int forRead, int forWrite, PGconn_min *conn) {
    return pqmWaitTimed(forRead, forWrite, conn, (time_t)-1);
}

/*
 * pqmWaitTimed: wait, but not past finish_time.
 *
 * If finish_time is exceeded then we return failure (EOF).  This is like
 * the response for a kernel exception because we don't want the caller
 * to try to read/write in that case.
 *
 * finish_time = ((time_t) -1) disables the wait limit.
 */
int
pqmWaitTimed(int forRead, int forWrite, PGconn_min *conn, time_t finish_time) {
    int result;

    result = pqmSocketCheck(conn, forRead, forWrite, finish_time);

    if (result < 0)
        return EOF; /* errorMessage is already set */

    if (result == 0) {
        /*
         * TODO: handle error message correctly
         */

        /*		printfPQExpBuffer(&conn->errorMessage, */

        /*						  libpq_gettext("timeout
         * expired\n")); */
        return EOF;
    }

    return 0;
}

/*
 * pqmReadReady: is select() saying the file is ready to read?
 * Returns -1 on failure, 0 if not ready, 1 if ready.
 */
int
pqmReadReady(PGconn_min *conn) {
    return pqmSocketCheck(conn, 1, 0, (time_t)0);
}

/*
 * Checks a socket, using poll or select, for data to be read, written,
 * or both.  Returns >0 if one or more conditions are met, 0 if it timed
 * out, -1 if an error occurred.
 *
 * If SSL is in use, the SSL buffer is checked prior to checking the socket
 * for read data directly.
 */
static int
pqmSocketCheck(PGconn_min *conn, int forRead, int forWrite, time_t end_time) {
    int result;

    if (!conn)
        return -1;
    if (conn->sock < 0) {
        /*
         * TODO: handle correctly
         */

        /*		printfPQExpBuffer(&conn->errorMessage, */

        /*						  libpq_gettext("socket
         * not
         * open\n")); */
        return -1;
    }

    /*
     * We will retry as long as we get EINTR
     */
    do
        result = pqmSocketPoll(conn->sock, forRead, forWrite, end_time);
    while (result < 0 && SOCK_ERRNO == EINTR);

    if (result < 0) {
        /*
         * TODO: handle correctly
         */

        /*		printfPQExpBuffer(&conn->errorMessage, */

        /*						  libpq_gettext("select()
         * failed:
         * %s\n"), */

        /*						SOCK_STRERROR(SOCK_ERRNO,
         * sebuf,
         * sizeof(sebuf))); */
    }

    return result;
}

/*
 * Check a file descriptor for read and/or write data, possibly waiting.
 * If neither forRead nor forWrite are set, immediately return a timeout
 * condition (without waiting).  Return >0 if condition is met, 0
 * if a timeout occurred, -1 if an error or interrupt occurred.
 *
 * Timeout is infinite if end_time is -1.  Timeout is immediate (no blocking)
 * if end_time is 0 (or indeed, any time before now).
 */
static int
pqmSocketPoll(int sock, int forRead, int forWrite, time_t end_time) {
/*
 * We use poll(2) if available, otherwise select(2)
 */
#ifdef HAVE_POLL
    struct pollfd input_fd;
    int           timeout_ms;

    if (!forRead && !forWrite)
        return 0;

    input_fd.fd      = sock;
    input_fd.events  = POLLERR;
    input_fd.revents = 0;

    if (forRead)
        input_fd.events |= POLLIN;
    if (forWrite)
        input_fd.events |= POLLOUT;

    /*
     * Compute appropriate timeout interval
     */
    if (end_time == ((time_t)-1))
        timeout_ms = -1;
    else {
        time_t now = time(NULL);

        if (end_time > now)
            timeout_ms = (end_time - now) * 1000;
        else
            timeout_ms = 0;
    }

    return poll(&input_fd, 1, timeout_ms);

#else  /* !HAVE_POLL */

    fd_set          input_mask;
    fd_set          output_mask;
    fd_set          except_mask;
    struct timeval  timeout;
    struct timeval *ptr_timeout;

    if (!forRead && !forWrite)
        return 0;

    FD_ZERO(&input_mask);
    FD_ZERO(&output_mask);
    FD_ZERO(&except_mask);
    if (forRead)
        FD_SET(sock, &input_mask);
    if (forWrite)
        FD_SET(sock, &output_mask);
    FD_SET(sock, &except_mask);

    /*
     * Compute appropriate timeout interval
     */
    if (end_time == ((time_t)-1))
        ptr_timeout = NULL;
    else {
        time_t now = time(NULL);

        if (end_time > now)
            timeout.tv_sec = end_time - now;
        else
            timeout.tv_sec = 0;
        timeout.tv_usec    = 0;
        ptr_timeout        = &timeout;
    }

    return select(sock + 1, &input_mask, &output_mask, &except_mask,
                  ptr_timeout);
#endif /* HAVE_POLL */
}
