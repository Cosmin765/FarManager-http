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

HANDLE WINAPI OpenW(const struct OpenInfo *OInfo)
{
	//FarPanelDirectory dirInfo{ sizeof(dirInfo), L"ceva"};
	//Info.PanelControl(PANEL_PASSIVE, FCTL_SETPANELDIRECTORY, 0, &dirInfo);

	// simple select menu
	//std::vector<FarMenuItem> fmi(2);
	//fmi[0] = FarMenuItem { .Flags = 0, .Text = L"Ceva" };
	//fmi[1] = FarMenuItem { .Flags = 0, .Text = L"Altceva" };
	//intptr_t BreakCode;
	//FarKey BreakKeys[]{ { VK_RETURN, SHIFT_PRESSED }, {} };
	//const auto ExitCode = Info.Menu(&MainGuid, {}, -1, -1, 0, FMENU_WRAPMODE, L"HTTP TEMP", {}, L"Contents", &BreakKeys[0], &BreakCode, fmi.data(), fmi.size());
	auto hPlugin = std::make_unique<HTTPclass>();
	return hPlugin.release();
}
