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

#pragma once

#include "vdo-map.h"
#include "vdo-stream.h"

#include <glib.h>
#include <stdio.h>

typedef struct {
    guint x;
    guint y;
    guint width;
    guint height;
} preprocess_crop_t;

typedef struct preprocess preprocess_t;

/**
 * Parse a crop argument of the form "x,y,width,height".
 * Returns FALSE if the string is malformed or width/height is zero.
 */
gboolean preprocess_parse_crop(const gchar* str, preprocess_crop_t* crop);

/**
 * Set up a larod (cpu-proc) pre-processing job for frames of the vdo stream
 * described by @p info. The input format is "nv12" or "rgb-interleaved".
 * If @p crop is NULL the whole frame is converted; otherwise only the crop
 * region is, and the output size equals the crop size.
 * @p output_format is "rgb-interleaved", "rgb-planar" or "nv12".
 * @p device_name is the larod pre-processing device, e.g. "cpu-proc" or
 * "a9-gpu-proc".
 */
preprocess_t* preprocess_new(VdoMap* info,
                             const gchar* input_format,
                             const preprocess_crop_t* crop,
                             const gchar* output_format,
                             const gchar* device_name);

/**
 * Run the pre-processing job on @p buffer and write the result to @p dest_f.
 * The job time is measured, and summarized in the log by preprocess_free().
 */
void preprocess_run(preprocess_t* pp, VdoBuffer* buffer, FILE* dest_f);

void preprocess_free(preprocess_t* pp);
