/*
 *			GPAC - Multimedia Framework C SDK
 *
 *  This file is part of GPAC / camera RAW image reframer filter
 *  based on LibRaw (https://www.libraw.org/)
 *
 */

#include <gpac/filters.h>
#include <string.h>
#include "libraw/libraw.h"

typedef struct
{
	// only one input pid declared
	GF_FilterPid *ipid;
	// only one output pid declared
	GF_FilterPid *opid;
	u32 src_timescale;
	Bool owns_timescale;
	u32 codec_id;

	Bool initial_play_done;
	Bool is_playing;
} GF_ReframeRawCtx;

static GF_Err rfraw_configure_pid(GF_Filter *filter, GF_FilterPid *pid, Bool is_remove)
{
	GF_ReframeRawCtx *ctx = gf_filter_get_udta(filter);
	const GF_PropertyValue *p;

	if (is_remove)
	{
		ctx->ipid = NULL;
		return GF_OK;
	}

	if (!gf_filter_pid_check_caps(pid))
		return GF_NOT_SUPPORTED;

	gf_filter_pid_set_framing_mode(pid, GF_TRUE);
	ctx->ipid = pid;
	// force retest of codecid
	ctx->codec_id = 0;

	p = gf_filter_pid_get_property(pid, GF_PROP_PID_TIMESCALE);
	if (p)
		ctx->src_timescale = p->value.uint;

	if (ctx->src_timescale && !ctx->opid)
	{
		ctx->opid = gf_filter_pid_new(filter);
		gf_filter_pid_copy_properties(ctx->opid, ctx->ipid);
		gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_UNFRAMED, NULL);
	}
	ctx->is_playing = GF_TRUE;
	return GF_OK;
}

static Bool rfraw_process_event(GF_Filter *filter, const GF_FilterEvent *evt)
{
	GF_FilterEvent fevt;
	GF_ReframeRawCtx *ctx = gf_filter_get_udta(filter);
	if (evt->base.on_pid != ctx->opid)
		return GF_TRUE;
	switch (evt->base.type)
	{
	case GF_FEVT_PLAY:
		if (ctx->is_playing)
		{
			return GF_TRUE;
		}

		ctx->is_playing = GF_TRUE;
		if (!ctx->initial_play_done)
		{
			ctx->initial_play_done = GF_TRUE;
			return GF_TRUE;
		}

		GF_FEVT_INIT(fevt, GF_FEVT_SOURCE_SEEK, ctx->ipid);
		fevt.seek.start_offset = 0;
		gf_filter_pid_send_event(ctx->ipid, &fevt);
		return GF_TRUE;
	case GF_FEVT_STOP:
		ctx->is_playing = GF_FALSE;
		return GF_FALSE;
	default:
		break;
	}
	// cancel all events
	return GF_TRUE;
}

static GF_Err rfraw_process(GF_Filter *filter)
{
	GF_ReframeRawCtx *ctx = gf_filter_get_udta(filter);
	GF_FilterPacket *pck, *dst_pck;
	GF_Err e;
	u8 *data;
	u32 size;

	pck = gf_filter_pid_get_packet(ctx->ipid);
	if (!pck)
	{
		if (gf_filter_pid_is_eos(ctx->ipid))
		{
			if (ctx->opid)
				gf_filter_pid_set_eos(ctx->opid);
			ctx->is_playing = GF_FALSE;
			return GF_EOS;
		}
		return GF_OK;
	}
	data = (u8 *)gf_filter_pck_get_data(pck, &size);

	if (!ctx->opid || !ctx->codec_id)
	{
		u32 w = 0, h = 0;
		libraw_data_t *raw = libraw_init(0);
		if (!raw)
			return GF_OUT_OF_MEM;

		int ret = libraw_open_buffer(raw, data, size);
		if (ret != LIBRAW_SUCCESS)
		{
			GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[RAW] libraw_open_buffer failed: %s\n", libraw_strerror(ret)));
			libraw_close(raw);
			return GF_NON_COMPLIANT_BITSTREAM;
		}

		w = raw->sizes.width;
		h = raw->sizes.height;

		ctx->codec_id = GF_4CC('R', 'A', 'W', 'I');
		ctx->opid = gf_filter_pid_new(filter);
		if (!ctx->opid)
		{
			libraw_close(raw);
			gf_filter_pid_drop_packet(ctx->ipid);
			return GF_SERVICE_ERROR;
		}

		// we don't have input reconfig for now
		gf_filter_pid_copy_properties(ctx->opid, ctx->ipid);
		gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_STREAM_TYPE, &PROP_UINT(GF_STREAM_VISUAL));
		gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_CODECID, &PROP_UINT(ctx->codec_id));
		if (w)
			gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_WIDTH, &PROP_UINT(w));
		if (h)
			gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_HEIGHT, &PROP_UINT(h));

		if (!gf_filter_pid_get_property(ctx->ipid, GF_PROP_PID_TIMESCALE))
		{
			gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_TIMESCALE, &PROP_UINT(1000));
			ctx->owns_timescale = GF_TRUE;
		}

		gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_NB_FRAMES, &PROP_UINT(1));
		gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PLAYBACK_MODE, &PROP_UINT(GF_PLAYBACK_MODE_FASTFORWARD));

		libraw_close(raw);
	}

	e = GF_OK;
	u32 start_offset = 0;

	dst_pck = gf_filter_pck_new_ref(ctx->opid, start_offset, size - start_offset, pck);
	if (!dst_pck)
		return GF_OUT_OF_MEM;

	gf_filter_pck_merge_properties(pck, dst_pck);
	if (ctx->owns_timescale)
	{
		gf_filter_pck_set_cts(dst_pck, 0);
		gf_filter_pck_set_sap(dst_pck, GF_FILTER_SAP_1);
		gf_filter_pck_set_duration(dst_pck, 1000);
	}

	gf_filter_pck_send(dst_pck);
	gf_filter_pid_drop_packet(ctx->ipid);

	return e;
}

