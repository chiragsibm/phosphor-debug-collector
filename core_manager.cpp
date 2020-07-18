#include "config.h"

#include "core_manager.hpp"

#include <experimental/filesystem>
#include <phosphor-logging/log.hpp>
#include <regex>
#include <sdbusplus/exception.hpp>

namespace phosphor
{
namespace dump
{
namespace core
{

using namespace phosphor::logging;
using namespace std;

void Manager::watchCallback(const UserMap& fileInfo)
{
  	log<level::ERR>("Enter core watchCallback");
    vector<string> files;
    for (const auto& i : fileInfo)
    {
        namespace fs = std::experimental::filesystem;
        fs::path file(i.first);
        std::string name = file.filename();

        /*
          As per coredump source code systemd-coredump uses below format
          https://github.com/systemd/systemd/blob/master/src/coredump/coredump.c
          /var/lib/systemd/coredump/core.%s.%s." SD_ID128_FORMAT_STR “
          systemd-coredump also creates temporary file in core file path prior
          to actual core file creation. Checking the file name format will help
          to limit dump creation only for the new core files.
        */
        if ("core" == name.substr(0, name.find('.')))
        {
		  	log<level::ERR>("Trace1 core watchCallback, core file name");
		  	log<level::ERR>(name.c_str());
            // Consider only file name start with "core."
            files.push_back(file);
        }
    }

    if (!files.empty())
    {
	  	log<level::ERR>("Trace2 core watchCallback, calling createHelper as file is not empty");
        createHelper(files);
    }
	log<level::ERR>("Exit core watchCallabck");
}

void Manager::createHelper(const vector<string>& files)
{
  	log<level::ERR>("Enter core createHelper");
    constexpr auto MAPPER_BUSNAME = "xyz.openbmc_project.ObjectMapper";
    constexpr auto MAPPER_PATH = "/xyz/openbmc_project/object_mapper";
    constexpr auto MAPPER_INTERFACE = "xyz.openbmc_project.ObjectMapper";
    constexpr auto IFACE_INTERNAL("xyz.openbmc_project.Dump.Internal.Create");
    constexpr auto APPLICATION_CORED =
        "xyz.openbmc_project.Dump.Internal.Create.Type.ApplicationCored";
    auto b = sdbusplus::bus::new_default();
    auto mapper = b.new_method_call(MAPPER_BUSNAME, MAPPER_PATH,
                                    MAPPER_INTERFACE, "GetObject");
    mapper.append(OBJ_INTERNAL, vector<string>({IFACE_INTERNAL}));

    auto mapperResponseMsg = b.call(mapper);
    if (mapperResponseMsg.is_method_error())
    {
        log<level::ERR>("Error in mapper call");
        return;
    }

	log<level::ERR>("Trace1 Core createHelper, After method_call GETOBJECT");
    map<string, vector<string>> mapperResponse;
    try
    {
	  	log<level::ERR>("Trace2 core createHelper before, mapperResponseMsg.read");
        mapperResponseMsg.read(mapperResponse);
    }
    catch (const sdbusplus::exception::SdBusError& e)
    {
        log<level::ERR>(
            "Failed to parse dump create message", entry("ERROR=%s", e.what()),
            entry("REPLY_SIG=%s", mapperResponseMsg.get_signature()));
        return;
    }
    if (mapperResponse.empty())
    {
        log<level::ERR>("Error reading mapper response");
        return;
    }

	log<level::ERR>("Trace3 Core createHelper, Before calling method_call Create");
    const auto& host = mapperResponse.cbegin()->first;
    auto m =
        b.new_method_call(host.c_str(), OBJ_INTERNAL, IFACE_INTERNAL, "Create");
    m.append(APPLICATION_CORED, files);
    b.call_noreply(m);
	log<level::ERR>("Exit core createHealper");
}

} // namespace core
} // namespace dump
} // namespace phosphor
