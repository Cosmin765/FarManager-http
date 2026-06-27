#include "HTTP.hpp"

HANDLE PanelHandle = INVALID_HANDLE_VALUE;

void WINAPI GetGlobalInfoW(struct GlobalInfo *GInfo)
{
	GInfo->StructSize=sizeof(struct GlobalInfo);
	GInfo->MinFarVersion=FARMANAGERVERSION;
	GInfo->Version=PLUGIN_VERSION;
	GInfo->Guid=MainGuid;
	GInfo->Title=PLUGIN_NAME;
	GInfo->Description=PLUGIN_DESC;
	GInfo->Author=PLUGIN_AUTHOR;
}

void WINAPI SetStartupInfoW(const struct PluginStartupInfo *psi)
{
	PsInfo = *psi;
}

void WINAPI GetPluginInfoW(struct PluginInfo *PInfo)
{
	PInfo->StructSize=sizeof(*PInfo);
	PInfo->Flags=PF_NONE;
	static const wchar_t *PluginMenuStrings[1];
	PluginMenuStrings[0]=GetMsg(MTitle);
	PInfo->PluginMenu.Guids=&MenuGuid;
	PInfo->PluginMenu.Strings=PluginMenuStrings;
	PInfo->PluginMenu.Count=ARRAYSIZE(PluginMenuStrings);
}

intptr_t WINAPI GetFindDataW(GetFindDataInfo* Info)
{
	return static_cast<HTTPclass*>(Info->hPanel)->GetFindData(Info->PanelItem, Info->ItemsNumber, Info->OpMode);
}

void WINAPI GetOpenPanelInfoW(OpenPanelInfo* Info)
{
	static_cast<HTTPclass*>(Info->hPanel)->GetOpenPanelInfo(Info);
}

intptr_t WINAPI ProcessPanelInputW(const ProcessPanelInputInfo* Info)
{
	return static_cast<HTTPclass*>(Info->hPanel)->ProcessKey(&Info->Rec);
}

intptr_t WINAPI PutFilesW(const PutFilesInfo* Info)
{
	return static_cast<HTTPclass*>(Info->hPanel)->PutFiles({ Info->PanelItem, Info->ItemsNumber }, Info->SrcPath, Info->OpMode);
}

intptr_t WINAPI ProcessSynchroEventW(const ProcessSynchroEventInfo* Info)
{
	if (PanelHandle == INVALID_HANDLE_VALUE)
		return FALSE;
	if (Info->Event != SE_COMMONSYNCHRO)
		return FALSE;

	SynchroAction* action = static_cast<SynchroAction*>(Info->Param);
	HTTPclass* panel = static_cast<HTTPclass*>(PanelHandle);
	return panel->ProcessSynchroEventW(action);
}


intptr_t WINAPI ProcessEditorInputW(const ProcessEditorInputInfo* Info)
{
	if (PanelHandle == INVALID_HANDLE_VALUE)
		return FALSE;
	return static_cast<HTTPclass*>(PanelHandle)->ProcessEditorKey(&Info->Rec);
}

intptr_t WINAPI ProcessEditorEventW(const ProcessEditorEventInfo* Info)
{
	if (PanelHandle == INVALID_HANDLE_VALUE)
		return FALSE;
	return static_cast<HTTPclass*>(PanelHandle)->ProcessEditorEventW(Info);
}


intptr_t WINAPI SetDirectoryW(const SetDirectoryInfo* Info)
{
	return TRUE;
}


intptr_t WINAPI ProcessViewerEventW(const ProcessViewerEventInfo* Info)
{
	if (PanelHandle == INVALID_HANDLE_VALUE)
		return FALSE;
	return static_cast<HTTPclass*>(PanelHandle)->ProcessViewerEventW(Info);
}


HANDLE WINAPI OpenW(const struct OpenInfo *OInfo)
{
	auto hPlugin = std::make_unique<HTTPclass>();
	PanelHandle = hPlugin.release();
	return PanelHandle;
}
