#include <string.h>	// memcpy
#include <errno.h>	// EBADF

#include "LiteLib.h"
#include "ZLCVFS.h"
#include "ZSyntheticFileRequest.h"
#include "ValidFileResult.h"
//#include "discovery_client.h"
#include "SyncFactory_Zutex.h"
#include "SocketFactory_Skinny.h"
#include "cheesy_thread.h"
#include "PatchPogoFilter.h"
#include "zftp_dir_format.h"
#include "ZLCEmitXdt.h"
#include "ThreadFactory_Cheesy.h"
#include "SendBufferFactory_Xnb.h"
#include "format_ip.h"
#include "cheesy_snprintf.h"
#include "zlc_util.h"
#include "ZFastFetch.h"
#include "MemBuf.h"

enum { RETRY_TIME_MS = 1500 };

ZLCHandle::ZLCHandle(ZLCVFS *zlcvfs, ZCachedFile *zcf, char *url_for_stat)
	: ZFTPDecoder(zlcvfs->get_zcache()->mf, zlcvfs, url_for_stat)
{
	this->zlcvfs = zlcvfs;
	this->zcf = zcf;
}

ZLCHandle::~ZLCHandle()
{
	// TODO drop ref to zcf
}

void ZLCHandle::_internal_read(void *buf, uint32_t count, uint32_t offset)
{
	lite_assert(offset <= zcf->get_filelen());
	lite_assert(offset+count <= zcf->get_filelen());

	while (true)
	{
		ZSyntheticFileRequest *zreq = new ZSyntheticFileRequest(
			zlcvfs->zcache->GetZLCEmit(),
			zlcvfs->get_zcache()->mf,
			&zcf->file_hash,
			zlcvfs->get_zcache()->sf,
			offset,
			offset+count);
		zcf->consider_request(zreq);
		InternalFileResult *i_result;
		while (true)
		{
			uint32_t old_ctr = zcf->get_progress_counter();
			i_result = zreq->wait_reply(RETRY_TIME_MS);
			if (i_result!=NULL)
			{
				break;
			}
			if (old_ctr == zcf->get_progress_counter())
			{
				// nothing has happened since the last timeout;
				// let's give up.
				break;
			}
			// else we DID make progress; loop around and wait some more.
			ZLC_TERSE(zlcvfs->ze,
				"ZLCVFS::_internal_read absorbs a timeout because progress is being made.\n");
		}

		if (i_result == NULL)
		{
			ZLC_TERSE(zlcvfs->ze,
				"ZLCVFS::_internal_read timeout without progress.\n");
			// timeout. Wah wah.
			zcf->withdraw_request(zreq);
			delete zreq;
			continue;
		}

		lite_assert(!i_result->is_error());
		ValidFileResult *vfr = (ValidFileResult *) i_result;

		MemBuf mb((uint8_t*)buf, count);
		vfr->read(&mb, offset, count);
		delete zreq;	// cleans up vfr, too.
		break;
	}
}


uint64_t ZLCHandle::get_file_len()
{
	return zcf->get_filelen();
}

bool ZLCHandle::is_dir()
{
	return zcf->is_dir();
}

//////////////////////////////////////////////////////////////////////////////

ZLCZCBHandle::ZLCZCBHandle(ZLCVFS *zlcvfs, ZeroCopyBuf *zcb)
	: ZFTPDecoder(
		zlcvfs->get_zcache()->mf,
		zlcvfs,
		NULL /* url_for_stat -- noone's going to stat the zarfile*/ )
{
	this->zlcvfs = zlcvfs;
	this->zcb = zcb;
}

ZLCZCBHandle::~ZLCZCBHandle()
{
	delete zcb;
}

void ZLCZCBHandle::_internal_read(void *buf, uint32_t count, uint32_t offset)
{
	// boy, it would be nifty to skip this memcpy, too!
	// but that would require getting really tricky with mmap interposition.
	// Someday!
	lite_assert(offset < zcb->len());
	lite_assert(offset+count <= zcb->len());
	memcpy(buf, zcb->data()+offset, count);
}


uint64_t ZLCZCBHandle::get_file_len()
{
	return zcb->len();
}

bool ZLCZCBHandle::is_dir()
{
	// TODO I guess we should really check into the metadata,
	// but ZFastFetch sort of hid that from us.
	return false;
}

void* ZLCZCBHandle::fast_mmap(size_t len, uint64_t offset)
{
	// NB I'm not recording which regions we've handed out this way;
	// it's up to a layer above (ZarfileHandle) to avoid giving the
	// same region out twice.
	lite_assert(offset < zcb->len());
	lite_assert(offset+len <= zcb->len());
	return zcb->data()+offset;
}

//////////////////////////////////////////////////////////////////////////////

