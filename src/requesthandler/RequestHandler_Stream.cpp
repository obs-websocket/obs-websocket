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

#include <QBuffer>
#include <QImageWriter>
#include <QFileInfo>
#include <QImage>
#include <QDir>

#include "RequestHandler.h"

QImage TakeStreamScreenshot(bool &success, uint32_t requestedWidth = 0, uint32_t requestedHeight = 0)
{
	// Get info about the program
	obs_video_info ovi;
	obs_get_video_info(&ovi);
	const uint32_t streamWidth = ovi.base_width;
	const uint32_t streamHeight = ovi.base_height;
	const double streamAspectRatio = ((double)streamWidth / (double)streamHeight);

	uint32_t imgWidth = streamWidth;
	uint32_t imgHeight = streamHeight;

	// Determine suitable image width
	if (requestedWidth) {
		imgWidth = requestedWidth;

		if (!requestedHeight)
			imgHeight = ((double)imgWidth / streamAspectRatio);
	}

	// Determine suitable image height
	if (requestedHeight) {
		imgHeight = requestedHeight;

		if (!requestedWidth)
			imgWidth = ((double)imgHeight * streamAspectRatio);
	}

	// Create final image texture
	QImage ret(imgWidth, imgHeight, QImage::Format::Format_RGBA8888);
	ret.fill(0);

	// Video image buffer
	uint8_t *videoData = nullptr;
	uint32_t videoLinesize = 0;

	// Enter graphics context
	obs_enter_graphics();

	gs_texrender_t *texRender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_stagesurf_t *stageSurface = gs_stagesurface_create(imgWidth, imgHeight, GS_RGBA);

	success = false;
	gs_texrender_reset(texRender);
	if (gs_texrender_begin(texRender, imgWidth, imgHeight)) {
		vec4 background;
		vec4_zero(&background);

		gs_clear(GS_CLEAR_COLOR, &background, 0.0f, 0);
		gs_ortho(0.0f, (float)streamWidth, 0.0f, (float)streamHeight, -100.0f, 100.0f);

		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

		obs_render_main_texture();

		gs_blend_state_pop();
		gs_texrender_end(texRender);

		gs_stage_texture(stageSurface, gs_texrender_get_texture(texRender));
		if (gs_stagesurface_map(stageSurface, &videoData, &videoLinesize)) {
			int lineSize = ret.bytesPerLine();
			for (uint y = 0; y < imgHeight; y++) {
				memcpy(ret.scanLine(y), videoData + (y * videoLinesize), lineSize);
			}
			gs_stagesurface_unmap(stageSurface);
			success = true;
		}
	}

	gs_stagesurface_destroy(stageSurface);
	gs_texrender_destroy(texRender);

	obs_leave_graphics();

	return ret;
}

bool IsStreamImageFormatValid(std::string format)
{
	QByteArrayList supportedFormats = QImageWriter::supportedImageFormats();
	return supportedFormats.contains(format.c_str());
}

/**
 * Gets the status of the stream output.
 *
 * @responseField outputActive        | Boolean | Whether the output is active
 * @responseField outputReconnecting  | Boolean | Whether the output is currently reconnecting
 * @responseField outputTimecode      | String  | Current formatted timecode string for the output
 * @responseField outputDuration      | Number  | Current duration in milliseconds for the output
 * @responseField outputCongestion    | Number  | Congestion of the output
 * @responseField outputBytes         | Number  | Number of bytes sent by the output
 * @responseField outputSkippedFrames | Number  | Number of frames skipped by the output's process
 * @responseField outputTotalFrames   | Number  | Total number of frames delivered by the output's process
 *
 * @requestType GetStreamStatus
 * @complexity 2
 * @rpcVersion -1
 * @initialVersion 5.0.0
 * @api requests
 * @category stream
 */
RequestResult RequestHandler::GetStreamStatus(const Request &)
{
	OBSOutputAutoRelease streamOutput = obs_frontend_get_streaming_output();

	uint64_t outputDuration = Utils::Obs::NumberHelper::GetOutputDuration(streamOutput);

	float outputCongestion = obs_output_get_congestion(streamOutput);
	if (std::isnan(outputCongestion)) // libobs does not handle NaN, so we're handling it here
		outputCongestion = 0.0f;

	json responseData;
	responseData["outputActive"] = obs_output_active(streamOutput);
	responseData["outputReconnecting"] = obs_output_reconnecting(streamOutput);
	responseData["outputTimecode"] = Utils::Obs::StringHelper::DurationToTimecode(outputDuration);
	responseData["outputDuration"] = outputDuration;
	responseData["outputCongestion"] = outputCongestion;
	responseData["outputBytes"] = (uint64_t)obs_output_get_total_bytes(streamOutput);
	responseData["outputSkippedFrames"] = obs_output_get_frames_dropped(streamOutput);
	responseData["outputTotalFrames"] = obs_output_get_total_frames(streamOutput);

	return RequestResult::Success(responseData);
}

/**
 * Toggles the status of the stream output.
 *
 * @responseField outputActive | Boolean | New state of the stream output
 *
 * @requestType ToggleStream
 * @complexity 1
 * @rpcVersion -1
 * @initialVersion 5.0.0
 * @api requests
 * @category stream
 */
RequestResult RequestHandler::ToggleStream(const Request &)
{
	json responseData;
	if (obs_frontend_streaming_active()) {
		obs_frontend_streaming_stop();
		responseData["outputActive"] = false;
	} else {
		obs_frontend_streaming_start();
		responseData["outputActive"] = true;
	}

	return RequestResult::Success(responseData);
}

