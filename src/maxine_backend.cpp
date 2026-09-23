// Maxine backend: NVIDIA Audio Effects SDK (the Broadcast-quality denoiser)
// loaded at runtime via dlopen — builds and runs without the SDK installed.
//
// Fetch (needs NVIDIA Developer account + EULA acceptance):
//   1. Download "Audio Effects SDK" for Linux from NGC / developer.nvidia.com
//   2. Extract, e.g. to ~/.local/share/nv-broadcast/maxine
//      (lib/libnv_audio_effects.so, models/denoiser_48k.trtpkg, headers)
//   3. backend = "maxine", [maxine] model = .../models/denoiser_48k.trtpkg
//
// API grounding: NvAFX_* C ABI (NvAFX_CreateEffect(NVAFX_EFFECT_DENOISER),
// NVAFX_PARAM_* string keys "model_path"/"input_sample_rate"/
// "output_sample_rate"/"num_streams"/"num_channels"/"enable_vad",
// NvAFX_Load, NvAFX_Run, NvAFX_GetU32 frame queries). Param names that the
// installed SDK rejects are retried against legacy spellings, else init
// fails with the numeric status so one run diagnoses any mismatch.
#include "denoiser.h"

#include <dlfcn.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace nvb {
namespace {

using NvAFX_Handle = void*;
using NvAFX_Status = int;
constexpr NvAFX_Status NVAFX_OK = 0;

using FnCreate = NvAFX_Status (*)(const char*, NvAFX_Handle*);
using FnDestroy = NvAFX_Status (*)(NvAFX_Handle);
using FnSetU32 = NvAFX_Status (*)(NvAFX_Handle, const char*, unsigned);
using FnSetStr = NvAFX_Status (*)(NvAFX_Handle, const char*, const char*);
using FnGetU32 = NvAFX_Status (*)(NvAFX_Handle, const char*, unsigned*);
using FnLoad = NvAFX_Status (*)(NvAFX_Handle);
using FnRun = NvAFX_Status (*)(NvAFX_Handle, const float* const*,
                               float* const*, unsigned, unsigned);

struct Api {
  FnCreate create = nullptr;
  FnDestroy destroy = nullptr;
  FnSetU32 set_u32 = nullptr;
  FnSetStr set_str = nullptr;
  FnGetU32 get_u32 = nullptr;
  FnLoad load = nullptr;
  FnRun run = nullptr;
};

bool FileExists(const std::string& p) {
  struct stat sb;
  return stat(p.c_str(), &sb) == 0;
}

std::string DefaultModel() {
  const char* dir = std::getenv("NVB_MAXINE_DIR");
  std::string base = dir ? dir : (std::string(std::getenv("HOME") ? std::getenv("HOME") : ".") +
                                  "/.local/share/nv-broadcast/maxine");
  return base + "/models/denoiser_48k.trtpkg";
}

class MaxineDenoiser : public Denoiser {
 public:
  explicit MaxineDenoiser(DenoiseConfig cfg) : cfg_(cfg) {}
  ~MaxineDenoiser() override {
    if (handle_) api_.destroy(handle_);
    if (lib_) dlclose(lib_);
  }

