// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 2015-2016 by Tuomas Lappeteläinen.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  twitter.h
/// \brief Twitter status updates

#ifndef TWITTER_H
#define TWITTER_H

void openssl_init(void);

int TwitterStatusUpdate(const char *message,
					const char *consumer_key, const char *consumer_secret,
					const char *auth_token, const char *auth_secret);

#endif
