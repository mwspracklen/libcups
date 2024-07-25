//
// IPP utilities for CUPS.
//
// Copyright © 2021-2024 by OpenPrinting.
// Copyright © 2007-2018 by Apple Inc.
// Copyright © 1997-2007 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "cups-private.h"
#include <fcntl.h>
#include <sys/stat.h>
#if defined(_WIN32) || defined(__EMX__)
#  include <io.h>
#else
#  include <unistd.h>
#endif // _WIN32 || __EMX__
#ifndef O_BINARY
#  define O_BINARY 0
#endif // O_BINARY
#ifndef MSG_DONTWAIT
#  define MSG_DONTWAIT 0
#endif // !MSG_DONTWAIT


//
// 'cupsDoFileRequest()' - Do an IPP request with a file.
//
// This function sends the IPP request and attached file to the specified
// server, retrying and authenticating as necessary.  The request is freed with
// @link ippDelete@.
//

ipp_t *					// O - Response data
cupsDoFileRequest(http_t     *http,	// I - Connection to server or `CUPS_HTTP_DEFAULT`
                  ipp_t      *request,	// I - IPP request
                  const char *resource,	// I - HTTP resource for POST
		  const char *filename)	// I - File to send or `NULL` for none
{
  ipp_t		*response;		// IPP response data
  int		infile;			// Input file


  DEBUG_printf("cupsDoFileRequest(http=%p, request=%p(%s), resource=\"%s\", filename=\"%s\")", (void *)http, (void *)request, request ? ippOpString(request->request.op.operation_id) : "?", resource, filename);

  if (filename)
  {
    if ((infile = open(filename, O_RDONLY | O_BINARY)) < 0)
    {
      // Can't get file information!
      _cupsSetError(errno == ENOENT ? IPP_STATUS_ERROR_NOT_FOUND : IPP_STATUS_ERROR_NOT_AUTHORIZED, NULL, false);

      ippDelete(request);

      return (NULL);
    }
  }
  else
  {
    infile = -1;
  }

  response = cupsDoIORequest(http, request, resource, infile, -1);

  if (infile >= 0)
    close(infile);

  return (response);
}


//
// 'cupsDoIORequest()' - Do an IPP request with file descriptors.
//
// This function sends the IPP request with the optional input file "infile" to
// the specified server, retrying and authenticating as necessary.  The request
// is freed with @link ippDelete@.
//
// If "infile" is a valid file descriptor, @code cupsDoIORequest@ copies
// all of the data from the file after the IPP request message.
//
// If "outfile" is a valid file descriptor, @code cupsDoIORequest@ copies
// all of the data after the IPP response message to the file.
//