static const char *rfraw_probe_data(const u8 *data, u32 size, GF_FilterProbeScore *score)
{
	if (size < 16)
		return NULL;

	// TIFF-based raw formats (CR2, CR3-in-TIFF-wrapper, NEF, NRW, ARW, DNG, RW2, PEF, SRW, ORF...)
	if ((data[0] == 'I' && data[1] == 'I' && data[2] == 0x2A && data[3] == 0x00) ||
		(data[0] == 'M' && data[1] == 'M' && data[2] == 0x00 && data[3] == 0x2A))
	{
		*score = GF_FPROBE_SUPPORTED;
		return "image/x-raw";
	}
	// Fujifilm RAF
	if (!memcmp(data, "FUJIFILMCCD-RAW", 15))
	{
		*score = GF_FPROBE_SUPPORTED;
		return "image/x-raw";
	}
	// Sigma X3F
	if (data[0] == 'F' && data[1] == 'O' && data[2] == 'V' && data[3] == 'b')
	{
		*score = GF_FPROBE_SUPPORTED;
		return "image/x-raw";
	}
	// Kodak DCR/KDC (older, non-TIFF signature variants) & Panasonic RW2 share TIFF header already handled above
	return NULL;
}

static const GF_FilterCapability ReframeRawCaps[] =
	{
		CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_FILE),
		CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_FILE_EXT, "cr2|cr3|crw|nef|nrw|arw|srf|sr2|dng|rw2|raw|pef|ptx|orf|raf|kdc|dcr|k25|mrw|x3f|erf|3fr|mef|mos|iiq|rwl"),
		CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_MIME, "image/x-raw|image/x-canon-cr2|image/x-nikon-nef|image/x-sony-arw|image/x-adobe-dng|image/x-panasonic-rw2|image/x-olympus-orf|image/x-fuji-raf"),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_VISUAL),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_CODECID, GF_4CC('R', 'A', 'W', 'I')),
};

GF_FilterRegister ReframeRawRegister = {
	.name = "rfraw",
	GF_FS_SET_DESCRIPTION("Camera RAW image reframer")
		GF_FS_SET_HELP("This filter parses camera RAW image files/data (via LibRaw) and outputs corresponding visual PID and frames.\n")
			.private_size = sizeof(GF_ReframeRawCtx),
	SETCAPS(ReframeRawCaps),
	.configure_pid = rfraw_configure_pid,
	.probe_data = rfraw_probe_data,
	.process = rfraw_process,
	.process_event = rfraw_process_event};

const GF_FilterRegister * EMSCRIPTEN_KEEPALIVE raw_reframe_register(GF_FilterSession *session)
{
	return &ReframeRawRegister;
}


#include "filter_register.h"
__attribute__((constructor))
void register_raw_reframe(void) {
    gf_filter_auto_register("raw_reframe", raw_reframe_register);
}
