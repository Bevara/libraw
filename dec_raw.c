/*
 *			GPAC - Multimedia Framework C SDK
 *
 *  This file is part of GPAC / camera RAW image decoder filter
 *  based on LibRaw (https://www.libraw.org/)
 *
 */

#include <gpac/filters.h>
#include <string.h>
#include "libraw/libraw.h"

typedef struct
{
	GF_FilterPid *ipid, *opid;

	Bool is_playing;
	u32 src_timescale;
	u32 codec_id;
	u32 ofmt;
} GF_RAWDecCtx;

static GF_Err rawdec_configure_pid(GF_Filter *filter, GF_FilterPid *pid, Bool is_remove)
{
	const GF_PropertyValue *prop;
	GF_RAWDecCtx *ctx = (GF_RAWDecCtx *)gf_filter_get_udta(filter);

	if (is_remove)
	{
		if (ctx->opid)
		{
			gf_filter_pid_remove(ctx->opid);
			ctx->opid = NULL;
		}
		ctx->ipid = NULL;
		return GF_OK;
	}
	if (!gf_filter_pid_check_caps(pid))
		return GF_NOT_SUPPORTED;

	prop = gf_filter_pid_get_property(pid, GF_PROP_PID_CODECID);
	if (!prop)
		return GF_NOT_SUPPORTED;
	ctx->ipid = pid;

	if (!ctx->opid)
	{
		ctx->opid = gf_filter_pid_new(filter);
	}

	// copy properties at init or reconfig
	gf_filter_pid_copy_properties(ctx->opid, ctx->ipid);
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_CODECID, &PROP_UINT(GF_CODECID_RAW));

	// actual pixel format/size are set once the image has been decoded,
	// since only LibRaw knows the final layout after demosaicing
	if (!ctx->ofmt)
	{
		ctx->ofmt = GF_PIXEL_RGB;
		gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PIXFMT, &PROP_UINT(GF_PIXEL_RGB));
	}

	return GF_OK;
}

static GF_Err rawdec_process(GF_Filter *filter)
{
	GF_FilterPacket *pck, *dst_pck;
	u8 *data, *output;
	u32 size;
	int ret, errc;
	libraw_data_t *raw;
	libraw_processed_image_t *img;
	GF_RAWDecCtx *ctx = (GF_RAWDecCtx *)gf_filter_get_udta(filter);

	pck = gf_filter_pid_get_packet(ctx->ipid);
	if (!pck)
	{
		if (gf_filter_pid_is_eos(ctx->ipid))
		{
			gf_filter_pid_set_eos(ctx->opid);
			return GF_EOS;
		}
		return GF_OK;
	}
	data = (u8 *)gf_filter_pck_get_data(pck, &size);

	if (!data)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_IO_ERR;
	}

	raw = libraw_init(0);
	if (!raw)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_OUT_OF_MEM;
	}

	ret = libraw_open_buffer(raw, data, size);
	if (ret != LIBRAW_SUCCESS)
	{
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[RAW] libraw_open_buffer failed: %s\n", libraw_strerror(ret)));
		libraw_close(raw);
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_NON_COMPLIANT_BITSTREAM;
	}

	raw->params.output_bps = 8;
	raw->params.output_color = 1; /* sRGB */
	raw->params.output_tiff = 0;
	raw->params.use_camera_wb = 1;

	ret = libraw_unpack(raw);
	if (ret != LIBRAW_SUCCESS)
	{
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[RAW] libraw_unpack failed: %s\n", libraw_strerror(ret)));
		libraw_close(raw);
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_NON_COMPLIANT_BITSTREAM;
	}

	ret = libraw_dcraw_process(raw);
	if (ret != LIBRAW_SUCCESS)
	{
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[RAW] libraw_dcraw_process failed: %s\n", libraw_strerror(ret)));
		libraw_close(raw);
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_NON_COMPLIANT_BITSTREAM;
	}

	errc = 0;
	img = libraw_dcraw_make_mem_image(raw, &errc);
	if (!img || errc != LIBRAW_SUCCESS)
	{
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[RAW] libraw_dcraw_make_mem_image failed: %s\n", libraw_strerror(errc)));
		if (img)
			libraw_dcraw_clear_mem(img);
		libraw_close(raw);
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_NON_COMPLIANT_BITSTREAM;
	}

	ctx->ofmt = (img->colors == 4) ? GF_PIXEL_RGBA : (img->colors == 1) ? GF_PIXEL_GREYSCALE : GF_PIXEL_RGB;
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PIXFMT, &PROP_UINT(ctx->ofmt));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_WIDTH, &PROP_UINT(img->width));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_HEIGHT, &PROP_UINT(img->height));

	dst_pck = gf_filter_pck_new_alloc(ctx->opid, img->data_size, &output);
	if (!dst_pck)
	{
		libraw_dcraw_clear_mem(img);
		libraw_close(raw);
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_OUT_OF_MEM;
	}
	memcpy(output, img->data, img->data_size);

	gf_filter_pck_merge_properties(pck, dst_pck);
	gf_filter_pck_set_dependency_flags(dst_pck, 0);
	gf_filter_pck_send(dst_pck);

	libraw_dcraw_clear_mem(img);
	libraw_close(raw);
	gf_filter_pid_drop_packet(ctx->ipid);

	return GF_OK;
}

static const GF_FilterCapability RAWDecCaps[] =
	{
		CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_VISUAL),
		CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_CODECID, GF_4CC('R', 'A', 'W', 'I')),
		CAP_BOOL(GF_CAPS_INPUT_EXCLUDED, GF_PROP_PID_UNFRAMED, GF_TRUE),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_VISUAL),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_CODECID, GF_CODECID_RAW),
};

GF_FilterRegister RAWDecoderRegister = {
	.name = "rawdec",
	GF_FS_SET_DESCRIPTION("Camera RAW image decoder")
		GF_FS_SET_HELP("This filter decodes camera RAW images (CR2, NEF, ARW, DNG, ORF, RAF, RW2, ...) using LibRaw.")
			.private_size = sizeof(GF_RAWDecCtx),
	SETCAPS(RAWDecCaps),
	.configure_pid = rawdec_configure_pid,
	.process = rawdec_process,
};

const GF_FilterRegister * EMSCRIPTEN_KEEPALIVE rawdec_register(GF_FilterSession *session)
{
	return &RAWDecoderRegister;
}


#include "filter_register.h"
__attribute__((constructor))
void register_rawdec(void) {
    gf_filter_auto_register("rawdec", rawdec_register);
}
