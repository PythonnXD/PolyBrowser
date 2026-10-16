// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/domain_reliability/uploader.h"

#include <utility>

#include "base/containers/unique_ptr_adapters.h"
#include "base/functional/callback.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/supports_user_data.h"
#include "components/domain_reliability/util.h"
#include "net/base/elements_upload_data_stream.h"
#include "net/base/isolation_info.h"
#include "net/base/net_errors.h"
#include "net/base/network_handle.h"
#include "net/base/upload_bytes_element_reader.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_util.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context.h"

namespace domain_reliability {

namespace {

// Each DR upload is tagged with an instance of this class, which identifies the
// depth of the upload. This is to prevent infinite loops of DR uploads about DR
// uploads, leading to an unbounded number of requests.
// Deeper requests will still generate a report beacon, but they won't trigger a
// separate upload. See DomainReliabilityContext::kMaxUploadDepthToSchedule.
struct UploadDepthData : public base::SupportsUserData::Data {
  explicit UploadDepthData(int depth) : depth(depth) {}

  // Key that identifies this data within SupportsUserData's map of data.
  static const void* const kUserDataKey;

  // This is 0 if the report being uploaded does not contain a beacon about a
  // DR upload request. Otherwise, it is 1 + the depth of the deepest DR upload
  // described in the report.
  int depth;
};

const void* const UploadDepthData::kUserDataKey =
    &UploadDepthData::kUserDataKey;

}  // namespace

class DomainReliabilityUploaderImpl : public DomainReliabilityUploader,
                                      public net::URLRequest::Delegate {
 public:
  DomainReliabilityUploaderImpl(MockableTime* time,
                                net::URLRequestContext* url_request_context)
      : time_(time),
        url_request_context_(url_request_context),
        discard_uploads_(true),
        shutdown_(false),
        discarded_upload_count_(0u) {
    DCHECK(url_request_context_);
  }

  ~DomainReliabilityUploaderImpl() override {
    DCHECK(shutdown_);
  }

  // DomainReliabilityUploader implementation:
  void UploadReport(
      const std::string& report_json,
      int max_upload_depth,
      const GURL& upload_url,
      const net::IsolationInfo& isolation_info,
      DomainReliabilityUploader::UploadCallback callback) override {
    DVLOG(1) << "Uploading report to " << upload_url;
    DVLOG(2) << "Report JSON: " << report_json;

    if (discard_uploads_)
      discarded_upload_count_++;

    // PolyBrowser: Completely discard telemetry reports to Google
    DVLOG(1) << "Discarding report instead of uploading.";
    UploadResult result;
    result.status = UploadResult::SUCCESS;
    std::move(callback).Run(result);
    return;
  }

  void SetDiscardUploads(bool discard_uploads) override {
    discard_uploads_ = discard_uploads;
    DVLOG(1) << "Setting discard_uploads to " << discard_uploads;
  }

  void Shutdown() override {
    DCHECK(!shutdown_);
    shutdown_ = true;
    uploads_.clear();
  }

  int GetDiscardedUploadCount() const override {
    return discarded_upload_count_;
  }

  // net::URLRequest::Delegate implementation:
  void OnResponseStarted(net::URLRequest* request, int net_error) override {
    DCHECK(!shutdown_);

    auto request_it = uploads_.find(request);
    CHECK(request_it != uploads_.end());

    int http_response_code = -1;
    base::TimeDelta retry_after;
    if (net_error == net::OK) {
      http_response_code = request->GetResponseCode();
      std::string retry_after_string;
      if (request->response_headers() &&
          request->response_headers()->EnumerateHeader(nullptr, "Retry-After",
                                                       &retry_after_string)) {
        net::HttpUtil::ParseRetryAfterHeader(retry_after_string, time_->Now(),
                                             &retry_after);
      }
    }

    DVLOG(1) << "Upload finished with net error " << net_error
             << ", response code " << http_response_code << ", retry after "
             << retry_after;

    std::move(request_it->second)
        .Run(GetUploadResultFromResponseDetails(net_error, http_response_code,
                                                retry_after));
    uploads_.erase(request_it);
  }

  // Requests are cancelled in OnResponseStarted() once response headers are
  // read, without reading the body, so this is not needed.
  void OnReadCompleted(net::URLRequest* request, int bytes_read) override {
    NOTREACHED();
  }

 private:
  raw_ptr<MockableTime> time_;
  raw_ptr<net::URLRequestContext> url_request_context_;
  // Stores each in-flight upload request with the callback to notify its
  // initiating DRContext of its completion.
  using UploadMap = std::map<std::unique_ptr<net::URLRequest>,
                             UploadCallback,
                             base::UniquePtrComparator>;
  UploadMap uploads_;
  bool discard_uploads_;
  bool shutdown_;
  int discarded_upload_count_;
};

DomainReliabilityUploader::DomainReliabilityUploader() = default;
DomainReliabilityUploader::~DomainReliabilityUploader() = default;

// static
std::unique_ptr<DomainReliabilityUploader> DomainReliabilityUploader::Create(
    MockableTime* time,
    net::URLRequestContext* url_request_context) {
  return std::make_unique<DomainReliabilityUploaderImpl>(time,
                                                         url_request_context);
}

// static
int DomainReliabilityUploader::GetURLRequestUploadDepth(
    const net::URLRequest& request) {
  UploadDepthData* data = static_cast<UploadDepthData*>(
      request.GetUserData(UploadDepthData::kUserDataKey));
  return data ? data->depth : 0;
}

}  // namespace domain_reliability