ipp_t *					// O - Response data
cupsDoIORequest(http_t     *http,	// I - Connection to server or `CUPS_HTTP_DEFAULT`
                ipp_t      *request,	// I - IPP request
                const char *resource,	// I - HTTP resource for POST
		int        infile,	// I - File to read from or `-1` for none
		int        outfile)	// I - File to write to or `-1` for none
{
  ipp_t		*response = NULL;	// IPP response data
  size_t	length = 0;		// Content-Length value
  http_status_t	status;			// Status of HTTP request
  struct stat	fileinfo;		// File information
  ssize_t	bytes;			// Number of bytes read/written
  char		buffer[32768];		// Output buffer


  DEBUG_printf("cupsDoIORequest(http=%p, request=%p(%s), resource=\"%s\", infile=%d, outfile=%d)", (void *)http, (void *)request, request ? ippOpString(request->request.op.operation_id) : "?", resource, infile, outfile);

  // Range check input...
  if (!request || !resource)
  {
    ippDelete(request);

    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), false);

    return (NULL);
  }

  // Get the default connection as needed...
  if (!http && (http = _cupsConnect()) == NULL)
  {
    ippDelete(request);

    return (NULL);
  }

  // See if we have a file to send...
  if (infile >= 0)
  {
    if (fstat(infile, &fileinfo))
    {
      // Can't get file information!
      _cupsSetError(errno == EBADF ? IPP_STATUS_ERROR_NOT_FOUND : IPP_STATUS_ERROR_NOT_AUTHORIZED, NULL, false);
      ippDelete(request);

      return (NULL);
    }

#ifdef _WIN32
    if (fileinfo.st_mode & _S_IFDIR)
#else
    if (S_ISDIR(fileinfo.st_mode))
#endif // _WIN32
    {
      // Can't send a directory...
      _cupsSetError(IPP_STATUS_ERROR_NOT_POSSIBLE, strerror(EISDIR), false);
      ippDelete(request);

      return (NULL);
    }

#ifndef _WIN32
    if (!S_ISREG(fileinfo.st_mode))
      length = 0;			// Chunk when piping
    else
#endif // !_WIN32
    length = ippGetLength(request) + (size_t)fileinfo.st_size;
  }
  else
  {
    length = ippGetLength(request);
  }

  DEBUG_printf("2cupsDoIORequest: Request length=%ld, total length=%ld", (long)ippGetLength(request), (long)length);

  // Clear any "Local" authentication data since it is probably stale...
  if (http->authstring && !strncmp(http->authstring, "Local ", 6))
    httpSetAuthString(http, NULL, NULL);

  // Loop until we can send the request without authorization problems.
  while (response == NULL)
  {
    DEBUG_puts("2cupsDoIORequest: setup...");

    // Send the request...
    status = cupsSendRequest(http, request, resource, length);

    DEBUG_printf("2cupsDoIORequest: status=%d", status);

    if (status == HTTP_STATUS_CONTINUE && request->state == IPP_STATE_DATA && infile >= 0)
    {
      DEBUG_puts("2cupsDoIORequest: file write...");

      // Send the file with the request...
#ifndef _WIN32
      if (S_ISREG(fileinfo.st_mode))
#endif // _WIN32
      lseek(infile, 0, SEEK_SET);

      while ((bytes = read(infile, buffer, sizeof(buffer))) > 0)
      {
        if ((status = cupsWriteRequestData(http, buffer, (size_t)bytes))
                != HTTP_STATUS_CONTINUE)
	  break;
      }
    }

    // Get the server's response...
    if (status <= HTTP_STATUS_CONTINUE || status == HTTP_STATUS_OK)
    {
      response = cupsGetResponse(http, resource);
      status   = httpGetStatus(http);
    }

    DEBUG_printf("2cupsDoIORequest: status=%d", status);

    if (status == HTTP_STATUS_ERROR || (status >= HTTP_STATUS_BAD_REQUEST && status != HTTP_STATUS_UNAUTHORIZED && status != HTTP_STATUS_UPGRADE_REQUIRED))
    {
      _cupsSetHTTPError(http, status);
      break;
    }

    if (response && outfile >= 0)
    {
      // Write trailing data to file...
      while ((bytes = httpRead(http, buffer, sizeof(buffer))) > 0)
      {
	if (write(outfile, buffer, (size_t)bytes) < bytes)
	  break;
      }
    }

    if (http->state != HTTP_STATE_WAITING)
    {
      // Flush any remaining data...
      httpFlush(http);
    }
  }

  // Delete the original request and return the response...
  ippDelete(request);

  return (response);
}


//
// 'cupsDoRequest()' - Do an IPP request.
//
// This function sends the IPP request to the specified server, retrying
// and authenticating as necessary.  The request is freed with @link ippDelete@.
//

ipp_t *					// O - Response data
cupsDoRequest(http_t     *http,		// I - Connection to server or `CUPS_HTTP_DEFAULT`
              ipp_t      *request,	// I - IPP request
              const char *resource)	// I - HTTP resource for POST
{
  DEBUG_printf("cupsDoRequest(http=%p, request=%p(%s), resource=\"%s\")", (void *)http, (void *)request, request ? ippOpString(request->request.op.operation_id) : "?", resource);

  return (cupsDoIORequest(http, request, resource, -1, -1));
}


