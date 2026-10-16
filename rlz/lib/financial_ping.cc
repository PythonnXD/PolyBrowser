// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Library functions related to the Financial Server ping.

#include "rlz/lib/financial_ping.h"

#include <stdint.h>

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <string>

#include "base/location.h"
#include "base/memory/ref_counted.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/threading/thread_restrictions.h"
#include "build/build_config.h"
#include "net/http/http_response_headers.h"
#include "rlz/lib/assert.h"
#include "rlz/lib/lib_values.h"
#include "rlz/lib/machine_id.h"
#include "rlz/lib/rlz_lib.h"
#include "rlz/lib/rlz_value_store.h"
#include "rlz/lib/string_utils.h"
#include "rlz/lib/time_util.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

#if !BUILDFLAG(IS_WIN)
#include "base/time/time.h"
#endif

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/time/time.h"
#include "net/base/load_flags.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "url/gurl.h"

namespace rlz_lib {

bool FinancialPing::FormRequest(Product product,
                                base::span<const AccessPoint> access_points,
                                std::string_view product_signature,
                                std::string_view product_brand,
                                std::string_view product_id,
                                std::string_view product_lang,
                                bool exclude_machine_id,
                                std::string* request) {
  if (!request) {
    ASSERT_STRING("FinancialPing::FormRequest: request is NULL");
    return false;
  }

  request->clear();

  ScopedRlzValueStoreLock lock;
  RlzValueStore* store = lock.GetStore();
  if (!store || !store->HasAccess(RlzValueStore::kReadAccess))
    return false;

  if (product_signature.empty()) {
    ASSERT_STRING("FinancialPing::FormRequest: product_signature is empty");
    return false;
  }

  if (!SupplementaryBranding::GetBrand().empty()) {
    if (SupplementaryBranding::GetBrand() != product_brand) {
      ASSERT_STRING("FinancialPing::FormRequest: supplementary branding bad");
      return false;
    }
  }

  base::StrAppend(request,
                  {kFinancialPingPath, "?", kProductSignatureCgiVariable, "=",
                   product_signature});

  if (!product_brand.empty()) {
    base::StrAppend(request,
                    {"&", kProductBrandCgiVariable, "=", product_brand});
  }

  if (!product_id.empty()) {
    base::StrAppend(request, {"&", kProductIdCgiVariable, "=", product_id});
  }

  if (!product_lang.empty()) {
    base::StrAppend(request,
                    {"&", kProductLanguageCgiVariable, "=", product_lang});
  }

  // Add the product events.
  std::optional<std::string> events_cgi = GetProductEventsAsCgi(product);
  if (events_cgi) {
    base::StrAppend(request, {"&", *events_cgi});
  }

  // If we don't have any events, we should ping all the AP's on the system
  // that we know about and have a current RLZ value, even if they are not
  // used by this product.
  std::array<AccessPoint, LAST_ACCESS_POINT> all_points{};
  size_t idx = 0;
  if (!events_cgi) {
    for (int ap = NO_ACCESS_POINT + 1; ap < LAST_ACCESS_POINT; ap++) {
      AccessPoint point = static_cast<AccessPoint>(ap);
      std::optional<std::string> rlz = GetAccessPointRlz(point);
      if (rlz && !rlz->empty()) {
        all_points[idx++] = point;
      }
    }
  }

  // Add the RLZ's and the DCC if needed. This is the same as get PingParams.
  // This will also include the RLZ Exchange Protocol CGI Argument.
  if (std::optional<std::string> ping_params = GetPingParams(
          product,
          events_cgi ? access_points : base::span(all_points).first(idx))) {
    base::StrAppend(request, {"&", *ping_params});
  }

  if (events_cgi && !exclude_machine_id) {
    std::string machine_id;
    if (GetMachineId(&machine_id)) {
      base::StrAppend(request, {"&", kMachineIdCgiVariable, "=", machine_id});
    }
  }

  return true;
}

namespace {

// A waitable event used to detect when either:
//
//   1/ the RLZ ping request completes
//   2/ the RLZ ping request times out
//   3/ browser shutdown begins
class RefCountedWaitableEvent
    : public base::RefCountedThreadSafe<RefCountedWaitableEvent> {
 public:
  RefCountedWaitableEvent()
      : event_(base::WaitableEvent::ResetPolicy::MANUAL,
               base::WaitableEvent::InitialState::NOT_SIGNALED) {}

  void SignalShutdown() { event_.Signal(); }

  void SignalFetchComplete(int response_code, std::string response) {
    base::AutoLock autolock(lock_);
    response_code_ = response_code;
    response_ = std::move(response);
    event_.Signal();
  }

  bool TimedWait(base::TimeDelta timeout) { return event_.TimedWait(timeout); }

  int GetResponseCode() {
    base::AutoLock autolock(lock_);
    return response_code_;
  }

  std::string TakeResponse() {
    base::AutoLock autolock(lock_);
    std::string temp = std::move(response_);
    response_.clear();
    return temp;
  }

 private:
  ~RefCountedWaitableEvent() = default;
  friend class base::RefCountedThreadSafe<RefCountedWaitableEvent>;

  base::WaitableEvent event_;
  base::Lock lock_;
  std::string response_;
  int response_code_ = -1;
};

}  // namespace

// The signal for the current ping request. It can be used to cancel the request
// in case of a shutdown.
scoped_refptr<RefCountedWaitableEvent>& GetPingResultEvent() {
  static base::NoDestructor<scoped_refptr<RefCountedWaitableEvent>>
      g_pingResultEvent;
  return *g_pingResultEvent;
}

// The pointer to URLRequestContextGetter used by FinancialPing::PingServer().
// It is atomic pointer because it can be accessed and modified by multiple
// threads.
std::atomic<network::mojom::URLLoaderFactory*> g_URLLoaderFactory;

bool FinancialPing::SetURLLoaderFactory(
    network::mojom::URLLoaderFactory* factory) {
  g_URLLoaderFactory.store(factory, std::memory_order_release);
  scoped_refptr<RefCountedWaitableEvent> event = GetPingResultEvent();
  if (!factory && event) {
    event->SignalShutdown();
  }
  return true;
}
FinancialPing::PingResponse FinancialPing::PingServer(const char* request,
                                                      std::string* response) {
  // PolyBrowser: Disable RLZ pings to Google
  if (response) {
    response->clear();
  }
  return PING_FAILURE;
}
bool FinancialPing::IsPingTime(Product product, bool no_delay) {
  ScopedRlzValueStoreLock lock;
  RlzValueStore* store = lock.GetStore();
  if (!store || !store->HasAccess(RlzValueStore::kReadAccess))
    return false;

  std::optional<int64_t> last_ping = store->ReadPingTime(product);
  if (!last_ping) {
    return true;
  }

  uint64_t now = GetSystemTimeAsInt64();
  int64_t interval = now - *last_ping;

  // If interval is negative, clock was probably reset. So ping.
  if (interval < 0)
    return true;

  // Check if this product has any unreported events.
  std::optional<std::string> cgi = GetProductEventsAsCgi(product);
  if (no_delay && cgi) {
    return true;
  }

  return interval >= (cgi ? kEventsPingInterval : kNoEventsPingInterval);
}


bool FinancialPing::UpdateLastPingTime(Product product) {
  ScopedRlzValueStoreLock lock;
  RlzValueStore* store = lock.GetStore();
  if (!store || !store->HasAccess(RlzValueStore::kWriteAccess))
    return false;

  uint64_t now = GetSystemTimeAsInt64();
  return store->WritePingTime(product, now);
}


bool FinancialPing::ClearLastPingTime(Product product) {
  ScopedRlzValueStoreLock lock;
  RlzValueStore* store = lock.GetStore();
  if (!store || !store->HasAccess(RlzValueStore::kWriteAccess))
    return false;
  return store->ClearPingTime(product);
}

}  // namespace rlz_lib
