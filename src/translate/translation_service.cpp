#include "translate/translation_service.hpp"

#include <exception>
#include <utility>

#include "util/log.hpp"
#include "util/thread_name.hpp"
#include "util/utf8.hpp"

namespace pdfread::translate {

TranslationService::TranslationService(TranslatorFactory factory, std::function<void()> wake, DeviceProber prober)
    : factory_(std::move(factory)), wake_(std::move(wake)), prober_(std::move(prober)), thread_([this] { run(); }) {}

TranslationService::~TranslationService() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
        pending_job_.reset();
        pending_load_.reset();
        if (running_cancel_) running_cancel_->store(true);
    }
    cv_.notify_all();
    thread_.join();
}

void TranslationService::load_model(ModelConfig config) {
    {
        std::lock_guard lock(mutex_);
        pending_load_ = std::move(config);
        if (running_cancel_) running_cancel_->store(true);  // giải phóng luồng sớm
    }
    cv_.notify_all();
}

std::uint64_t TranslationService::submit(TranslateRequest request) {
    std::uint64_t id = 0;
    {
        std::lock_guard lock(mutex_);
        id = next_id_++;
        if (running_cancel_) running_cancel_->store(true);
        if (pending_job_) {
            emit({TranslationEvent::Kind::Cancelled, pending_job_->id, {}, {}});
        }
        pending_job_ = Job{id, std::move(request), std::make_shared<std::atomic<bool>>(false)};
    }
    cv_.notify_all();
    return id;
}

void TranslationService::cancel() {
    std::lock_guard lock(mutex_);
    if (running_cancel_) running_cancel_->store(true);
    if (pending_job_) {
        emit({TranslationEvent::Kind::Cancelled, pending_job_->id, {}, {}});
        pending_job_.reset();
    }
}

std::string TranslationService::model_name() const {
    std::lock_guard lock(mutex_);
    return model_name_;
}

std::string TranslationService::device_name() const {
    std::lock_guard lock(mutex_);
    return device_name_;
}

void TranslationService::probe_devices() {
    {
        std::lock_guard lock(mutex_);
        if (!prober_ || probing_) return;
        pending_probe_ = true;
        probing_ = true;
    }
    cv_.notify_all();
}

bool TranslationService::devices_probed() const {
    std::lock_guard lock(mutex_);
    return probed_;
}

bool TranslationService::probing_devices() const {
    std::lock_guard lock(mutex_);
    return probing_;
}

std::vector<GpuDevice> TranslationService::devices() const {
    std::lock_guard lock(mutex_);
    return devices_;
}

void TranslationService::emit(TranslationEvent e) {
    events_.push(std::move(e));
    if (wake_) wake_();
}

void TranslationService::run() {
    name_current_thread("inference");
    for (;;) {
        std::optional<ModelConfig> load;
        std::optional<Job> job;
        bool probe = false;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || pending_load_ || pending_job_ || pending_probe_; });
            if (stop_) break;
            if (pending_probe_ && !pending_job_) {  // dò GPU khi không có yêu cầu dịch đang chờ
                probe = true;
                pending_probe_ = false;
            } else if (pending_load_) {  // nạp mô hình được ưu tiên
                load = std::move(pending_load_);
                pending_load_.reset();
            } else {
                job = std::move(pending_job_);
                pending_job_.reset();
                running_cancel_ = job->cancel;
            }
        }
        if (probe) {
            std::vector<GpuDevice> found;
            try {
                found = prober_();
            } catch (const std::exception& e) {
                log::warn("Dò GPU thất bại: {}", e.what());
            }
            {
                std::lock_guard lock(mutex_);
                devices_ = std::move(found);
                probed_ = true;
                probing_ = false;
            }
            emit({TranslationEvent::Kind::DevicesListed, 0, {}, {}});
        }
        if (load) do_load(*load);
        if (job) do_translate(*job);
        std::lock_guard lock(mutex_);
        running_cancel_.reset();
    }
    translator_.reset();  // giải phóng mô hình trên chính luồng đã nạp nó
}

void TranslationService::do_load(const ModelConfig& config) {
    translator_.reset();
    {
        std::lock_guard lock(mutex_);
        device_name_.clear();
    }
    state_ = ModelState::Loading;
    const std::string file = path_to_utf8(config.model_path.filename());
    emit({TranslationEvent::Kind::ModelLoading, 0, file, {}});
    try {
        translator_ = factory_(config);
        {
            std::lock_guard lock(mutex_);
            model_name_ = translator_->name();
            device_name_ = translator_->device();
        }
        state_ = ModelState::Ready;
        emit({TranslationEvent::Kind::ModelReady, 0, translator_->name(), {}});
    } catch (const std::exception& e) {
        translator_.reset();
        state_ = ModelState::Failed;
        log::error("Nạp mô hình thất bại: {}", e.what());
        emit({TranslationEvent::Kind::ModelFailed, 0, e.what(), {}});
    }
}

void TranslationService::do_translate(Job& job) {
    if (job.cancel->load()) {
        emit({TranslationEvent::Kind::Cancelled, job.id, {}, {}});
        return;
    }
    if (!translator_) {
        emit({TranslationEvent::Kind::Failed, job.id,
              state_ == ModelState::Failed ? "Mô hình dịch nạp lỗi" : "Chưa chọn mô hình dịch", {}});
        return;
    }
    emit({TranslationEvent::Kind::Started, job.id, {}, {}});
    try {
        TranslationStats stats;
        const bool done = translator_->translate(
            job.request,
            [&](std::string_view piece) { emit({TranslationEvent::Kind::Token, job.id, std::string(piece), {}}); },
            *job.cancel, &stats);
        emit({done ? TranslationEvent::Kind::Done : TranslationEvent::Kind::Cancelled, job.id, {}, stats});
    } catch (const std::exception& e) {
        log::warn("Dịch thất bại: {}", e.what());
        emit({TranslationEvent::Kind::Failed, job.id, e.what(), {}});
    }
}

}  // namespace pdfread::translate
