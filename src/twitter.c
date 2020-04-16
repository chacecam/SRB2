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

// Lactozilla: Based on http://www.aatosjalo.com/blog/2015/02/01/using-twitter-api-with-c/

#include "twitter.h"

#include "doomdef.h"
#include "doomtype.h"

#include "d_main.h"
#include "m_random.h"
#include "z_zone.h"

#include "console.h"
#include "command.h"

static char tw_hostname[22];

static char tw_consumer_key[TW_APIKEYLENGTH + 1], tw_consumer_secret[TW_APISECRETLENGTH + 1];
static char tw_auth_token[TW_AUTHTOKENLENGTH + 1], tw_auth_secret[TW_AUTHSECRETLENGTH + 1];

static boolean Twitter_LoadAuth(void);
static void Twitter_ClearAuth(void);

static void Twitter_Dealloc(void *ptr, size_t len);

static void Command_Twitterupdate_f(void);

static consvar_t cv_twusehashtag = {"tw_usehashtag", "No", 0, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

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

void Twitter_Init(void)
{
	// Init all necessary OpenSSL modules
	SSL_library_init();
	SSL_load_error_strings();
	ERR_load_BIO_strings();
	OpenSSL_add_all_algorithms();
	ENGINE_load_builtin_engines();
	ENGINE_register_all_complete();

	// Add Twitter commands
	COM_AddCommand("twitterupdate", Command_Twitterupdate_f);

	// Add Twitter variables
	CV_RegisterVar(&cv_twusehashtag);

	// Set hostname
	strcpy(tw_hostname, "api.twitter.com:https");
}

// Base64 encode the given string.
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

// Generate nonce used as oauth_nonce.
#define NONCE_LENGTH 32
static char *generate_nonce(void)
{
	size_t i;
	char *encoded_nonce = ZZ_Alloc(NONCE_LENGTH + 1);

	for (i = 0; i < NONCE_LENGTH; i++)
	{
		const char a = M_RandomKey(26*2);
		if (a < 26) // uppercase
			encoded_nonce[i] = 'A'+a;
		else // lowercase
			encoded_nonce[i] = 'a'+(a-26);
	}

	encoded_nonce[NONCE_LENGTH] = '\0';
	return encoded_nonce;
}

// Does character c need percent encoding or is it a unreserved character?
static int need_percent_encoding(char c)
{
	return !(isalnum((int)c) || c == '-' || c == '.' || c == '_' || c == '~');
}

#define PERCENT_ENCODED_LENGTH(len) (len * 3)

// Percent encode (or URL encode) given string. There should be enough room in
// the str (three times uncoded charactes).
static char *percent_encode(char *str)
{
	// Length has to be updated on each iteration.
	unsigned int i;
	char tmp[2];

	for (i = 0; str[i] != '\0'; i++)
	{
		if (need_percent_encoding(str[i]))
		{
			// Make room for two characters.
			memmove(str + i + 3, str + i + 1, strlen(str) - i + 1);
			// Write '%' and two bytes of which the character consists of.
			snprintf(tmp, 2, "%X", str[i] >> 4);
			str[i + 1] = tmp[0];
			snprintf(tmp, 2, "%X", str[i] & 0xf);
			str[i + 2] = tmp[0];
			str[i] = '%';
		}
	}

	return str;
}

static unsigned char *hmac_sha1_encode(const char *data, const char *hmac_key, unsigned int *result_len)
{
	HMAC_CTX *ctx;
	unsigned char *result;

	*result_len = 20; // Should be always 20 bytes.
	result = ZZ_Alloc(*result_len);

	ctx = HMAC_CTX_new();
	HMAC_Init_ex(ctx, hmac_key, (int)strlen(hmac_key), EVP_sha1(), NULL);
	HMAC_Update(ctx, (const unsigned char *)data, strlen(data));
	HMAC_Final(ctx, result, result_len);
	HMAC_CTX_free(ctx);

	return result;
}

// Compute the signature from given parameters as required by the OAuth.
static char *compute_signature(const char *timestamp, const char *nonce, const char *status,
							   const char *consumer_key, const char *consumer_secret,
							   const char *auth_token, const char *auth_secret)
{
	size_t sigbufsize = 8192;
	char *signature_base = ZZ_Alloc(sigbufsize);
	size_t encoded_size;
	char *encoded_status;
	char *encoded_ckey, *encoded_nonce, *encoded_auth;
	char *encoded_csecret, *encoded_asecret;
	char *hmac_key;
	size_t hmac_size;
	unsigned int encoded_len;
	char *hmac_encoded;
	char *encoded, *z_encoded;
	int ret;

	// Encode the status again.
	encoded_size = PERCENT_ENCODED_LENGTH(strlen(status)) + 1;
	encoded_status = ZZ_Alloc(encoded_size);

	snprintf(encoded_status, encoded_size, "%s", status);
	percent_encode(encoded_status);

#define ENCODE(encstr, encbuf, enclen) \
	encbuf = ZZ_Alloc(PERCENT_ENCODED_LENGTH(enclen) + 1); \
	snprintf(encbuf, enclen, "%s", encstr); \
	percent_encode(encbuf);

	// encode consumer key / auth token / nonce
	ENCODE(consumer_key, encoded_ckey, TW_APIKEYLENGTH);
	ENCODE(auth_token, encoded_auth, TW_AUTHTOKENLENGTH);
	ENCODE(nonce, encoded_nonce, NONCE_LENGTH);

	// Create base signature string
	ret = snprintf(signature_base, sigbufsize, "POST&https%%3A%%2F%%2Fapi.twitter.com"
					   "%%2F1.1%%2Fstatuses%%2Fupdate.json&"
					   "include_entities%%3Dtrue%%26"
					   "oauth_consumer_key%%3D%s%%26oauth_nonce%%3D%s"
					   "%%26oauth_signature_method%%3DHMAC-SHA1"
					   "%%26oauth_timestamp%%3D%s%%26oauth_token%%3D%s"
					   "%%26oauth_version%%3D1.0"
					   "%%26status%%3D%s",
					   encoded_ckey, encoded_nonce, timestamp, encoded_auth, encoded_status);

	// Free encoded strings
	Twitter_Dealloc(encoded_status, encoded_size);
	Twitter_Dealloc(encoded_ckey, PERCENT_ENCODED_LENGTH(TW_APIKEYLENGTH) + 1);
	Twitter_Dealloc(encoded_auth, PERCENT_ENCODED_LENGTH(TW_AUTHTOKENLENGTH) + 1);
	Twitter_Dealloc(encoded_nonce, PERCENT_ENCODED_LENGTH(NONCE_LENGTH) + 1);

	if (ret < 0 || (size_t)ret > sigbufsize)
	{
		Z_Free(signature_base);
		return NULL;
	}

	// percent encode consumer secret and auth secret for the hmac key
	encoded_csecret = ZZ_Alloc(PERCENT_ENCODED_LENGTH(TW_APISECRETLENGTH) + 1);
	encoded_asecret = ZZ_Alloc(PERCENT_ENCODED_LENGTH(TW_AUTHSECRETLENGTH) + 1);

	snprintf(encoded_csecret, TW_APISECRETLENGTH, "%s", consumer_secret);
	snprintf(encoded_asecret, TW_AUTHSECRETLENGTH, "%s", auth_secret);

	percent_encode(encoded_csecret);
	percent_encode(encoded_asecret);

	// create hmac key
	hmac_size = TW_APISECRETLENGTH + TW_AUTHSECRETLENGTH + 2;
	hmac_key = ZZ_Alloc(hmac_size);
	snprintf(hmac_key, hmac_size, "%s&%s", encoded_csecret, encoded_asecret);

	Twitter_Dealloc(encoded_csecret, PERCENT_ENCODED_LENGTH(TW_APISECRETLENGTH) + 1);
	Twitter_Dealloc(encoded_asecret, PERCENT_ENCODED_LENGTH(TW_AUTHSECRETLENGTH) + 1);

	// encode signature
	hmac_encoded = (char *)hmac_sha1_encode(signature_base, hmac_key, &encoded_len);
	Twitter_Dealloc(signature_base, sigbufsize);

	// base64 encode hmac
	encoded = base64_encode(hmac_encoded, encoded_len);
	z_encoded = Z_StrDup(encoded);

	memset(encoded, 0x00, strlen(encoded) + 1);
	free(encoded);
	Twitter_Dealloc(hmac_encoded, strlen(hmac_encoded) + 1);

	return percent_encode(z_encoded);
}

//
// Authentication
//

static boolean Twitter_LoadAuth(void)
{
	FILE *f;
	char type[11], key[50];

	f = fopen(va("%s"PATHSEP"%s", srb2home, "twitterauth.cfg"), "rt");
	if (!f)
	{
		CONS_Printf("%s %s\n", M_GetText("Twitter_LoadAuth: error loading twitterauth.cfg:"), strerror(errno));
		Twitter_ClearAuth();
		return false;
	}

	while (fscanf(f, "%s %s", type, key) == 2)
	{
		// consumer keys
		if (!stricmp(type, "apikey"))
			strncpy(tw_consumer_key, key, TW_APIKEYLENGTH);
		else if (!stricmp(type, "apisecret"))
			strncpy(tw_consumer_secret, key, TW_APISECRETLENGTH);
		// auth keys
		else if (!stricmp(type, "authtoken"))
			strncpy(tw_auth_token, key, TW_AUTHTOKENLENGTH);
		else if (!stricmp(type, "authsecret"))
			strncpy(tw_auth_secret, key, TW_AUTHSECRETLENGTH);
	}

	memset(type, 0x00, 11);
	memset(key, 0x00, 50);
	fclose(f);

	return true;
}

static void Twitter_ClearAuth(void)
{
	memset(tw_consumer_key, 0x00, (TW_APIKEYLENGTH + 1));
	memset(tw_consumer_secret, 0x00, (TW_APISECRETLENGTH + 1));
	memset(tw_auth_token, 0x00, (TW_AUTHTOKENLENGTH + 1));
	memset(tw_auth_secret, 0x00, (TW_AUTHSECRETLENGTH + 1));
}

static void Twitter_Dealloc(void *buf, size_t len)
{
	memset(buf, 0x00, len);
	Z_Free(buf);
}

//
// Sending tweets
//

static char *CreateTweet(const char *timestamp, const char *nonce, const char *status,
						 const char *signature, const char *consumer_key,
						 const char *auth_token)
{
	size_t postbufsize = 8192;
	char *post = ZZ_Alloc(postbufsize);
	int ret = snprintf(post, postbufsize,
		"POST /1.1/statuses/update.json?include_entities=true HTTP/1.1\r\n"
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

	if (ret < 0 || (size_t)ret >= postbufsize)
	{
		Twitter_Dealloc(post, postbufsize);
		return NULL;
	}

	return post;
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
static int SendTweet(const char *post)
{
	SSL_CTX *ctx;
	BIO *bio;
	SSL *ssl;

	char buf[1024];
	int ret, read_bytes;

	buf[0] = '\0';

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
	BIO_set_conn_hostname(bio, tw_hostname);

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
			CONS_Printf("HTTP OK\n");
			ret = 0;
		}
		else if (strstr(buf, "HTTP/1.1 403 Forbidden") != NULL)
		{
			// Twitter doesn't allow consecutive duplicates and will respond with 403 in such case.
			CONS_Alert(CONS_ERROR, "Twitter responded with HTTP 403\n");
			ret = -2;
		}
		else
		{
			CONS_Printf("Error occurred! Received:\n%s\n", buf);
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

void Twitter_StatusUpdate(const char *message)
{
	char status[(TW_STATUSLENGTH * 3) + strlen(TW_HASHTAG) + 2];
	char *nonce;
	char *sig;
	char *post;

	time_t t;
	char timestamp[11];

	// Copy tweet and percent encode
	if (cv_twusehashtag.value)
		snprintf(status, (TW_STATUSLENGTH + strlen(TW_HASHTAG) + 2), "%s %s", message, TW_HASHTAG);
	else
		snprintf(status, (TW_STATUSLENGTH + 1), "%s", message);
	percent_encode(status);

	// Generate nonce
	nonce = generate_nonce();

	// Get timestamp
	t = time(NULL);
	snprintf(timestamp, sizeof(timestamp), "%s", sizeu1((size_t)t));

	// Compute signature
	sig = compute_signature(timestamp, nonce, status, tw_consumer_key, tw_consumer_secret, tw_auth_token, tw_auth_secret);
	if (sig == NULL)
	{
		CONS_Alert(CONS_ERROR, "Twitter_StatusUpdate: could not compute signature\n");
		Z_Free(nonce);
		return;
	}

	Twitter_ClearAuth();

	// Create Tweet
	post = CreateTweet(timestamp, nonce, status, sig, tw_consumer_key, tw_auth_token);
	Twitter_Dealloc(sig, strlen(sig) + 1);
	Twitter_Dealloc(nonce, NONCE_LENGTH + 1);

	if (post == NULL)
	{
		CONS_Alert(CONS_ERROR, "Twitter_StatusUpdate: could not create post\n");
		return;
	}

	SendTweet(post);
	Twitter_Dealloc(post, strlen(post) + 1);
}

static void Command_Twitterupdate_f(void)
{
	if (COM_Argc() < 2)
	{
		CONS_Printf(M_GetText("twitterupdate <message>: post a status update to Twitter\n"));
		return;
	}

	if (!Twitter_LoadAuth())
		return;

	Twitter_StatusUpdate(COM_Argv(1));
	Twitter_ClearAuth();
}
