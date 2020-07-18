#include "config.h"

#include "core_manager.hpp"
#include "watch.hpp"
#include "xyz/openbmc_project/Common/error.hpp"

#include <phosphor-logging/elog-errors.hpp>
#include <sdbusplus/bus.hpp>

int main(int argc, char* argv[])
{
    using namespace phosphor::logging;
	log<level::ERR>("Enter core main");
    using InternalFailure =
        sdbusplus::xyz::openbmc_project::Common::Error::InternalFailure;

    auto bus = sdbusplus::bus::new_default();
    sd_event* event = nullptr;
	log<level::ERR>("Trace 1 core main, before sd_event_default");
    auto rc = sd_event_default(&event);
    if (rc < 0)
    {
        log<level::ERR>("Error occurred during the sd_event_default",
                        entry("RC=%d", rc));
        report<InternalFailure>();
        return -1;
    }
	log<level::ERR>("Trace 2 core main, before eventP");
    phosphor::dump::EventPtr eventP{event};
    event = nullptr;

    try
    {
        phosphor::dump::core::Manager manager(eventP);
		log<level::ERR>("Trace 3 core main, Calling sd_event_loop");
        auto rc = sd_event_loop(eventP.get());
        if (rc < 0)
        {
            log<level::ERR>("Error occurred during the sd_event_loop",
                            entry("RC=%d", rc));
            elog<InternalFailure>();
        }
    }

    catch (InternalFailure& e)
    {
        commit<InternalFailure>();
        return -1;
    }
	log<level::ERR>("Exit core main");
    return 0;
}
