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

#include "RequestHandler.h"

RequestResult RequestHandler::GetInputList(const Request& request)
{
	std::string inputKind;

	if (request.Contains("inputKind")) {
		RequestStatus::RequestStatus statusCode;
		std::string comment;
		if (!request.ValidateOptionalString("inputKind", statusCode, comment))
			return RequestResult::Error(statusCode, comment);

		inputKind = request.RequestData["inputKind"];
	}

	json responseData;
	responseData["inputs"] = Utils::Obs::ListHelper::GetInputList(inputKind);
	return RequestResult::Success(responseData);
}

RequestResult RequestHandler::GetInputKindList(const Request& request)
{
	bool unversioned = false;

	if (request.Contains("unversioned")) {
		RequestStatus::RequestStatus statusCode;
		std::string comment;
		if (!request.ValidateOptionalBoolean("unversioned", statusCode, comment))
			return RequestResult::Error(statusCode, comment);

		unversioned = request.RequestData["unversioned"];
	}

	json responseData;
	responseData["inputKinds"] = Utils::Obs::ListHelper::GetInputKindList(unversioned);
	return RequestResult::Success(responseData);
}

RequestResult RequestHandler::CreateInput(const Request& request)
{
	RequestStatus::RequestStatus statusCode;
	std::string comment;
	OBSSourceAutoRelease sceneSource = request.ValidateScene("sceneName", statusCode, comment);
	if (!(sceneSource && request.ValidateString("inputName", statusCode, comment) && request.ValidateString("inputKind", statusCode, comment)))
		return RequestResult::Error(statusCode, comment);

	std::string inputName = request.RequestData["inputName"];
	OBSSourceAutoRelease existingInput = obs_get_source_by_name(inputName.c_str());
	if (existingInput)
		return RequestResult::Error(RequestStatus::ResourceAlreadyExists, "A source already exists by that input name.");

	std::string inputKind = request.RequestData["inputKind"];
	auto kinds = Utils::Obs::ListHelper::GetInputKindList();
	if (std::find(kinds.begin(), kinds.end(), inputKind) == kinds.end())
		return RequestResult::Error(RequestStatus::InvalidInputKind, "Your specified input kind is not supported by OBS. Check that your specified kind is properly versioned and that any necessary plugins are loaded.");

	OBSDataAutoRelease inputSettings = nullptr;
	if (request.Contains("inputSettings")) {
		if (!request.ValidateOptionalObject("inputSettings", statusCode, comment, true))
			return RequestResult::Error(statusCode, comment);

		inputSettings = Utils::Json::JsonToObsData(request.RequestData["inputSettings"]);
	}

	OBSScene scene = obs_scene_from_source(sceneSource);

	bool sceneItemEnabled = true;
	if (request.Contains("sceneItemEnabled")) {
		if (!request.ValidateOptionalBoolean("sceneItemEnabled", statusCode, comment))
			return RequestResult::Error(statusCode, comment);

		sceneItemEnabled = request.RequestData["sceneItemEnabled"];
	}

	// Create the input and add it as a scene item to the destination scene
	OBSSceneItemAutoRelease sceneItem = Utils::Obs::ActionHelper::CreateInput(inputName, inputKind, inputSettings, scene, sceneItemEnabled);

	if (!sceneItem)
		return RequestResult::Error(RequestStatus::ResourceCreationFailed, "Creation of the input or scene item failed.");

	json responseData;
	responseData["sceneItemId"] = obs_sceneitem_get_id(sceneItem);
	return RequestResult::Success(responseData);
}

RequestResult RequestHandler::RemoveInput(const Request& request)
{
	RequestStatus::RequestStatus statusCode;
	std::string comment;
	OBSSourceAutoRelease input = request.ValidateInput("inputName", statusCode, comment);
	if (!input)
		return RequestResult::Error(statusCode, comment);

	// Some implementations of removing sources release before remove, and some release after.
	// Releasing afterwards guarantees that we don't accidentally destroy the source before
	// remove if we happen to hold the last ref (very, very rare)
	obs_source_remove(input);

	return RequestResult::Success();
}

