#include "config.h"

#include "dump_manager.hpp"

#include "bmc_dump_entry.hpp"
#include "dump_internal.hpp"
#include "system_dump_entry.hpp"
#include "xyz/openbmc_project/Common/error.hpp"
#include "xyz/openbmc_project/Dump/Create/error.hpp"

#include <sys/inotify.h>
#include <unistd.h>

#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/elog.hpp>
#include <regex>

namespace phosphor
{
namespace dump
{

using namespace sdbusplus::xyz::openbmc_project::Common::Error;
using namespace phosphor::logging;

namespace internal
{

void Manager::create(Type type, std::vector<std::string> fullPaths)
{
  	log<level::ERR>("In Manager::create");
    dumpMgr.phosphor::dump::Manager::captureDump(type, fullPaths);
}

} // namespace internal

uint32_t Manager::createDump()
{
  	log<level::ERR>("In Manager::createDump");
    std::vector<std::string> paths;
    return captureDump(Type::UserRequested, paths);
}

uint32_t Manager::captureDump(Type type,
                              const std::vector<std::string>& fullPaths)
{
  	log<level::ERR>("Enter Manager::captureDump");
    // Get Dump size.
    auto size = getAllowedSize();

    pid_t pid = fork();

    if (pid == 0)
    {
        fs::path dumpPath(BMC_DUMP_PATH);
        auto id = std::to_string(lastEntryId + 1);
        dumpPath /= id;

        // get dreport type map entry
        auto tempType = TypeMap.find(type);

        execl("/usr/bin/dreport", "dreport", "-d", dumpPath.c_str(), "-i",
              id.c_str(), "-s", std::to_string(size).c_str(), "-q", "-v", "-p",
              fullPaths.empty() ? "" : fullPaths.front().c_str(), "-t",
              tempType->second.c_str(), nullptr);

        // dreport script execution is failed.
        auto error = errno;
        log<level::ERR>("Error occurred during dreport function execution",
                        entry("ERRNO=%d", error));
        elog<InternalFailure>();
    }
    else if (pid > 0)
    {
        auto rc = sd_event_add_child(eventLoop.get(), nullptr, pid,
                                     WEXITED | WSTOPPED, callback, nullptr);
        if (0 > rc)
        {
            // Failed to add to event loop
            log<level::ERR>("Error occurred during the sd_event_add_child call",
                            entry("RC=%d", rc));
            elog<InternalFailure>();
        }
    }
    else
    {
        auto error = errno;
        log<level::ERR>("Error occurred during fork", entry("ERRNO=%d", error));
        elog<InternalFailure>();
    }
	log<level::ERR>("Exit Manager::captureDump");
    return ++lastEntryId;
}

void Manager::createEntry(const fs::path& file)
{
  	log<level::ERR>("Enter Manager::createEntry");
    // Dump File Name format obmcdump_ID_EPOCHTIME.EXT
    static constexpr auto ID_POS = 1;
    static constexpr auto EPOCHTIME_POS = 2;
    std::regex file_regex("obmcdump_([0-9]+)_([0-9]+).([a-zA-Z0-9]+)");

    std::smatch match;
    std::string name = file.filename();
	log<level::ERR>(name.c_str());
    if (!((std::regex_search(name, match, file_regex)) && (match.size() > 0)))
    {
        log<level::ERR>("Invalid Dump file name",
                        entry("FILENAME=%s", file.filename().c_str()));
        return;
    }

    auto idString = match[ID_POS];
    auto msString = match[EPOCHTIME_POS];

    try
    {
	  	log<level::ERR>("Trace 1 Manager::createEntry");
        auto id = stoul(idString);
        // Entry Object path.
        auto objPath = fs::path(OBJ_ENTRY) / std::to_string(id);

        entries.insert(
            std::make_pair(id, std::make_unique<bmc::Entry>(
                                   bus, objPath.c_str(), id, stoull(msString),
                                   fs::file_size(file), file, *this)));
    }
    catch (const std::invalid_argument& e)
    {
        log<level::ERR>(e.what());
        return;
    }
	log<level::ERR>("Exit Manager::createEntry");
}

void Manager::erase(uint32_t entryId)
{
  	log<level::ERR>("In Manager::erase");
    entries.erase(entryId);
}

void Manager::deleteAll()
{
  	log<level::ERR>("In Manager::deleteAll");
    auto iter = entries.begin();
    while (iter != entries.end())
    {
        auto& entry = iter->second;
        ++iter;
        entry->delete_();
    }
}

void Manager::watchCallback(const UserMap& fileInfo)
{
  	log<level::ERR>("Enter Manager::watchCallback");
    for (const auto& i : fileInfo)
    {
        // For any new dump file create dump entry object
        // and associated inotify watch.
        if (IN_CLOSE_WRITE == i.second)
        {
            removeWatch(i.first);

            createEntry(i.first);
        }
        // Start inotify watch on newly created directory.
        else if ((IN_CREATE == i.second) && fs::is_directory(i.first))
        {
		  	log<level::ERR>("Trace 1, Manager::watchCallback");
            auto watchObj = std::make_unique<Watch>(
                eventLoop, IN_NONBLOCK, IN_CLOSE_WRITE, EPOLLIN, i.first,
                std::bind(std::mem_fn(&phosphor::dump::Manager::watchCallback),
                          this, std::placeholders::_1));

            childWatchMap.emplace(i.first, std::move(watchObj));
        }
    }
	log<level::ERR>("Exit Manager::watchCallback");
}

void Manager::removeWatch(const fs::path& path)
{
  	log<level::ERR>("In Manager::removeWatch");
    // Delete Watch entry from map.
    childWatchMap.erase(path);
}

void Manager::restore()
{
  	log<level::ERR>("Enter Manager::restore");
    fs::path dir(BMC_DUMP_PATH);
    if (!fs::exists(dir) || fs::is_empty(dir))
    {
	  	log<level::ERR>("Trace 1 Manager::restore, dir not valid");
        return;
    }

    // Dump file path: <BMC_DUMP_PATH>/<id>/<filename>
    for (const auto& p : fs::directory_iterator(dir))
    {
	  	log<level::ERR>("Trace 2 Manager::restore, directory_iterator");
        auto idStr = p.path().filename().string();

        // Consider only directory's with dump id as name.
        // Note: As per design one file per directory.
        if ((fs::is_directory(p.path())) &&
            std::all_of(idStr.begin(), idStr.end(), ::isdigit))
        {
            lastEntryId =
                std::max(lastEntryId, static_cast<uint32_t>(std::stoul(idStr)));
            auto fileIt = fs::directory_iterator(p.path());
            // Create dump entry d-bus object.
            if (fileIt != fs::end(fileIt))
            {
			  	log<level::ERR>("Trace 3 Manager::restore, createEntry");
                createEntry(fileIt->path());
            }
        }
    }
	log<level::ERR>("Exit Manager::restore");
}

size_t Manager::getAllowedSize()
{
  	log<level::ERR>("Enter Manager::getAllowedSize");
    using namespace sdbusplus::xyz::openbmc_project::Dump::Create::Error;
    using Reason = xyz::openbmc_project::Dump::Create::QuotaExceeded::REASON;

    auto size = 0;

    // Get current size of the dump directory.
    for (const auto& p : fs::recursive_directory_iterator(BMC_DUMP_PATH))
    {
        if (!fs::is_directory(p))
        {
            size += fs::file_size(p);
        }
    }

    // Convert size into KB
    size = size / 1024;

    // Set the Dump size to Maximum  if the free space is greater than
    // Dump max size otherwise return the available size.

    size = (size > BMC_DUMP_TOTAL_SIZE ? 0 : BMC_DUMP_TOTAL_SIZE - size);

    if (size < BMC_DUMP_MIN_SPACE_REQD)
    {
        // Reached to maximum limit
        elog<QuotaExceeded>(Reason("Not enough space: Delete old dumps"));
    }
    if (size > BMC_DUMP_MAX_SIZE)
    {
        size = BMC_DUMP_MAX_SIZE;
    }
	log<level::ERR>("Exit Manager::getAllowedSize");
    return size;
}

void Manager::notify(NewDump::DumpType dumpType, uint32_t dumpId, uint64_t size)
{
  	log<level::ERR>("Enter Manager::notify");
    // Get the timestamp
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
    // Get the id
    auto id = lastEntryId + 1;
    auto idString = std::to_string(id);
    auto objPath = fs::path(OBJ_ENTRY) / idString;
    entries.insert(std::make_pair(
        id, std::make_unique<system::Entry>(bus, objPath.c_str(), id, ms, size,
                                            dumpId, *this)));
	log<level::ERR>(objPath.c_str());
    lastEntryId++;
}

} // namespace dump
} // namespace phosphor
