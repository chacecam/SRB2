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
#include "jsmn.h"

#include "doomdef.h"
#include "doomtype.h"

#include "d_main.h"
#include "d_netfil.h" // nameonly
#include "m_misc.h" // screenshots
#include "m_random.h"
#include "z_zone.h"

#include "console.h"
#include "command.h"

#define BUFSIZE 8192

static char tw_hostname[64];

static char tw_consumer_key[TW_APIKEYLENGTH + 1], tw_consumer_secret[TW_APISECRETLENGTH + 1];
static char tw_auth_token[TW_AUTHTOKENLENGTH + 1], tw_auth_secret[TW_AUTHSECRETLENGTH + 1];

static boolean Twitter_LoadAuth(void);
static void Twitter_ClearAuth(void);

static void Twitter_Dealloc(void *ptr, size_t len);

static void Command_Twitterupdate_f(void);

static void AttachmentChange(char *attachment, consvar_t *cvar)
{
	char lastfile[MAX_WADPATH];
	if (strlen(attachment))
		nameonly(strcpy(lastfile, attachment));

	if (!cvar->value)
	{
		if (strlen(attachment))
			CONS_Printf("Not attaching %s anymore\n", lastfile);
		return;
	}

	if (strlen(attachment) == 0)
	{
		CV_StealthSetValue(cvar, 0);
		CONS_Alert(CONS_NOTICE, "Nothing to attach\n");
		return;
	}

	CONS_Printf("Attaching %s\n", lastfile);
}

static void TwAttachScreenshot_OnChange(void);
static void TwAttachMovie_OnChange(void);

