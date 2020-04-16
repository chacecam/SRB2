// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 2015-2016 by Tuomas Lappeteläinen.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  twitter.c
/// \brief Twitter status updates

// Lactozilla: Mostly taken from http://www.aatosjalo.com/blog/2015/02/01/using-twitter-api-with-c/

#include "twitter.h"

#include "openssl/ossl_typ.h"
#include "openssl/bio.h"
#include "openssl/ssl.h"
#include "openssl/err.h"
#include "openssl/engine.h"
#include "openssl/rand.h"
#include "openssl/crypto.h"
#include "openssl/hmac.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>

#include "doomdef.h"
#include "doomtype.h"

#include "m_random.h"

#include "console.h"

static const size_t buf_size = 8192;

/* Init all necessary openssl modules. */
void openssl_init(void)
{
	static boolean init = false;
	if (!init)
	{
		SSL_library_init();
		SSL_load_error_strings();
		ERR_load_BIO_strings();
		OpenSSL_add_all_algorithms();
		ENGINE_load_builtin_engines();
		ENGINE_register_all_complete();
	}
}

/**
 * Posts given POST to api.twitter.com.
 *
 * From https://thunked.org/programming/openssl-tutorial-client-t11.html and
 * example in https://www.openssl.org/docs/crypto/BIO_f_ssl.html.
 *
 * This doesn't verify the server certificate, i.e. it will accept certificates
 * signed by any CA.
 */
static int send_to_twitter(const char *post)
{
	SSL_CTX *ctx;
	BIO *bio;
	SSL *ssl;

	char hostname[22];
	char buf[1024] = { 0 };
	int ret, read_bytes;

	ctx = SSL_CTX_new(SSLv23_client_method());
	if (ctx == NULL)
	{
		CONS_Alert(CONS_ERROR, "Error creating SSL_CTX\n");
		return -1;
	}

	bio = BIO_new_ssl_connect(ctx);
	if (bio == NULL)
	{
		CONS_Alert(CONS_ERROR, "Error creating BIO!\n");
		SSL_CTX_free(ctx);
		return -1;
	}

	BIO_get_ssl(bio, &ssl);
	if (ssl == NULL)
	{
		CONS_Alert(CONS_ERROR, "BIO_get_ssl failed\n");
		BIO_free_all(bio);
		SSL_CTX_free(ctx);
		return -1;
	}

	SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);

	strcpy(hostname, "api.twitter.com:https");
	BIO_set_conn_hostname(bio, hostname);

	if (BIO_do_connect(bio) <= 0)
	{
		CONS_Alert(CONS_ERROR, "Failed to connect! %s\n", ERR_error_string(ERR_get_error(), NULL));
		BIO_free_all(bio);
		SSL_CTX_free(ctx);
		return -1;
	}

	if (BIO_do_handshake(bio) <= 0)
	{
		CONS_Alert(CONS_ERROR, "Failed to do SSL handshake!\n");
		BIO_free_all(bio);
		SSL_CTX_free(ctx);
		return -1;
	}

	BIO_puts(bio, post);
	read_bytes = BIO_read(bio, buf, sizeof(buf) - 1);
	if (read_bytes > 0)
	{
		CONS_Printf("Read %d bytes\n", read_bytes);
		buf[read_bytes] = '\0';

		if (strstr(buf, "HTTP/1.1 200 OK") != NULL)
		{
			CONS_Printf("http ok\n");
			ret = 0;
		}
		else if (strstr(buf, "HTTP/1.1 403 Forbidden") != NULL)
		{
			/* Twitter doesn't allow consecutive duplicates and will respond
			 * with 403 in such case. */
			CONS_Alert(CONS_ERROR, "Twitter responded with 403!\n");
			ret = -2;
		}
		else
		{
			CONS_Printf("Error occurred! Received:\n"
				   "%s\n", buf);
			// also read JSON error
			read_bytes = BIO_read(bio, buf, sizeof(buf) - 1);
			if (read_bytes > 0)
			{
				CONS_Printf("Read %d bytes\n", read_bytes);
				buf[read_bytes] = '\0';
				CONS_Printf("%s\n", buf);
			}
			ret = -1;
		}
	}
	else
	{
		CONS_Alert(CONS_ERROR, "Read failed!\n");
		ret = -1;
	}

	BIO_free_all(bio);
	SSL_CTX_free(ctx);

	return ret;
}

