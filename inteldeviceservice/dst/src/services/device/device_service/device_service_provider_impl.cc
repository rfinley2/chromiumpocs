// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/device/device_service/device_service_provider_impl.h"

#include <windows.h>

#include "base/bind.h"
#include "mojo/public/cpp/bindings/remote_set.h"
#include "services/device/device_service/frequencylimiter.h"
#include "services/device/device_service/device_service_platform_provider.h"

frequencyLimiter FrequencyLimiter;

namespace device {

constexpr uint32_t kUpdateIntervalMS = 50;

DeviceServiceProviderImpl::DeviceServiceProviderImpl(
    std::unique_ptr<DeviceServicePlatformProvider> platform_provider)
    : last_update_(0), platform_provider_(std::move(platform_provider)) {
  DCHECK(platform_provider_);

  platform_provider_->SetServiceProvider(this);
  // We need to  listen to disconnections so that if there is nobody interested
  // in service changes we can shutdown the native backends.
  receivers_.set_disconnect_handler(
      base::BindRepeating(&DeviceServiceProviderImpl::OnReceiverConnectionError,
                          weak_ptr_factory_.GetWeakPtr()));
}

DeviceServiceProviderImpl::~DeviceServiceProviderImpl() = default;

void DeviceServiceProviderImpl::AddListenerAndGetCurrentService(
    mojo::PendingRemote<mojom::DeviceServiceProviderClient> client,
    AddListenerAndGetCurrentServiceCallback callback) {
  clients_.Add(std::move(client));
  mojom::DeviceServiceType service = platform_provider_->GetDeviceService();
  std::move(callback).Run(service);
}

void DeviceServiceProviderImpl::SubmitTaskCapacityHint(
    uint32_t system_time,
    uint32_t process_id,
    uint32_t thread_id,
    mojom::Capacity capacity,
    SubmitTaskCapacityHintCallback callback) {

  if (system_time < last_update_) {
    std::move(callback).Run(-1);

    #if BUILDFLAG(ENABLE_LOGGING)
    LOG(ERROR) << "event in the past: " << system_time << " " << last_update_;
    #endif

    return;
  }

  if (capacity > last_capacity_) {
    last_capacity_ = capacity;
  }
  
  if ((system_time - last_update_) > kUpdateIntervalMS) {

    #if BUILDFLAG(ENABLE_LOGGING)
    const char* enum_name = "";
    #endif

    switch (last_capacity_) {
      case mojom::Capacity::kCapacityIdle:

        #if BUILDFLAG(ENABLE_LOGGING)
        enum_name = "IDLE";
        #endif

        #if BUILDFLAG(ENABLE_IPF)
        GearDown(); //if > floor then newvalues for current - 8*steps+Min=Max Max-Min/8 and > min else min = steps
        #endif

        FrequencyLimiter.GearDown(10); //rrw

        break;
      case mojom::Capacity::kCapacityUnder:

        #if BUILDFLAG(ENABLE_LOGGING)
        enum_name = "UNDER";
        #endif

        #if BUILDFLAG(ENABLE_IPF)
        GearDown(); //if > floor then newvalues for current - 8*steps+Min=Max Max-Min/8 = steps
        #endif
        FrequencyLimiter.GearDown(10); //rrw

        //FrequencyLimiter.GearDown(10); //rrw

        break;
      case mojom::Capacity::kCapacityMeet:
        #if BUILDFLAG(ENABLE_LOGGING)
        enum_name = "MEET";
        #endif

        #if BUILDFLAG(ENABLE_IPF) //if < min + step else if !prev then (-step and clear prev) else do nothing
        #endif
        FrequencyLimiter.GearDown(10); //rrw

        break;
      case mojom::Capacity::kCapacityOver:
        #if BUILDFLAG(ENABLE_LOGGING)
        enum_name = "OVER";
        #endif

        #if BUILDFLAG(ENABLE_IPF)
        GearUp(); //set to  0 0 0 0 
        #endif

        FrequencyLimiter.GearUp(100); //rrw

        break;
    }

    #if BUILDFLAG(ENABLE_LOGGING)
    LOG(ERROR) << ::GetCurrentProcessId() << " time, " << system_time << " pid, " << process_id << " tid, " << thread_id << ", " << enum_name;
    #endif

    last_update_ = system_time;
    last_capacity_ = mojom::Capacity::kCapacityIdle;
  }

  std::move(callback).Run(42);
}

void DeviceServiceProviderImpl::Bind(
    mojo::PendingReceiver<mojom::DeviceServiceProvider> receiver) {
  if (receivers_.empty()) {
    platform_provider_->StartListening();
  }
  receivers_.Add(this, std::move(receiver));
}

void DeviceServiceProviderImpl::OnDeviceServiceChanged(
    const mojom::DeviceServiceType& service) {
  for (auto& client : clients_) {
    client->OnServiceChanged(service);
  }
}

void DeviceServiceProviderImpl::OnReceiverConnectionError() {
  if (receivers_.empty()) {
    platform_provider_->StopListening();
  }
}

}  // namespace device
