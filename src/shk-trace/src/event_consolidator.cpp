#include "event_consolidator.h"

namespace shk {

void EventConsolidator::event(EventType type, std::string &&path) {
  switch (type) {
  case EventType::Read:
    if (_created.count(path) == 0) {
      // When a program reads from a file that it created itself, that doesn't
      // affect the result of the program; it can only see what it itself has
      // written.
      _read.insert(std::move(path));
    }
    break;

  case EventType::Write:
    if (_created.count(path) == 0) {
      // When a program writes to a file that it created itself, it is
      // sufficient to remember that the file was created by that program.
      // Ignoring writes like this makes it possible for actors that use the
      // output of this class if the program wrote to a file without completely
      // overwriting it.
      _written.insert(std::move(path));
    }
    break;

  case EventType::Create:
    // The program has now entirely overwritten the file. See WRITE
    _written.erase(path);
    // The program has now created a file at this path. There is no need to
    // say that it deleted the file earilier; that is implied by CREATE.
    _deleted.erase(path);

    _created.insert(std::move(path));
    break;

  case EventType::Delete:
    {
      // When a program writes to a file and then deletes it, it doesn't matter
      // that it wrote to the file before. (However, if it read from the file,
      // that still matters; it could have affected the program output.)
      _written.erase(path);

      auto created_it = _created.find(path);
      if (created_it == _created.end()) {
        _deleted.insert(std::move(path));
      } else {
        _created.erase(created_it);
      }
    }
    break;

  case EventType::FatalError:
    _fatal_errors.insert(std::move(path));
    break;
  }
}

std::vector<EventConsolidator::Event> EventConsolidator::getConsolidatedEventsAndReset() {
  std::vector<Event> ans;

  auto insert_events = [&](std::unordered_set<std::string> &map, EventType event_type) {
    for (auto &&entry : map) {
      ans.emplace_back(event_type, std::move(entry));
    }
  };

  insert_events(_fatal_errors, EventType::FatalError);
  insert_events(_deleted, EventType::Delete);
  insert_events(_created, EventType::Create);
  insert_events(_read, EventType::Read);
  insert_events(_written, EventType::Write);

  *this = EventConsolidator();

  return ans;
}

}  // namespace shk
