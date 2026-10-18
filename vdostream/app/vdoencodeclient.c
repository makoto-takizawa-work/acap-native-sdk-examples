/**
 * Copyright (C) 2019-2021, Axis Communications AB, Lund, Sweden
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * - vdoencodeclient -
 *
 * This application is a basic vdo type of application.
 *
 * The application starts a vdo steam and then illustrates how to continuously
 * capture frames from the vdo service, access the received buffer contents
 * as well as the frame metadata.
 *
 * The application expects three arguments on the command line in the following
 * order: format frames output.
 *
 * First argument, format, is a string describing the video compression format.
 * Possible values are av1, avif, h264 (default), h265, jpeg, nv12, rgb and y800.
 *
 * Second argument, frames, is an integer for number of captured frames.
 *
 * Finally, the third argument, output, is the output filename.
 *
 * Optionally, width and height select the stream resolution (default 640x360).
 *
 * With crop and/or output-format, each nv12 or rgb frame is passed through a
 * larod pre-processing job (cpu-proc) before it is written. crop takes
 * "x,y,width,height" and output-format one of rgb-interleaved (default),
 * rgb-planar or nv12. The written frame has the crop size. pp-device selects
 * the larod device, cpu-proc (default) or a9-gpu-proc.
 *
 * Suppose that you have done through the steps of installation.
 * Then you would go to /usr/local/packages/vdoencodeclient on your device
 * and then for example run:
 *     ./vdoencodeclient \
 *         --format h264 \
 *         --frames 10 \
 *         --output vdo.out \
 *         --width 1280 \
 *         --height 720
 *
 * or in short argument syntax
 *      ./vdoencodeclient \
 *         -t h264 \
 *         -n 10 \
 *         -o vdo.out \
 *         -W 1280 \
 *         -H 720
 *
 * or, cropping a 320x240 region at (100,50) and converting it to planar RGB:
 *      ./vdoencodeclient \
 *         --format nv12 \
 *         --frames 10 \
 *         --output vdo.out \
 *         --crop 100,50,320,240 \
 *         --output-format rgb-planar \
 *         --pp-device a9-gpu-proc
 */

#include "vdo-error.h"
#include "vdo-map.h"
#include "vdo-stream.h"
#include "vdo-types.h"

#include <glib.h>
#include <glib/gstdio.h>
// Needed for g_autoptr
#include <glib-object.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <syslog.h>

#include "panic.h"
#include "preprocess.h"

static gboolean shutdown       = FALSE;
static const gchar* param_desc = "";
static const gchar* summary    = "Encoded video client";

// Facilitate graceful shutdown with CTRL-C
static void handle_sigint(int signum) {
    (void)signum;
    shutdown = TRUE;
}

// Determine and log the received frame type
static void print_frame(VdoFrame* frame) {
    gchar* frame_type;
    switch (vdo_frame_get_frame_type(frame)) {
        case VDO_FRAME_TYPE_AVIF:
            frame_type = "avif";
            break;
        case VDO_FRAME_TYPE_H264_IDR:
        case VDO_FRAME_TYPE_H265_IDR:
        case VDO_FRAME_TYPE_H264_I:
        case VDO_FRAME_TYPE_H265_I:
        case VDO_FRAME_TYPE_AV1_KEY:
            frame_type = "I";
            break;
        case VDO_FRAME_TYPE_H264_P:
        case VDO_FRAME_TYPE_H265_P:
        case VDO_FRAME_TYPE_AV1_INTER:
            frame_type = "P";
            break;
        case VDO_FRAME_TYPE_JPEG:
            frame_type = "jpeg";
            break;
        case VDO_FRAME_TYPE_YUV:
            frame_type = "yuv";
            break;
        case VDO_FRAME_TYPE_RGB:
            frame_type = "rgb";
            break;
        default:
            frame_type = "NA";
    }

    syslog(LOG_INFO,
           "frame = %4u, type = %s, size = %zu\n",
           vdo_frame_get_sequence_nbr(frame),
           frame_type,
           vdo_frame_get_size(frame));
}

