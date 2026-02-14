#include "HTTP.hpp"

HTTPTemplateDialog::HTTPTemplateDialog()
	: PluginDialogBuilder(PsInfo, MainGuid, ConfigDialogGuid, MTitle, TEXT("Template"))
{

}

bool HTTPTemplateDialog::ShowDialog()
{
	return true;
}
