#pragma once
#include "warp.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
namespace paint {
enum class WarpTask {
    None,
    CompileSelection,
    CompileStamp,
    CompileRotation,
    PreviewMesh,
    CommitMesh,
    Stamp,
    Transform,
    PreviewRotation,
    CommitRotation
};
struct WarpResult {
    WarpTask task = WarpTask::None;
    std::shared_ptr<const ConvWarpField> field;
    Image image;
    std::string error;
    std::uint64_t generation = 0;
    Rect bounds;
};
// One worker owns all mutable job storage. The UI borrows immutable compiled fields.
// Publication uses release/acquire; take() joins before moving the result.
class WarpWorker {
  public:
    WarpWorker() = default;
    ~WarpWorker();
    WarpWorker(const WarpWorker&) = delete;
    WarpWorker& operator=(const WarpWorker&) = delete;
    bool busy() const;
    void set_completion(std::function<void()> completion);
    void wait();
    void cancel();
    void compile(WarpTask task, const Image& source, std::uint64_t generation = 0);
    void mesh(WarpTask task, std::shared_ptr<const ConvWarpField> field, const ReshapeMesh& mesh, Rect bounds,
              std::uint64_t generation);
    void affine(WarpTask task, std::shared_ptr<const ConvWarpField> field, const AffineMap& map, Rect bounds,
                std::uint64_t generation);
    void transform(const Image& source, const AffineMap& map, Rect bounds, std::uint64_t generation = 0);
    bool take(WarpResult& result);

  private:
    void launch(WarpTask task);
    static void run(WarpWorker& worker);
    std::function<void()> completion_;
    Image source_;
    std::shared_ptr<const ConvWarpField> field_;
    ReshapeMesh mesh_;
    AffineMap map_;
    Rect bounds_;
    std::uint64_t generation_ = 0;
    WarpTask task_ = WarpTask::None;
    WarpResult result_;
    bool busy_ = false;
    std::atomic<bool> finished_{false}, cancelled_{false};
    std::jthread thread_;
};
} // namespace paint
