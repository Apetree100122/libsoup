/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2008 Red Hat, Inc.
 */

#include "test-utils.h"

static SoupSession *session;
static SoupURI *base_uri;

typedef struct {
	SoupSession *session;
	GBytes *chunks[3];
	int next, nwrote, nfreed;
	gboolean streaming;
} PutTestData;

static void
write_next_chunk (SoupMessage *msg, gpointer user_data)
{
	PutTestData *ptd = user_data;

	debug_printf (2, "  writing chunk %d\n", ptd->next);

	if (ptd->streaming && ptd->next > 0) {
		soup_test_assert (ptd->chunks[ptd->next - 1] == NULL,
				  "next chunk requested before last one freed");
	}

	if (ptd->next < G_N_ELEMENTS (ptd->chunks)) {
		soup_message_body_append_bytes (msg->request_body,
						 ptd->chunks[ptd->next]);
		g_bytes_unref (ptd->chunks[ptd->next]);
		ptd->next++;
	} else
		soup_message_body_complete (msg->request_body);
}

/* This is not a supported part of the API. Use SOUP_MESSAGE_CAN_REBUILD
 * instead.
 */
static void
write_next_chunk_streaming_hack (SoupMessage *msg, gpointer user_data)
{
	PutTestData *ptd = user_data;
	GBytes *chunk;

	debug_printf (2, "  freeing chunk at %d\n", ptd->nfreed);
	chunk = soup_message_body_get_chunk (msg->request_body, ptd->nfreed);
	if (chunk) {
		ptd->nfreed += g_bytes_get_size (chunk);
		soup_message_body_wrote_chunk (msg->request_body, chunk);
		g_bytes_unref (chunk);
	} else {
		soup_test_assert (chunk,
				  "written chunk does not exist");
	}
	write_next_chunk (msg, user_data);
}

static void
wrote_body_data (SoupMessage *msg, GBytes *chunk, gpointer user_data)
{
	PutTestData *ptd = user_data;

	debug_printf (2, "  wrote_body_data, %d bytes\n",
		      (int)g_bytes_get_size (chunk));
	ptd->nwrote += g_bytes_get_size (chunk);
}

static void
clear_buffer_ptr (gpointer data)
{
	GBytes **buffer_ptr = data;

	debug_printf (2, "  clearing chunk\n");
	if (*buffer_ptr) {
                *buffer_ptr = NULL;
	} else {
		soup_test_assert (*buffer_ptr,
				  "chunk is already clear");
	}
}

/* Put a chunk containing @text into *@buffer, set up so that it will
 * clear out *@buffer when the chunk is freed, allowing us to make sure
 * the set_accumulate(FALSE) is working.
 */
static void
make_put_chunk (GBytes **buffer, const char *text)
{
	*buffer = g_bytes_new_with_free_func (g_strdup (text), strlen (text),
					      clear_buffer_ptr, buffer);
}

static void
setup_request_body (PutTestData *ptd)
{
	make_put_chunk (&ptd->chunks[0], "one\r\n");
	make_put_chunk (&ptd->chunks[1], "two\r\n");
	make_put_chunk (&ptd->chunks[2], "three\r\n");
	ptd->next = ptd->nwrote = ptd->nfreed = 0;
}

static void
restarted_streaming (SoupMessage *msg, gpointer user_data)
{
	PutTestData *ptd = user_data;

	debug_printf (2, "  --restarting--\n");

	/* We're streaming, and we had to restart. So the data need
	 * to be regenerated.
	 */
	setup_request_body (ptd);

	/* The 302 redirect will turn it into a GET request and
	 * reset the body encoding back to "NONE". Fix that.
	 */
	soup_message_headers_set_encoding (msg->request_headers,
					   SOUP_ENCODING_CHUNKED);
	msg->method = SOUP_METHOD_PUT;
}

static void
restarted_streaming_hack (SoupMessage *msg, gpointer user_data)
{
	restarted_streaming (msg, user_data);
	soup_message_body_truncate (msg->request_body);
}

typedef enum {
	HACKY_STREAMING  = (1 << 0),
	PROPER_STREAMING = (1 << 1),
	RESTART          = (1 << 2)
} RequestTestFlags;

