/* Copyright (c) 2008-2009, AbiSource Corporation B.V.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of AbiSource Corporation B.V. nor the
 *       names of other contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ABISOURCE CORPORATION B.V. AND OTHER
 * CONTRIBUTORS ''AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL ABISOURCE
 * CORPORATION B.V OR OTHER CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <string>
#include <glib.h>
#include <gio/gio.h>
#include <libsoup/soup.h>
#include "soa_soup.h"
#include "ut_assert.h"

namespace soup_soa {

	namespace {

		bool do_invoke(const std::string& url, const soa::method_invocation& mi, const std::string& ssl_ca_file,
				const boost::function<void (GCancellable*, uint32_t)>& progress_cb, std::string& result)
		{
			GError* error = NULL;

			SoupSession* session = NULL;
			GTlsDatabase* tls_db = NULL;
			if (ssl_ca_file.empty())
			{
				session = soup_session_new();
			}
			else
			{
				tls_db = g_tls_file_database_new(ssl_ca_file.c_str(), &error);
				if (!tls_db)
				{
					UT_DEBUGMSG(("soup_soa::do_invoke: failed to load CA file '%s': %s\n",
								ssl_ca_file.c_str(), error ? error->message : "unknown error"));
					g_clear_error(&error);
					return false;
				}
				session = static_cast<SoupSession*>(g_object_new(SOUP_TYPE_SESSION,
							"tls-database", tls_db, NULL));
				g_object_unref(tls_db);
			}

			std::string soap_msg = mi.str();
			SoupMessage* msg = soup_message_new("POST", url.c_str());
			if (!msg)
			{
				g_object_unref(session);
				return false;
			}

			GBytes* body = g_bytes_new(soap_msg.data(), soap_msg.size());
			soup_message_set_request_body_from_bytes(msg, "text/xml", body);
			g_bytes_unref(body);

			GCancellable* cancellable = g_cancellable_new();

			GInputStream* in = soup_session_send(session, msg, cancellable, &error);
			if (!in)
			{
				UT_DEBUGMSG(("soup_soa::do_invoke: request failed: %s\n", error ? error->message : "unknown error"));
				g_clear_error(&error);
				g_object_unref(cancellable);
				g_object_unref(msg);
				g_object_unref(session);
				return false;
			}

			guint status = soup_message_get_status(msg);
			if (!(SOUP_STATUS_IS_SUCCESSFUL(status) ||
				status == SOUP_STATUS_INTERNAL_SERVER_ERROR /* used for SOAP Faults */))
			{
				g_input_stream_close(in, NULL, NULL);
				g_object_unref(in);
				g_object_unref(cancellable);
				g_object_unref(msg);
				g_object_unref(session);
				return false;
			}

			goffset content_length = soup_message_headers_get_content_length(soup_message_get_response_headers(msg));

			char buf[8192];
			gssize n_read;
			bool ok = true;
			while ((n_read = g_input_stream_read(in, buf, sizeof(buf), cancellable, &error)) > 0)
			{
				result.append(buf, n_read);

				if (progress_cb && content_length > 0)
				{
					uint32_t progress = (uint32_t)(((double)result.size() / (double)content_length) * 100);
					if (progress > 100)
						progress = 100;
					progress_cb(cancellable, progress);
				}
			}

			if (n_read < 0)
			{
				UT_DEBUGMSG(("soup_soa::do_invoke: read failed: %s\n", error ? error->message : "unknown error"));
				g_clear_error(&error);
				ok = false;
			}

			g_input_stream_close(in, NULL, NULL);
			g_object_unref(in);
			g_object_unref(cancellable);
			g_object_unref(msg);
			g_object_unref(session);

			return ok;
		}

	}

	/* public functions */

	soa::GenericPtr invoke(const std::string& url, const soa::method_invocation& mi, const std::string& ssl_ca_file) {
		std::string result;
		if (!do_invoke(url, mi, ssl_ca_file, boost::function<void (GCancellable*, uint32_t)>(), result))
			return soa::GenericPtr();
		return soa::parse_response(result, mi.function().response());
	}

	soa::GenericPtr invoke(const std::string& url, const soa::method_invocation& mi, const std::string& ssl_ca_file,
						   boost::function<void (GCancellable*, uint32_t)> progress_cb) {
		std::string result;
		if (!do_invoke(url, mi, ssl_ca_file, progress_cb, result))
			return soa::GenericPtr();
		return soa::parse_response(result, mi.function().response());
	}

	bool invoke(const std::string& url, const soa::method_invocation& mi, const std::string& ssl_ca_file, std::string& result) {
		return do_invoke(url, mi, ssl_ca_file, boost::function<void (GCancellable*, uint32_t)>(), result);
	}

	bool invoke(const std::string& url, const soa::method_invocation& mi, const std::string& ssl_ca_file,
						   boost::function<void (GCancellable*, uint32_t)> progress_cb, std::string& result) {
		return do_invoke(url, mi, ssl_ca_file, progress_cb, result);
	}

}