RequestResult RequestHandler::SetInputName(const Request& request)
{
	RequestStatus::RequestStatus statusCode;
	std::string comment;
	OBSSourceAutoRelease input = request.ValidateInput("inputName", statusCode, comment);
	if (!(input && request.ValidateString("newInputName", statusCode, comment)))
		return RequestResult::Error(statusCode, comment);

	std::string newInputName = request.RequestData["newInputName"];

	OBSSourceAutoRelease existingSource = obs_get_source_by_name(newInputName.c_str());
	if (existingSource)
		return RequestResult::Error(RequestStatus::ResourceAlreadyExists, "A source already exists by that new input name.");

	obs_source_set_name(input, newInputName.c_str());

	return RequestResult::Success();
}

RequestResult RequestHandler::GetInputDefaultSettings(const Request& request)
{
	RequestStatus::RequestStatus statusCode;
	std::string comment;
	if (!request.ValidateString("inputKind", statusCode, comment))
		return RequestResult::Error(statusCode, comment);

	std::string inputKind = request.RequestData["inputKind"];

	OBSDataAutoRelease defaultSettings = obs_get_source_defaults(inputKind.c_str());
	if (!defaultSettings)
		return RequestResult::Error(RequestStatus::InvalidInputKind);

	json responseData;
	responseData["defaultInputSettings"] = Utils::Json::ObsDataToJson(defaultSettings, true);
	return RequestResult::Success(responseData);
}

RequestResult RequestHandler::GetInputSettings(const Request& request)
{
	RequestStatus::RequestStatus statusCode;
	std::string comment;
	OBSSourceAutoRelease input = request.ValidateInput("inputName", statusCode, comment);
	if (!input)
		return RequestResult::Error(statusCode, comment);

	OBSDataAutoRelease inputSettings = obs_source_get_settings(input);

	json responseData;
	responseData["inputSettings"] = Utils::Json::ObsDataToJson(inputSettings);
	responseData["inputKind"] = obs_source_get_id(input);
	return RequestResult::Success(responseData);
}

RequestResult RequestHandler::SetInputSettings(const Request& request)
{
	RequestStatus::RequestStatus statusCode;
	std::string comment;
	OBSSourceAutoRelease input = request.ValidateInput("inputName", statusCode, comment);
	if (!(input && request.ValidateObject("inputSettings", statusCode, comment, true)))
		return RequestResult::Error(statusCode, comment);

	bool overlay = true;
	if (request.Contains("overlay")) {
		if (!request.ValidateOptionalBoolean("overlay", statusCode, comment))
			return RequestResult::Error(statusCode, comment);

		overlay = request.RequestData["overlay"];
	}

	// Get the new settings and convert it to obs_data_t*
	OBSDataAutoRelease newSettings = Utils::Json::JsonToObsData(request.RequestData["inputSettings"]);
	if (!newSettings)
		// This should never happen
		return RequestResult::Error(RequestStatus::RequestProcessingFailed, "An internal data conversion operation failed. Please report this!");

	if (overlay)
		// Applies the new settings on top of the existing user settings
		obs_source_update(input, newSettings);
	else
		// Clears all user settings (leaving defaults) then applies the new settings
		obs_source_reset_settings(input, newSettings);

	// Tells any open source properties windows to perform a UI refresh
	obs_source_update_properties(input);

	return RequestResult::Success();
}

RequestResult RequestHandler::GetInputMute(const Request& request)
{
	RequestStatus::RequestStatus statusCode;
	std::string comment;
	OBSSourceAutoRelease input = request.ValidateInput("inputName", statusCode, comment);
	if (!input)
		return RequestResult::Error(statusCode, comment);

	json responseData;
	responseData["inputMuted"] = obs_source_muted(input);
	return RequestResult::Success(responseData);
}

RequestResult RequestHandler::SetInputMute(const Request& request)
{
	RequestStatus::RequestStatus statusCode;
	std::string comment;
	OBSSourceAutoRelease input = request.ValidateInput("inputName", statusCode, comment);
	if (!(input && request.ValidateBoolean("inputMuted", statusCode, comment)))
		return RequestResult::Error(statusCode, comment);

	obs_source_set_muted(input, request.RequestData["inputMuted"]);

	return RequestResult::Success();
}

