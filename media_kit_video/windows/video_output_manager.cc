// This file is a part of media_kit
// (https://github.com/media-kit/media-kit).
//
// Copyright © 2021 & onwards, Hitesh Kumar Saini <saini123hitesh@gmail.com>.
// All rights reserved.
// Use of this source code is governed by MIT license that can be found in the
// LICENSE file.

#include "video_output_manager.h"

VideoOutputManager::VideoOutputManager(
    flutter::PluginRegistrarWindows* registrar) {
  state_->registrar = registrar;
}

void VideoOutputManager::Create(
    int64_t handle,
    VideoOutputConfiguration configuration,
    std::function<void(int64_t, int64_t, int64_t)> texture_update_callback) {
  std::thread([state = state_, handle, configuration,
               texture_update_callback]() {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!state->alive) {
      return;
    }
    if (state->video_outputs.find(handle) == state->video_outputs.end()) {
      auto instance = std::make_unique<VideoOutput>(
          handle, configuration, state->registrar, state->thread_pool.get());
      instance->SetTextureUpdateCallback(texture_update_callback);
      state->video_outputs.insert(std::make_pair(handle, std::move(instance)));
    }
  }).detach();
}

void VideoOutputManager::SetSize(int64_t handle,
                                 std::optional<int64_t> width,
                                 std::optional<int64_t> height) {
  std::thread([state = state_, handle, width, height]() {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!state->alive) {
      return;
    }
    auto it = state->video_outputs.find(handle);
    if (it != state->video_outputs.end()) {
      it->second->SetSize(width, height);
    }
  }).detach();
}

void VideoOutputManager::Dispose(int64_t handle,
                                 std::function<void()> on_disposed) {
  std::thread([state = state_, handle, on_disposed]() {
    std::unique_ptr<VideoOutput> output = nullptr;
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      if (state->alive) {
        auto it = state->video_outputs.find(handle);
        if (it != state->video_outputs.end()) {
          output = std::move(it->second);
          state->video_outputs.erase(it);
        }
      }
    }
    output.reset();
    if (on_disposed) {
      on_disposed();
    }
  }).detach();
}

VideoOutputManager::~VideoOutputManager() {
  std::unordered_map<int64_t, std::unique_ptr<VideoOutput>> outputs;
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->alive = false;
    outputs.swap(state_->video_outputs);
  }
  // |VideoOutput| destructor will do the relevant cleanup while |thread_pool|
  // remains alive in |state_|.
  outputs.clear();
  // This destructor is only called when the plugin is being destroyed i.e. the
  // application is being closed.
}
