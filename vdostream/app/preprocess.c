/**
 * Copyright (C) 2025, Axis Communications AB, Lund, Sweden
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
 * - preprocess -
 *
 * Crops and/or converts vdo frames with a larod pre-processing job running on
 * a *-proc device, e.g. cpu-proc (libyuv) or a9-gpu-proc (OpenCL). One input tensor is tracked per vdo buffer, since vdo
 * rotates a small pool of buffers; the output tensor is allocated by larod and
 * mmapped once.
 */

#include "preprocess.h"

#include "larod.h"
#include "panic.h"

#include <stdlib.h>
#include <sys/mman.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

struct preprocess {
    larodConnection* conn;
    larodModel* model;
    larodJobRequest* req;  // Created on the first frame, when an input exists
    larodMap* params;
    larodTensor** outputs;
    size_t num_outputs;
    void* out_data;
    size_t out_fd_size;
    size_t out_byte_size;

    // Input tensor cache, one entry per vdo buffer fd
    guint nbr_buffers;
    int* vdo_fds;
    larodTensor*** inputs;

    larodTensorLayout in_layout;
    guint width;
    guint height;
    guint pitch;
    gboolean dmabuf;

    // larodRunJob() timing in ms. The first job is kept apart since it
    // includes one-time setup in the backend.
    gchar* device_name;
    guint nbr_jobs;
    double first_ms;
    double sum_ms;
    double min_ms;
    double max_ms;
};

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

gboolean preprocess_parse_crop(const gchar* str, preprocess_crop_t* crop) {
    int x, y, w, h, end = 0;
    if (sscanf(str, "%d,%d,%d,%d%n", &x, &y, &w, &h, &end) != 4 || str[end] != '\0')
        return FALSE;
    if (x < 0 || y < 0 || w <= 0 || h <= 0)
        return FALSE;
    crop->x      = (guint)x;
    crop->y      = (guint)y;
    crop->width  = (guint)w;
    crop->height = (guint)h;
    return TRUE;
}

static larodTensorLayout layout_from_format(const gchar* format) {
    if (g_strcmp0(format, "nv12") == 0)
        return LAROD_TENSOR_LAYOUT_420SP;
    if (g_strcmp0(format, "rgb-interleaved") == 0)
        return LAROD_TENSOR_LAYOUT_NHWC;
    if (g_strcmp0(format, "rgb-planar") == 0)
        return LAROD_TENSOR_LAYOUT_NCHW;
    panic("%s: Format \"%s\" is not supported", __func__, format);
}

static void log_devices(larodConnection* conn) {
    larodError* error = NULL;
    size_t num_devices;
    const larodDevice** devices = larodListDevices(conn, &num_devices, &error);
    if (!devices)
        panic("%s: Could not list larod devices: %s", __func__, error->msg);
    for (size_t i = 0; i < num_devices; i++) {
        const char* name = larodGetDeviceName(devices[i], &error);
        if (!name)
            panic("%s: Could not get device name: %s", __func__, error->msg);
        syslog(LOG_INFO, "larod device: %s", name);
    }
    // The list is owned by larod and must not be freed
}

static larodModel* load_model(preprocess_t* pp,
                              const gchar* input_format,
                              guint out_width,
                              guint out_height,
                              const gchar* output_format) {
    larodError* error = NULL;

    larodMap* map = larodCreateMap(&error);
    if (!map)
        panic("%s: Could not create larodMap: %s", __func__, error->msg);
    if (!larodMapSetStr(map, "image.input.format", input_format, &error) ||
        !larodMapSetIntArr2(map, "image.input.size", pp->width, pp->height, &error) ||
        !larodMapSetInt(map, "image.input.row-pitch", pp->pitch, &error) ||
        !larodMapSetStr(map, "image.output.format", output_format, &error) ||
        !larodMapSetIntArr2(map, "image.output.size", out_width, out_height, &error))
        panic("%s: Failed setting preprocessing parameters: %s", __func__, error->msg);

    const larodDevice* device = larodGetDevice(pp->conn, pp->device_name, 0, &error);
    if (!device)
        panic("%s: Could not get device %s: %s", __func__, pp->device_name, error->msg);

    // fd = -1: a pre-processing model is defined by the map, not by a file
    larodModel* model =
        larodLoadModel(pp->conn, -1, device, LAROD_ACCESS_PRIVATE, "preprocess", map, &error);
    if (!model)
        panic("%s: Unable to load preprocessing model: %s", __func__, error->msg);

    larodDestroyMap(&map);
    return model;
}