  bool init(int sample_rate, const DenoiseConfig& cfg) override {
    cfg_ = cfg;
    model_ = cfg_.maxine_model.empty() ? DefaultModel() : cfg_.maxine_model;
    if (model_ == "denoiser_48k") model_ = DefaultModel();  // SDK shorthand

    if (!FileExists(model_)) {
      std::fprintf(stderr,
                   "maxine: model not found: %s\n"
                   "  Download the Audio Effects SDK (NVIDIA Developer account,\n"
                   "  accept the Maxine EULA) and set [maxine] model to\n"
                   "  <sdk>/models/denoiser_48k.trtpkg\n",
                   model_.c_str());
      return false;
    }

    std::string lib = cfg_.maxine_lib.empty() ? "libnv_audio_effects.so"
                                              : cfg_.maxine_lib;
    if (const char* env = std::getenv("NVB_MAXINE_LIB")) lib = env;
    if (const char* env = std::getenv("NVB_MAXINE_MODEL")) model_ = env;

    lib_ = dlopen(lib.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!lib_) {
      std::fprintf(stderr,
                   "maxine: dlopen(%s) failed: %s\n"
                   "  Install the SDK (or set [maxine] lib / $NVB_MAXINE_LIB).\n",
                   lib.c_str(), dlerror());
      return false;
    }
#define LOAD(sym, Type, field)                                 \
  api_.field = reinterpret_cast<Type>(dlsym(lib_, "NvAFX_" #sym)); \
  if (!api_.field) {                                          \
    std::fprintf(stderr, "maxine: dlsym(NvAFX_" #sym ") failed\n"); \
    return false;                                             \
  }
    LOAD(CreateEffect, FnCreate, create);
    LOAD(DestroyEffect, FnDestroy, destroy);
    LOAD(SetU32, FnSetU32, set_u32);
    LOAD(SetString, FnSetStr, set_str);
    LOAD(GetU32, FnGetU32, get_u32);
    LOAD(Load, FnLoad, load);
    LOAD(Run, FnRun, run);
#undef LOAD

    const char* effect =
        cfg_.maxine_effect.empty() ? "denoiser" : cfg_.maxine_effect.c_str();
    NvAFX_Status st = api_.create(effect, &handle_);
    if (st != NVAFX_OK || !handle_) {
      std::fprintf(stderr, "maxine: CreateEffect(%s) status=%d\n", effect, st);
      return false;
    }

    auto req_u32 = [&](const char* key, unsigned v) {
      NvAFX_Status s = api_.set_u32(handle_, key, v);
      if (s != NVAFX_OK) {
        std::fprintf(stderr, "maxine: SetU32(%s=%u) status=%d\n", key, v, s);
        return false;
      }
      return true;
    };
    auto opt_u32 = [&](const char* key, unsigned v) {
      NvAFX_Status s = api_.set_u32(handle_, key, v);
      if (s != NVAFX_OK)
        std::fprintf(stderr, "maxine: note: SetU32(%s) unsupported (%d)\n",
                     key, s);
    };

    if (!req_u32("input_sample_rate", (unsigned)sample_rate)) return false;
    // SDK 2.x wants the output rate too; 1.x only knows "sample_rate".
    if (api_.set_u32(handle_, "output_sample_rate", (unsigned)sample_rate) !=
        NVAFX_OK)
      opt_u32("sample_rate", (unsigned)sample_rate);
    opt_u32("num_streams", 1);
    opt_u32("num_channels", 1);
    if (cfg_.maxine_enable_vad) opt_u32("enable_vad", 1);

    if (api_.set_str(handle_, "model_path", model_.c_str()) != NVAFX_OK) {
      std::fprintf(stderr, "maxine: SetString(model_path) failed\n");
      return false;
    }
    st = api_.load(handle_);
    if (st != NVAFX_OK) {
      std::fprintf(stderr, "maxine: Load(%s) status=%d\n", model_.c_str(), st);
      return false;
    }

    frame_ = 480;
    unsigned q = 0;
    if (api_.get_u32(handle_, "num_input_samples_per_frame", &q) != NVAFX_OK)
      api_.get_u32(handle_, "num_samples_per_input_frame", &q);
    if (q >= 64 && q <= 8192) frame_ = (int)q;
    scratch_.assign(frame_, 0.0f);
    std::fprintf(stderr, "maxine: ready model=%s frame=%d\n", model_.c_str(),
                 frame_);
    return true;
  }

  float process(float* frames, int n) override {
    if (!handle_) return 0.0f;
    // Accumulate caller chunks into SDK-sized frames.
    buf_.insert(buf_.end(), frames, frames + n);
    int done = 0;
    while ((int)buf_.size() - done >= frame_) {
      const float* in[1] = {buf_.data() + done};
      float* out[1] = {scratch_.data()};
      NvAFX_Status st = api_.run(handle_, in, out, (unsigned)frame_, 1);
      if (st != NVAFX_OK) {
        std::fprintf(stderr, "maxine: Run status=%d\n", st);
        break;
      }
      for (int i = 0; i < frame_; ++i) frames[done + i] = scratch_[i];
      done += frame_;
    }
    if (done) buf_.erase(buf_.begin(), buf_.begin() + done);
    return 1.0f;  // SDK runs its own VAD/intensity internally
  }

  std::string name() const override { return "maxine"; }

 private:
  DenoiseConfig cfg_;
  void* lib_ = nullptr;
  Api api_;
  NvAFX_Handle handle_ = nullptr;
  std::string model_;
  int frame_ = 480;
  std::vector<float> buf_;
  std::vector<float> scratch_;
};

}  // namespace

Denoiser* CreateMaxineDenoiser(DenoiseConfig cfg) {
  return new MaxineDenoiser(cfg);
}

}  // namespace nvb
