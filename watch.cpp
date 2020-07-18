#include "watch.hpp"

#include "xyz/openbmc_project/Common/error.hpp"

#include <phosphor-logging/elog-errors.hpp>

namespace phosphor
{
namespace dump
{
namespace inotify
{

using namespace std::string_literals;
using namespace phosphor::logging;
using namespace sdbusplus::xyz::openbmc_project::Common::Error;

Watch::~Watch()
{
    if ((fd() >= 0) && (wd >= 0))
    {
        inotify_rm_watch(fd(), wd);
    }
}

Watch::Watch(const EventPtr& eventObj, const int flags, const uint32_t mask,
             const uint32_t events, const fs::path& path, UserType userFunc) :
    flags(flags),
    mask(mask), events(events), path(path), fd(inotifyInit()),
    userFunc(userFunc)
{
  	log<level::ERR>("Enter Watch");
  	log<level::ERR>("Watch directory",entry("DIR=%s", path.c_str()));
    // Check if watch DIR exists.
    if (!fs::is_directory(path))
    {
        log<level::ERR>("Watch directory doesn't exist",
                        entry("DIR=%s", path.c_str()));
        elog<InternalFailure>();
    }
	log<level::ERR>(path.c_str());
	log<level::ERR>("Trace 1 in Watch, before inotify_add_watch");
    wd = inotify_add_watch(fd(), path.c_str(), mask);
    if (-1 == wd)
    {
        auto error = errno;
        log<level::ERR>("Error occurred during the inotify_add_watch call",
                        entry("ERRNO=%d", error));
        elog<InternalFailure>();
    }
	log<level::ERR>("Trace 2 in Watch, before sd_event_add_io");
    auto rc =
        sd_event_add_io(eventObj.get(), nullptr, fd(), events, callback, this);
    if (0 > rc)
    {
        // Failed to add to event loop
        log<level::ERR>("Error occurred during the sd_event_add_io call",
                        entry("RC=%d", rc));
        elog<InternalFailure>();
    }
	log<level::ERR>("Exit Watch");
}

int Watch::inotifyInit()
{
  	log<level::ERR>("Entering inotifyInit in watch.cpp");
    auto fd = inotify_init1(flags);

    if (-1 == fd)
    {
        auto error = errno;
        log<level::ERR>("Error occurred during the inotify_init1",
                        entry("ERRNO=%d", error));
        elog<InternalFailure>();
    }
	log<level::ERR>("Exiting inotifyInit in watch.cpp");
    return fd;
}

int Watch::callback(sd_event_source* s, int fd, uint32_t revents,
                    void* userdata)
{
  	log<level::ERR>("Enter Watch::callback");
    auto userData = static_cast<Watch*>(userdata);

    if (!(revents & userData->events))
    {
	  	log<level::ERR>("Trace in Watch::callback, returning 0");
        return 0;
    }

    // Maximum inotify events supported in the buffer
    constexpr auto maxBytes = sizeof(struct inotify_event) + NAME_MAX + 1;
    uint8_t buffer[maxBytes];

    auto bytes = read(fd, buffer, maxBytes);
	log<level::ERR>("Trace 1 in Watch::callback, getting the bytes");
    if (0 > bytes)
    {
        // Failed to read inotify event
        // Report error and return
        auto error = errno;
        log<level::ERR>("Error occurred during the read",
                        entry("ERRNO=%d", error));
        report<InternalFailure>();
        return 0;
    }

    auto offset = 0;

    UserMap userMap;
	log<level::ERR>("Trace 2 in Watch::callback, before entering while loop");
    while (offset < bytes)
    {
        auto event = reinterpret_cast<inotify_event*>(&buffer[offset]);
        auto mask = event->mask & userData->mask;

        if (mask)
        {
            userMap.emplace((userData->path / event->name), mask);
        }

        offset += offsetof(inotify_event, name) + event->len;
    }

    // Call user call back function in case valid data in the map
    if (!userMap.empty())
    {
	  	log<level::ERR>("Trace 3 in Watch::callback, unserMap is not empty");
        userData->userFunc(userMap);
    }
	log<level::ERR>("Exit Watch::callback");
    return 0;
}

} // namespace inotify
} // namespace dump
} // namespace phosphor