//
// 'cupsGetResponse()' - Get a response to an IPP request.
//
// Use this function to get the response for an IPP request sent using
// @link cupsSendRequest@. For requests that return additional data, use
// @link cupsReadResponseData@ after getting a successful response,
// otherwise call @link httpFlush@ to complete the response processing.
//

ipp_t *					// O - Response or `NULL` on HTTP error
cupsGetResponse(http_t     *http,	// I - Connection to server or `CUPS_HTTP_DEFAULT`
                const char *resource)	// I - HTTP resource for POST
{
  http_status_t	status;			// HTTP status
  ipp_state_t	state;			// IPP read state
  ipp_t		*response = NULL;	// IPP response


  DEBUG_printf("cupsGetResponse(http=%p, resource=\"%s\")", (void *)http, resource);
  DEBUG_printf("1cupsGetResponse: http->state=%d", http ? http->state : HTTP_STATE_ERROR);

  // Connect to the default server as needed...
  if (!http)
  {
    _cups_globals_t *cg = _cupsGlobals();
					// Pointer to library globals

    if ((http = cg->http) == NULL)
    {
      _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("No active connection."), true);
      DEBUG_puts("1cupsGetResponse: No active connection - returning NULL.");
      return (NULL);
    }
  }

  if (http->state != HTTP_STATE_POST_RECV && http->state != HTTP_STATE_POST_SEND)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("No request sent."), true);
    DEBUG_puts("1cupsGetResponse: Not in POST state - returning NULL.");
    return (NULL);
  }

  // Check for an unfinished chunked request...
  if (http->data_encoding == HTTP_ENCODING_CHUNKED)
  {
    // Send a 0-length chunk to finish off the request...
    DEBUG_puts("2cupsGetResponse: Finishing chunked POST...");

    if (httpWrite(http, "", 0) < 0)
    {
      _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to finish request."), true);
      return (NULL);
    }
  }

  // Wait for a response from the server...
  DEBUG_printf("2cupsGetResponse: Update loop, http->status=%d...", http->status);

  do
  {
    status = httpUpdate(http);
  }
  while (status == HTTP_STATUS_CONTINUE);

  DEBUG_printf("2cupsGetResponse: status=%d", status);

  if (status == HTTP_STATUS_OK)
  {
    // Get the IPP response...
    response = ippNew();

    while ((state = ippRead(http, response)) != IPP_STATE_DATA)
    {
      if (state == IPP_STATE_ERROR)
	break;
    }

    if (state == IPP_STATE_ERROR)
    {
      // Flush remaining data and delete the response...
      _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to read response."), true);
      DEBUG_puts("1cupsGetResponse: IPP read error!");

      httpFlush(http);

      ippDelete(response);
      response = NULL;

      http->status = HTTP_STATUS_ERROR;
      http->error  = EINVAL;
    }
  }
  else if (status != HTTP_STATUS_ERROR)
  {
    // Flush any error message...
    httpFlush(http);

    _cupsSetHTTPError(http, status);

    // Then handle encryption and authentication...
    if (status == HTTP_STATUS_UNAUTHORIZED)
    {
      // See if we can do authentication...
      DEBUG_puts("2cupsGetResponse: Need authorization...");

      if (cupsDoAuthentication(http, "POST", resource))
      {
        if (!httpConnectAgain(http, 30000, NULL))
          http->status = HTTP_STATUS_ERROR;
      }
      else
      {
        http->status = HTTP_STATUS_CUPS_AUTHORIZATION_CANCELED;
      }
    }
    else if (status == HTTP_STATUS_UPGRADE_REQUIRED)
    {
      // Force a reconnect with encryption...
      DEBUG_puts("2cupsGetResponse: Need encryption...");

      if (httpConnectAgain(http, 30000, NULL))
        httpSetEncryption(http, HTTP_ENCRYPTION_REQUIRED);
    }
  }

  if (response)
  {
    ipp_attribute_t	*attr;		// status-message attribute

    attr = ippFindAttribute(response, "status-message", IPP_TAG_TEXT);

    DEBUG_printf("1cupsGetResponse: status-code=%s, status-message=\"%s\"", ippErrorString(response->request.status.status_code), attr ? attr->values[0].string.text : "");

    _cupsSetError(response->request.status.status_code, attr ? attr->values[0].string.text : ippErrorString(response->request.status.status_code), false);
  }

  return (response);
}


