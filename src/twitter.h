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

#define TW_HASHTAG             "#srb2"    // Tweet hashtag
#define TW_STATUSLENGTH        280        // 280 characters in a Tweet

#define TW_APIKEYLENGTH        25         // API key length
#define TW_APISECRETLENGTH     50         // API secret length
#define TW_AUTHTOKENLENGTH     50         // Auth token length
#define TW_AUTHSECRETLENGTH    45         // Auth secret length

void Twitter_Init(void);

void Twitter_StatusUpdate(const char *message);

#endif