/* Base64 encode the given string. */
static char *base64_encode(const char *msg, size_t msg_len)
{
	BIO *b64_bio = BIO_new(BIO_f_base64());
	BIO *mem_bio = BIO_new(BIO_s_mem());
	BUF_MEM *b64;

	BIO_push(b64_bio, mem_bio);
	BIO_set_flags(b64_bio, BIO_FLAGS_BASE64_NO_NL);

	BIO_write(b64_bio, msg, msg_len);
	BIO_flush(b64_bio);

	BIO_get_mem_ptr(mem_bio, &b64);
	BIO_set_close(mem_bio, BIO_NOCLOSE);
	BIO_free_all(b64_bio);

	BUF_MEM_grow(b64, (*b64).length + 1);
	(*b64).data[(*b64).length-1] = '\0';
	return (*b64).data;
}

/* Generate nonce used as oauth_nonce. */
static char *generate_nonce(void)
{
	size_t nonce_size = 32, i;
	char *encoded_nonce = malloc(nonce_size + 1);
	if (encoded_nonce == NULL)
		return NULL;

	for (i = 0; i < nonce_size; i++)
	{
		const char a = M_RandomKey(26*2);
		if (a < 26) // uppercase
			encoded_nonce[i] = 'A'+a;
		else // lowercase
			encoded_nonce[i] = 'a'+(a-26);
	}

	encoded_nonce[nonce_size] = '\0';
	return encoded_nonce;
}

/* Does character c need percent encoding or is it a unreserved character? */
static int need_percent_encoding(char c)
{
	return !(isalnum((int)c) || c == '-' || c == '.' || c == '_' || c == '~');

}

/* Percent encode (or URL encode) given string. There should be enough room in
 * the str (three times uncoded charactes). */
static char *percent_encode(char *str)
{
	/* Length has to be updated on each iteration. */
	unsigned int i;
	char tmp[2];
	for (i = 0; str[i] != '\0'; i++)
	{
		if (need_percent_encoding(str[i]))
		{
			/* Make room for two characters. */
			memmove(str + i + 3, str + i + 1, strlen(str) - i + 1);
			/* Write '%' and two bytes of which the character consists of. */
			snprintf(tmp, 2, "%X", str[i] >> 4);
			str[i + 1] = tmp[0];
			snprintf(tmp, 2, "%X", str[i] & 0xf);
			str[i + 2] = tmp[0];
			str[i] = '%';
		}
	}

	return str;
}

static char *create_post(const char *timestamp, const char *nonce, const char *status,
						 const char *signature, const char *consumer_key,
						 const char *auth_token)
{
	int ret;
	char *post = malloc(buf_size);
	if (post == NULL)
		return NULL;

	ret = snprintf(post, buf_size, "POST /1.1/statuses/update.json?include_entities=true HTTP/1.1\r\n"
	   "Accept: */*\r\n"
	   "Connection: close\r\n"
	   "User-Agent: OAuth gem v0.4.4\r\n"
	   "Host: api.twitter.com\r\n"
	   "Content-Type: application/x-www-form-urlencoded\r\n"
	   "Authorization: OAuth oauth_consumer_key=\"%s\", oauth_nonce=\"%s\", "
	   "oauth_signature=\"%s\", oauth_signature_method=\"HMAC-SHA1\", "
	   "oauth_timestamp=\"%s\", oauth_token=\"%s\", oauth_version=\"1.0\"\r\n"
	   "Content-Length: %s\r\n\r\n"
	   "status=%s",
	   consumer_key, nonce, signature, timestamp, auth_token,
	   sizeu1(strlen("status=") + strlen(status)), status);

	if (ret < 0 || (size_t)ret >= buf_size)
	{
		free(post);
		return NULL;
	}

	return post;
}

static unsigned char *hmac_sha1_encode(const char *data, const char *hmac_key,
									   unsigned int *result_len)
{
	HMAC_CTX *ctx;
	unsigned char *result;

	*result_len = 20; /* Should be always 20 bytes. */
	result = malloc(*result_len);
	if (result == NULL)
		return NULL;

	ctx = HMAC_CTX_new();
	HMAC_Init_ex(ctx, hmac_key, (int)strlen(hmac_key), EVP_sha1(), NULL);
	HMAC_Update(ctx, (const unsigned char *)data, strlen(data));
	HMAC_Final(ctx, result, result_len);
	HMAC_CTX_free(ctx);

	return result;
}