//
// 'cupsGetError()' - Return the last IPP status code received on the current
//                     thread.
//

ipp_status_t				// O - IPP status code from last request
cupsGetError(void)
{
  return (_cupsGlobals()->last_error);
}


//
// 'cupsGetErrorString()' - Return the last IPP status-message received on the
//                           current thread.
//

const char *				// O - "status-message" text from last request
cupsGetErrorString(void)
{
  return (_cupsGlobals()->last_status_message);
}


//
// 'cupsReadResponseData()' - Read additional data after the IPP response.
//
// This function is used after @link cupsGetResponse@ to read any trailing
// document data after an IPP response.
//

ssize_t					// O - Bytes read, 0 on EOF, -1 on error
cupsReadResponseData(
    http_t *http,			// I - Connection to server or `CUPS_HTTP_DEFAULT`
    char   *buffer,			// I - Buffer to use
    size_t length)			// I - Number of bytes to read
{
  // Get the default connection as needed...
  DEBUG_printf("cupsReadResponseData(http=%p, buffer=%p, length=" CUPS_LLFMT ")", (void *)http, (void *)buffer, CUPS_LLCAST length);

  if (!http)
  {
    _cups_globals_t *cg = _cupsGlobals();
					// Pointer to library globals

    if ((http = cg->http) == NULL)
    {
      _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("No active connection"), true);
      return (-1);
    }
  }

  // Then read from the HTTP connection...
  return (httpRead(http, buffer, length));
}


//
// 'cupsSendRequest()' - Send an IPP request.
//
// Use @link cupsWriteRequestData@ to write any additional data (document, etc.)
// for the request, @link cupsGetResponse@ to get the IPP response, and
// @link cupsReadResponseData@ to read any additional data following the
// response. Only one request can be sent/queued at a time per `http_t`
// connection.
//
// Returns the initial HTTP status code, which will be `HTTP_STATUS_CONTINUE`
// on a successful send of the request.
//
// Note: Unlike @link cupsDoFileRequest@, @link cupsDoIORequest@, and
// @link cupsDoRequest@, the request is NOT freed with @link ippDelete@.
//

