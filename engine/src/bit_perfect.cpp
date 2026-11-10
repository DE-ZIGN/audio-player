#include <ap/engine/bit_perfect.h>

namespace ap::engine {

BitPerfectVerdict judgeBitPerfect(const SignalPath& path, std::span<const StageState> stages,
                                  std::optional<DeliveryResult> delivery)
{
    BitPerfectVerdict v;
    v.dop = path.dop.has_value();
    v.dummyOutput = path.output.api == OutputApi::Dummy;
    if (const auto& ev = path.output.endpointVolume; ev && ev->hardwareVolume)
    {
        v.deviceMuted = ev->muted;
        if (ev->scalar < 1.0f || ev->muted)
            v.deviceVolume = ev->scalar;
    }

    if (path.processing)
    {
        v.bitPerfect = false;
        v.stages.assign(stages.begin(), stages.end());
        v.quantized = path.processingRequired;
        v.dither = path.dither;
        v.comparison = path.comparison;
        return v;
    }
    v.delivery = delivery;
    v.deliveryMismatch = delivery && delivery->mismatchedFrames > 0;
    v.bitPerfect = !v.deliveryMismatch;
    v.repacked = path.repack != Repack::None;
    v.floatSource = path.source.type == SampleType::Float;
    return v;
}

} // namespace ap::engine