RequestResult RequestHandler::ToggleInputMute(const Request& request)
{
	RequestStatus::RequestStatus statusCode;
	std::string comment;
	OBSSourceAutoRelease input = request.ValidateInput("inputName", statusCode, comment);
	if (!input)
		return RequestResult::Error(statusCode, comment);

	bool inputMuted = !obs_source_muted(input);
	obs_source_set_muted(input, inputMuted);

	json responseData;
	responseData["inputMuted"] = inputMuted;
	return RequestResult::Success(responseData);
}

RequestResult RequestHandler::GetInputVolume(const Request& request)
{
	RequestStatus::RequestStatus statusCode;
	std::string comment;
	OBSSourceAutoRelease input = request.ValidateInput("inputName", statusCode, comment);
	if (!input)
		return RequestResult::Error(statusCode, comment);

	float inputVolumeMul = obs_source_get_volume(input);
	float inputVolumeDb = obs_mul_to_db(inputVolumeMul);
	if (inputVolumeDb == -INFINITY)
		inputVolumeDb = -100.0;

	json responseData;
	responseData["inputVolumeMul"] = inputVolumeMul;
	responseData["inputVolumeDb"] = inputVolumeDb;
	return RequestResult::Success(responseData);
}

RequestResult RequestHandler::SetInputVolume(const Request& request)
{
	RequestStatus::RequestStatus statusCode;
	std::string comment;
	OBSSourceAutoRelease input = request.ValidateInput("inputName", statusCode, comment);
	if (!input)
		return RequestResult::Error(statusCode, comment);

	bool hasMul = request.Contains("inputVolumeMul");
	if (hasMul && !request.ValidateOptionalNumber("inputVolumeMul", statusCode, comment, 0, 20))
		return RequestResult::Error(statusCode, comment);

	bool hasDb = request.Contains("inputVolumeDb");
	if (hasDb && !request.ValidateOptionalNumber("inputVolumeDb", statusCode, comment, -100, 26))
		return RequestResult::Error(statusCode, comment);

	if (hasMul && hasDb)
		return RequestResult::Error(RequestStatus::TooManyRequestFields, "You may only specify one volume field.");

	if (!hasMul && !hasDb)
		return RequestResult::Error(RequestStatus::MissingRequestField, "You must specify one volume field.");

	float inputVolumeMul;
	if (hasMul)
		inputVolumeMul = request.RequestData["inputVolumeMul"];
	else
		inputVolumeMul = obs_db_to_mul(request.RequestData["inputVolumeDb"]);

	obs_source_set_volume(input, inputVolumeMul);

	return RequestResult::Success();
}

RequestResult RequestHandler::GetInputAudioSyncOffset(const Request& request)
{
	RequestStatus::RequestStatus statusCode;
	std::string comment;
	OBSSourceAutoRelease input = request.ValidateInput("inputName", statusCode, comment);
	if (!input)
		return RequestResult::Error(statusCode, comment);

	json responseData;
	//									   Offset is stored in nanoseconds in OBS.
	responseData["inputAudioSyncOffset"] = obs_source_get_sync_offset(input) / 1000000;

	return RequestResult::Success(responseData);
}

RequestResult RequestHandler::SetInputAudioSyncOffset(const Request& request)
{
	RequestStatus::RequestStatus statusCode;
	std::string comment;
	OBSSourceAutoRelease input = request.ValidateInput("inputName", statusCode, comment);
	if (!(input && request.ValidateNumber("inputAudioSyncOffset", statusCode, comment, -950, 20000)))
		return RequestResult::Error(statusCode, comment);

	int64_t syncOffset = request.RequestData["inputAudioSyncOffset"];
	obs_source_set_sync_offset(input, syncOffset * 1000000);

	return RequestResult::Success();
}

RequestResult RequestHandler::GetInputAudioMonitorType(const Request& request)
{
	RequestStatus::RequestStatus statusCode;
	std::string comment;
	OBSSourceAutoRelease input = request.ValidateInput("inputName", statusCode, comment);
	if (!input)
		return RequestResult::Error(statusCode, comment);

	json responseData;
	responseData["monitorType"] = Utils::Obs::StringHelper::GetInputMonitorType(input);

	return RequestResult::Success(responseData);
}