http_status_t				// O - Initial HTTP status
cupsSendRequest(http_t     *http,	// I - Connection to server or `CUPS_HTTP_DEFAULT`
                ipp_t      *request,	// I - IPP request
                const char *resource,	// I - Resource path
		size_t     length)	// I - Length of data to follow or `CUPS_LENGTH_VARIABLE`
{
  http_status_t		status;		// Status of HTTP request
  bool			got_status;	// Did we get the status?
  ipp_state_t		state;		// State of IPP processing
  http_status_t		expect;		// Expect: header to use
  char			date[256];	// Date: header value
  int			digest;		// Are we using Digest authentication?


  DEBUG_printf("cupsSendRequest(http=%p, request=%p(%s), resource=\"%s\", length=" CUPS_LLFMT ")", (void *)http, (void *)request, request ? ippOpString(request->request.op.operation_id) : "?", resource, CUPS_LLCAST length);

  // Range check input...
  if (!request || !resource)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), false);

    return (HTTP_STATUS_ERROR);
  }

  // Get the default connection as needed...
  if (!http && (http = _cupsConnect()) == NULL)
    return (HTTP_STATUS_SERVICE_UNAVAILABLE);

  // If the prior request was not flushed out, do so now...
  if (http->state == HTTP_STATE_GET_SEND || http->state == HTTP_STATE_POST_SEND)
  {
    DEBUG_puts("2cupsSendRequest: Flush prior response.");
    httpFlush(http);
  }
  else if (http->state != HTTP_STATE_WAITING)
  {
    DEBUG_printf("1cupsSendRequest: Unknown HTTP state (%d), reconnecting.", http->state);
    if (!httpConnectAgain(http, 30000, NULL))
      return (HTTP_STATUS_ERROR);
  }

  // See if we have an auth-info attribute and are communicating over
  // a non-local link.  If so, encrypt the link so that we can pass
  // the authentication information securely...
  if (ippFindAttribute(request, "auth-info", IPP_TAG_TEXT) && !httpAddrIsLocalhost(http->hostaddr) && !http->tls && !httpSetEncryption(http, HTTP_ENCRYPTION_REQUIRED))
  {
    DEBUG_puts("1cupsSendRequest: Unable to encrypt connection.");
    return (HTTP_STATUS_SERVICE_UNAVAILABLE);
  }

  // Reconnect if the last response had a "Connection: close"...
  if (!_cups_strcasecmp(httpGetField(http, HTTP_FIELD_CONNECTION), "close"))
  {
    DEBUG_puts("2cupsSendRequest: Connection: close");
    httpClearFields(http);
    if (!httpConnectAgain(http, 30000, NULL))
    {
      DEBUG_puts("1cupsSendRequest: Unable to reconnect.");
      return (HTTP_STATUS_SERVICE_UNAVAILABLE);
    }
  }

  // Loop until we can send the request without authorization problems.
  expect = HTTP_STATUS_CONTINUE;

  for (;;)
  {
    DEBUG_puts("2cupsSendRequest: Setup...");

    // Setup the HTTP variables needed...
    httpClearFields(http);
    httpSetExpect(http, expect);
    httpSetField(http, HTTP_FIELD_CONTENT_TYPE, "application/ipp");
    httpSetField(http, HTTP_FIELD_DATE, httpGetDateString(time(NULL), date, sizeof(date)));
    httpSetLength(http, length);

    digest = http->authstring && !strncmp(http->authstring, "Digest ", 7);

    if (digest)
    {
      // Update the Digest authentication string...
      _httpSetDigestAuthString(http, http->nextnonce, "POST", resource);
    }

    httpSetField(http, HTTP_FIELD_AUTHORIZATION, http->authstring);

    DEBUG_printf("2cupsSendRequest: authstring=\"%s\"", http->authstring);

    // Try the request...
    DEBUG_puts("2cupsSendRequest: Sending HTTP POST...");

    if (!httpWriteRequest(http, "POST", resource))
    {
      DEBUG_puts("2cupsSendRequest: POST failed, reconnecting.");
      if (!httpConnectAgain(http, 30000, NULL))
      {
        DEBUG_puts("1cupsSendRequest: Unable to reconnect.");
        return (HTTP_STATUS_SERVICE_UNAVAILABLE);
      }
      else
      {
        continue;
      }
    }

    // Send the IPP data...
    DEBUG_puts("2cupsSendRequest: Writing IPP request...");

    request->state = IPP_STATE_IDLE;
    status         = HTTP_STATUS_CONTINUE;
    got_status     = false;

    while ((state = ippWrite(http, request)) != IPP_STATE_DATA)
    {
      if (httpWait(http, 0))
      {
        got_status = true;

        _httpUpdate(http, &status);
	if (status >= HTTP_STATUS_MULTIPLE_CHOICES)
	  break;
      }
      else if (state == IPP_STATE_ERROR)
      {
	break;
      }
    }

    if (state == IPP_STATE_ERROR)
    {
      // We weren't able to send the IPP request. But did we already get a HTTP
      // error status?
      if (!got_status || status < HTTP_STATUS_MULTIPLE_CHOICES)
      {
        // No, something else went wrong.
	DEBUG_puts("1cupsSendRequest: Unable to send IPP request.");

	http->status = HTTP_STATUS_ERROR;
	http->state  = HTTP_STATE_WAITING;

	return (HTTP_STATUS_ERROR);
      }
    }

    // Wait up to 1 second to get the 100-continue response as needed...
    if (!got_status || (digest && status == HTTP_STATUS_CONTINUE))
    {
      if (expect == HTTP_STATUS_CONTINUE || digest)
      {
	DEBUG_puts("2cupsSendRequest: Waiting for 100-continue...");

	if (httpWait(http, 1000))
	  _httpUpdate(http, &status);
      }
      else if (httpWait(http, 0))
      {
	_httpUpdate(http, &status);
      }
    }

    DEBUG_printf("2cupsSendRequest: status=%d", status);

    // Process the current HTTP status...
    if (status >= HTTP_STATUS_MULTIPLE_CHOICES)
    {
      int temp_status;			// Temporary status

      _cupsSetHTTPError(http, status);

      do
      {
	temp_status = httpUpdate(http);
      }
      while (temp_status != HTTP_STATUS_ERROR && http->state == HTTP_STATE_POST_RECV);

      httpFlush(http);
    }

    switch (status)
    {
      case HTTP_STATUS_CONTINUE :
      case HTTP_STATUS_OK :
      case HTTP_STATUS_ERROR :
          DEBUG_printf("1cupsSendRequest: Returning %d.", status);
          return (status);

      case HTTP_STATUS_UNAUTHORIZED :
          if (!cupsDoAuthentication(http, "POST", resource))
	  {
            DEBUG_puts("1cupsSendRequest: Returning HTTP_STATUS_CUPS_AUTHORIZATION_CANCELED.");
	    return (HTTP_STATUS_CUPS_AUTHORIZATION_CANCELED);
	  }

          DEBUG_puts("2cupsSendRequest: Reconnecting after HTTP_STATUS_UNAUTHORIZED.");

	  if (!httpConnectAgain(http, 30000, NULL))
	  {
	    DEBUG_puts("1cupsSendRequest: Unable to reconnect.");
	    return (HTTP_STATUS_SERVICE_UNAVAILABLE);
	  }
	  break;

      case HTTP_STATUS_UPGRADE_REQUIRED :
	  // Flush any error message, reconnect, and then upgrade with
	  // encryption...
          DEBUG_puts("2cupsSendRequest: Reconnecting after HTTP_STATUS_UPGRADE_REQUIRED.");

	  if (!httpConnectAgain(http, 30000, NULL))
	  {
	    DEBUG_puts("1cupsSendRequest: Unable to reconnect.");
	    return (HTTP_STATUS_SERVICE_UNAVAILABLE);
	  }

	  DEBUG_puts("2cupsSendRequest: Upgrading to TLS.");
	  if (!httpSetEncryption(http, HTTP_ENCRYPTION_REQUIRED))
	  {
	    DEBUG_puts("1cupsSendRequest: Unable to encrypt connection.");
	    return (HTTP_STATUS_SERVICE_UNAVAILABLE);
	  }
	  break;

      case HTTP_STATUS_EXPECTATION_FAILED :
	  // Don't try using the Expect: header the next time around...
	  expect = HTTP_STATUS_NONE;

          DEBUG_puts("2cupsSendRequest: Reconnecting after HTTP_EXPECTATION_FAILED.");

	  if (!httpConnectAgain(http, 30000, NULL))
	  {
	    DEBUG_puts("1cupsSendRequest: Unable to reconnect.");
	    return (HTTP_STATUS_SERVICE_UNAVAILABLE);
	  }
	  break;

      default :
          // Some other error...
	  return (status);
    }
  }
}