static void setup_output(preprocess_t* pp) {
    larodError* error = NULL;

    pp->outputs = larodAllocModelOutputs(pp->conn,
                                         pp->model,
                                         LAROD_FD_PROP_READWRITE | LAROD_FD_PROP_MAP,
                                         &pp->num_outputs,
                                         NULL,
                                         &error);
    if (!pp->outputs)
        panic("%s: Failed allocating output tensors: %s", __func__, error->msg);
    if (pp->num_outputs != 1)
        panic("%s: Expected 1 output tensor, got %zu", __func__, pp->num_outputs);

    if (!larodGetTensorFdSize(pp->outputs[0], &pp->out_fd_size, &error))
        panic("%s: Could not get output fd size: %s", __func__, error->msg);
    if (!larodGetTensorByteSize(pp->outputs[0], &pp->out_byte_size, &error))
        panic("%s: Could not get output byte size: %s", __func__, error->msg);

    int fd = larodGetTensorFd(pp->outputs[0], &error);
    if (fd == LAROD_INVALID_FD)
        panic("%s: Could not get output fd: %s", __func__, error->msg);
    pp->out_data = mmap(NULL, pp->out_fd_size, PROT_READ, MAP_SHARED, fd, 0);
    if (pp->out_data == MAP_FAILED)
        panic("%s: Could not mmap output: %m", __func__);
}

static larodMap* create_job_params(const preprocess_crop_t* crop) {
    larodError* error = NULL;
    if (!crop)
        return NULL;

    larodMap* params = larodCreateMap(&error);
    if (!params)
        panic("%s: Could not create larodMap: %s", __func__, error->msg);
    if (!larodMapSetIntArr4(params,
                            "image.input.crop",
                            crop->x,
                            crop->y,
                            crop->width,
                            crop->height,
                            &error))
        panic("%s: Failed setting crop: %s", __func__, error->msg);
    return params;
}

preprocess_t* preprocess_new(VdoMap* info,
                             const gchar* input_format,
                             const preprocess_crop_t* crop,
                             const gchar* output_format,
                             const gchar* device_name) {
    larodError* error = NULL;
    preprocess_t* pp  = g_new0(preprocess_t, 1);

    pp->device_name = g_strdup(device_name);

    pp->width       = vdo_map_get_uint32(info, "width", 0);
    pp->height      = vdo_map_get_uint32(info, "height", 0);
    pp->pitch       = vdo_map_get_uint32(info, "pitch", 0);
    pp->nbr_buffers = vdo_map_get_uint32(info, "buffer.count", 0);
    pp->dmabuf = g_strcmp0(vdo_map_get_string(info, "buffer.type", NULL, "memfd"), "vmem") != 0;
    pp->in_layout = layout_from_format(input_format);
    layout_from_format(output_format);  // Validate early

    syslog(LOG_INFO,
           "vdo stream for preprocessing: %ux%u, pitch %u, %u buffers, %s",
           pp->width,
           pp->height,
           pp->pitch,
           pp->nbr_buffers,
           pp->dmabuf ? "dmabuf" : "vmem");
    if (pp->nbr_buffers == 0)
        panic("%s: vdo reported no buffer count", __func__);

    guint out_width  = pp->width;
    guint out_height = pp->height;
    if (crop) {
        if (crop->x + crop->width > pp->width || crop->y + crop->height > pp->height)
            panic("%s: Crop %u,%u,%u,%u is outside the %ux%u frame",
                  __func__,
                  crop->x,
                  crop->y,
                  crop->width,
                  crop->height,
                  pp->width,
                  pp->height);
        // libyuv requires even sizes, and nv12 chroma requires even offsets
        if ((crop->x | crop->y | crop->width | crop->height) & 1)
            panic("%s: Crop values must be even", __func__);
        out_width  = crop->width;
        out_height = crop->height;
    }

    if (!larodConnect(&pp->conn, &error))
        panic("%s: Could not connect to larod: %s", __func__, error->msg);
    log_devices(pp->conn);

    pp->model = load_model(pp, input_format, out_width, out_height, output_format);
    setup_output(pp);

    pp->vdo_fds = g_new(int, pp->nbr_buffers);
    for (guint i = 0; i < pp->nbr_buffers; i++)
        pp->vdo_fds[i] = -1;
    pp->inputs = g_new0(larodTensor**, pp->nbr_buffers);

    pp->params = create_job_params(crop);

    syslog(LOG_INFO,
           "Preprocessing on %s: %s %ux%u -> crop %u,%u,%u,%u -> %s %ux%u (%zu bytes)",
           pp->device_name,
           input_format,
           pp->width,
           pp->height,
           crop ? crop->x : 0,
           crop ? crop->y : 0,
           out_width,
           out_height,
           output_format,
           out_width,
           out_height,
           pp->out_byte_size);
    return pp;
}