RequestResult RequestHandler::SetInputAudioMonitorType(const Request& request)
{
	RequestStatus::RequestStatus statusCode;
	std::string comment;
	OBSSourceAutoRelease input = request.ValidateInput("inputName", statusCode, comment);
	if (!(input && request.ValidateString("monitorType", statusCode, comment)))
		return RequestResult::Error(statusCode, comment);

	enum obs_monitoring_type monitorType;
	std::string monitorTypeString = request.RequestData["monitorType"];
	if (monitorTypeString == "OBS_MONITORING_TYPE_NONE")
		monitorType = OBS_MONITORING_TYPE_NONE;
	else if (monitorTypeString == "OBS_MONITORING_TYPE_MONITOR_ONLY")
		monitorType = OBS_MONITORING_TYPE_MONITOR_ONLY;
	else if (monitorTypeString == "OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT")
		monitorType = OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT;
	else
		return RequestResult::Error(RequestStatus::InvalidRequestField, std::string("Unknown monitor type: ") + monitorTypeString);

	obs_source_set_monitoring_type(input, monitorType);

	return RequestResult::Success();
}

std::vector<json> GetListPropertyItems(obs_property_t *property)
{
	std::vector<json> ret;

	enum obs_combo_format itemFormat = obs_property_list_format(property);
	size_t itemCount = obs_property_list_item_count(property);

	for (size_t i = 0; i < itemCount; i++) {
		json itemData;
		itemData["itemName"] = obs_property_list_item_name(property, i);
		itemData["itemEnabled"] = !obs_property_list_item_disabled(property, i);
		if (itemFormat == OBS_COMBO_FORMAT_INT) {
			itemData["itemValue"] = obs_property_list_item_int(property, i);
		} else if (itemFormat == OBS_COMBO_FORMAT_FLOAT) {
			itemData["itemValue"] = obs_property_list_item_float(property, i);
		} else if (itemFormat == OBS_COMBO_FORMAT_STRING) {
			itemData["itemValue"] = obs_property_list_item_string(property, i);
		} else {
			itemData["itemValue"] = nullptr;
		}
		ret.push_back(itemData);
	}

	return ret;
}

RequestResult RequestHandler::GetInputPropertiesListPropertyItems(const Request& request)
{
	RequestStatus::RequestStatus statusCode;
	std::string comment;
	OBSSourceAutoRelease input = request.ValidateInput("inputName", statusCode, comment);
	if (!(input && request.ValidateString("propertyName", statusCode, comment)))
		return RequestResult::Error(statusCode, comment);

	std::string propertyName = request.RequestData["propertyName"];

	OBSPropertiesAutoDestroy inputProperties = obs_source_properties(input);
	obs_property_t *property = obs_properties_get(inputProperties, propertyName.c_str());
	if (!property)
		return RequestResult::Error(RequestStatus::ResourceNotFound, "Unable to find a property by that name.");
	if (obs_property_get_type(property) != OBS_PROPERTY_LIST)
		return RequestResult::Error(RequestStatus::InvalidResourceType, "The property found is not a list.");

	json responseData;
	responseData["propertyItems"] = GetListPropertyItems(property);

	return RequestResult::Success(responseData);
}

RequestResult RequestHandler::PressInputPropertiesButton(const Request& request)
{
	RequestStatus::RequestStatus statusCode;
	std::string comment;
	OBSSourceAutoRelease input = request.ValidateInput("inputName", statusCode, comment);
	if (!(input && request.ValidateString("propertyName", statusCode, comment)))
		return RequestResult::Error(statusCode, comment);

	std::string propertyName = request.RequestData["propertyName"];

	OBSPropertiesAutoDestroy inputProperties = obs_source_properties(input);
	obs_property_t *property = obs_properties_get(inputProperties, propertyName.c_str());
	if (!property)
		return RequestResult::Error(RequestStatus::ResourceNotFound, "Unable to find a property by that name.");
	if (obs_property_get_type(property) != OBS_PROPERTY_BUTTON)
		return RequestResult::Error(RequestStatus::InvalidResourceType, "The property found is not a button.");
	if (!obs_property_enabled(property))
		return RequestResult::Error(RequestStatus::InvalidResourceState, "The property item found is not enabled.");

	obs_property_button_clicked(property, input);

	return RequestResult::Success();
}
