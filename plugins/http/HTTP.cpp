#include "HTTP.hpp"

struct PluginStartupInfo PsInfo;

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

const wchar_t *GetMsg(int MsgId)
{
	return PsInfo.GetMsg(&MainGuid,MsgId);
}

void WINAPI SetStartupInfoW(const struct PluginStartupInfo *psi)
{
	PsInfo = *psi;
}

void WINAPI GetPluginInfoW(struct PluginInfo *PInfo)
{
	PInfo->StructSize=sizeof(*PInfo);
	PInfo->Flags=PF_EDITOR;
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
	if (Info->Event != SE_COMMONSYNCHRO)
		return 0;

	HTTPclass* panel = static_cast<HTTPclass*>(Info->Param);
	PsInfo.PanelControl(panel, FCTL_UPDATEPANEL, 1, {});
	PsInfo.PanelControl(panel, FCTL_REDRAWPANEL, NULL, {});

	return 1;
}


HANDLE WINAPI OpenW(const struct OpenInfo *OInfo)
{
	auto hPlugin = std::make_unique<HTTPclass>();
	return hPlugin.release();
}
