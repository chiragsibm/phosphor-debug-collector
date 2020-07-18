#include "config.h"

#include "dump_internal.hpp"
#include "dump_manager.hpp"
#include "elog_watch.hpp"
#include "watch.hpp"
#include "xyz/openbmc_project/Common/error.hpp"

#include <phosphor-logging/elog-errors.hpp>
#include <sdbusplus/bus.hpp>

int main(int argc, char* argv[])
{
    using namespace phosphor::logging;
    using InternalFailure =
        sdbusplus::xyz::openbmc_project::Common::Error::InternalFailure;

  	log<level::ERR>("Enter dump main");
    auto bus = sdbusplus::bus::new_default();
    sd_event* event = nullptr;
	log<level::ERR>("Trace 1 dump main, before sd_event_default");
    auto rc = sd_event_default(&event);
    if (rc < 0)
    {
        log<level::ERR>("Error occurred during the sd_event_default",
                        entry("RC=%d", rc));
        report<InternalFailure>();
        return rc;
    }
	log<level::ERR>("Trace 2 dump main, before eventP");
    phosphor::dump::EventPtr eventP{event};
    event = nullptr;

    // Blocking SIGCHLD is needed for calling sd_event_add_child
    sigset_t mask;
    if (sigemptyset(&mask) < 0)
    {
        log<level::ERR>("Unable to initialize signal set",
                        entry("ERRNO=%d", errno));
        return EXIT_FAILURE;
    }

    if (sigaddset(&mask, SIGCHLD) < 0)
    {
        log<level::ERR>("Unable to add signal to signal set",
                        entry("ERRNO=%d", errno));
        return EXIT_FAILURE;
    }

    // Block SIGCHLD first, so that the event loop can handle it
    if (sigprocmask(SIG_BLOCK, &mask, nullptr) < 0)
    {
        log<level::ERR>("Unable to block signal", entry("ERRNO=%d", errno));
        return EXIT_FAILURE;
    }

	log<level::ERR>("Trace 3 dump main, before bus.request_name");
    // Add sdbusplus ObjectManager for the 'root' path of the DUMP manager.
    sdbusplus::server::manager::manager objManager(bus, DUMP_OBJPATH);
    bus.request_name(DUMP_BUSNAME);

    try
    {
	  	log<level::ERR>("Trace 4 dump main, before manager");
        phosphor::dump::Manager manager(bus, eventP, DUMP_OBJPATH);
        // Restore dump d-bus objects.
        manager.restore();
		log<level::ERR>("Trace 5 dump main, before Manager mgr");
        phosphor::dump::internal::Manager mgr(bus, manager, OBJ_INTERNAL);
		log<level::ERR>("Trace 6 dump main, before Watch eWatch");
        phosphor::dump::elog::Watch eWatch(bus, mgr);
		log<level::ERR>("Trace 7 dump main, before bus.attach_event");
        bus.attach_event(eventP.get(), SD_EVENT_PRIORITY_NORMAL);

		log<level::ERR>("Trace 8 dump main, before sd_event_loop");
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
	log<level::ERR>("Exit dump main");
    return 0;
}