ZLCVFS::ZLCVFS(XaxPosixEmulation* xpe, ZLCEmit* ze)
{
	this->xpe = xpe;
	this->ze = ze;
	this->zlcargs.origin_lookup =
		(UDPEndpoint*) mf_malloc(xpe->mf, sizeof(UDPEndpoint));
	this->zlcargs.origin_zftp =
		(UDPEndpoint*) mf_malloc(xpe->mf, sizeof(UDPEndpoint));
	ZLC_TERSE(ze, "ZLCVFS starts, calling evil!\n");
	_evil_get_server_addresses(zlcargs.origin_lookup, zlcargs.origin_zftp);
	SendBufferFactory_Xnb *sbf = new SendBufferFactory_Xnb(xpe->zdt);
	SyncFactory *sf = xpe->xax_skinny_network->get_timer_sf();
	this->zcache = new ZCache(
		&this->zlcargs, xpe->mf, sf, ze, sbf);


	socket_factory = new SocketFactory_Skinny(xpe->xax_skinny_network);

	ThreadFactory *thread_factory = new ThreadFactory_Cheesy(xpe->zdt);

	uint32_t mtu = 16834;
	debug_get_link_mtu_f *debug_get_link_mtu = (debug_get_link_mtu_f *)
		(xpe->zdt->zoog_lookup_extension)(DEBUG_GET_LINK_MTU_NAME);
	if (debug_get_link_mtu != NULL)
	{
		mtu = (debug_get_link_mtu)();
	}
	ZLC_CHATTY(ze, "ZLCVFS ctor: Using MTU %d",, mtu);
	uint32_t max_payload = mtu - (sizeof(IP4Header) + sizeof(UDPPacket));

	this->zfile_client = new ZFileClient(this->zcache, this->zlcargs.origin_zftp, socket_factory, thread_factory, max_payload);
	this->zcache->configure(NULL, this->zfile_client);

	this->zlookup_client = new ZLookupClient(this->zlcargs.origin_lookup, socket_factory, sf, ze);
}

void ZLCVFS::_evil_get_server_addresses(
	UDPEndpoint *out_lookup_ep,
	UDPEndpoint *out_zftp_ep)
{
//	discovery_client_get_zftp_server_only(xpe->zdt, xpe->xax_skinny_network, ipv4, &out_zftp_ep->ipaddr);

	XIPifconfig *ipv4_ifconfig = xpe->xax_skinny_network->get_ifconfig(ipv4);
	out_zftp_ep->ipaddr = ipv4_ifconfig->gateway;
	
	out_zftp_ep->port = z_htons(ZFTP_PORT);
	*out_lookup_ep = *out_zftp_ep;
	out_lookup_ep->port = z_htons(ZFTP_HASH_LOOKUP_PORT);
}

bool ZLCVFS::_find_fast_fetch_origin(UDPEndpoint *out_origin)
{
	// query ipv6 broadcast for anyone willing to answer ZFTP queries
	UDPEndpoint bcast_ep;
	bcast_ep.ipaddr = get_ip_subnet_broadcast_address(ipv6);
	bcast_ep.port = z_htons(ZFTP_PORT);

	AbstractSocket *test_socket = socket_factory->new_socket(
		socket_factory->get_inaddr_any(ipv6), true);
	if (test_socket==NULL)
	{
		ZLC_TERSE(ze, "_find_fast_fetch_origin: No ipv6 address available.\n");
		delete test_socket;
		return false;
	}

	{
		ZeroCopyBuf *zcb = test_socket->zc_allocate(sizeof(ZFTPRequestPacket));
		ZFTPRequestPacket *zrp = (ZFTPRequestPacket *) zcb->data();
		zrp->hash_len = z_htons(sizeof(hash_t));
		zrp->url_hint_len = z_htong(0, sizeof(zrp->url_hint_len));	// TODO may need to pass along
		zrp->file_hash = get_zero_hash();
		zrp->num_tree_locations = z_htong(0, sizeof(zrp->num_tree_locations));
		zrp->data_start = z_htong(0, sizeof(zrp->data_start));
		zrp->data_end = z_htong(0, sizeof(zrp->data_end));

		bool rc = test_socket->zc_send(&bcast_ep, zcb);
		lite_assert(rc);

		test_socket->zc_release(zcb);
	}

	UDPEndpoint remote;
	ZeroCopyBuf *reply = test_socket->recvfrom(&remote);
	if (reply==NULL)
	{
		// timeout.
		ZLC_TERSE(ze, "_find_fast_fetch_origin: timeout polling server.\n");
		delete test_socket;
		return false;
	}

	char disp[100];
	if (reply->len() < sizeof(ZFTPReplyHeader))
	{
		cheesy_snprintf(disp, sizeof(disp), "short reply %d bytes", reply->len());
	}
	else
	{
		ZFTPReplyHeader *zrh = (ZFTPReplyHeader *) reply->data();
		cheesy_snprintf(disp, sizeof(disp), "reply code %x", Z_NTOHG(zrh->code));
	}

	char ipbuf[100];
	format_ip(ipbuf, sizeof(ipbuf), &remote.ipaddr);
	ZLC_TERSE(ze, "Got ipv6 zftp reply from %s; %s\n",, ipbuf, disp);
	*out_origin = remote;
	delete test_socket;
	return true;	// could be false when we learn to timeout
}

