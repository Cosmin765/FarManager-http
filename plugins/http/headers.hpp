#include <list>
#include <memory>
#include <string>
#include <string_view>
#include <span>
#include <vector>
#include <utility>
#include <format>
#include <filesystem>
#include <assert.h>
#include <unordered_set>

#include <plugin.hpp>
#include <initguid.h>
#include <string_utils.hpp>
#include <DlgBuilder.hpp>
#include <PluginSettings.hpp>
#include <scope_exit.hpp>

#include "curl/curl.h"

using string = std::wstring;
using string_view = std::wstring_view;