/* Compute the signature from given parameters as required by the OAuth. */
static char *compute_signature(const char *timestamp, const char *nonce, const char *status,
							   const char *consumer_key, const char *consumer_secret,
							   const char *auth_token, const char *auth_secret)
{
	int ret;
	char *signature_base;
	size_t encoded_size;
	char *encoded_status;
	char *encoded_ckey, *encoded_nonce, *encoded_auth;
	char *hmac_key, *encoded_csecret, *encoded_asecret;
	unsigned int encoded_len;
	char *hmac_encoded;
	char *encoded;

	signature_base = malloc(buf_size);
	if (signature_base == NULL) {
		return NULL;
	}

	/* Encode the status again. */
	encoded_size = strlen(status) * 3 + 1;
	encoded_status = malloc(encoded_size);
	if (encoded_status == NULL)
	{
		free(signature_base);
		return NULL;
	}

	snprintf(encoded_status, encoded_size, "%s", status);
	percent_encode(encoded_status);

	// encode consumer key / nonce / auth
	encoded_ckey = malloc(buf_size);
	if (encoded_ckey == NULL)
		return NULL;

	encoded_nonce = malloc(buf_size);
	if (encoded_nonce == NULL)
	{
		free(encoded_ckey);
		return NULL;
	}

	encoded_auth = malloc(buf_size);
	if (encoded_auth == NULL)
	{
		free(encoded_ckey);
		free(encoded_nonce);
		return NULL;
	}

	snprintf(encoded_ckey, buf_size, "%s", consumer_key);
	snprintf(encoded_nonce, buf_size, "%s", nonce);
	snprintf(encoded_auth, buf_size, "%s", auth_token);

	percent_encode(encoded_ckey);
	percent_encode(encoded_nonce);
	percent_encode(encoded_auth);

	ret = snprintf(signature_base, buf_size, "POST&https%%3A%%2F%%2Fapi.twitter.com"
					   "%%2F1.1%%2Fstatuses%%2Fupdate.json&"
					   "include_entities%%3Dtrue%%26"
					   "oauth_consumer_key%%3D%s%%26oauth_nonce%%3D%s"
					   "%%26oauth_signature_method%%3DHMAC-SHA1"
					   "%%26oauth_timestamp%%3D%s%%26oauth_token%%3D%s"
					   "%%26oauth_version%%3D1.0"
					   "%%26status%%3D%s",
					   encoded_ckey, encoded_nonce, timestamp, encoded_auth, encoded_status);

	free(encoded_status);
	free(encoded_ckey);
	free(encoded_nonce);
	free(encoded_auth);

	if (ret < 0 || (size_t)ret > buf_size)
	{
		free(signature_base);
		return NULL;
	}

	// create hmac key
	hmac_key = malloc(buf_size);
	if (hmac_key == NULL)
		return NULL;

	encoded_csecret = malloc(buf_size);
	if (encoded_csecret == NULL)
	{
		free(hmac_key);
		return NULL;
	}

	encoded_asecret = malloc(buf_size);
	if (encoded_asecret == NULL)
	{
		free(hmac_key);
		free(encoded_csecret);
		return NULL;
	}

	snprintf(encoded_csecret, buf_size, "%s", consumer_secret);
	snprintf(encoded_asecret, buf_size, "%s", auth_secret);

	percent_encode(encoded_csecret);
	percent_encode(encoded_asecret);

	snprintf(hmac_key, buf_size, "%s&%s", encoded_csecret, encoded_asecret);

	free(encoded_csecret);
	percent_encode(encoded_asecret);

	// encode hmac
	hmac_encoded = (char *)hmac_sha1_encode(signature_base, hmac_key, &encoded_len);
	free(signature_base);

	if (hmac_encoded == NULL)
		return NULL;

	// base64 encode hmac
	encoded = base64_encode(hmac_encoded, encoded_len);
	free(hmac_encoded);

	if (encoded == NULL)
		return NULL;

	return percent_encode(encoded);
}

int TwitterStatusUpdate(const char *message,
						   const char *consumer_key, const char *consumer_secret,
						   const char *auth_token, const char *auth_secret)
{
	char status[141];
	char *nonce;
	char *sig;
	char *post;

	int ret;
	time_t t;
	char timestamp[11];

	snprintf(status, sizeof(status), "%s", message);
	percent_encode(status);

	nonce = generate_nonce();
	if (nonce == NULL)
	{
		CONS_Alert(CONS_ERROR, "TwitterStatusUpdate: could not generate nonce\n");
		return -1;
	}

	t = time(NULL);
	snprintf(timestamp, sizeof(timestamp), "%s", sizeu1((size_t)t));

	sig = compute_signature(timestamp, nonce, status,
							consumer_key, consumer_secret,
							auth_token, auth_secret);
	if (sig == NULL)
	{
		CONS_Alert(CONS_ERROR, "TwitterStatusUpdate: could not compute signature\n");
		free(nonce);
		return -1;
	}

	post = create_post(timestamp, nonce, status, sig,
						consumer_key, auth_token);
	free(sig);
	free(nonce);

	if (post == NULL)
	{
		CONS_Alert(CONS_ERROR, "TwitterStatusUpdate: could not create post\n");
		return -1;
	}

	ret = send_to_twitter(post);
	free(post);
	return ret;
}