static void
do_request_test (gconstpointer data)
{
	RequestTestFlags flags = GPOINTER_TO_UINT (data);
	SoupURI *uri;
	PutTestData ptd;
	SoupMessage *msg;
	const char *client_md5, *server_md5;
	GChecksum *check;
	int i, length;

	g_test_skip ("FIXME");
	return;

	if (flags & RESTART)
		uri = soup_uri_new_with_base (base_uri, "/redirect");
	else
		uri = soup_uri_copy (base_uri);

	ptd.session = session;
	setup_request_body (&ptd);
	ptd.streaming = flags & (HACKY_STREAMING | PROPER_STREAMING);

	check = g_checksum_new (G_CHECKSUM_MD5);
	length = 0;
	for (i = 0; i < 3; i++) {
		g_checksum_update (check, (guchar *)g_bytes_get_data (ptd.chunks[i], NULL),
				   g_bytes_get_size (ptd.chunks[i]));
		length += g_bytes_get_size (ptd.chunks[i]);
	}
	client_md5 = g_checksum_get_string (check);

	msg = soup_message_new_from_uri ("PUT", uri);
	soup_message_headers_set_encoding (msg->request_headers, SOUP_ENCODING_CHUNKED);
	soup_message_body_set_accumulate (msg->request_body, FALSE);
	if (flags & HACKY_STREAMING) {
		g_signal_connect (msg, "wrote_chunk",
				  G_CALLBACK (write_next_chunk_streaming_hack), &ptd);
		if (flags & RESTART) {
			g_signal_connect (msg, "restarted",
					  G_CALLBACK (restarted_streaming_hack), &ptd);
		}
	} else {
		g_signal_connect (msg, "wrote_chunk",
				  G_CALLBACK (write_next_chunk), &ptd);
	}

	if (flags & PROPER_STREAMING) {
		soup_message_set_flags (msg, SOUP_MESSAGE_CAN_REBUILD);
		if (flags & RESTART) {
			g_signal_connect (msg, "restarted",
					  G_CALLBACK (restarted_streaming), &ptd);
		}
	}

	g_signal_connect (msg, "wrote_headers",
			  G_CALLBACK (write_next_chunk), &ptd);
	g_signal_connect (msg, "wrote_body_data",
			  G_CALLBACK (wrote_body_data), &ptd);
#if 0
	soup_session_send_message (session, msg);
#endif

	soup_test_assert_message_status (msg, SOUP_STATUS_CREATED);
	g_assert_null (msg->request_body->data);
	g_assert_cmpint (msg->request_body->length, ==, length);
	g_assert_cmpint (length, ==, ptd.nwrote);

	server_md5 = soup_message_headers_get_one (msg->response_headers,
						   "Content-MD5");
	g_assert_cmpstr (client_md5, ==, server_md5);

	g_object_unref (msg);
	g_checksum_free (check);

	soup_uri_free (uri);
}

/* Make sure TEMPORARY buffers are handled properly with non-accumulating
 * message bodies.
 */

static void
temp_test_wrote_chunk (SoupMessage *msg, gpointer session)
{
	GBytes *chunk;

	chunk = soup_message_body_get_chunk (msg->request_body, 5);

	/* When the bug is present, the second chunk will also be
	 * discarded after the first is written, which will cause
	 * the I/O to stall since soup-message-io will think it's
	 * done, but it hasn't written Content-Length bytes yet.
	 */
	if (chunk)
		g_bytes_unref (chunk);
	else {
		soup_test_assert (chunk, "Lost second chunk");
		soup_session_abort (session);
	}

	g_signal_handlers_disconnect_by_func (msg, temp_test_wrote_chunk, session);
}

static void
do_temporary_test (void)
{
	SoupMessage *msg;
	char *client_md5;
	const char *server_md5;

	g_test_skip ("FIXME");
	return;

	g_test_bug_base ("https://bugs.webkit.org/");
	g_test_bug ("18343");

	msg = soup_message_new_from_uri ("PUT", base_uri);
	soup_message_body_append (msg->request_body, SOUP_MEMORY_STATIC,
				  "one\r\n", 5);
	soup_message_body_append (msg->request_body, SOUP_MEMORY_STATIC,
				  "two\r\n", 5);
	soup_message_body_set_accumulate (msg->request_body, FALSE);

	client_md5 = g_compute_checksum_for_string (G_CHECKSUM_MD5,
						    "one\r\ntwo\r\n", 10);
	g_signal_connect (msg, "wrote_chunk",
			  G_CALLBACK (temp_test_wrote_chunk), session);
#if 0
	soup_session_send_message (session, msg);
#endif

	soup_test_assert_message_status (msg, SOUP_STATUS_CREATED);

	server_md5 = soup_message_headers_get_one (msg->response_headers,
						   "Content-MD5");
	g_assert_cmpstr (client_md5, ==, server_md5);

	g_free (client_md5);
	g_object_unref (msg);
}

#define LARGE_CHUNK_SIZE 1000000

typedef struct {
	GBytes *buf;
	gsize offset;
} LargeChunkData;

