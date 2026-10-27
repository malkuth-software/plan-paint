#include "warp_session.hpp"
#include <stdexcept>
#include <utility>
namespace paint {
WarpWorker::~WarpWorker() {
    wait();
}
void WarpWorker::set_completion(std::function<void()> completion) {
    if (busy_) {
        throw std::logic_error("Cannot replace an active worker completion.");
    }
    completion_ = std::move(completion);
}
void WarpWorker::cancel() {
    cancelled_.store(true, std::memory_order_relaxed);
}
void WarpWorker::wait() {
    if (thread_.joinable()) {
        thread_.join();
    }
}
bool WarpWorker::busy() const {
    return busy_;
}
void WarpWorker::launch(WarpTask task) {
    cancelled_.store(false, std::memory_order_relaxed);
    task_ = task;
    result_ = {};
    finished_.store(false, std::memory_order_relaxed);
    busy_ = true;
    try {
        thread_ = std::jthread(run, std::ref(*this));
    } catch (...) {
        busy_ = false;
        task_ = WarpTask::None;
        throw;
    }
}
void WarpWorker::compile(WarpTask task, const Image& source, std::uint64_t generation) {
    if (busy_) {
        throw std::logic_error("A warp operation is already running.");
    }
    source_ = source;
    generation_ = generation;
    bounds_ = {};
    launch(task);
}
void WarpWorker::mesh(WarpTask task, std::shared_ptr<const ConvWarpField> field, const ReshapeMesh& mesh,
                      Rect bounds, std::uint64_t generation) {
    if (busy_) {
        throw std::logic_error("A warp operation is already running.");
    }
    mesh_ = mesh;
    field_ = std::move(field);
    bounds_ = bounds;
    generation_ = generation;
    for (MeshNode& node : mesh_.nodes) {
        node.target.x -= bounds.x;
        node.target.y -= bounds.y;
    }
    launch(task);
}
void WarpWorker::affine(WarpTask task, std::shared_ptr<const ConvWarpField> field, const AffineMap& map,
                        Rect bounds, std::uint64_t generation) {
    if (busy_) {
        throw std::logic_error("A warp operation is already running.");
    }
    field_ = std::move(field);
    map_ = map;
    bounds_ = bounds;
    generation_ = generation;
    map_.tx -= bounds.x;
    map_.ty -= bounds.y;
    launch(task);
}
void WarpWorker::transform(const Image& source, const AffineMap& map, Rect bounds, std::uint64_t generation) {
    if (busy_) {
        throw std::logic_error("A warp operation is already running.");
    }
    source_ = source;
    map_ = map;
    bounds_ = bounds;
    generation_ = generation;
    map_.tx -= bounds.x;
    map_.ty -= bounds.y;
    launch(WarpTask::Transform);
}
void WarpWorker::run(WarpWorker& worker) {
    WarpResult result;
    result.task = worker.task_;
    result.generation = worker.generation_;
    result.bounds = worker.bounds_;
    try {
        if (worker.task_ == WarpTask::CompileSelection || worker.task_ == WarpTask::CompileStamp ||
            worker.task_ == WarpTask::CompileRotation || worker.task_ == WarpTask::Transform) {
            std::shared_ptr<ConvWarpField> field = std::make_shared<ConvWarpField>();
            (*field).compile(worker.source_, &worker.cancelled_);
            result.field = std::move(field);
            if (worker.task_ == WarpTask::Transform) {
                render_affine(*result.field, worker.map_, worker.bounds_.w, worker.bounds_.h, result.image,
                              WarpSampling::Area);
                result.field.reset();
            }
        } else if (worker.task_ == WarpTask::PreviewMesh || worker.task_ == WarpTask::CommitMesh) {
            render_mesh(*worker.field_, worker.mesh_, worker.bounds_.w, worker.bounds_.h, result.image,
                        worker.task_ == WarpTask::PreviewMesh ? WarpSampling::Point : WarpSampling::Area);
        } else if (worker.task_ == WarpTask::Stamp || worker.task_ == WarpTask::PreviewRotation ||
                   worker.task_ == WarpTask::CommitRotation) {
            render_affine(*worker.field_, worker.map_, worker.bounds_.w, worker.bounds_.h, result.image,
                          worker.task_ == WarpTask::PreviewRotation ? WarpSampling::Point
                                                                    : WarpSampling::Area);
        }
    } catch (const std::exception& exception) {
        result.error = exception.what();
    } catch (...) {
        result.error = "The image transform could not finish.";
    }
    worker.result_ = std::move(result);
    worker.finished_.store(true, std::memory_order_release);
    if (worker.completion_) {
        try {
            worker.completion_();
        } catch (...) {
            // A host may have detached while the numerical operation finished.
        }
    }
}
bool WarpWorker::take(WarpResult& result) {
    if (!busy_ || !finished_.load(std::memory_order_acquire)) {
        return false;
    }
    wait();
    result = std::move(result_);
    source_ = {};
    mesh_ = {};
    field_.reset();
    busy_ = false;
    task_ = WarpTask::None;
    return true;
}
} // namespace paint
