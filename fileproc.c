/*	$Id$ */
/*
 * Copyright (c) 2016 Kristaps Dzonsons <kristaps@bsd.lv>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <sys/stat.h>

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __APPLE__
# include <sandbox.h>
#endif

#include "extern.h"

#define	CERT_PEM "cert.pem"
#define	CERT_BAK "cert.pem~"
#define	CHAIN_PEM "chain.pem"
#define	CHAIN_BAK "chain.pem~"
#define	FCHAIN_PEM "fullchain.pem"
#define	FCHAIN_BAK "fullchain.pem~"

static int
serialise(const char *tmp, const char *real, 
	const char *v, size_t vsz,
	const char *v2, size_t v2sz)
{
	FILE	*f;

	/* Write into backup location, overwriting. */

	if (NULL == (f = fopen(tmp, "w"))) {
		dowarn(tmp);
		return(0);
	} else if (vsz != fwrite(v, 1, vsz, f)) {
		dowarnx(tmp);
		fclose(f);
		return(0);
	} else if (NULL != v2 && v2sz != fwrite(v2, 1, v2sz, f)) {
		dowarnx(tmp);
		fclose(f);
		return(0);
	} else if (-1 == fclose(f)) {
		dowarn(tmp);
		fclose(f);
		return(0);
	}

	/* Atomically (?) rename to real file and chmod. */

	if (-1 == rename(tmp, real)) {
		dowarn(real);
		return(0);
	} else if (-1 == chmod(real, 0444)) {
		dowarn(real);
		return(0);
	}

	return(1);
}

int
fileproc(int certsock, const char *certdir)
{
	char		*csr, *ch;
	size_t		 chsz, csz;
	int		 rc;
	FILE		*f;

	csr = ch = NULL;
	rc = 0;
	f = NULL;

	/* File-system and sandbox jailing. */

#ifdef __APPLE__
	if (-1 == sandbox_init(kSBXProfileNoNetwork, 
 	    SANDBOX_NAMED, NULL)) {
		dowarn("sandbox_init");
		goto error;
	}
#endif
	if ( ! dropfs(certdir)) {
		dowarnx("dropfs");
		goto error;
	} 
#if defined(__OpenBSD__) && OpenBSD >= 201605
	if (-1 == pledge("stdio cpath wpath", NULL)) {
		dowarn("pledge");
		goto error;
	}
#endif
	/*
	 * Start by downloading the chain PEM as a buffer.
	 * This is not nil-terminated, but we're just going to guess
	 * that it's well-formed and not actually touch the data.
	 * Once downloaded, dump it into CHAIN_BAK.
	 */

	if (NULL == (ch = readbuf(certsock, COMM_CHAIN, &chsz)))
		goto error;
	if ( ! serialise(CHAIN_BAK, CHAIN_PEM, ch, chsz, NULL, 0))
		goto error;

	dodbg("%s: created", CHAIN_PEM);

	/*
	 * Next, wait until we receive the DER encoded (signed)
	 * certificate from the network process.
	 * This comes as a stream of bytes: we don't know how many, so
	 * just keep downloading.
	 */

	if (NULL == (csr = readbuf(certsock, COMM_CSR, &csz)))
		goto error;
	if ( ! serialise(CERT_BAK, CERT_PEM, csr, csz, NULL, 0))
		goto error;

	dodbg("%s: created", CERT_PEM);

	/*
	 * Finally, create the full-chain file.
	 * This is just the concatenation of the certificate and chain.
	 */

	if ( ! serialise(FCHAIN_BAK, FCHAIN_PEM, csr, csz, ch, chsz))
		goto error;

	dodbg("%s: created", FCHAIN_PEM);

	rc = 1;
error:
	if (NULL != f)
		fclose(f);
	free(csr);
	free(ch);
	close(certsock);
	return(rc);
}