static void
large_wrote_body_data (SoupMessage *msg, GBytes *chunk, gpointer user_data)
{
	LargeChunkData *lcd = user_data;
        gsize chunk_length;
        const guchar *chunk_data = g_bytes_get_data (chunk, &chunk_length);

	soup_assert_cmpmem (chunk_data, chunk_length,
			    (guchar*)g_bytes_get_data (lcd->buf, NULL) + lcd->offset,
			    chunk_length);
	lcd->offset += chunk_length;
}

static void
do_large_chunk_test (void)
{
	SoupMessage *msg;
	char *buf_data;
	int i;
	LargeChunkData lcd;

	g_test_skip ("FIXME");
	return;

	msg = soup_message_new_from_uri ("PUT", base_uri);

	buf_data = g_malloc0 (LARGE_CHUNK_SIZE);
	for (i = 0; i < LARGE_CHUNK_SIZE; i++)
		buf_data[i] = i & 0xFF;
	lcd.buf = g_bytes_new_take (buf_data, LARGE_CHUNK_SIZE);
	lcd.offset = 0;
	soup_message_body_append_bytes (msg->request_body, lcd.buf);
	soup_message_body_set_accumulate (msg->request_body, FALSE);

	g_signal_connect (msg, "wrote_body_data",
			  G_CALLBACK (large_wrote_body_data), &lcd);
#if 0
	soup_session_send_message (session, msg);
#endif

	soup_test_assert_message_status (msg, SOUP_STATUS_CREATED);

	g_bytes_unref (lcd.buf);
	g_object_unref (msg);
}

static void
server_callback (SoupServer *server, SoupMessage *msg,
		 const char *path, GHashTable *query,
		 SoupClientContext *context, gpointer data)
{
	SoupMessageBody *md5_body;
	char *md5;

	if (g_str_has_prefix (path, "/redirect")) {
		soup_message_set_redirect (msg, SOUP_STATUS_FOUND, "/");
		return;
	}

	if (msg->method == SOUP_METHOD_GET) {
		soup_message_set_response (msg, "text/plain",
					   SOUP_MEMORY_STATIC,
					   "three\r\ntwo\r\none\r\n",
					   strlen ("three\r\ntwo\r\none\r\n"));
		g_bytes_unref (soup_message_body_flatten (msg->response_body));
		md5_body = msg->response_body;
		soup_message_set_status (msg, SOUP_STATUS_OK);
	} else if (msg->method == SOUP_METHOD_PUT) {
		soup_message_set_status (msg, SOUP_STATUS_CREATED);
		md5_body = msg->request_body;
	} else {
		soup_message_set_status (msg, SOUP_STATUS_METHOD_NOT_ALLOWED);
		return;
	}

	md5 = g_compute_checksum_for_data (G_CHECKSUM_MD5,
					   (guchar *)md5_body->data,
					   md5_body->length);
	soup_message_headers_append (msg->response_headers,
				     "Content-MD5", md5);
	g_free (md5);
}

int
main (int argc, char **argv)
{
	GMainLoop *loop;
	SoupServer *server;
	int ret;

	test_init (argc, argv, NULL);

	server = soup_test_server_new (SOUP_TEST_SERVER_IN_THREAD);
	soup_server_add_handler (server, NULL,
				 server_callback, NULL, NULL);

	loop = g_main_loop_new (NULL, TRUE);

	base_uri = soup_test_server_get_uri (server, "http", NULL);
	session = soup_test_session_new (SOUP_TYPE_SESSION, NULL);

	g_test_add_data_func ("/chunks/request/unstreamed", GINT_TO_POINTER (0), do_request_test);
	g_test_add_data_func ("/chunks/request/proper-streaming", GINT_TO_POINTER (PROPER_STREAMING), do_request_test);
	g_test_add_data_func ("/chunks/request/proper-streaming/restart", GINT_TO_POINTER (PROPER_STREAMING | RESTART), do_request_test);
	g_test_add_data_func ("/chunks/request/hacky-streaming", GINT_TO_POINTER (HACKY_STREAMING), do_request_test);
	g_test_add_data_func ("/chunks/request/hacky-streaming/restart", GINT_TO_POINTER (HACKY_STREAMING | RESTART), do_request_test);
	g_test_add_func ("/chunks/temporary", do_temporary_test);
	g_test_add_func ("/chunks/large", do_large_chunk_test);

	ret = g_test_run ();

	soup_test_session_abort_unref (session);

	soup_uri_free (base_uri);

	g_main_loop_unref (loop);
	soup_test_server_quit_unref (server);

	test_cleanup ();
	return ret;
}