static consvar_t cv_twusehashtag = {"tw_usehashtag", "No", 0, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
static consvar_t cv_twsendsshots = {"tw_attachscreenshot", "No", CV_CALL | CV_NOINIT, CV_OnOff, TwAttachScreenshot_OnChange, 0, NULL, NULL, 0, 0, NULL};
static consvar_t cv_twsendmovies = {"tw_attachmovie", "No", CV_CALL | CV_NOINIT, CV_OnOff, TwAttachMovie_OnChange, 0, NULL, NULL, 0, 0, NULL};

static void TwAttachScreenshot_OnChange(void)
{
	AttachmentChange(lastsshot, &cv_twsendsshots);
}

static void TwAttachMovie_OnChange(void)
{
	AttachmentChange(lastmovie, &cv_twsendmovies);
}

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
	COM_AddCommand("tw_statusupdate", Command_Twitterupdate_f);

	// Add Twitter variables
	CV_RegisterVar(&cv_twusehashtag);
	CV_RegisterVar(&cv_twsendsshots);
	CV_RegisterVar(&cv_twsendmovies);
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
static char *compute_signature(const char *timestamp, const char *nonce, const char *status, boolean use_status,
							   const char *consumer_key, const char *consumer_secret,
							   const char *auth_token, const char *auth_secret)
{
	size_t sigbufsize = BUFSIZE;
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
	if (use_status)
	{
		encoded_size = PERCENT_ENCODED_LENGTH(strlen(status)) + 1;
		encoded_status = ZZ_Alloc(encoded_size);

		snprintf(encoded_status, encoded_size, "%s", status);
		percent_encode(encoded_status);
	}

#define ENCODE(encstr, encbuf, enclen) \
	encbuf = ZZ_Alloc(PERCENT_ENCODED_LENGTH(enclen) + 1); \
	snprintf(encbuf, enclen + 1, "%s", encstr); \
	percent_encode(encbuf);

	// encode consumer key / auth token / nonce
	ENCODE(consumer_key, encoded_ckey, TW_APIKEYLENGTH);
	ENCODE(auth_token, encoded_auth, TW_AUTHTOKENLENGTH);
	ENCODE(nonce, encoded_nonce, NONCE_LENGTH);

#define BASESIG \
	"POST&https%%3A%%2F%%2Fapi.twitter.com" \
	"%%2F1.1%%2Fstatuses%%2Fupdate.json&" \
	"include_entities%%3Dtrue%%26" \
	"oauth_consumer_key%%3D%s%%26oauth_nonce%%3D%s" \
	"%%26oauth_signature_method%%3DHMAC-SHA1" \
	"%%26oauth_timestamp%%3D%s%%26oauth_token%%3D%s" \
	"%%26oauth_version%%3D1.0"

	// Create base signature string
	if (use_status)
		ret = snprintf(signature_base, sigbufsize, BASESIG "%%26status%%3D%s", encoded_ckey, encoded_nonce, timestamp, encoded_auth, encoded_status);
	else
		ret = snprintf(signature_base, sigbufsize, BASESIG, encoded_ckey, encoded_nonce, timestamp, encoded_auth);

	// Free encoded strings
	if (use_status)
		Twitter_Dealloc(encoded_status, encoded_size);
	Twitter_Dealloc(encoded_ckey, PERCENT_ENCODED_LENGTH(TW_APIKEYLENGTH) + 1);
	Twitter_Dealloc(encoded_auth, PERCENT_ENCODED_LENGTH(TW_AUTHTOKENLENGTH) + 1);
	Twitter_Dealloc(encoded_nonce, PERCENT_ENCODED_LENGTH(NONCE_LENGTH) + 1);

	if (ret < 0 || (size_t)ret > sigbufsize)
	{
		Twitter_Dealloc(signature_base, sigbufsize);
		return NULL;
	}

	// percent encode consumer secret and auth secret for the hmac key
	encoded_csecret = ZZ_Alloc(PERCENT_ENCODED_LENGTH(TW_APISECRETLENGTH) + 1);
	encoded_asecret = ZZ_Alloc(PERCENT_ENCODED_LENGTH(TW_AUTHSECRETLENGTH) + 1);

	snprintf(encoded_csecret, TW_APISECRETLENGTH + 1, "%s", consumer_secret);
	snprintf(encoded_asecret, TW_AUTHSECRETLENGTH + 1, "%s", auth_secret);

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

#define AUTH_HEADER \
	"Authorization: OAuth oauth_consumer_key=\"%s\", oauth_nonce=\"%s\", " \
	"oauth_signature=\"%s\", oauth_signature_method=\"HMAC-SHA1\", " \
	"oauth_timestamp=\"%s\", oauth_token=\"%s\", oauth_version=\"1.0\"\r\n"

// Handle HTTP response
static int Twitter_HandleHTTPResponse(char *buf)
{
	if (strstr(buf, "HTTP/1.1 200 OK") != NULL)
	{
		CONS_Printf("HTTP OK\n");
		return 0;
	}
	else if (strstr(buf, "HTTP/1.1 403 Forbidden") != NULL)
	{
		// Twitter doesn't allow consecutive duplicates and will respond with 403 in such case.
		CONS_Alert(CONS_ERROR, "Twitter_HandleHTTPResponse: Twitter responded with HTTP 403\n");
		return -2;
	}

	CONS_Alert(CONS_ERROR, "Twitter_HandleHTTPResponse: Error occurred! Received:\n%s\n", buf);
	return -1;
}

// Setup a connection to Twitter. Does not set hostname.
static int Twitter_SetupConnection(SSL_CTX **ctx, BIO **bio, SSL **ssl)
{
	(*ctx) = SSL_CTX_new(SSLv23_client_method());
	if ((*ctx) == NULL)
	{
		CONS_Alert(CONS_ERROR, "Twitter_SetupConnection: Error creating SSL_CTX\n");
		return -1;
	}

	(*bio) = BIO_new_ssl_connect(*ctx);
	if ((*bio) == NULL)
	{
		CONS_Alert(CONS_ERROR, "Twitter_SetupConnection: Error creating BIO\n");
		SSL_CTX_free(*ctx);
		return -1;
	}

	BIO_get_ssl(*bio, ssl);
	if ((*ssl) == NULL)
	{
		CONS_Alert(CONS_ERROR, "Twitter_SetupConnection: BIO_get_ssl failed\n");
		BIO_free_all(*bio);
		SSL_CTX_free(*ctx);
		return -1;
	}

	SSL_set_mode((*ssl), SSL_MODE_AUTO_RETRY);
	return 0;
}

// Performs both a connection and a handshake.
static int Twitter_DoConnectionAndHandshake(SSL_CTX **ctx, BIO **bio)
{
	if (BIO_do_connect(*bio) <= 0)
	{
		CONS_Alert(CONS_ERROR, "Twitter_DoConnectionAndHandshake: Failed to connect! %s\n", ERR_error_string(ERR_get_error(), NULL));
		BIO_free_all(*bio);
		SSL_CTX_free(*ctx);
		return -1;
	}

	if (BIO_do_handshake(*bio) <= 0)
	{
		CONS_Alert(CONS_ERROR, "Twitter_DoConnectionAndHandshake: Failed to do SSL handshake!\n");
		BIO_free_all(*bio);
		SSL_CTX_free(*ctx);
		return -1;
	}

	return 0;
}

#define HEADER_ACCEPT_DEFAULT \
	"Accept: */*\r\n" \

#define HEADER_CONTENT_TYPE_DEFAULT \
	"Content-Type: application/x-www-form-urlencoded\r\n"

#define HEADER_BASE(host) \
	"Connection: close\r\n" \
	"User-Agent: OAuth gem v0.4.4\r\n" \
	"Host: " host "\r\n"

// Generate multipart HTTP request boundary.
#define MULTIPART_BOUNDARY_LENGTH 48
static char *generate_multipart_request_boundary(void)
{
	size_t i;
	char *boundary = ZZ_Alloc(MULTIPART_BOUNDARY_LENGTH + 1);

	for (i = 0; i < MULTIPART_BOUNDARY_LENGTH; i++)
	{
		const char a = M_RandomKey(26*2);
		if (a < 26) // uppercase
			boundary[i] = 'A'+a;
		else // lowercase
			boundary[i] = 'a'+(a-26);
	}

	boundary[MULTIPART_BOUNDARY_LENGTH] = '\0';
	return boundary;
}

//
// Attachments
//

static char *CreateAttachment(const char *type, const char *file, int sizelimit, size_t *retlen,
						 const char *timestamp, const char *nonce,
						 const char *signature, const char *consumer_key,
						 const char *auth_token)
{
	UINT8 *filedata, *attachdata;
	size_t postbufsize = BUFSIZE, length, baselength, endlength;
	char *post;
	char *boundary;
	char *end;
	int ret;

	FILE *f;
	long filesize;

	// attachment name only (srb2xxxx.png)
	char attachname[MAX_WADPATH];
	strcpy(attachname, file);
	nameonly(attachname);

	// open file (with full path)
	f = fopen(file, "rb");
	if (!f)
	{
		CONS_Alert(CONS_ERROR, "CreateAttachment: could not open attachment %s for sending\n", attachname);
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	filesize = ftell(f);

	// file too big
	if (filesize > (sizelimit<<20))
	{
		CONS_Alert(CONS_ERROR, "CreateAttachment: attachment exceeds filesize limit of %dMB", sizelimit);
		fclose(f);
		return NULL;
	}

	fseek(f, 0, SEEK_SET);
	filedata = ZZ_Alloc(filesize);
	fread(filedata, 1, filesize, f);
	fclose(f);

	length = postbufsize + (filesize + 1);
	post = ZZ_Alloc(length);
	memset(post, 0x00, length);

	boundary = generate_multipart_request_boundary();

	ret = snprintf(post, postbufsize,
		"POST /1.1/media/upload.json?media_category=%s HTTP/1.1\r\n"
		HEADER_BASE(TW_ENDPOINT_UPLOAD)
		HEADER_ACCEPT_DEFAULT
		AUTH_HEADER
		"Content-Type: multipart/form-data; boundary=%s\r\n"
		"Content-Length: %s\r\n\r\n"
		"--%s\r\n"
		"Content-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\n"
		"Content-Type: application/octet-stream\r\n\r\n",
		type, consumer_key, nonce, signature, timestamp, auth_token,
		boundary, sizeu1(strlen("media=") + filesize),
		boundary, "sshot", attachname);

	if (ret < 0 || (size_t)ret >= postbufsize)
	{
		CONS_Alert(CONS_ERROR, "CreateAttachment: too large!");
		Z_Free(filedata);
		Twitter_Dealloc(post, length);
		return NULL;
	}

	baselength = strlen(post);
	attachdata = (UINT8 *)(post + baselength);
	memcpy(attachdata, filedata, filesize);

	// total length
	length = (size_t)(baselength + filesize);

	// append last boundary
	endlength = (2 + 2 + strlen(boundary) + 2); // \r\n--(boundary)--\r\n
	end = ZZ_Alloc(endlength + 1);
	snprintf(end, (endlength + 1), "\r\n--%s--\r\n", boundary);

	// copy string
	memcpy(attachdata + filesize, end, endlength);
	(*retlen) = (length + endlength);
	Z_Free(filedata);
	free(end);

	// return POST request
	return post;
}

#define PRINTJSONERROR(err) CONS_Alert(CONS_ERROR, "Twitter_SendAttachment: JSON read error (" err ")\n")

#define JSON_VARS \
	jsmn_parser p; \
	jsmntok_t t[9]; \
	int r, i;\

#define JSON_START_PARSE \
	jsmn_init(&p); \
	r = jsmn_parse(&p, buf, (read_bytes - 1), t, sizeof(t) / sizeof(t[0])); \
	if (r < 1 || t[0].type != JSMN_OBJECT) \
		PRINTJSONERROR("expected object"); \
	else \
	{ \
		char parsed[BUFSIZE]; \
		size_t parselen; \
		for (i = 1; i < r; i++) \
		{ \
			parselen = (t[i + 1].end - t[i + 1].start); \
			memcpy(parsed, buf + t[i + 1].start, parselen); \
			parsed[parselen] = '\0';

#define JSON_END_PARSE \
		} \
	}

static int jsoneq(const char *json, jsmntok_t *tok, const char *s)
{
	if (tok->type == JSMN_STRING
	&& (int)strlen(s) == tok->end - tok->start
	&& strncmp(json + tok->start, s, tok->end - tok->start) == 0)
		return 0;
	return -1;
}

static char *Twitter_SendAttachment(const char *attachment, size_t attachlen)
{
	SSL_CTX *ctx;
	BIO *bio;
	SSL *ssl;

	char buf[1024];
	char *ret = NULL;
	int read_bytes;

	buf[0] = '\0';

	if (Twitter_SetupConnection(&ctx, &bio, &ssl) != 0)
		return NULL;

	strcpy(tw_hostname, TW_ENDPOINT_UPLOAD ":https");
	BIO_set_conn_hostname(bio, tw_hostname);

	if (Twitter_DoConnectionAndHandshake(&ctx, &bio) != 0)
		return NULL;

	BIO_write(bio, attachment, attachlen);
	read_bytes = BIO_read(bio, buf, sizeof(buf) - 1);
	if (read_bytes > 0)
	{
		CONS_Printf("Twitter_SendAttachment: Read %d bytes\n", read_bytes);
		buf[read_bytes] = '\0';
		if (Twitter_HandleHTTPResponse(buf) == 0)
		{
			// Read JSON response
			JSON_VARS

			read_bytes = BIO_read(bio, buf, sizeof(buf) - 1);
			if (read_bytes > 0)
			{
				CONS_Printf("Twitter_SendAttachment: Read %d JSON bytes\n", read_bytes);
				buf[read_bytes] = '\0';

				JSON_START_PARSE
				if (jsoneq(buf, &t[i], "media_id_string") == 0)
				{
					ret = Z_StrDup(parsed);
					break;
				}
				JSON_END_PARSE

				if (ret == NULL)
					PRINTJSONERROR("Could not find the media id");
			}
			else
				CONS_Alert(CONS_ERROR, "Twitter_SendAttachment: Failed to read JSON response!\n");
		}
		else
		{
			// Read JSON error
			read_bytes = BIO_read(bio, buf, sizeof(buf) - 1);
			if (read_bytes > 0)
			{
				CONS_Printf("Twitter_SendAttachment: Read %d bytes\n", read_bytes);
				buf[read_bytes] = '\0';
				CONS_Printf("%s\n", buf);
			}
		}
	}
	else
		CONS_Alert(CONS_ERROR, "Twitter_SendAttachment: Read failed! %s\n", ERR_error_string(ERR_get_error(), NULL));

	BIO_free_all(bio);
	SSL_CTX_free(ctx);

	return ret;
}

//
// Sending tweets
//

static char *CreateTweet(const char *status, const char *media_id, boolean hasmessage,
						 const char *timestamp, const char *nonce,
						 const char *signature, const char *consumer_key,
						 const char *auth_token)
{
	char append[BUFSIZE];
	size_t postbufsize = BUFSIZE;
	char *post = ZZ_Alloc(postbufsize);
	int ret;

	if (media_id)
	{
		if (hasmessage)
			snprintf(append, BUFSIZE, "media_ids=%s&status=%s", media_id, status);
		else
			snprintf(append, BUFSIZE, "media_ids=%s", media_id);
	}
	else
		snprintf(append, BUFSIZE, "status=%s", status);

	ret = snprintf(post, postbufsize,
		"POST /1.1/statuses/update.json?include_entities=true HTTP/1.1\r\n"
		HEADER_BASE(TW_ENDPOINT_API)
		HEADER_ACCEPT_DEFAULT
		HEADER_CONTENT_TYPE_DEFAULT
		AUTH_HEADER
		"Content-Length: %s\r\n\r\n"
		"%s",
		consumer_key, nonce, signature, timestamp, auth_token,
		sizeu1(strlen(append)), append);

	if (ret < 0 || (size_t)ret >= postbufsize)
	{
		Twitter_Dealloc(post, postbufsize);
		return NULL;
	}

	// return POST request
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
static int Twitter_SendTweet(const char *post)
{
	SSL_CTX *ctx;
	BIO *bio;
	SSL *ssl;

	char buf[1024];
	int ret = 0, read_bytes;

	buf[0] = '\0';

	if (Twitter_SetupConnection(&ctx, &bio, &ssl) != 0)
		return -1;

	strcpy(tw_hostname, TW_ENDPOINT_API ":https");
	BIO_set_conn_hostname(bio, tw_hostname);

	if (Twitter_DoConnectionAndHandshake(&ctx, &bio) != 0)
		return -1;

	BIO_puts(bio, post);
	read_bytes = BIO_read(bio, buf, sizeof(buf) - 1);
	if (read_bytes > 0)
	{
		CONS_Printf("Twitter_SendTweet: Read %d bytes\n", read_bytes);
		buf[read_bytes] = '\0';
		if (Twitter_HandleHTTPResponse(buf) == -1)
		{
			// Read JSON error
			read_bytes = BIO_read(bio, buf, sizeof(buf) - 1);
			if (read_bytes > 0)
			{
				CONS_Printf("Twitter_SendTweet: Read %d bytes\n", read_bytes);
				buf[read_bytes] = '\0';
				CONS_Printf("%s\n", buf);
			}
		}
	}
	else
	{
		CONS_Alert(CONS_ERROR, "Twitter_SendTweet: Read failed! %s\n", ERR_error_string(ERR_get_error(), NULL));
		ret = -1;
	}

	BIO_free_all(bio);
	SSL_CTX_free(ctx);

	return ret;
}

static void Twitter_StatusUpdate(const char *message, boolean has_media)
{
	char status[(TW_STATUSLENGTH * 3) + strlen(TW_HASHTAG) + 2];
	char *nonce;
	char *sig;
	char *post, *attachment;
	char *media_id = NULL;

	time_t t;
	char timestamp[11];

	boolean empty = (strlen(message) < 1);

	// Copy tweet
	if (cv_twusehashtag.value)
	{
		size_t msglen = (TW_STATUSLENGTH + strlen(TW_HASHTAG) + 2);
		if (!empty)
			snprintf(status, msglen, "%s %s", message, TW_HASHTAG);
		else
			snprintf(status, msglen, "%s", TW_HASHTAG);
	}
	else if (!empty)
		snprintf(status, (TW_STATUSLENGTH + 1), "%s", message);
	else
		status[0] = ' ';

	// Percent encode tweet
	percent_encode(status);

	// Generate nonce
	nonce = generate_nonce();

	// Get timestamp
	t = time(NULL);
	snprintf(timestamp, sizeof(timestamp), "%s", sizeu1((size_t)t));

	// Compute signature
	sig = compute_signature(timestamp, nonce, status, (!has_media),
							tw_consumer_key, tw_consumer_secret, tw_auth_token, tw_auth_secret);
	if (sig == NULL)
	{
		CONS_Alert(CONS_ERROR, "Twitter_StatusUpdate: could not compute signature\n");
		Z_Free(nonce);
		return;
	}

	// Send last movie / screenshot
	if (cv_twsendmovies.value)
	{
		size_t length;
		attachment = CreateAttachment("tweet_gif", lastmovie, 15, &length, timestamp, nonce, sig, tw_consumer_key, tw_auth_token);
		if (!attachment)
			return;

		media_id = Twitter_SendAttachment(attachment, length);
		if (media_id == NULL)
		{
			Twitter_Dealloc(attachment, length);
			return;
		}
	}
	else if (cv_twsendsshots.value)
	{
		size_t length;
		attachment = CreateAttachment("tweet_image", lastsshot, 5, &length, timestamp, nonce, sig, tw_consumer_key, tw_auth_token);
		if (!attachment)
			return;

		media_id = Twitter_SendAttachment(attachment, length);
		if (media_id == NULL)
		{
			Twitter_Dealloc(attachment, length);
			return;
		}
	}

	// Create tweet
	post = CreateTweet(status, media_id, (!empty), timestamp, nonce, sig, tw_consumer_key, tw_auth_token);
	Twitter_Dealloc(sig, strlen(sig) + 1);
	Twitter_Dealloc(nonce, NONCE_LENGTH + 1);

	if (post == NULL)
	{
		CONS_Alert(CONS_ERROR, "Twitter_StatusUpdate: could not create post\n");
		return;
	}

	Twitter_SendTweet(post);
	Twitter_Dealloc(post, strlen(post) + 1);

	if (media_id)
	{
		Twitter_Dealloc(media_id, strlen(media_id) + 1);

		CV_StealthSetValue(&cv_twsendsshots, 0);
		CV_StealthSetValue(&cv_twsendmovies, 0);

		lastsshot[0] = '\0';
		lastmovie[0] = '\0';
	}
}

static void Command_Twitterupdate_f(void)
{
	size_t msglen = (TW_STATUSLENGTH * 4) + 1;
	char msg[msglen];
	size_t i;
	boolean has_media = false; // (cv_twsendmovies.value || cv_twsendsshots.value);
	boolean needs_text = (!has_media);

	if (needs_text && (COM_Argc() < 2))
	{
		CONS_Printf(M_GetText("tw_statusupdate <message>: post a status update to Twitter\n"));
		return;
	}

	if (!Twitter_LoadAuth())
		return;

	if (COM_Argc() >= 2)
	{
		for (i = 0; i < COM_Argc() - 1; i++)
		{
			if (i > 0)
				strlcat(msg, " ", msglen);
			strlcat(msg, COM_Argv(i + 1), msglen);
		}
	}
	else
		msg[0] = '\0';

	Twitter_StatusUpdate(msg, has_media);
	Twitter_ClearAuth();
}