// Set vdo format from input parameter
static void set_format(VdoMap* settings, gchar* format) {
    if (g_strcmp0(format, "av1") == 0) {
        vdo_map_set_uint32(settings, "format", VDO_FORMAT_AV1);
    } else if (g_strcmp0(format, "avif") == 0) {
        vdo_map_set_uint32(settings, "format", VDO_FORMAT_AVIF);
    } else if (g_strcmp0(format, "h264") == 0) {
        vdo_map_set_uint32(settings, "format", VDO_FORMAT_H264);
    } else if (g_strcmp0(format, "h265") == 0) {
        vdo_map_set_uint32(settings, "format", VDO_FORMAT_H265);
    } else if (g_strcmp0(format, "jpeg") == 0) {
        vdo_map_set_uint32(settings, "format", VDO_FORMAT_JPEG);
    } else if (g_strcmp0(format, "nv12") == 0) {
        vdo_map_set_uint32(settings, "format", VDO_FORMAT_YUV);
        vdo_map_set_string(settings, "subformat", "NV12");
    } else if (g_strcmp0(format, "y800") == 0) {
        vdo_map_set_uint32(settings, "format", VDO_FORMAT_YUV);
        vdo_map_set_string(settings, "subformat", "Y800");
    } else if (g_strcmp0(format, "rgb") == 0) {
        vdo_map_set_uint32(settings, "format", VDO_FORMAT_RGB);
    } else {
        panic("%s: Format \"%s\" is not supported\n", __func__, format);
    }
    vdo_map_set_boolean(settings, "frame.chunks", true);
}

static void save_frame_to_file(VdoBuffer* buffer, FILE* dest_f) {
    g_autoptr(GError) error = NULL;
    // Lifetimes of buffer and frame are linked, no need to free frame
    VdoFrame* frame = vdo_buffer_get_frame(buffer);

    print_frame(frame);

    if (vdo_buffer_is_contiguous(frame)) {
        VdoChunk chunk = vdo_frame_take_chunk(frame, &error);
        if (chunk.type == VDO_CHUNK_ERROR)
            panic("%s: Failed to get chunk: %m", __func__);
        if (!fwrite(chunk.data, chunk.size, 1, dest_f))
            panic("%s: Failed to write frame: %m", __func__);
    } else {
        while (true) {
            VdoChunk chunk = vdo_frame_take_chunk(frame, &error);
            if (chunk.type == VDO_CHUNK_ERROR)
                panic("%s: Failed to get chunk: %m", __func__);
            if (chunk.size == 0u)
                break;
            if (!fwrite(chunk.data, chunk.size, 1, dest_f))
                panic("%s: Failed to write frame: %m", __func__);
        }
    }
}

static int handle_vdo_failed(GError* error) {
    // Maintenance/Installation in progress (e.g. Global-Rotation)
    if (vdo_error_is_expected(&error)) {
        syslog(LOG_INFO, "Expected vdo error %s", error->message);
        return EXIT_SUCCESS;
    } else {
        panic("Unexpected vdo error %s", error->message);
    }
    return EXIT_FAILURE;
}

/**
 * Main function that starts a stream with the following options:
 *
 * --format [av1, avif, h264, h265, jpeg, nv12, rgb, y800]
 * --frames [number of frames]
 * --output [output filename]
 * --width [stream width]
 * --height [stream height]
 * --crop [x,y,width,height]
 * --output-format [rgb-interleaved, rgb-planar, nv12]
 * --pp-device [cpu-proc, a9-gpu-proc]
 */
