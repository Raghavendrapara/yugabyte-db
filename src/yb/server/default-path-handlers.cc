// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "yb/server/default-path-handlers.h"

#include <sys/stat.h>

#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <boost/algorithm/string.hpp>

#ifdef TCMALLOC_ENABLED
#include <gperftools/malloc_extension.h>
#endif

#include "yb/fs/fs_manager.h"

#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/human_readable.h"
#include "yb/gutil/strings/split.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/server/pprof-path-handlers.h"
#include "yb/server/webserver.h"
#include "yb/util/flag_tags.h"
#include "yb/util/format.h"
#include "yb/util/histogram.pb.h"
#include "yb/util/logging.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/memory/memory.h"
#include "yb/util/metrics.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/result.h"
#include "yb/util/status_log.h"
#include "yb/util/version_info.h"
#include "yb/util/version_info.pb.h"

DEFINE_uint64(web_log_bytes, 1024 * 1024,
    "The maximum number of bytes to display on the debug webserver's log page");
DECLARE_int32(max_tables_metrics_breakdowns);
TAG_FLAG(web_log_bytes, advanced);
TAG_FLAG(web_log_bytes, runtime);

namespace yb {

using boost::replace_all;
using google::CommandlineFlagsIntoString;
using std::ifstream;
using std::string;
using std::endl;
using std::shared_ptr;
using strings::Substitute;

using namespace std::placeholders;

namespace {

// Html/Text formatting tags
struct Tags {
  string pre_tag, end_pre_tag, line_break, header, end_header;