// Create and track an input tensor for a vdo buffer not seen before
static larodTensor** track_input(preprocess_t* pp, VdoBuffer* buffer) {
    larodError* error = NULL;
    int vdo_fd        = vdo_buffer_get_fd(buffer);
    int64_t offset    = vdo_buffer_get_offset(buffer);

    guint slot = 0;
    while (slot < pp->nbr_buffers && pp->vdo_fds[slot] != -1)
        slot++;
    if (slot == pp->nbr_buffers)
        panic("%s: More vdo buffers than the %u reported", __func__, pp->nbr_buffers);

    int buf_fd = vdo_fd;
    if (!pp->dmabuf) {
        buf_fd = larodConvertVmemFdToDmabuf(vdo_fd, offset, &error);
        if (buf_fd == LAROD_INVALID_FD)
            panic("%s: Failed to convert vmem fd: %s", __func__, error->msg);
        offset = 0;
    }
    int duped_fd = dup(buf_fd);
    if (duped_fd < 0)
        panic("%s: Failed to dup fd: %m", __func__);

    larodTensor** t = larodCreateTensors(1, &error);
    if (!t)
        panic("%s: Failed to create tensor: %s", __func__, error->msg);
    if (!larodSetTensorDataType(t[0], LAROD_TENSOR_DATA_TYPE_UINT8, &error) ||
        !larodSetTensorLayout(t[0], pp->in_layout, &error) ||
        !larodBuildTensorDims(t[0], pp->in_layout, pp->width, pp->height, 3, &error) ||
        !larodBuildTensorPitches(t[0], pp->in_layout, pp->pitch, pp->height, 3, &error) ||
        !larodSetTensorFdProps(t[0], LAROD_FD_PROP_MAP | LAROD_FD_PROP_DMABUF, &error) ||
        !larodSetTensorFd(t[0], duped_fd, &error) ||
        !larodSetTensorFdOffset(t[0], offset, &error) ||
        !larodSetTensorFdSize(t[0], vdo_buffer_get_capacity(buffer), &error) ||
        !larodTrackTensor(pp->conn, t[0], &error))
        panic("%s: Failed to set up input tensor: %s", __func__, error->msg);

    pp->vdo_fds[slot] = vdo_fd;
    pp->inputs[slot]  = t;
    return t;
}

void preprocess_run(preprocess_t* pp, VdoBuffer* buffer, FILE* dest_f) {
    larodError* error = NULL;
    int vdo_fd        = vdo_buffer_get_fd(buffer);

    larodTensor** input = NULL;
    for (guint i = 0; i < pp->nbr_buffers; i++) {
        if (pp->vdo_fds[i] == vdo_fd) {
            input = pp->inputs[i];
            break;
        }
    }
    if (!input)
        input = track_input(pp, buffer);

    if (!pp->req) {
        pp->req =
            larodCreateJobRequest(pp->model, input, 1, pp->outputs, 1, pp->params, &error);
        if (!pp->req)
            panic("%s: Could not create job request: %s", __func__, error->msg);
    } else if (!larodSetJobRequestInputs(pp->req, input, 1, &error)) {
        panic("%s: Failed to set job inputs: %s", __func__, error->msg);
    }
    double start = now_ms();
    if (!larodRunJob(pp->conn, pp->req, &error))
        panic("%s: Failed to run preprocessing job: %s", __func__, error->msg);
    double elapsed = now_ms() - start;

    if (pp->nbr_jobs == 0) {
        pp->first_ms = elapsed;
    } else {
        pp->sum_ms += elapsed;
        if (pp->nbr_jobs == 1 || elapsed < pp->min_ms)
            pp->min_ms = elapsed;
        if (elapsed > pp->max_ms)
            pp->max_ms = elapsed;
    }
    pp->nbr_jobs++;

    if (!fwrite(pp->out_data, pp->out_byte_size, 1, dest_f))
        panic("%s: Failed to write frame: %m", __func__);
}

void preprocess_free(preprocess_t* pp) {
    if (!pp)
        return;

    if (pp->nbr_jobs > 1)
        syslog(LOG_INFO,
               "Preprocessing time on %s: first %.2f ms, then avg %.2f ms, min %.2f ms, "
               "max %.2f ms over %u jobs",
               pp->device_name,
               pp->first_ms,
               pp->sum_ms / (pp->nbr_jobs - 1),
               pp->min_ms,
               pp->max_ms,
               pp->nbr_jobs - 1);

    larodDestroyJobRequest(&pp->req);
    larodDestroyMap(&pp->params);
    if (pp->out_data && pp->out_data != MAP_FAILED)
        munmap(pp->out_data, pp->out_fd_size);
    for (guint i = 0; i < pp->nbr_buffers; i++) {
        if (pp->inputs[i])
            larodDestroyTensors(pp->conn, &pp->inputs[i], 1, NULL);
    }
    larodDestroyTensors(pp->conn, &pp->outputs, pp->num_outputs, NULL);
    larodDestroyModel(&pp->model);
    larodDisconnect(&pp->conn, NULL);

    g_free(pp->vdo_fds);
    g_free(pp->inputs);
    g_free(pp->device_name);
    g_free(pp);
}