int main(int argc, char* argv[]) {
    g_autoptr(GError) error     = NULL;
    g_autoptr(VdoMap) settings  = NULL;
    g_autoptr(VdoMap) info      = NULL;
    g_autoptr(VdoStream) stream = NULL;
    gchar* format               = "h264";
    guint frames                = G_MAXUINT;
    gchar* output_file          = "/dev/null";
    gint width                  = 640;
    gint height                 = 360;
    gchar* crop_str             = NULL;
    gchar* output_format        = NULL;
    gchar* pp_device            = "cpu-proc";
    preprocess_crop_t crop      = {0};
    preprocess_t* pp            = NULL;
    FILE* dest_f                = NULL;

    GOptionEntry options[] = {
        {"format",
         't',
         0,
         G_OPTION_ARG_STRING,
         &format,
         "format (av1, avif, h264, h265, jpeg, nv12, rgb, y800)",
         NULL},
        {"frames", 'n', 0, G_OPTION_ARG_INT, &frames, "number of frames", NULL},
        {"output", 'o', 0, G_OPTION_ARG_FILENAME, &output_file, "output filename", NULL},
        {"width", 'W', 0, G_OPTION_ARG_INT, &width, "stream width (default 640)", NULL},
        {"height", 'H', 0, G_OPTION_ARG_INT, &height, "stream height (default 360)", NULL},
        {"crop",
         'c',
         0,
         G_OPTION_ARG_STRING,
         &crop_str,
         "crop region with larod preprocessing",
         "X,Y,W,H"},
        {"output-format",
         'f',
         0,
         G_OPTION_ARG_STRING,
         &output_format,
         "larod preprocessing output format (rgb-interleaved (default), rgb-planar, nv12)",
         NULL},
        {"pp-device",
         'd',
         0,
         G_OPTION_ARG_STRING,
         &pp_device,
         "larod preprocessing device (cpu-proc (default), a9-gpu-proc)",
         NULL},
        {
            NULL,
            0,
            0,
            0,
            NULL,
            NULL,
            NULL,
        }};

    GOptionContext* context = g_option_context_new(param_desc);
    if (!context)
        panic("%s Context is null", __func__);

    g_option_context_set_summary(context, summary);
    g_option_context_add_main_entries(context, options, NULL);
    if (!g_option_context_parse(context, &argc, &argv, &error))
        panic("%s Failed to use option_context_parse: %s", __func__, error->message);

    if (width <= 0 || height <= 0)
        panic("%s Invalid resolution %dx%d", __func__, width, height);

    // Preprocessing is used when either crop or output format is given
    const gchar* pp_input_format = NULL;
    if (crop_str || output_format) {
        if (crop_str && !preprocess_parse_crop(crop_str, &crop))
            panic("%s Invalid crop \"%s\", expected x,y,width,height", __func__, crop_str);
        if (!output_format)
            output_format = "rgb-interleaved";
        if (g_strcmp0(format, "nv12") == 0)
            pp_input_format = "nv12";
        else if (g_strcmp0(format, "rgb") == 0)
            pp_input_format = "rgb-interleaved";
        else
            panic("%s Preprocessing requires format nv12 or rgb, not %s", __func__, format);
    }

    dest_f = fopen(output_file, "wb");
    if (!dest_f)
        panic("%s open failed: %m", __func__);

    if (signal(SIGINT, handle_sigint) == SIG_ERR)
        panic("%s Failed to install signal handler: %m", __func__);

    settings = vdo_map_new();
    set_format(settings, format);

    VdoPair32u resolution = {
        .w = (guint)width,
        .h = (guint)height,
    };
    vdo_map_set_pair32u(settings, "resolution", resolution);

    // Use snapshot API when nbr of frames are 1
    if (frames == 1 && !pp_input_format) {
        g_autoptr(VdoBuffer) buffer = vdo_stream_snapshot(settings, &error);
        if (!buffer)
            panic("%s: Failed to get snapshot: %s", __func__, error->message);
        syslog(LOG_INFO,
               "Starting stream: %s, %ux%u, 1 fps\n",
               format,
               vdo_map_get_uint32(settings, "width", 0),
               vdo_map_get_uint32(settings, "height", 0));
        save_frame_to_file(buffer, dest_f);
        goto exit;
    }

    // When several frames should be retrieved
    // Not to be used for AVIF
    if (g_strcmp0(format, "avif") == 0)
        panic("AVIF should not be used for more frames than one");

    // Create a new stream
    stream = vdo_stream_new(settings, NULL, &error);
    if (!stream)
        panic("%s: Failed creating vdo stream: %s", __func__, error->message);

    info = vdo_stream_get_info(stream, &error);
    if (!info)
        panic("%s: Failed to get vdo stream info: %s", __func__, error->message);

    syslog(LOG_INFO,
           "Starting stream: %s, %ux%u, %u fps\n",
           format,
           vdo_map_get_uint32(info, "width", 0),
           vdo_map_get_uint32(info, "height", 0),
           (unsigned int)(vdo_map_get_double(info, "framerate", 0.0) + 0.5));

    if (pp_input_format)
        pp = preprocess_new(info,
                            pp_input_format,
                            crop_str ? &crop : NULL,
                            output_format,
                            pp_device);

    // Start the stream
    if (!vdo_stream_start(stream, &error))
        panic("%s: Failed to start vdo stream : %s", __func__, error->message);

    // Loop until interrupt by Ctrl-C or reaching G_MAXUINT
    for (guint n = 0; n < frames; ++n) {
        // SIGINT occurred
        if (shutdown)
            goto exit;

        g_autoptr(VdoBuffer) buffer = vdo_stream_get_buffer(stream, &error);
        if (!buffer && g_error_matches(error, VDO_ERROR, VDO_ERROR_NO_DATA)) {
            g_clear_error(&error);
            continue;  // Transient error -> Retry
        }

        if (!buffer)
            return handle_vdo_failed(error);

        if (pp) {
            print_frame(vdo_buffer_get_frame(buffer));
            preprocess_run(pp, buffer, dest_f);
        } else {
            save_frame_to_file(buffer, dest_f);
        }

        // Release the buffer and allow the server to reuse it
        if (!vdo_stream_buffer_unref(stream, &buffer, &error)) {
            if (!vdo_error_is_expected(&error))
                panic("%s: Unexpected error: %s", __func__, error->message);
            g_clear_error(&error);
        }
    }

exit:
    preprocess_free(pp);

    if (dest_f)
        fclose(dest_f);

    g_option_context_free(context);

    syslog(LOG_INFO, "Exit %s", argv[0]);
    return 0;
}
