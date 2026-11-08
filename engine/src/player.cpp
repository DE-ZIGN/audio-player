#include <ap/engine/player.h>

#include <ap/engine/output/wasapi_exclusive_output.h>

namespace ap::engine {

namespace {

// Applies seeks requested from other threads at the next read and publishes
// the read position. Never touches sample values.
class SeekingSource final : public PcmSource
{
public:
    SeekingSource(PcmSource& inner, std::atomic<std::int64_t>& pendingSeek, std::atomic<std::uint64_t>& position,
                  std::atomic<bool>& stopRequested)
        : inner_(inner), pendingSeek_(pendingSeek), position_(position), stopRequested_(stopRequested)
    {
    }

    const PcmFormat& format() const noexcept override { return inner_.format(); }
    std::uint64_t totalFrames() const noexcept override { return inner_.totalFrames(); }
    std::uint64_t position() const noexcept override { return inner_.position(); }
    bool seek(std::uint64_t frame) override { return inner_.seek(frame); }

    std::size_t read(std::span<std::byte> dst) override
    {
        if (stopRequested_)
            return 0;
        const std::int64_t target = pendingSeek_.exchange(-1);
        if (target >= 0)
            inner_.seek(static_cast<std::uint64_t>(target));
        const std::size_t frames = inner_.read(dst);
        position_ = inner_.position();
        return frames;
    }

private:
    PcmSource& inner_;
    std::atomic<std::int64_t>& pendingSeek_;
    std::atomic<std::uint64_t>& position_;
    std::atomic<bool>& stopRequested_;
};

} // namespace

Player::Player(OutputFactory outputFactory, PlaybackOptions options)
    : outputFactory_(std::move(outputFactory)), options_(std::move(options))
{
}

Player::~Player()
{
    stop();
}

void Player::load(std::unique_ptr<PcmSource> source)
{
    stop();
    std::lock_guard lock(mutex_);
    source_ = std::move(source);
    status_ = PlayerStatus{};
    position_ = 0;
    pendingSeek_ = -1;
    if (source_)
    {
        status_.state = PlayerState::Stopped;
        status_.sourceFormat = source_->format();
        status_.totalFrames = source_->totalFrames();
        source_->seek(0);
    }
}

void Player::play()
{
    std::lock_guard lock(mutex_);
    if (!source_ || running_)
        return;
    if (thread_.joinable())
        thread_.join(); // previous run has finished; reap it

    if (status_.state == PlayerState::Ended)
    {
        source_->seek(0);
        position_ = 0;
    }
    status_.state = PlayerState::Playing;
    status_.error = PlayerError{};
    status_.plan.reset();
    stopRequested_ = false;
    running_ = true;
    thread_ = std::thread([this] { threadMain(); });
}

void Player::stop()
{
    {
        std::lock_guard lock(mutex_);
        if (!running_)
        {
            joinThread();
            return;
        }
        stopRequested_ = true;
        if (activeOutput_)
            activeOutput_->requestStop();
    }
    joinThread();
}

void Player::seek(std::uint64_t frame)
{
    std::lock_guard lock(mutex_);
    if (!source_)
        return;
    frame = std::min(frame, source_->totalFrames());
    if (running_)
    {
        pendingSeek_ = static_cast<std::int64_t>(frame);
        position_ = frame;
        return;
    }
    source_->seek(frame);
    position_ = source_->position();
    if (status_.state == PlayerState::Ended)
        status_.state = PlayerState::Stopped;
}

PlayerStatus Player::status() const
{
    std::lock_guard lock(mutex_);
    PlayerStatus s = status_;
    s.positionFrames = position_;
    return s;
}

void Player::waitUntilIdle()
{
    {
        std::unique_lock lock(mutex_);
        idle_.wait(lock, [this] { return !running_; });
    }
    joinThread();
}

void Player::joinThread()
{
    // Called without holding mutex_ (the thread takes it on exit), or from
    // stop() after running_ is already false.
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id())
        thread_.join();
}

void Player::threadMain()
{
    ScopedComInit com; // WASAPI needs COM on the thread that drives the device
    PlayerError error;
    std::uint64_t endPosition = 0;

    try
    {
        auto output = outputFactory_();
        {
            std::lock_guard lock(mutex_);
            activeOutput_ = output.get();
            status_.outputName = output->deviceName();
        }

        SeekingSource source(*source_, pendingSeek_, position_, stopRequested_);
        PlaybackOptions options = options_;
        options.onStart = [this, output = output.get(), userOnStart = options_.onStart](const PlaybackPlan& plan) {
            {
                std::lock_guard lock(mutex_);
                status_.plan = plan;
            }
            // open() clears an earlier stop request inside the output; re-issue it.
            if (stopRequested_)
                output->requestStop();
            if (userOnStart)
                userOnStart(plan);
        };

        ap::engine::play(source, *output, options);
        endPosition = source.position();

        std::lock_guard lock(mutex_);
        activeOutput_ = nullptr;
    }
    catch (const FormatNotSupportedError& e)
    {
        error = {PlayerErrorKind::FormatNotSupported, e.check(), OutputError::FormatNotSupported, e.what()};
    }
    catch (const OutputException& e)
    {
        error = {PlayerErrorKind::Output, {}, e.code(), e.what()};
    }
    catch (const std::exception& e)
    {
        error = {PlayerErrorKind::Other, {}, OutputError::DeviceError, e.what()};
    }

    std::lock_guard lock(mutex_);
    activeOutput_ = nullptr;
    if (error.kind != PlayerErrorKind::None)
    {
        status_.state = PlayerState::Error;
        status_.error = error;
    }
    else if (stopRequested_)
    {
        status_.state = PlayerState::Stopped;
    }
    else
    {
        status_.state = PlayerState::Ended;
        position_ = endPosition;
    }
    running_ = false;
    idle_.notify_all();
}

} // namespace ap::engine