//
// 'cupsWriteRequestData()' - Write additional data after an IPP request.
//
// This function is used after @link cupsSendRequest@ to provide a PPD and
// after @link cupsStartDocument@ to provide a document file.
//

http_status_t				// O - `HTTP_STATUS_CONTINUE` if OK or HTTP status on error
cupsWriteRequestData(
    http_t     *http,			// I - Connection to server or `CUPS_HTTP_DEFAULT`
    const char *buffer,			// I - Bytes to write
    size_t     length)			// I - Number of bytes to write
{
  int	wused;				// Previous bytes in buffer


  // Get the default connection as needed...
  DEBUG_printf("cupsWriteRequestData(http=%p, buffer=%p, length=" CUPS_LLFMT ")", (void *)http, (void *)buffer, CUPS_LLCAST length);

  if (!http)
  {
    _cups_globals_t *cg = _cupsGlobals();
					// Pointer to library globals

    if ((http = cg->http) == NULL)
    {
      _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("No active connection"), true);
      DEBUG_puts("1cupsWriteRequestData: Returning HTTP_STATUS_ERROR.");
      return (HTTP_STATUS_ERROR);
    }
  }

  // Then write to the HTTP connection...
  wused = http->wused;

  if (httpWrite(http, buffer, length) < 0)
  {
    DEBUG_puts("1cupsWriteRequestData: Returning HTTP_STATUS_ERROR.");
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(http->error), false);
    return (HTTP_STATUS_ERROR);
  }

  // Finally, check if we have any pending data from the server...
  if (length >= HTTP_MAX_BUFFER || http->wused < wused || (wused > 0 && (size_t)http->wused == length))
  {
    // We've written something to the server, so check for response data...
    if (_httpWait(http, 0, 1))
    {
      http_status_t	status;		// Status from _httpUpdate

      _httpUpdate(http, &status);
      if (status >= HTTP_STATUS_MULTIPLE_CHOICES)
      {
        _cupsSetHTTPError(http, status);

	do
	{
	  status = httpUpdate(http);
	}
	while (status != HTTP_STATUS_ERROR && http->state == HTTP_STATE_POST_RECV);

        httpFlush(http);
      }

      DEBUG_printf("1cupsWriteRequestData: Returning %d.\n", status);
      return (status);
    }
  }

  DEBUG_puts("1cupsWriteRequestData: Returning HTTP_STATUS_CONTINUE.");
  return (HTTP_STATUS_CONTINUE);
}