XaxVFSHandleIfc *ZLCVFS::_fast_fetch_zarfile(const char *fetch_url)
{
	UDPEndpoint fast_origin;
	bool rc = false;
	rc = _find_fast_fetch_origin(&fast_origin);
	if (!rc)
	{
		return NULL;
	}

	uint8_t seed[RandomSupply::SEED_SIZE];
	this->xpe->zdt->zoog_get_random(sizeof(seed), seed);
	RandomSupply* random_supply = new RandomSupply(seed);

	uint8_t app_secret[SYM_KEY_BITS/8];
	this->xpe->zdt->zoog_get_app_secret(sizeof(app_secret), app_secret);
	KeyDerivationKey* appKey = new KeyDerivationKey(app_secret, sizeof(app_secret));
	
	ThreadFactory* thread_factory = new ThreadFactory_Cheesy(xpe->zdt);
	SyncFactory* sync_factory = xpe->xax_skinny_network->get_timer_sf();

	ZFastFetch zff(ze, xpe->mf, thread_factory, sync_factory, zlookup_client, &fast_origin, socket_factory, xpe->perf_measure, appKey, random_supply);
	ZeroCopyBuf *zcb = zff.fetch(xpe->vendor_name_string, fetch_url);
	if (zcb!=NULL)
	{
		return new ZLCZCBHandle(this, zcb);
	}
	else
	{
		return NULL;
	}
}

char *ZLCVFS::assemble_url(MallocFactory *mf, const char *scheme, const char *path)
{
	char *url_hint = (char*) mf_malloc(mf, lite_strlen(scheme)+1+lite_strlen(path)+1);
	lite_strcpy(url_hint, scheme);
	lite_strcat(url_hint, "/");
	lite_strcat(url_hint, path);
	return url_hint;
}

void ZLCVFS::assemble_urls(MallocFactory *mf, XfsPath *path, char **out_url, char **out_url_for_stat)
{
	// ugly in-band signalling. This stinks, and I'm sorry I didn't spend
	// the time to think of the right interface.
	if (lite_strcmp(path->pathstr, RAW_URL_SENTINEL)==0)
	{
		*out_url = mf_strdup(mf, path->suffix);
		*out_url_for_stat = NULL;
	}
	else
	{
		*out_url = assemble_url(mf, FILE_SCHEME, path->suffix);
		*out_url_for_stat = assemble_url(mf, STAT_SCHEME, path->suffix);
	}
}

XaxVFSHandleIfc *ZLCVFS::open(
	XfsErr *err, XfsPath *path, int oflag, XVOpenHooks *open_hooks)
{
	XaxVFSHandleIfc *result = NULL;

	MallocFactory *mf = xpe->mf;

	char *url_hint, *url_for_stat;
	assemble_urls(mf, path, &url_hint, &url_for_stat);

	// TODO this is a silly test. Better to actually try fetching the
	// first block, and if the file is huge, then switch to FastFetch.
	if (lite_ends_with("zarfile", url_hint))
	{
		result = _fast_fetch_zarfile(url_hint);
	}

	if (result==NULL)
	{
		hash_t requested_hash = zlookup_client->lookup_url(url_hint);

		if (lite_memequal((char*)&requested_hash, 0, sizeof(requested_hash)))
		{
			*err = (XfsErr) ENOENT;
			result = NULL;
			goto done;
		}

		// request file metadata
		ZCachedFile *zcf = zcache->lookup_hash(&requested_hash);

		while (true)
		{
			ZSyntheticFileRequest zreq(zcache->GetZLCEmit(), mf, &requested_hash, zcache->sf, 0, 0);
			zcf->consider_request(&zreq);
		
			InternalFileResult *file_result = zreq.wait_reply(RETRY_TIME_MS);
				// NB file_result is owned by zreq, and is cleaned up
				// by zreq's destructor.
			if (file_result==NULL)
			{
				ZLC_TERSE(ze, "ZLCVFS::open: timeout\n");
				zcf->withdraw_request(&zreq);
				continue;
			}

			if (file_result->is_error())
			{
				*err = (XfsErr) ENOENT;
				result = NULL;
				goto done;
			}
			// Yay: we have a successful result indicating fetching metadata.
			// Drop out and build the result object that wraps the zcf.
			break;
		}

		result = new ZLCHandle(this, zcf, url_for_stat);
		url_for_stat = NULL;	// now owned by handle
	}
	*err = XFS_NO_ERROR;

done:
	mf_free(mf, url_hint);
	if (url_for_stat!=NULL)
	{
		mf_free(mf, url_for_stat);
	}

	return result;
}
