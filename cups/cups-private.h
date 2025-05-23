//
// Private definitions for CUPS.
//
// Copyright © 2021-2025 by OpenPrinting.
// Copyright © 2007-2019 by Apple Inc.
// Copyright © 1997-2007 by Easy Software Products, all rights reserved.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef _CUPS_CUPS_PRIVATE_H_
#  define _CUPS_CUPS_PRIVATE_H_
#  include "string-private.h"
#  include "debug-internal.h"
#  include "array.h"
#  include "ipp-private.h"
#  include "http-private.h"
#  include "language.h"
#  include "pwg-private.h"
#  include "thread.h"
#  include "cups.h"
#  ifdef __APPLE__
#    include <sys/cdefs.h>
#    include <CoreFoundation/CoreFoundation.h>
#  endif // __APPLE__
#  ifdef __cplusplus
extern "C" {
#  endif // __cplusplus
#  ifdef _WIN32
typedef int mode_t;			// Windows doesn't support mode_t type @private@
#  endif // _WIN32
#  include <regex.h>


//
// Macro for localized text...
//

#  define _(x) x


//
// Types...
//

typedef struct _cups_buffer_s		// Read/write buffer
{
  struct _cups_buffer_s	*next;		// Next buffer in list
  size_t		size;		// Size of buffer
  char			used,		// Is this buffer used?
			d[1];		// Data buffer
} _cups_buffer_t;

typedef struct _cups_raster_error_s	// Error buffer structure
{
  char	*start,				// Start of buffer
	*current,			// Current position in buffer
	*end;				// End of buffer
} _cups_raster_error_t;

typedef enum _cups_digestoptions_e	// Digest Options values
{
  _CUPS_DIGESTOPTIONS_NONE,		// No Digest authentication options
  _CUPS_DIGESTOPTIONS_DENYMD5		// Do not use MD5 hashes for digest
} _cups_digestoptions_t;

typedef enum _cups_uatokens_e		// UserAgentTokens values
{
  _CUPS_UATOKENS_NONE,			// Do not send User-Agent
  _CUPS_UATOKENS_PRODUCT_ONLY,		// CUPS IPP
  _CUPS_UATOKENS_MAJOR,			// CUPS/major IPP/2
  _CUPS_UATOKENS_MINOR,			// CUPS/major.minor IPP/2.1
  _CUPS_UATOKENS_MINIMAL,		// CUPS/major.minor.patch IPP/2.1
  _CUPS_UATOKENS_OS,			// CUPS/major.minor.patch (osname osversion) IPP/2.1
  _CUPS_UATOKENS_FULL			// CUPS/major.minor.patch (osname osversion; architecture) IPP/2.1
} _cups_uatokens_t;

typedef struct _cups_globals_s		// CUPS global state data
{
  // Multiple places...
  const char		*datadir,	// Data directory (CUPS_DATADIR environment var)
			*sysconfig;	// System config files (CUPS_SERVERROOT environment var)
  char			*userconfig;	// User-specific config files
#ifndef _WIN32
#define PW_BUF_SIZE 16384		// As per glibc manual page
  char			pw_buf[PW_BUF_SIZE];
					// Big buffer for struct passwd buffers
#endif

  // debug.c
#  ifdef DEBUG
  int			thread_id;	// Friendly thread ID
#  endif // DEBUG

  // file.c
  cups_file_t		*stdio_files[3];// stdin, stdout, stderr

  // http.c
  char			http_date[256];	// Date+time buffer

  // http-addr.c
  unsigned		ip_addr;	// Packed IPv4 address
  char			*ip_ptrs[2];	// Pointer to packed address
  struct hostent	hostent;	// Host entry for IP address
  char			hostname[1024];	// Hostname
  int			need_res_init;	// Need to reinitialize resolver?

  // http-support.c
  char			http_status[256];
					// Unknown HTTP statuses

  // ipp.c
  ipp_uchar_t		ipp_date[11];	// RFC-2579 date/time data
  _cups_buffer_t	*cups_buffers;	// Buffer list

  // ipp-support.c
  int			ipp_port;	// IPP port number
  char			ipp_unknown[255];
					// Unknown error statuses

  // lang*.c
  cups_lang_t		*lang_default;	// Default (current) language
  cups_encoding_t	lang_encoding;	// Current encoding
  char			lang_name[32];	// Current language name

  // pwg-media.c
  cups_array_t		*leg_size_lut,	// Lookup table for legacy names
			*ppd_size_lut,	// Lookup table for PPD names
			*pwg_size_lut;	// Lookup table for PWG names
  pwg_media_t		pwg_media;	// PWG media data for custom size
  char			pwg_name[65],	// PWG media name for custom size
			ppd_name[41];	// PPD media name for custom size

  // raster-error.c
  _cups_raster_error_t	raster_error;	// Raster error information

  // request.c
  http_t		*http;		// Current server connection
  ipp_status_t		last_error;	// Last IPP error
  char			*last_status_message;
					// Last IPP status-message

  // tempfile.c
  char			tempfile[1024];	// cupsTempFd/File buffer

  // usersys.c
  bool			client_conf_loaded;
					// Has client.conf been loaded?
  _cups_digestoptions_t	digestoptions;	// DigestOptions setting
  _cups_uatokens_t	uatokens;	// UserAgentTokens setting
  http_encryption_t	encryption;	// Encryption setting
  char			user[65],	// User name
			user_agent[256],// User-Agent string
			server[256],	// Server address
			servername[256],// Server hostname
			password[128];	// Password for default callback
  cups_oauth_cb_t	oauth_cb;	// OAuth callback
  void			*oauth_data;	// OAuth user data
  cups_password_cb_t	password_cb;	// Password callback
  void			*password_data;	// Password user data
  _http_tls_credentials_t *credentials;	// Default client TLS credentials, if any
  int			server_version,	// Server IPP version
			trust_first,	// Trust on first use?
			any_root,	// Allow any (e.g., self-signed) root
			expired_certs,	// Allow expired certs
			validate_certs;	// Validate certificates

  cups_array_t		*browse_domains;// BrowseDomains list
  cups_array_t		*filter_location_array;
					// FilterLocation list
  regex_t		*filter_location_regex;
					// FilterLocation regular expression
  cups_ptype_t		filter_type;	// FilterType values from client.conf
  cups_ptype_t		filter_type_mask;
					// FilterType mask

  // util.c
  char			def_printer[256];
					// Default printer
} _cups_globals_t;

typedef struct _cups_media_db_s		// Media database
{
  char		*color,			// Media color, if any
		*key,			// Media key, if any
		*info,			// Media human-readable name, if any
		*size_name,		// Media PWG size name, if provided
		*source,		// Media source, if any
		*type;			// Media type, if any
  int		width,			// Width in hundredths of millimeters
		length,			// Length in hundredths of millimeters
		bottom,			// Bottom margin in hundredths of millimeters
		left,			// Left margin in hundredths of  millimeters
		right,			// Right margin in hundredths of millimeters
		top;			// Top margin in hundredths of millimeters
} _cups_media_db_t;

typedef struct _cups_dconstres_s	// Constraint/resolver
{
  char	*name;				// Name of resolver
  ipp_t	*collection;			// Collection containing attrs
} _cups_dconstres_t;

struct _cups_dinfo_s			// Destination capability and status information
{
  int			version;	// IPP version
  const char		*uri;		// Printer URI
  char			*resource;	// Resource path
  ipp_t			*attrs;		// Printer attributes
  size_t		num_defaults;	// Number of default options
  cups_option_t		*defaults;	// Default options
  cups_array_t		*constraints;	// Job constraints
  cups_array_t		*resolvers;	// Job resolvers
  bool			localizations;	// Localization information loaded?
  cups_array_t		*media_db;	// Media database
  _cups_media_db_t	min_size,	// Minimum size
			max_size;	// Maximum size
  unsigned		cached_flags;	// Flags used for cached media
  cups_array_t		*cached_db;	// Cache of media from last index/default
  time_t		ready_time;	// When xxx-ready attributes were last queried
  ipp_t			*ready_attrs;	// xxx-ready attributes
  cups_array_t		*ready_db;	// media[-col]-ready media database
};


//
// Prototypes...
//

#  ifdef __APPLE__
extern CFStringRef	_cupsAppleCopyDefaultPaperID(void) _CUPS_PRIVATE;
extern CFStringRef	_cupsAppleCopyDefaultPrinter(void) _CUPS_PRIVATE;
extern bool		_cupsAppleGetUseLastPrinter(void) _CUPS_PRIVATE;
extern void		_cupsAppleSetDefaultPaperID(CFStringRef name) _CUPS_PRIVATE;
extern void		_cupsAppleSetDefaultPrinter(CFStringRef name) _CUPS_PRIVATE;
extern void		_cupsAppleSetUseLastPrinter(int uselast) _CUPS_PRIVATE;
#  endif // __APPLE__
extern char		*_cupsBufferGet(size_t size) _CUPS_PRIVATE;
extern void		_cupsBufferRelease(char *b) _CUPS_PRIVATE;
extern http_t		*_cupsConnect(void) _CUPS_PRIVATE;
extern char		*_cupsCreateDest(const char *name, const char *info, const char *device_id, const char *device_uri, char *uri, size_t urisize) _CUPS_PRIVATE;
extern bool		_cupsDirCreate(const char *path, mode_t mode) _CUPS_PRIVATE;
extern ipp_attribute_t	*_cupsEncodeOption(ipp_t *ipp, ipp_tag_t group_tag, _ipp_option_t *map, const char *name, const char *value) _CUPS_PRIVATE;
extern const char	*_cupsGetDestResource(cups_dest_t *dest, unsigned flags, char *resource, size_t resourcesize) _CUPS_PRIVATE;
extern size_t		_cupsGetDests(http_t *http, ipp_op_t op, const char *name, cups_dest_t **dests, cups_ptype_t type, cups_ptype_t mask) _CUPS_PRIVATE;
extern const char	*_cupsGetPassword(const char *prompt) _CUPS_PRIVATE;
extern char		*_cupsGetUserDefault(char *name, size_t namesize) _CUPS_INTERNAL;
extern void		_cupsGlobalLock(void) _CUPS_PRIVATE;
extern void		_cupsGlobalUnlock(void) _CUPS_PRIVATE;
extern _cups_globals_t	*_cupsGlobals(void) _CUPS_PRIVATE;
extern void		_cupsSetDefaults(void) _CUPS_INTERNAL;
extern void		_cupsSetError(ipp_status_t status, const char *message, bool localize) _CUPS_PRIVATE;
extern void		_cupsSetHTTPError(http_t *http, http_status_t status) _CUPS_INTERNAL;


#  ifdef __cplusplus
}
#  endif // __cplusplus
#endif // !_CUPS_CUPS_PRIVATE_H_