//
// '_cupsConnect()' - Get the default server connection...
//

http_t *				// O - HTTP connection
_cupsConnect(void)
{
  _cups_globals_t *cg = _cupsGlobals();	// Pointer to library globals


  // See if we are connected to the same server...
  if (cg->http)
  {
    // Compare the connection hostname, port, and encryption settings to
    // the cached defaults; these were initialized the first time we
    // connected...
    if (strcmp(cg->http->hostname, cg->server) ||
#ifdef AF_LOCAL
        (httpAddrGetFamily(cg->http->hostaddr) != AF_LOCAL && cg->ipp_port != httpAddrGetPort(cg->http->hostaddr)) ||
#else
        cg->ipp_port != httpAddrGetPort(cg->http->hostaddr) ||
#endif // AF_LOCAL
        (cg->http->encryption != cg->encryption &&
	 cg->http->encryption == HTTP_ENCRYPTION_NEVER))
    {
      // Need to close the current connection because something has changed...
      httpClose(cg->http);
      cg->http = NULL;
    }
    else
    {
      // Same server, see if the connection is still established...
      char	ch;			// Connection check byte
      ssize_t	n;			// Number of bytes

#ifdef _WIN32
      if ((n = recv(cg->http->fd, &ch, 1, MSG_PEEK)) == 0 ||
          (n < 0 && WSAGetLastError() != WSAEWOULDBLOCK))
#else
      if ((n = recv(cg->http->fd, &ch, 1, MSG_PEEK | MSG_DONTWAIT)) == 0 ||
          (n < 0 && errno != EWOULDBLOCK))
#endif // _WIN32
      {
        // Nope, close the connection...
	httpClose(cg->http);
	cg->http = NULL;
      }
    }
  }

  // (Re)connect as needed...
  if (!cg->http)
  {
    if ((cg->http = httpConnect(cupsGetServer(), ippGetPort(), NULL, AF_UNSPEC, cupsGetEncryption(), 1, 30000, NULL)) == NULL)
    {
      if (errno)
        _cupsSetError(IPP_STATUS_ERROR_SERVICE_UNAVAILABLE, NULL, false);
      else
        _cupsSetError(IPP_STATUS_ERROR_SERVICE_UNAVAILABLE, _("Unable to connect to host."), true);
    }
  }

  // Return the cached connection...
  return (cg->http);
}


