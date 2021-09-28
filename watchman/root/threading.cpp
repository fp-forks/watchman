/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "watchman/QueryableView.h"
#include "watchman/TriggerCommand.h"
#include "watchman/watchman_root.h"

using namespace watchman;

std::shared_ptr<QueryableView> Root::view() const {
  // We grab a read lock on the recrawl info to ensure that we
  // can't race with scheduleRecrawl and observe a nullptr for
  // the view_.
  auto info = recrawlInfo.rlock();
  return inner.view_;
}

void Root::recrawlTriggered(const char* why) {
  recrawlInfo.wlock()->recrawlCount++;

  log(ERR, root_path, ": ", why, ": tree recrawl triggered\n");
}

void Root::scheduleRecrawl(const char* why) {
  {
    auto info = recrawlInfo.wlock();

    if (!info->shouldRecrawl) {
      info->recrawlCount++;
      if (!config.getBool("suppress_recrawl_warnings", false)) {
        info->warning = w_string::build(
            "Recrawled this watch ",
            info->recrawlCount,
            " times, most recently because:\n",
            why,
            "To resolve, please review the information on\n",
            cfg_get_trouble_url(),
            "#recrawl");
      }

      log(ERR, root_path, ": ", why, ": scheduling a tree recrawl\n");
    }
    info->shouldRecrawl = true;
  }
  view()->wakeThreads();
}

void Root::signalThreads() {
  view()->signalThreads();
}

// Cancels a watch.
bool Root::cancel() {
  bool cancelled = false;

  if (!inner.cancelled) {
    cancelled = true;

    log(DBG, "marked ", root_path, " cancelled\n");
    inner.cancelled = true;

    // The client will fan this out to all matching subscriptions.
    // This happens in listener.cpp.
    unilateralResponses->enqueue(json_object(
        {{"root", w_string_to_json(root_path)}, {"canceled", json_true()}}));

    signalThreads();
    removeFromWatched();

    {
      auto map = triggers.rlock();
      for (auto& it : *map) {
        it.second->stop();
      }
    }
  }

  return cancelled;
}

bool Root::stopWatch() {
  bool stopped = removeFromWatched();

  if (stopped) {
    cancel();
    saveGlobalStateHook_();
  }
  signalThreads();

  return stopped;
}

/* vim:ts=2:sw=2:et:
 */
