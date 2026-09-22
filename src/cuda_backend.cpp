// Phase-2 GPU backend seam. Compiles without CUDA; returns a passthrough
// denoiser until a real GPU model is plugged in.
//
// To implement (RTX 3090, CC 8.6):
//   Option A (recommended OSS): DeepFilterNet + ONNX Runtime with CUDA
//     provider (--use_cuda). Process 48 kHz frames on GPU, copy back.
//   Option B (closest to Broadcast): NVIDIA Maxine Audio Effects SDK
//     (nv_audio_effects): nvAFX_CreateEffect(NVAFX_EFFECT_DENOISER),
//     SetU32(NVAFX_CUDA_STREAM / device), LoadModel("denoiser_48k.trtpkg"),
//     Run() per frame. Requires Maxine redist + CUDA 12/13.
// Either way, keep the Denoiser interface unchanged.
#include "denoiser.h"

namespace nvb {
namespace {

class CudaStub : public Denoiser {
 public:
  CudaStub(int device_id, std::string model)
      : device_id_(device_id), model_(std::move(model)) {}
  bool init(int, const DenoiseConfig& cfg) override {
    cfg_ = cfg;
    return true;
  }
  float process(float* frames, int n) override {
    (void)frames;
    (void)n;  // TODO: GPU inference here; placeholder passes audio through.
    return 1.0f;
  }
  std::string name() const override { return "cuda-stub:" + model_; }

 private:
  int device_id_;
  std::string model_;
  DenoiseConfig cfg_;
};

}  // namespace

Denoiser* CreateCudaDenoiser(int device_id, const std::string& model) {
  return new CudaStub(device_id, model);
}

}  // namespace nvb