//
// '_cupsSetError()' - Set the last IPP status code and status-message.
//

void
_cupsSetError(ipp_status_t status,	// I - IPP status code
              const char   *message,	// I - status-message value
	      bool         localize)	// I - Localize the message?
{
  _cups_globals_t	*cg;		// Global data


  if (!message && errno)
  {
    message  = strerror(errno);
    localize = 0;
  }

  cg             = _cupsGlobals();
  cg->last_error = status;

  if (cg->last_status_message)
  {
    _cupsStrFree(cg->last_status_message);

    cg->last_status_message = NULL;
  }

  if (message)
  {
    if (localize)
    {
      // Get the message catalog...
      if (!cg->lang_default)
	cg->lang_default = cupsLangDefault();

      cg->last_status_message = _cupsStrAlloc(cupsLangGetString(cg->lang_default, message));
    }
    else
    {
      cg->last_status_message = _cupsStrAlloc(message);
    }
  }

  DEBUG_printf("4_cupsSetError: last_error=%s, last_status_message=\"%s\"", ippErrorString(cg->last_error), cg->last_status_message);
}


//
// '_cupsSetHTTPError()' - Set the last error using the HTTP status.
//

void
_cupsSetHTTPError(http_t        *http,	// I - HTTP connection
                  http_status_t status)	// I - HTTP status code
{
  switch (status)
  {
    case HTTP_STATUS_NOT_FOUND :
	_cupsSetError(IPP_STATUS_ERROR_NOT_FOUND, httpStatusString(status), false);
	break;

    case HTTP_STATUS_NOT_MODIFIED :
	_cupsSetError(IPP_STATUS_OK_EVENTS_COMPLETE, httpStatusString(status), false);
	break;

    case HTTP_STATUS_UNAUTHORIZED :
	_cupsSetError(IPP_STATUS_ERROR_NOT_AUTHENTICATED, httpStatusString(status), false);
	break;

    case HTTP_STATUS_CUPS_AUTHORIZATION_CANCELED :
	_cupsSetError(IPP_STATUS_ERROR_CUPS_AUTHENTICATION_CANCELED, httpStatusString(status), false);
	break;

    case HTTP_STATUS_FORBIDDEN :
	_cupsSetError(IPP_STATUS_ERROR_FORBIDDEN, httpStatusString(status), false);
	break;

    case HTTP_STATUS_BAD_REQUEST :
	_cupsSetError(IPP_STATUS_ERROR_BAD_REQUEST, httpStatusString(status), false);
	break;

    case HTTP_STATUS_CONTENT_TOO_LARGE :
	_cupsSetError(IPP_STATUS_ERROR_REQUEST_VALUE, httpStatusString(status), false);
	break;

    case HTTP_STATUS_NOT_IMPLEMENTED :
	_cupsSetError(IPP_STATUS_ERROR_OPERATION_NOT_SUPPORTED, httpStatusString(status), false);
	break;

    case HTTP_STATUS_HTTP_VERSION_NOT_SUPPORTED :
	_cupsSetError(IPP_STATUS_ERROR_VERSION_NOT_SUPPORTED, httpStatusString(status), false);
	break;

    case HTTP_STATUS_UPGRADE_REQUIRED :
	_cupsSetError(IPP_STATUS_ERROR_CUPS_UPGRADE_REQUIRED, httpStatusString(status), false);
        break;

    case HTTP_STATUS_CUPS_PKI_ERROR :
	_cupsSetError(IPP_STATUS_ERROR_CUPS_PKI, httpStatusString(status), false);
        break;

    case HTTP_STATUS_ERROR :
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(http->error), false);
        break;

    default :
        if ((int)status >= 300)
        {
	  DEBUG_printf("4_cupsSetHTTPError: HTTP error %d mapped to IPP_STATUS_ERROR_SERVICE_UNAVAILABLE!", status);
	  _cupsSetError(IPP_STATUS_ERROR_SERVICE_UNAVAILABLE, httpStatusString(status), false);
	}
	break;
  }
}
