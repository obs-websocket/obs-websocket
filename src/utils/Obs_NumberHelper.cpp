/*
obs-websocket
Copyright (C) 2016-2021 Stephane Lepin <stephane.lepin@gmail.com>
Copyright (C) 2020-2021 Kyle Manning <tt2468@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <stdint.h>
#include <util/util_uint64.h>

#include "Obs.h"
#include "plugin-macros.generated.h"

uint64_t Utils::Obs::NumberHelper::GetOutputDuration(obs_output_t *output)
{
	if (!output || !obs_output_active(output))
		return 0;

	video_t *video = obs_output_video(output);
	uint64_t frameTimeNs = video_output_get_frame_time(video);
	int totalFrames = obs_output_get_total_frames(output);

	// calculating the divisor is a hack to support multiple video encoders, someone please improve this in the future
	uint64_t divisor = 0ULL;
	for (size_t i = 0; i < MAX_OUTPUT_VIDEO_ENCODERS; i++) {
		obs_encoder_t *encoder = obs_output_get_video_encoder2(output, i);
		if (!encoder)
			continue;
		uint32_t encoder_divisor = obs_encoder_get_frame_rate_divisor(encoder);
		divisor += encoder_divisor <= 1 ? 1000000ULL : 1000000ULL / encoder_divisor;
	}

	return util_mul_div64(totalFrames, frameTimeNs, divisor ? divisor : 1000000ULL);
}

size_t Utils::Obs::NumberHelper::GetSceneCount()
{
	size_t ret;
	auto sceneEnumProc = [](void *param, obs_source_t *scene) {
		auto ret = static_cast<size_t *>(param);

		if (obs_source_is_group(scene))
			return true;

		(*ret)++;
		return true;
	};

	obs_enum_scenes(sceneEnumProc, &ret);

	return ret;
}

size_t Utils::Obs::NumberHelper::GetSourceFilterIndex(obs_source_t *source, obs_source_t *filter)
{
	struct FilterSearch {
		obs_source_t *filter;
		bool found;
		size_t index;
	};

	auto search = [](obs_source_t *, obs_source_t *filter, void *priv_data) {
		auto filterSearch = static_cast<FilterSearch *>(priv_data);

		if (filter == filterSearch->filter)
			filterSearch->found = true;

		if (!filterSearch->found)
			filterSearch->index++;
	};

	FilterSearch filterSearch = {filter, 0, 0};

	obs_source_enum_filters(source, search, &filterSearch);

	return filterSearch.index;
}