  // If as_text is true, set the html tags to a corresponding raw text representation.
  explicit Tags(bool as_text) {
    if (as_text) {
      pre_tag = "";
      end_pre_tag = "\n";
      line_break = "\n";
      header = "";
      end_header = "";
    } else {
      pre_tag = "<pre>";
      end_pre_tag = "</pre>";
      line_break = "<br/>";
      header = "<h2>";
      end_header = "</h2>";
    }
  }
};

// Writes the last FLAGS_web_log_bytes of the INFO logfile to a webpage
// Note to get best performance, set GLOG_logbuflevel=-1 to prevent log buffering
static void LogsHandler(const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  bool as_text = (req.parsed_args.find("raw") != req.parsed_args.end());
  Tags tags(as_text);
  string logfile;
  GetFullLogFilename(google::INFO, &logfile);
  (*output) << tags.header <<"INFO logs" << tags.end_header << endl;
  (*output) << "Log path is: " << logfile << endl;

  struct stat file_stat;
  if (stat(logfile.c_str(), &file_stat) == 0) {
    size_t size = file_stat.st_size;
    size_t seekpos = size < FLAGS_web_log_bytes ? 0L : size - FLAGS_web_log_bytes;
    ifstream log(logfile.c_str(), std::ios::in);
    // Note if the file rolls between stat and seek, this could fail
    // (and we could wind up reading the whole file). But because the
    // file is likely to be small, this is unlikely to be an issue in
    // practice.
    log.seekg(seekpos);
    (*output) << tags.line_break <<"Showing last " << FLAGS_web_log_bytes
              << " bytes of log" << endl;
    (*output) << tags.line_break << tags.pre_tag << log.rdbuf() << tags.end_pre_tag;

  } else {
    (*output) << tags.line_break << "Couldn't open INFO log file: " << logfile;
  }
}

// Registered to handle "/flags", and prints out all command-line flags and their values
static void FlagsHandler(const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  bool as_text = (req.parsed_args.find("raw") != req.parsed_args.end());
  Tags tags(as_text);
  (*output) << tags.header << "Command-line Flags" << tags.end_header;
  (*output) << tags.pre_tag;
  std::vector<google::CommandLineFlagInfo> flag_infos;
  google::GetAllFlags(&flag_infos);
  for (const auto& flag_info : flag_infos) {
    (*output) << "--" << flag_info.name << "=";
    std::unordered_set<string> tags;
    GetFlagTags(flag_info.name, &tags);
    if (PREDICT_FALSE(ContainsKey(tags, "sensitive_info"))) {
      (*output) << "****" << endl;
    } else {
      (*output) << flag_info.current_value << endl;
    }
  }
  (*output) << tags.end_pre_tag;
}

// Registered to handle "/status", and simply returns empty JSON.
static void StatusHandler(const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  (*output) << "{}";
}

// Registered to handle "/memz", and prints out memory allocation statistics.
static void MemUsageHandler(const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  bool as_text = (req.parsed_args.find("raw") != req.parsed_args.end());
  Tags tags(as_text);

  (*output) << tags.pre_tag;
#ifndef TCMALLOC_ENABLED
  (*output) << "Memory tracking is not available unless tcmalloc is enabled.";
#else
  auto tmp = TcMallocStats();
  // Replace new lines with <br> for html.
  replace_all(tmp, "\n", tags.line_break);
  (*output) << tmp << tags.end_pre_tag;
#endif
}

// Registered to handle "/mem-trackers", and prints out to handle memory tracker information.
static void MemTrackersHandler(const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  *output << "<h1>Memory usage by subsystem</h1>\n";
  *output << "<table class='table table-striped'>\n";
  *output << "  <tr><th>Id</th><th>Current Consumption</th>"
      "<th>Peak consumption</th><th>Limit</th></tr>\n";

  int max_depth = INT_MAX;
  string depth = FindWithDefault(req.parsed_args, "max_depth", "");
  if (depth != "") {
    max_depth = std::stoi(depth);
  }

  std::vector<MemTrackerData> trackers;
  CollectMemTrackerData(MemTracker::GetRootTracker(), 0, &trackers);
  for (const auto& data : trackers) {
    // If the data.depth >= max_depth, skip the info.
    if (data.depth > max_depth) {
      continue;
    }
    const auto& tracker = data.tracker;
    const std::string limit_str =
        tracker->limit() == -1 ? "none" : HumanReadableNumBytes::ToString(tracker->limit());
    const std::string current_consumption_str =
        HumanReadableNumBytes::ToString(tracker->consumption());
    const std::string peak_consumption_str =
        HumanReadableNumBytes::ToString(tracker->peak_consumption());
    *output << Format("  <tr data-depth=\"$0\" class=\"level$0\">\n", data.depth);
    *output << "    <td>" << tracker->id() << "</td>";
    // UpdateConsumption returns true if consumption is taken from external source,
    // for instance tcmalloc stats. So we should show only it in this case.
    if (!data.consumption_excluded_from_ancestors || data.tracker->UpdateConsumption()) {
      *output << Format("<td>$0</td>", current_consumption_str);
    } else {
      auto full_consumption_str = HumanReadableNumBytes::ToString(
          tracker->consumption() + data.consumption_excluded_from_ancestors);
      *output << Format("<td>$0 ($1)</td>", current_consumption_str, full_consumption_str);
    }
    *output << Format("<td>$0</td><td>$1</td>\n", peak_consumption_str, limit_str);
    *output << "  </tr>\n";
  }

  *output << "</table>\n";
}

static MetricLevel MetricLevelFromName(const std::string& level) {
  if (level == "debug") {
    return MetricLevel::kDebug;
  } else if (level == "info") {
    return MetricLevel::kInfo;
  } else if (level == "warn") {
    return MetricLevel::kWarn;
  } else {
    return MetricLevel::kDebug;
  }
}

static void ParseRequestOptions(const Webserver::WebRequest& req,
                                vector<string> *requested_metrics,
                                MetricPrometheusOptions *promethus_opts,
                                MetricJsonOptions *json_opts = nullptr,
                                JsonWriter::Mode *json_mode = nullptr) {

  if (requested_metrics) {
    const string* requested_metrics_param = FindOrNull(req.parsed_args, "metrics");
    if (requested_metrics_param != nullptr) {
      SplitStringUsing(*requested_metrics_param, ",", requested_metrics);
    } else {
      // Default to including all metrics.
      requested_metrics->push_back("*");
    }
  }

  string arg;
  if (json_opts) {
    arg = FindWithDefault(req.parsed_args, "include_raw_histograms", "false");
    json_opts->include_raw_histograms = ParseLeadingBoolValue(arg.c_str(), false);

    arg = FindWithDefault(req.parsed_args, "include_schema", "false");
    json_opts->include_schema_info = ParseLeadingBoolValue(arg.c_str(), false);

    arg = FindWithDefault(req.parsed_args, "level", "debug");
    json_opts->level = MetricLevelFromName(arg);
  }

  if (promethus_opts) {
    arg = FindWithDefault(req.parsed_args, "level", "debug");
    promethus_opts->level = MetricLevelFromName(arg);
    promethus_opts->max_tables_metrics_breakdowns = std::stoi(FindWithDefault(req.parsed_args,
      "max_tables_metrics_breakdowns", std::to_string(FLAGS_max_tables_metrics_breakdowns)));
    promethus_opts->priority_regex = FindWithDefault(req.parsed_args, "priority_regex", "");
  }

  if (json_mode) {
    arg = FindWithDefault(req.parsed_args, "compact", "false");
    *json_mode =
        ParseLeadingBoolValue(arg.c_str(), false) ? JsonWriter::COMPACT : JsonWriter::PRETTY;
  }
}

static void WriteMetricsAsJson(const MetricRegistry* const metrics,
                               const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  vector<string> requested_metrics;
  MetricJsonOptions opts;
  JsonWriter::Mode json_mode;
  ParseRequestOptions(req, &requested_metrics, /* prometheus opts */ nullptr, &opts, &json_mode);

  std::stringstream* output = &resp->output;
  JsonWriter writer(output, json_mode);

  WARN_NOT_OK(metrics->WriteAsJson(&writer, requested_metrics, opts),
              "Couldn't write JSON metrics over HTTP");
}

static void WriteMetricsForPrometheus(const MetricRegistry* const metrics,
                               const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  vector<string> requested_metrics;
  MetricPrometheusOptions opts;
  ParseRequestOptions(req, &requested_metrics, &opts);

  std::stringstream *output = &resp->output;
  PrometheusWriter writer(output);
  WARN_NOT_OK(metrics->WriteForPrometheus(&writer, requested_metrics, opts),
              "Couldn't write text metrics for Prometheus");
}

static void HandleGetVersionInfo(
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;

  VersionInfoPB version_info;
  VersionInfo::GetVersionInfoPB(&version_info);

  JsonWriter jw(output, JsonWriter::COMPACT);
  jw.StartObject();

  jw.String("build_id");
  jw.String(version_info.build_id());
  jw.String("build_type");
  jw.String(version_info.build_type());
  jw.String("build_number");
  jw.String(version_info.build_number());
  jw.String("build_timestamp");
  jw.String(version_info.build_timestamp());
  jw.String("build_username");
  jw.String(version_info.build_username());
  jw.String("version_number");
  jw.String(version_info.version_number());
  jw.String("build_hostname");
  jw.String(version_info.build_hostname());
  jw.String("git_revision");
  jw.String(version_info.git_hash());

  jw.EndObject();
}

} // anonymous namespace

void AddDefaultPathHandlers(Webserver* webserver) {
  webserver->RegisterPathHandler("/logs", "Logs", LogsHandler, true, false);
  webserver->RegisterPathHandler("/varz", "Flags", FlagsHandler, true, false);
  webserver->RegisterPathHandler("/status", "Status", StatusHandler, false, false);
  webserver->RegisterPathHandler("/memz", "Memory (total)", MemUsageHandler, true, false);
  webserver->RegisterPathHandler("/mem-trackers", "Memory (detail)",
                                 MemTrackersHandler, true, false);
  webserver->RegisterPathHandler("/api/v1/version-info", "Build Version Info",
                                 HandleGetVersionInfo, false, false);

  AddPprofPathHandlers(webserver);
}

void RegisterMetricsJsonHandler(Webserver* webserver, const MetricRegistry* const metrics) {
  Webserver::PathHandlerCallback callback = std::bind(WriteMetricsAsJson, metrics, _1, _2);
  Webserver::PathHandlerCallback prometheus_callback = std::bind(
      WriteMetricsForPrometheus, metrics, _1, _2);
  bool not_styled = false;
  bool not_on_nav_bar = false;
  webserver->RegisterPathHandler("/metrics", "Metrics", callback, not_styled, not_on_nav_bar);

  // The old name -- this is preserved for compatibility with older releases of
  // monitoring software which expects the old name.
  webserver->RegisterPathHandler("/jsonmetricz", "Metrics", callback, not_styled, not_on_nav_bar);

  webserver->RegisterPathHandler(
      "/prometheus-metrics", "Metrics", prometheus_callback, not_styled, not_on_nav_bar);
}

// Registered to handle "/drives", and prints out paths usage
static void PathUsageHandler(FsManager* fsmanager,
                             const Webserver::WebRequest& req,
                             Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  *output << "<h1>Drives usage by subsystem</h1>\n";
  *output << "<table class='table table-striped'>\n";
  *output << "  <tr><th>Path</th><th>Used Space</th>"
      "<th>Total Space</th></tr>\n";

  Env* env = fsmanager->env();
  for (const auto& path : fsmanager->GetDataRootDirs()) {
    const auto stats = env->GetFilesystemStatsBytes(path);
    if (!stats.ok()) {
      LOG(WARNING) << stats.status();
      *output << Format("  <tr><td>$0</td><td>NA</td><td>NA</td></tr>\n", path);
      continue;
    }
    const std::string used_space_str = HumanReadableNumBytes::ToString(stats->used_space);
    const std::string total_space_str = HumanReadableNumBytes::ToString(stats->total_space);
    *output << Format("  <tr><td>$0</td><td>$1</td><td>$2</td></tr>\n",
                      path, used_space_str, total_space_str);
  }
  *output << "</table>\n";
}

void RegisterPathUsageHandler(Webserver* webserver, FsManager* fsmanager) {
  Webserver::PathHandlerCallback callback = std::bind(PathUsageHandler, fsmanager, _1, _2);
  webserver->RegisterPathHandler("/drives", "Drives", callback, true, false);
}

} // namespace yb
