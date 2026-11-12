#include "translate/translation_service.hpp"

#include <exception>
#include <utility>

#include "util/log.hpp"
#include "util/thread_name.hpp"
#include "util/utf8.hpp"

namespace pdfread::translate {

TranslationService::TranslationService(TranslatorFactory factory, std::function<void()> wake)
    : factory_(std::move(factory)), wake_(std::move(wake)), thread_([this] { run(); }) {}

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

void TranslationService::emit(TranslationEvent e) {
    events_.push(std::move(e));
    if (wake_) wake_();
}

void TranslationService::run() {
    name_current_thread("inference");
    for (;;) {
        std::optional<ModelConfig> load;
        std::optional<Job> job;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || pending_load_ || pending_job_; });
            if (stop_) break;
            if (pending_load_) {  // nạp mô hình được ưu tiên
                load = std::move(pending_load_);
                pending_load_.reset();
            } else {
                job = std::move(pending_job_);
                pending_job_.reset();
                running_cancel_ = job->cancel;
            }
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
    state_ = ModelState::Loading;
    const std::string file = path_to_utf8(config.model_path.filename());
    emit({TranslationEvent::Kind::ModelLoading, 0, file, {}});
    try {
        translator_ = factory_(config);
        {
            std::lock_guard lock(mutex_);
            model_name_ = translator_->name();
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
