/*
 * Communication with ifd handler
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <openct/openct.h>
#include <openct/socket.h>
#include <openct/tlv.h>
#include <openct/error.h>
#include <openct/protocol.h>
#include <openct/pathnames.h>

struct ct_handle {
	ct_socket_t *		sock;
	unsigned int		index;			/* reader index */
	unsigned int		card[OPENCT_MAX_SLOTS];	/* card seq */
	const ct_info_t *	info;
};

static void	ct_args_int(ct_buf_t *, ifd_tag_t, unsigned int);
static void	ct_args_string(ct_buf_t *, ifd_tag_t, const char *);

/*
 * Connect to a reader manager
 */
ct_handle *
ct_reader_connect(unsigned int reader)
{
	const ct_info_t	*info;
	char		path[128];
	ct_handle	*h;
	int		rc;

	if ((rc = ct_status(&info)) < 0
	 || reader > (unsigned int) rc)
		return NULL;

	if (!(h = (ct_handle *) calloc(1, sizeof(*h))))
		return NULL;

	if (!(h->sock = ct_socket_new(512))) {
		free(h);
		return NULL;
	}

	snprintf(path, sizeof(path), "%u", reader);
	if (ct_socket_connect(h->sock, path) < 0) {
		ct_reader_disconnect(h);
		return NULL;
	}

	h->info = info + reader;
	return h;
}

/*
 * Disconnect from reader manager
 */
void
ct_reader_disconnect(ct_handle *h)
{
	if (h->sock)
		ct_socket_close(h->sock);
	memset(h, 0, sizeof(*h));
	free(h);
}

/*
 * Retrieve reader status
 */
int
ct_reader_status(ct_handle *h, ct_info_t *info)
{
	*info = *h->info;
	return 0;
}

/*
 * Get card status
 */
int
ct_card_status(ct_handle *h, unsigned int slot, int *status)
{
	const ct_info_t	*info;
	unsigned int	seq;

	info = h->info;
	if (slot > info->ct_slots) 
		return IFD_ERROR_INVALID_ARG;

	seq = info->ct_card[slot];

	*status = 0;
	if (seq != 0) {
		*status = IFD_CARD_PRESENT;
		if (seq != h->card[slot])
			*status |= IFD_CARD_STATUS_CHANGED;
	}

	h->card[slot] = seq;
	return 0;
}

/*
 * Reset the card - this is the same as "request icc" without parameters
 */
int
ct_card_reset(ct_handle *h, unsigned int slot, void *atr, size_t atr_len)
{
	return ct_card_request(h, slot, 0, NULL, atr, atr_len);
}

int
ct_card_request(ct_handle *h, unsigned int slot,
		unsigned int timeout, const char *message,
		void *atr, size_t atr_len)
{
	ct_tlv_parser_t tlv;
	unsigned char	buffer[256];
	ct_buf_t	args, resp;
	int		rc;

	ct_buf_init(&args, buffer, sizeof(buffer));
	ct_buf_init(&resp, buffer, sizeof(buffer));

	ct_buf_putc(&args, CT_CMD_RESET);
	ct_buf_putc(&args, slot);

	/* Add arguments if given */
	if (timeout)
		ct_args_int(&args, CT_TAG_TIMEOUT, timeout);
	if (message)
		ct_args_string(&args, CT_TAG_MESSAGE, message);

	rc = ct_socket_call(h->sock, &args, &resp);
	if (rc < 0)
		return rc;

	if ((rc = ct_tlv_parse(&tlv, &resp)) < 0)
		return rc;

	/* Get the ATR */
	return ct_tlv_get_bytes(&tlv, CT_TAG_ATR, atr, atr_len);
}

/*
 * Transceive an APDU
 */
int
ct_card_transact(ct_handle *h, unsigned int slot,
			const void *send_data, size_t send_len,
			void *recv_buf, size_t recv_size)
{
	unsigned char	buffer[512];
	ct_buf_t	args, resp;
	int		rc;

	ct_buf_init(&args, buffer, sizeof(buffer));
	ct_buf_init(&resp, recv_buf, recv_size);

	ct_buf_putc(&args, CT_CMD_TRANSACT);
	ct_buf_putc(&args, slot);
	if ((rc = ct_buf_put(&args, send_data, send_len)) < 0)
		return rc;

	rc = ct_socket_call(h->sock, &args, &resp);
	if (rc < 0)
		return rc;

	return ct_buf_avail(&resp);
}

/*
 * Lock/unlock a card
 */
int
ct_card_lock(ct_handle *h, unsigned int slot, int type, ct_lock_handle *res)
{
	ct_tlv_parser_t tlv;
	unsigned char	buffer[256];
	ct_buf_t	args, resp;
	int		rc;

	ct_buf_init(&args, buffer, sizeof(buffer));
	ct_buf_init(&resp, buffer, sizeof(buffer));

	ct_buf_putc(&args, CT_CMD_LOCK);
	ct_buf_putc(&args, slot);

	ct_args_int(&args, CT_TAG_LOCKTYPE, type);

	rc = ct_socket_call(h->sock, &args, &resp);
	if (rc < 0)
		return rc;

	if ((rc = ct_tlv_parse(&tlv, &resp)) < 0)
		return rc;

	if (ct_tlv_get_int(&tlv, CT_TAG_LOCK, res) < 0)
		return IFD_ERROR_GENERIC;

	return 0;
}

int
ct_card_unlock(ct_handle *h, unsigned int slot, ct_lock_handle lock)
{
	unsigned char	buffer[256];
	ct_buf_t	args, resp;

	ct_buf_init(&args, buffer, sizeof(buffer));
	ct_buf_init(&resp, buffer, sizeof(buffer));

	ct_buf_putc(&args, CT_CMD_UNLOCK);
	ct_buf_putc(&args, slot);

	ct_args_int(&args, CT_TAG_LOCK, lock);

	return ct_socket_call(h->sock, &args, &resp);
}

/*
 * Add arguments when calling a resource manager function
 */
void
ct_args_int(ct_buf_t *bp, ifd_tag_t tag, unsigned int value)
{
	ct_tlv_builder_t builder;

	ct_tlv_builder_init(&builder, bp);
	ct_tlv_put_int(&builder, tag, value);
}

void	ct_args_string(ct_buf_t *bp, ifd_tag_t tag, const char *value)
{
	ct_tlv_builder_t builder;

	ct_tlv_builder_init(&builder, bp);
	ct_tlv_put_string(&builder, tag, value);
}
