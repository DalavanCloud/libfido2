/*
 * Copyright (c) 2018 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <openssl/ec.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../openbsd-compat/openbsd-compat.h"

#include "fido.h"
#include "fido/es256.h"
#include "fido/rs256.h"
#include "extern.h"

static const unsigned char cdh[32] = {
	0xec, 0x8d, 0x8f, 0x78, 0x42, 0x4a, 0x2b, 0xb7,
	0x82, 0x34, 0xaa, 0xca, 0x07, 0xa1, 0xf6, 0x56,
	0x42, 0x1c, 0xb6, 0xf6, 0xb3, 0x00, 0x86, 0x52,
	0x35, 0x2d, 0xa2, 0x62, 0x4a, 0xbe, 0x89, 0x76,
};

static void
usage(void)
{
	fprintf(stderr, "usage: assert [-t ecdsa|rsa] [-a cred_id] [-P pin] "
	    "[-puv] <pubkey> <device>\n");
	exit(EXIT_FAILURE);
}

static void
verify_assert(int type, const unsigned char *authdata_ptr, size_t authdata_len,
    const unsigned char *sig_ptr, size_t sig_len, bool up, bool uv,
    const char *key)
{
	fido_assert_t	*assert = NULL;
	EC_KEY		*ec = NULL;
	RSA		*rsa = NULL;
	es256_pk_t	*es256_pk = NULL;
	rs256_pk_t	*rs256_pk = NULL;
	void		*pk;
	int		 r;

	/* credential pubkey */
	if (type == COSE_ES256) {
		if ((ec = read_ec_pubkey(key)) == NULL)
			errx(1, "read_ec_pubkey");

		if ((es256_pk = es256_pk_new()) == NULL)
			errx(1, "es256_pk_new");

		if (es256_pk_from_EC_KEY(es256_pk, ec) != FIDO_OK)
			errx(1, "es256_pk_from_EC_KEY");

		pk = es256_pk;
		EC_KEY_free(ec);
		ec = NULL;
	} else {
		if ((rsa = read_rsa_pubkey(key)) == NULL)
			errx(1, "read_rsa_pubkey");

		if ((rs256_pk = rs256_pk_new()) == NULL)
			errx(1, "rs256_pk_new");

		if (rs256_pk_from_RSA(rs256_pk, rsa) != FIDO_OK)
			errx(1, "rs256_pk_from_RSA");

		pk = rs256_pk;
		RSA_free(rsa);
		rsa = NULL;
	}

	if ((assert = fido_assert_new()) == NULL)
		errx(1, "fido_assert_new");

	/* client data hash */
	r = fido_assert_set_clientdata_hash(assert, cdh, sizeof(cdh));
	if (r != FIDO_OK)
		errx(1, "fido_assert_set_clientdata_hash: %s (0x%x)",
		    fido_strerr(r), r);

	/* relying party */
	r = fido_assert_set_rp(assert, "localhost");
	if (r != FIDO_OK)
		errx(1, "fido_assert_set_rp: %s (0x%x)", fido_strerr(r), r);

	/* authdata */
	r = fido_assert_set_count(assert, 1);
	if (r != FIDO_OK)
		errx(1, "fido_assert_set_count: %s (0x%x)", fido_strerr(r), r);
	r = fido_assert_set_authdata(assert, 0, authdata_ptr, authdata_len);
	if (r != FIDO_OK)
		errx(1, "fido_assert_set_authdata: %s (0x%x)", fido_strerr(r), r);

	/* options */
	r = fido_assert_set_options(assert, up, uv);
	if (r != FIDO_OK)
		errx(1, "fido_assert_set_options: %s (0x%x)", fido_strerr(r), r);

	/* sig */
	r = fido_assert_set_sig(assert, 0, sig_ptr, sig_len);
	if (r != FIDO_OK)
		errx(1, "fido_assert_set_sig: %s (0x%x)", fido_strerr(r), r);

	r = fido_assert_verify(assert, 0, type, pk);
	if (r != FIDO_OK)
		errx(1, "fido_assert_verify: %s (0x%x)", fido_strerr(r), r);

	es256_pk_free(&es256_pk);
	rs256_pk_free(&rs256_pk);

	fido_assert_free(&assert);
}

int
main(int argc, char **argv)
{
	bool		 up = false;
	bool		 uv = false;
	bool		 u2f = false;
	fido_dev_t	*dev = NULL;
	fido_assert_t	*assert = NULL;
	const char	*pin = NULL;
	unsigned char	*body = NULL;
	size_t		 len;
	int		 type = COSE_ES256;
	int		 ch;
	int		 r;

	if ((assert = fido_assert_new()) == NULL)
		errx(1, "fido_assert_new");

	while ((ch = getopt(argc, argv, "P:a:pt:uv")) != -1) {
		switch (ch) {
		case 'P':
			pin = optarg;
			break;
		case 'a':
			if (read_blob(optarg, &body, &len) < 0)
				errx(1, "read_blob: %s", optarg);
			if ((r = fido_assert_allow_cred(assert, body,
			    len)) != FIDO_OK)
				errx(1, "fido_assert_allow_cred: %s (0x%x)",
				    fido_strerr(r), r);
			free(body);
			body = NULL;
			break;
		case 'p':
			up = true;
			break;
		case 't':
			if (strcmp(optarg, "ecdsa") == 0)
				type = COSE_ES256;
			else if (strcmp(optarg, "rsa") == 0)
				type = COSE_RS256;
			else
				errx(1, "unknown type %s", optarg);
			break;
		case 'u':
			u2f = true;
			break;
		case 'v':
			uv = true;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 2)
		usage();

	fido_init(0);

	if ((dev = fido_dev_new()) == NULL)
		errx(1, "fido_dev_new");

	r = fido_dev_open(dev, argv[1]);
	if (r != FIDO_OK)
		errx(1, "fido_dev_open: %s (0x%x)", fido_strerr(r), r);
	if (u2f)
		fido_dev_force_u2f(dev);

	/* client data hash */
	r = fido_assert_set_clientdata_hash(assert, cdh, sizeof(cdh));
	if (r != FIDO_OK)
		errx(1, "fido_assert_set_clientdata_hash: %s (0x%x)",
		    fido_strerr(r), r);

	/* relying party */
	r = fido_assert_set_rp(assert, "localhost");
	if (r != FIDO_OK)
		errx(1, "fido_assert_set_rp: %s (0x%x)", fido_strerr(r), r);

	/* options */
	r = fido_assert_set_options(assert, up, uv);
	if (r != FIDO_OK)
		errx(1, "fido_assert_set_options: %s (0x%x)", fido_strerr(r), r);

	r = fido_dev_get_assert(dev, assert, pin);
	if (r != FIDO_OK)
		errx(1, "fido_dev_get_assert: %s (0x%x)", fido_strerr(r), r);
	r = fido_dev_close(dev);
	if (r != FIDO_OK)
		errx(1, "fido_dev_close: %s (0x%x)", fido_strerr(r), r);

	fido_dev_free(&dev);

	if (fido_assert_count(assert) != 1)
		errx(1, "fido_assert_count: %d signatures returned",
		    (int)fido_assert_count(assert));

	verify_assert(type, fido_assert_authdata_ptr(assert, 0),
	    fido_assert_authdata_len(assert, 0), fido_assert_sig_ptr(assert, 0),
	    fido_assert_sig_len(assert, 0), up, uv, argv[0]);

	fido_assert_free(&assert);

	exit(0);
}