/**
 * Starts the stream output.
 *
 * @requestType StartStream
 * @complexity 1
 * @rpcVersion -1
 * @initialVersion 5.0.0
 * @api requests
 * @category stream
 */
RequestResult RequestHandler::StartStream(const Request &)
{
	if (obs_frontend_streaming_active())
		return RequestResult::Error(RequestStatus::OutputRunning);

	// TODO: Call signal directly to perform blocking wait
	obs_frontend_streaming_start();

	return RequestResult::Success();
}

/**
 * Stops the stream output.
 *
 * @requestType StopStream
 * @complexity 1
 * @rpcVersion -1
 * @initialVersion 5.0.0
 * @api requests
 * @category stream
 */
RequestResult RequestHandler::StopStream(const Request &)
{
	if (!obs_frontend_streaming_active())
		return RequestResult::Error(RequestStatus::OutputNotRunning);

	// TODO: Call signal directly to perform blocking wait
	obs_frontend_streaming_stop();

	return RequestResult::Success();
}

/**
 * Sends CEA-608 caption text over the stream output.
 *
 * @requestField captionText | String | Caption text
 *
 * @requestType SendStreamCaption
 * @complexity 2
 * @rpcVersion -1
 * @initialVersion 5.0.0
 * @category stream
 * @api requests
 */
RequestResult RequestHandler::SendStreamCaption(const Request &request)
{
	RequestStatus::RequestStatus statusCode;
	std::string comment;
	if (!request.ValidateString("captionText", statusCode, comment, true))
		return RequestResult::Error(statusCode, comment);

	if (!obs_frontend_streaming_active())
		return RequestResult::Error(RequestStatus::OutputNotRunning);

	std::string captionText = request.RequestData["captionText"];

	OBSOutputAutoRelease output = obs_frontend_get_streaming_output();

	// 0.0 means no delay until the next caption can be sent
	obs_output_output_caption_text2(output, captionText.c_str(), 0.0);

	return RequestResult::Success();
}

/**
 * Gets a Base64-encoded screenshot of the stream.
 *
 * The `imageWidth` and `imageHeight` parameters are treated as "scale to inner", meaning the smallest ratio will be used and the aspect ratio of the original resolution is kept.
 * If `imageWidth` and `imageHeight` are not specified, the compressed image will use the full resolution of the stream.
 *
 * @requestField imageFormat              | String | Image compression format to use. Use `GetVersion` to get compatible image formats
 * @requestField ?imageWidth              | Number | Width to scale the screenshot to                                                                                         | >= 8, <= 4096 | Stream value is used
 * @requestField ?imageHeight             | Number | Height to scale the screenshot to                                                                                        | >= 8, <= 4096 | Stream value is used
 * @requestField ?imageCompressionQuality | Number | Compression quality to use. 0 for high compression, 100 for uncompressed. -1 to use "default" (whatever that means, idk) | >= -1, <= 100 | -1
 *
 * @responseField imageData | String | Base64-encoded screenshot
 *
 * @requestType GetOutputScreenshot
 * @complexity 4
 * @rpcVersion -1
 * @initialVersion 5.4.0
 * @category stream
 * @api requests
 */
RequestResult RequestHandler::GetStreamScreenshot(const Request &request)
{
	RequestStatus::RequestStatus statusCode;
	std::string comment;
	std::string imageFormat = request.RequestData["imageFormat"];

	if (!IsStreamImageFormatValid(imageFormat))
		return RequestResult::Error(RequestStatus::InvalidRequestField,
					    "Your specified image format is invalid or not supported by this system.");

	uint32_t requestedWidth{0};
	uint32_t requestedHeight{0};
	int compressionQuality{-1};

	if (request.Contains("imageWidth")) {
		if (!request.ValidateOptionalNumber("imageWidth", statusCode, comment, 8, 4096))
			return RequestResult::Error(statusCode, comment);

		requestedWidth = request.RequestData["imageWidth"];
	}

	if (request.Contains("imageHeight")) {
		if (!request.ValidateOptionalNumber("imageHeight", statusCode, comment, 8, 4096))
			return RequestResult::Error(statusCode, comment);

		requestedHeight = request.RequestData["imageHeight"];
	}

	if (request.Contains("imageCompressionQuality")) {
		if (!request.ValidateOptionalNumber("imageCompressionQuality", statusCode, comment, -1, 100))
			return RequestResult::Error(statusCode, comment);

		compressionQuality = request.RequestData["imageCompressionQuality"];
	}

	bool success;
	QImage renderedImage = TakeStreamScreenshot(success, requestedWidth, requestedHeight);

	if (!success)
		return RequestResult::Error(RequestStatus::RequestProcessingFailed, "Failed to render screenshot.");

	QByteArray encodedImgBytes;
	QBuffer buffer(&encodedImgBytes);
	buffer.open(QBuffer::WriteOnly);

	if (!renderedImage.save(&buffer, imageFormat.c_str(), compressionQuality))
		return RequestResult::Error(RequestStatus::RequestProcessingFailed, "Failed to encode screenshot.");

	buffer.close();

	QString encodedPicture = QString("data:image/%1;base64,").arg(imageFormat.c_str()).append(encodedImgBytes.toBase64());

	json responseData;
	responseData["imageData"] = encodedPicture.toStdString();
	return RequestResult::Success(responseData);
}
